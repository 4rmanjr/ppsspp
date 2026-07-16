#include <openssl/evp.h>

#include "LANSync/LANSyncPairing.h"
#include "LANSync/LANSyncServer.h"
#include "LANSync/LANSyncClient.h"
#include "LANSync/LANSyncJson.h"
#include "LANSync/TLSTransport.h"
#include "LANSync/LANSyncConfig.h"
#include "LANSync/PlatformKeyStore.h"
#include "Core/Config.h"
#include "Core/System.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <sstream>
#include <thread>
#include <openssl/rand.h>

namespace LANSync {

PairingManager::PairingManager(TLSContext *tlsCtx)
    : tlsCtx_(tlsCtx) {
}

PairingManager::~PairingManager() {
    CancelPairing();
}

// --- PIN Computation ---

std::string PairingManager::ComputePin(const std::string &nonce) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, nonce.data(), nonce.size());
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);

    unsigned int pinNum = (hash[0] << 16) | (hash[1] << 8) | hash[2];
    pinNum %= 1000000;

    std::ostringstream ss;
    ss << std::setw(6) << std::setfill('0') << pinNum;
    return ss.str();
}

// --- Nonce Generation ---

std::string PairingManager::GenerateNonce() {
    unsigned char nonceBytes[32];
    RAND_bytes(nonceBytes, sizeof(nonceBytes));

    std::string nonce;
    for (auto b : nonceBytes) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", b);
        nonce += hex;
    }
    return nonce;
}

std::string PairingManager::GetLocalPeerId() {
    return tlsCtx_ ? tlsCtx_->GetDeviceId() : "PPSSPP-Unknown";
}

// --- HTTP Handlers ---

void PairingManager::RegisterHandlers(LANSyncServer *server) {
    server->RegisterHandler("/pair/begin",
        [this](const std::string &method, const std::string &path, const std::string &body, const LANSyncServer::ConnectionCtx &ctx) {
            (void)ctx;
            return HandlePairBegin(method, path, body);
        });

    server->RegisterHandler("/pair/verify",
        [this](const std::string &method, const std::string &path, const std::string &body, const LANSyncServer::ConnectionCtx &ctx) {
            (void)ctx;
            return HandlePairVerify(method, path, body);
        });
}

std::string PairingManager::HandlePairBegin(const std::string &method, const std::string &path, const std::string &body) {
    (void)path;
    if (method != "POST") {
        return "{\"error\":\"method_not_allowed\"}";
    }

    std::string clientCertPEM = JsonGetString(body, "certPEM");

    std::string nonce = GenerateNonce();

    std::string fingerprint;
    if (tlsCtx_) {
        fingerprint = tlsCtx_->GetCertFingerprint();
    }

    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(nonceMutex_);
        PendingNonce pn;
        pn.nonce = nonce;
        pn.createdAt = now;
        pn.peerFingerprint = fingerprint;
        pn.certPEM = std::move(clientCertPEM);
        pendingNonces_.push_back(std::move(pn));
    }

    std::string serverCertPEM;
    if (tlsCtx_) {
        serverCertPEM = tlsCtx_->GetCertPEM();
    }

    return "{\"nonce\":\"" + nonce + "\",\"certFingerprint\":\"" + fingerprint + "\",\"certPEM\":\"" + JsonEscape(serverCertPEM) + "\"}";
}

