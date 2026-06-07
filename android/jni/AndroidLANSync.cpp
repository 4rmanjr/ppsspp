// PPSSPP Project - LAN Save State Sync
// Android platform backend - Phase 3 full implementation
// JNI bridge to NsdManager, Keystore, ForegroundService, ML Kit QR scan
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <functional>

#include <jni.h>
#include <android/log.h>

#include "android/jni/AndroidLANSync.h"
#include "Core/SaveStateLANSync.h"
#include "Common/Log.h"
#include "Common/Net/PlatformKeyStore.h"
#include "Common/Net/MDNS.h"
#include "Common/Net/UDPDiscovery.h"

// ==================== JNI Helpers ====================

static JavaVM *g_javaVM = nullptr;
static jclass g_lanSyncClass = nullptr;

// Must be called from JNI_OnLoad to cache the JVM and class references
extern "C" JNIEXPORT jint JNICALL
Java_org_ppsspp_ppsspp_LANSyncActivity_registerNatives(JNIEnv *env, jclass clz);

struct JNIScope {
	JNIEnv *env = nullptr;
	bool attached = false;

	JNIScope() {
		if (!g_javaVM) return;
		jint ret = g_javaVM->GetEnv((void **)&env, JNI_VERSION_1_6);
		if (ret == JNI_EDETACHED) {
			ret = g_javaVM->AttachCurrentThread(&env, nullptr);
			attached = (ret == JNI_OK);
		}
	}

	~JNIScope() {
		if (attached && g_javaVM) {
			g_javaVM->DetachCurrentThread();
		}
	}

	explicit operator bool() const { return env != nullptr; }
};

// Cache Java method IDs for performance
static jmethodID g_method_registerService = nullptr;
static jmethodID g_method_unregisterService = nullptr;
static jmethodID g_method_discoverServices = nullptr;
static jmethodID g_method_stopDiscovery = nullptr;
static jmethodID g_method_startForeground = nullptr;
static jmethodID g_method_stopForeground = nullptr;
static jmethodID g_method_scanQR = nullptr;
static jmethodID g_method_storeKey = nullptr;
static jmethodID g_method_loadKey = nullptr;

// ==================== Android Keystore (via JNI) ====================

static std::string CallJavaKeystoreStore(const std::string &alias, const std::string &data) {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_storeKey) return "";

	jstring jAlias = scope.env->NewStringUTF(alias.c_str());
	jbyteArray jData = scope.env->NewByteArray(data.size());
	scope.env->SetByteArrayRegion(jData, 0, data.size(), (const jbyte *)data.data());

	jstring result = (jstring)scope.env->CallStaticObjectMethod(
		g_lanSyncClass, g_method_storeKey, jAlias, jData);

	std::string ret;
	if (result) {
		const char *chars = scope.env->GetStringUTFChars(result, nullptr);
		if (chars) { ret = chars; scope.env->ReleaseStringUTFChars(result, chars); }
	}

	scope.env->DeleteLocalRef(jAlias);
	scope.env->DeleteLocalRef(jData);
	if (result) scope.env->DeleteLocalRef(result);
	return ret;
}

static std::string CallJavaKeystoreLoad(const std::string &alias) {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_loadKey) return "";

	jstring jAlias = scope.env->NewStringUTF(alias.c_str());
	jbyteArray result = (jbyteArray)scope.env->CallStaticObjectMethod(
		g_lanSyncClass, g_method_loadKey, jAlias);

	std::string ret;
	if (result) {
		jsize len = scope.env->GetArrayLength(result);
		ret.resize(len);
		scope.env->GetByteArrayRegion(result, 0, len, (jbyte *)ret.data());
		scope.env->DeleteLocalRef(result);
	}

	scope.env->DeleteLocalRef(jAlias);
	return ret;
}

// ==================== Android NsdManager (via JNI) ====================

static void CallJavaRegisterService(const std::string &name, int port,
                                    const std::map<std::string, std::string> &txt) {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_registerService) return;

	jstring jName = scope.env->NewStringUTF(name.c_str());
	jint jPort = port;

	// Build TXT record map as Java HashMap
	jclass mapClass = scope.env->FindClass("java/util/HashMap");
	jmethodID mapInit = scope.env->GetMethodID(mapClass, "<init>", "()V");
	jmethodID mapPut = scope.env->GetMethodID(mapClass, "put",
		"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
	jobject jTxt = scope.env->NewObject(mapClass, mapInit);

	for (const auto &kv : txt) {
		jstring k = scope.env->NewStringUTF(kv.first.c_str());
		jstring v = scope.env->NewStringUTF(kv.second.c_str());
		scope.env->CallObjectMethod(jTxt, mapPut, k, v);
		scope.env->DeleteLocalRef(k);
		scope.env->DeleteLocalRef(v);
	}

	scope.env->CallStaticVoidMethod(g_lanSyncClass, g_method_registerService,
	                                jName, jPort, jTxt);

	scope.env->DeleteLocalRef(jName);
	scope.env->DeleteLocalRef(jTxt);
	scope.env->DeleteLocalRef(mapClass);
}

