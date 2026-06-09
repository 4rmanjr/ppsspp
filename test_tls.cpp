// TLS handshake test - client/server on localhost
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static std::string GetSSLError() {
    unsigned long err = ERR_get_error();
    if (err == 0) return "unknown";
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return std::string(buf);
}

// Generate self-signed ECDSA P-256 cert
static EVP_PKEY* GenerateKey() {
    EVP_PKEY *pkey = EVP_PKEY_new();
    EC_KEY *ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_set_asn1_flag(ec, OPENSSL_EC_NAMED_CURVE);
    EC_KEY_generate_key(ec);
    EVP_PKEY_set1_EC_KEY(pkey, ec);
    EC_KEY_free(ec);
    return pkey;
}

static X509* GenerateCert(EVP_PKEY *pkey) {
    X509 *cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 365 * 86400);
    X509_set_pubkey(cert, pkey);
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (const unsigned char*)"PPSSPP-Test", -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_sign(cert, pkey, EVP_sha256());
    return cert;
}

static std::string GetCertFingerprint(X509 *cert) {
    unsigned char *der = nullptr;
    int len = i2d_X509(cert, &der);
    unsigned char hash[32];
    SHA256(der, len, hash);
    OPENSSL_free(der);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i*2, 3, "%02x", hash[i]);
    return std::string(hex);
}

// Server thread
static void ServerThread(SSL_CTX *ctx, int port, bool *ready, bool *result) {
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    
    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("SERVER: bind failed\n");
        *result = false;
        return;
    }
    listen(listenSock, 1);
    *ready = true;
    
    int clientSock = accept(listenSock, nullptr, nullptr);
    if (clientSock < 0) {
        printf("SERVER: accept failed\n");
        *result = false;
        return;
    }
    
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, clientSock);
    
    int ret = SSL_accept(ssl);
    if (ret <= 0) {
        printf("SERVER: SSL_accept failed: %s\n", GetSSLError().c_str());
        SSL_free(ssl);
        close(clientSock);
        *result = false;
        return;
    }
    
    printf("SERVER: SSL_accept OK, cipher=%s\n", SSL_get_cipher_name(ssl));
    
    // Read test message
    char buf[256] = {0};
    int n = SSL_read(ssl, buf, sizeof(buf)-1);
    if (n > 0) {
        printf("SERVER: received '%s'\n", buf);
        SSL_write(ssl, "PONG", 4);
    }
    
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(clientSock);
    close(listenSock);
    *result = true;
}

int main() {
    printf("=== TLS Handshake Test ===\n\n");
    
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Generate cert
    printf("[1] Generate ECDSA P-256 cert: ");
    EVP_PKEY *pkey = GenerateKey();
    X509 *cert = GenerateCert(pkey);
    std::string fp = GetCertFingerprint(cert);
    printf("OK (fingerprint=%s)\n", fp.substr(0, 16).c_str());
    
    // Setup server SSL context
    printf("[2] Setup server SSL_CTX: ");
    SSL_CTX *serverCtx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(serverCtx, TLS1_2_VERSION);
    SSL_CTX_use_certificate(serverCtx, cert);
    SSL_CTX_use_PrivateKey(serverCtx, pkey);
    printf("OK\n");
    
    // Start server thread
    printf("[3] Start TLS server on loopback: ");
    bool serverReady = false;
    bool serverResult = false;
    int port = 12345;
    std::thread server(ServerThread, serverCtx, port, &serverReady, &serverResult);
    
    // Wait for server to be ready
    while (!serverReady) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    printf("OK (port=%d)\n", port);
    
    // Client connect
    printf("[4] Client TLS connect: ");
    SSL_CTX *clientCtx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(clientCtx, TLS1_2_VERSION);
    SSL_CTX_set_verify(clientCtx, SSL_VERIFY_NONE, nullptr);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    SSL *ssl = SSL_new(clientCtx);
    SSL_set_fd(ssl, sock);
    int ret = SSL_connect(ssl);
    if (ret <= 0) {
        printf("FAIL: SSL_connect: %s\n", GetSSLError().c_str());
        return 1;
    }
    printf("OK (cipher=%s)\n", SSL_get_cipher_name(ssl));
    
    // TOFU fingerprint check
    printf("[5] TOFU fingerprint check: ");
    X509 *serverCert = SSL_get_peer_certificate(ssl);
    std::string serverFp = GetCertFingerprint(serverCert);
    X509_free(serverCert);
    if (serverFp == fp) {
        printf("OK (match)\n");
    } else {
        printf("FAIL (expected=%s got=%s)\n", fp.substr(0,16).c_str(), serverFp.substr(0,16).c_str());
        return 1;
    }
    
    // Send test message
    printf("[6] TLS data transfer: ");
    SSL_write(ssl, "PING", 4);
    char buf[256] = {0};
    int n = SSL_read(ssl, buf, sizeof(buf)-1);
    if (n > 0 && strcmp(buf, "PONG") == 0) {
        printf("OK (sent PING, received PONG)\n");
    } else {
        printf("FAIL\n");
        return 1;
    }
    
    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(clientCtx);
    
    server.join();
    SSL_CTX_free(serverCtx);
    EVP_PKEY_free(pkey);
    X509_free(cert);
    
    printf("\n=== ALL 6 TLS TESTS PASSED ===\n");
    return 0;
}