std::string PairingManager::HandlePairVerify(const std::string &method, const std::string &path, const std::string &body) {
    (void)path;
    if (method != "POST") {
        return "{\"error\":\"method_not_allowed\"}";
    }

    std::string nonce = JsonGetString(body, "nonce");
    std::string pin = JsonGetString(body, "pin");
    std::string peerId = JsonGetString(body, "peerId");

    if (nonce.empty() || pin.empty() || peerId.empty()) {
        return "{\"success\":false}";
    }

    // Verify nonce was issued by us
    bool nonceFound = false;
    std::string clientCertPEM;
    {
        std::lock_guard<std::mutex> lock(nonceMutex_);
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        for (auto it = pendingNonces_.begin(); it != pendingNonces_.end(); ) {
            if (it->nonce == nonce) {
                nonceFound = true;
                clientCertPEM = it->certPEM;
                it = pendingNonces_.erase(it);
            } else if (now - it->createdAt > 300000) {
                it = pendingNonces_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string expectedPin = ComputePin(nonce);
    bool pinMatch = (pin == expectedPin) && nonceFound;

    if (pinMatch) {
        TrustedPeer peer;
        peer.peerId = peerId;
        peer.deviceName = peerId;
        peer.certPEM = clientCertPEM;
        peer.pairedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        PlatformKeyStore::SavePeer(peer);
    }

    return "{\"success\":" + std::string(pinMatch ? "true" : "false") + ",\"peerId\":\"" + GetLocalPeerId() + "\"}";
}

// --- Client-side Pairing ---

bool PairingManager::PairWithPeer(const std::string &host, int port, PairingCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_) {
        return false;
    }

    pending_ = std::make_unique<PendingPairing>();
    pending_->host = host;
    pending_->port = port;
    pending_->callback = std::move(callback);

    std::thread([this, host, port]() {
        LANSyncClient client(tlsCtx_);
        if (!client.Connect(host, port)) {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_ && pending_->callback) {
                pending_->callback(false, "");
            }
            pending_.reset();
            return;
        }

        std::string certPEM;
        if (tlsCtx_) certPEM = tlsCtx_->GetCertPEM();
        std::string beginBody = "{\"certPEM\":\"" + JsonEscape(certPEM) + "\"}";
        HTTPResponse resp = client.Post("/pair/begin", "application/json", beginBody);
        if (resp.statusCode != 200) {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_ && pending_->callback) {
                pending_->callback(false, "");
            }
            pending_.reset();
            return;
        }

        std::string nonce = JsonGetString(resp.body, "nonce");

        if (nonce.empty()) {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_ && pending_->callback) {
                pending_->callback(false, "");
            }
            pending_.reset();
            return;
        }

        std::string serverCertPEM = JsonGetString(resp.body, "certPEM");

        std::string pin = ComputePin(nonce);

        {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_) {
                pending_->nonce = nonce;
                pending_->expectedPin = pin;
            }
        }

        std::string localPeerId = GetLocalPeerId();
        {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_) {
                pending_->nonce = nonce;
                pending_->expectedPin = pin;
                pending_->localPeerId = localPeerId;
            }
        }

        // Wait for user to confirm/cancel via dialog
        {
            std::unique_lock<std::mutex> lk(dialogMutex_);
            pendingDialog_ = std::make_unique<PendingDialogInfo>();
            pendingDialog_->isInitiator = true;
            pendingDialog_->pin = pin;
            pendingDialog_->peerName = host + ":" + std::to_string(port);
            dialogCv_.wait(lk, [this] { return !pendingDialog_; });
        }

        std::string enteredPin;
        {
            std::lock_guard<std::mutex> l(mutex_);
            enteredPin = pending_ ? confirmPin_ : "";
        }
        if (enteredPin.empty()) {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_ && pending_->callback) {
                pending_->callback(false, "");
            }
            pending_.reset();
            return;
        }

        std::string verifyBody = "{\"nonce\":\"" + nonce + "\",\"pin\":\"" + enteredPin + "\",\"peerId\":\"" + localPeerId + "\"}";
        resp = client.Post("/pair/verify", "application/json", verifyBody);

        bool success = (resp.statusCode == 200 && resp.body.find("\"success\":true") != std::string::npos);

        if (success) {
            TrustedPeer peer;
            peer.peerId = localPeerId + "@" + host;
            peer.deviceName = localPeerId;
            peer.certPEM = serverCertPEM;
            peer.lastIP = host;
            peer.pairedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            PlatformKeyStore::SavePeer(peer);
        }

        {
            std::lock_guard<std::mutex> l(mutex_);
            if (pending_ && pending_->callback) {
                pending_->callback(success, localPeerId);
            }
            pending_.reset();
        }
    }).detach();

    return true;
}

void PairingManager::CancelPairing() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_ && pending_->callback) {
            pending_->callback(false, "");
        }
        pending_.reset();
    }
    {
        std::lock_guard<std::mutex> lk(dialogMutex_);
        pendingDialog_.reset();
    }
    dialogCv_.notify_one();
}

void PairingManager::ConfirmPin(const std::string &pin) {
    {
        std::lock_guard<std::mutex> lk(dialogMutex_);
        pendingDialog_.reset();
    }
    // [PPSSPP-FORK] SR3: write confirmPin_ under the same mutex_ that the
    // PairWithPeer worker thread reads it under, so the two never race.
    {
        std::lock_guard<std::mutex> l(mutex_);
        confirmPin_ = pin;
    }
    dialogCv_.notify_one();
}

void PairingManager::SetScreenManager(ScreenManager *sm) {
    screenManager_ = sm;
}

bool PairingManager::HasPendingDialog() const {
    std::lock_guard<std::mutex> lock(dialogMutex_);
    return pendingDialog_ != nullptr;
}

PendingDialogInfo *PairingManager::GetPendingDialog() const {
    std::lock_guard<std::mutex> lock(dialogMutex_);
    return pendingDialog_.get();
}

bool PairingManager::IsPairingInProgress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ != nullptr;
}

} // namespace LANSync