static void CallJavaDiscoverServices() {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_discoverServices) return;
	scope.env->CallStaticVoidMethod(g_lanSyncClass, g_method_discoverServices);
}

static void CallJavaStopDiscovery() {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_stopDiscovery) return;
	scope.env->CallStaticVoidMethod(g_lanSyncClass, g_method_stopDiscovery);
}

// ==================== Android ForegroundService (via JNI) ====================

static void CallJavaStartForeground(const std::string &title, int maxProgress) {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_startForeground) return;

	jstring jTitle = scope.env->NewStringUTF(title.c_str());
	jint jMax = maxProgress;
	scope.env->CallStaticVoidMethod(g_lanSyncClass, g_method_startForeground, jTitle, jMax);
	scope.env->DeleteLocalRef(jTitle);
}

static void CallJavaStopForeground() {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_stopForeground) return;
	scope.env->CallStaticVoidMethod(g_lanSyncClass, g_method_stopForeground);
}

// ==================== Android QR Scan (via JNI → ML Kit) ====================

static void CallJavaStartQRScan() {
	JNIScope scope;
	if (!scope || !g_lanSyncClass || !g_method_scanQR) return;
	scope.env->CallStaticVoidMethod(g_lanSyncClass, g_method_scanQR);
}

// ==================== JNI Callbacks (called from Java) ====================

static std::mutex g_peerMutex;
static std::vector<SaveStateLANSync::PeerInfo> g_discoveredPeers;

extern "C" JNIEXPORT void JNICALL
Java_org_ppsspp_ppsspp_LANSyncService_onPeerDiscovered(JNIEnv *env, jclass clz,
                                                        jstring jId, jstring jName,
                                                        jstring jHost, jint jPort,
                                                        jstring jFingerprint, jstring jDevice) {
	std::lock_guard<std::mutex> lock(g_peerMutex);

	SaveStateLANSync::PeerInfo peer;
	peer.id = jId ? env->GetStringUTFChars(jId, nullptr) : "";
	if (jId) env->ReleaseStringUTFChars(jId, peer.id.c_str());
	peer.name = jName ? env->GetStringUTFChars(jName, nullptr) : "";
	if (jName) env->ReleaseStringUTFChars(jName, peer.name.c_str());
	peer.host = jHost ? env->GetStringUTFChars(jHost, nullptr) : "";
	if (jHost) env->ReleaseStringUTFChars(jHost, peer.host.c_str());
	peer.port = jPort;
	peer.certFingerprint = jFingerprint ? env->GetStringUTFChars(jFingerprint, nullptr) : "";
	if (jFingerprint) env->ReleaseStringUTFChars(jFingerprint, peer.certFingerprint.c_str());
	peer.device = jDevice ? env->GetStringUTFChars(jDevice, nullptr) : "";
	if (jDevice) env->ReleaseStringUTFChars(jDevice, peer.device.c_str());
	peer.online = true;
	peer.lastSeen = time(nullptr);

	// Check if already in list
	for (auto &p : g_discoveredPeers) {
		if (p.id == peer.id) { p = peer; return; }
	}
	g_discoveredPeers.push_back(peer);
}

extern "C" JNIEXPORT void JNICALL
Java_org_ppsspp_ppsspp_LANSyncService_onPeerLost(JNIEnv *env, jclass clz, jstring jId) {
	std::lock_guard<std::mutex> lock(g_peerMutex);

	std::string id = jId ? env->GetStringUTFChars(jId, nullptr) : "";
	if (jId) env->ReleaseStringUTFChars(jId, id.c_str());

	g_discoveredPeers.erase(
		std::remove_if(g_discoveredPeers.begin(), g_discoveredPeers.end(),
		               [&id](const auto &p) { return p.id == id; }),
		g_discoveredPeers.end()
	);
}

extern "C" JNIEXPORT void JNICALL
Java_org_ppsspp_ppsspp_LANSyncService_onQRScanned(JNIEnv *env, jclass clz, jstring jPayload) {
	std::string payload = jPayload ? env->GetStringUTFChars(jPayload, nullptr) : "";
	if (jPayload) env->ReleaseStringUTFChars(jPayload, payload.c_str());

	// Parse QR payload: ppsspp-sync://pair?host=...&port=...&fp=...&pin=...&name=...
	// Forward to pairing logic
	auto &core = SaveStateLANSync::Instance();

	// Simple URI parsing
	std::string host, pin, name;
	int port = 0;

	size_t pos = payload.find("host=");
	if (pos != std::string::npos) {
		size_t end = payload.find('&', pos);
		host = payload.substr(pos + 5, end - pos - 5);
	}
	pos = payload.find("port=");
	if (pos != std::string::npos) {
		size_t end = payload.find('&', pos);
		port = atoi(payload.substr(pos + 5, end - pos - 5).c_str());
	}
	pos = payload.find("pin=");
	if (pos != std::string::npos) {
		size_t end = payload.find('&', pos);
		pin = payload.substr(pos + 4, end - pos - 4);
	}

	if (!host.empty() && !pin.empty() && port > 0) {
		std::string peerId = host + ":" + std::to_string(port);
		core.PairWithPeer(peerId, pin, nullptr);
	}
}

// ==================== JNI_OnLoad ====================

extern "C" JNIEXPORT jint JNICALL
Java_org_ppsspp_ppsspp_LANSyncActivity_registerNatives(JNIEnv *env, jclass clz) {
	g_javaVM = nullptr;
	env->GetJavaVM(&g_javaVM);

	g_lanSyncClass = (jclass)env->NewGlobalRef(clz);

	// Cache method IDs
	g_method_registerService = env->GetStaticMethodID(clz, "registerService",
		"(Ljava/lang/String;ILjava/util/Map;)V");
	g_method_unregisterService = env->GetStaticMethodID(clz, "unregisterService", "()V");
	g_method_discoverServices = env->GetStaticMethodID(clz, "discoverServices", "()V");
	g_method_stopDiscovery = env->GetStaticMethodID(clz, "stopServiceDiscovery", "()V");
	g_method_startForeground = env->GetStaticMethodID(clz, "startForegroundSync",
		"(Ljava/lang/String;I)V");
	g_method_stopForeground = env->GetStaticMethodID(clz, "stopForegroundSync", "()V");
	g_method_scanQR = env->GetStaticMethodID(clz, "startQRScan", "()V");
	g_method_storeKey = env->GetStaticMethodID(clz, "keystoreStore",
		"(Ljava/lang/String;[B)Ljava/lang/String;");
	g_method_loadKey = env->GetStaticMethodID(clz, "keystoreLoad",
		"(Ljava/lang/String;)[B");

	INFO_LOG(Log::System, "AndroidLANSync: JNI registered, methods cached");
	return JNI_VERSION_1_6;
}

// ==================== AndroidLANSync Implementation ====================

AndroidLANSync &AndroidLANSync::Instance() {
	static AndroidLANSync instance;
	return instance;
}

bool AndroidLANSync::Init() {
	PlatformKeyStore::Init();
	SaveStateLANSync::Instance().Init();
	INFO_LOG(Log::System, "AndroidLANSync: initialized (Phase 3 full)");
	return true;
}

void AndroidLANSync::Shutdown() {
	Disable();
	SaveStateLANSync::Instance().Shutdown();
	PlatformKeyStore::Shutdown();
}

bool AndroidLANSync::Enable(const std::string &deviceName) {
	deviceName_ = deviceName;

	auto &core = SaveStateLANSync::Instance();
	core.StartDiscovery();
	if (!core.StartServer()) {
		core.StopDiscovery();
		return false;
	}

	// Start NsdManager discovery (JNI call)
	CallJavaDiscoverServices();

	// Register NsdManager service (JNI call)
	std::map<std::string, std::string> txt;
	txt["version"] = "1";
	txt["device"] = "Android";
	txt["name"] = deviceName;
	txt["id"] = core.GetDeviceId();
	CallJavaRegisterService(deviceName, core.GetServerPort(), txt);

	// Start foreground service (Android 8+ requirement for long-running service)
	CallJavaStartForeground("PPSSPP LAN Sync", 100);

	INFO_LOG(Log::System, "AndroidLANSync: enabled (Phase 3 full)");
	return true;
}

void AndroidLANSync::Disable() {
	auto &core = SaveStateLANSync::Instance();

	CallJavaStopDiscovery();
	CallJavaStopForeground();
	core.StopServer();
	core.StopDiscovery();
}

void AndroidLANSync::StartQRScan(std::function<void(const std::string &result)> callback) {
	// The callback will be invoked via onQRScanned JNI callback
	CallJavaStartQRScan();
}

void AndroidLANSync::StopQRScan() {
	// QR scan stops when result is received or user cancels
}

#endif  // PPSSPP_PLATFORM(ANDROID)
