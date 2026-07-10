#include "LANSync/MDNS.h"
#include "Common/Log.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
#include <mutex>
#include "android/jni/app-android.h"

extern JavaVM *gJvm;

namespace LANSync {
namespace {

std::mutex g_browserMutex;
class MDNSBrowserAndroid *g_activeBrowser = nullptr;

std::mutex g_announcerMutex;
class MDNSAnnouncerAndroid *g_activeAnnouncer = nullptr;

JNIEnv* EnsureAttached() {
	JNIEnv *env;
	if (gJvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
		gJvm->AttachCurrentThread(&env, nullptr);
	}
	return env;
}

struct JNICache {
	jclass clazz;
	jmethodID startDiscovery;
	jmethodID stopDiscovery;
	jmethodID startAnnounce;
	jmethodID stopAnnounce;

	static JNICache& Get() {
		static JNICache *c = nullptr;
		static std::once_flag initFlag;
		std::call_once(initFlag, [&]() {
			JNIEnv *env = EnsureAttached();
			jclass local = findClass("org/ppsspp/ppsspp/LANSyncMDNSHelper");
			if (!local) {
				ERROR_LOG(Log::System, "mDNS: Failed to find LANSyncMDNSHelper class");
				return;
			}
			static JNICache cache;
			cache.clazz = (jclass)env->NewGlobalRef(local);
			env->DeleteLocalRef(local);

			cache.startDiscovery = env->GetStaticMethodID(cache.clazz, "startDiscovery", "(Ljava/lang/String;)V");
			cache.stopDiscovery = env->GetStaticMethodID(cache.clazz, "stopDiscovery", "()V");
			cache.startAnnounce = env->GetStaticMethodID(cache.clazz, "startAnnounce", "(Ljava/lang/String;ILjava/lang/String;)V");
			cache.stopAnnounce = env->GetStaticMethodID(cache.clazz, "stopAnnounce", "()V");

			if (!cache.startDiscovery || !cache.stopDiscovery || !cache.startAnnounce || !cache.stopAnnounce) {
				ERROR_LOG(Log::System, "mDNS: Failed to find JNI methods");
			}
			c = &cache;
		});
		return *c;
	}
};

class MDNSAnnouncerAndroid : public MDNSAnnouncer {
public:
	~MDNSAnnouncerAndroid() override { Stop(); }

	bool Start(const std::string &serviceType, int port, const std::string &deviceName) override {
		JNIEnv *env = EnsureAttached();
		auto &cache = JNICache::Get();
		if (!cache.clazz) return false;

		jstring jType = env->NewStringUTF(serviceType.c_str());
		jstring jName = env->NewStringUTF(deviceName.c_str());

		{
			std::lock_guard<std::mutex> lock(g_announcerMutex);
			g_activeAnnouncer = this;
		}

		env->CallStaticVoidMethod(cache.clazz, cache.startAnnounce, jType, port, jName);

		env->DeleteLocalRef(jName);
		env->DeleteLocalRef(jType);

		INFO_LOG(Log::System, "mDNS: Android announcer started for %s:%d (%s)", serviceType.c_str(), port, deviceName.c_str());
		return true;
	}

	void Stop() override {
		JNIEnv *env = EnsureAttached();
		auto &cache = JNICache::Get();
		if (cache.clazz) {
			env->CallStaticVoidMethod(cache.clazz, cache.stopAnnounce);
		}
		std::lock_guard<std::mutex> lock(g_announcerMutex);
		g_activeAnnouncer = nullptr;
	}
};

class MDNSBrowserAndroid : public MDNSBrowser {
public:
	OnPeerFound onFound_;
	OnPeerLost onLost_;

	~MDNSBrowserAndroid() override { Stop(); }

	bool Start(const std::string &serviceType, OnPeerFound onFound, OnPeerLost onLost) override {
		onFound_ = std::move(onFound);
		onLost_ = std::move(onLost);

		JNIEnv *env = EnsureAttached();
		auto &cache = JNICache::Get();
		if (!cache.clazz) return false;

		jstring jType = env->NewStringUTF(serviceType.c_str());

		{
			std::lock_guard<std::mutex> lock(g_browserMutex);
			g_activeBrowser = this;
		}

		env->CallStaticVoidMethod(cache.clazz, cache.startDiscovery, jType);

		env->DeleteLocalRef(jType);

		INFO_LOG(Log::System, "mDNS: Android browser started for %s", serviceType.c_str());
		return true;
	}

	void Stop() override {
		JNIEnv *env = EnsureAttached();
		auto &cache = JNICache::Get();
		if (cache.clazz) {
			env->CallStaticVoidMethod(cache.clazz, cache.stopDiscovery);
		}
		std::lock_guard<std::mutex> lock(g_browserMutex);
		g_activeBrowser = nullptr;
	}
};

}  // anonymous namespace

MDNSAnnouncer *CreateMDNSAnnouncerAndroid() {
	return new MDNSAnnouncerAndroid();
}

MDNSBrowser *CreateMDNSBrowserAndroid() {
	return new MDNSBrowserAndroid();
}

}  // namespace LANSync

// JNI native callbacks from Java LANSyncMDNSHelper
// Wrapped in LANSync namespace to access anonymous-namespace globals.
namespace LANSync {
extern "C" void JNICALL
Java_org_ppsspp_ppsspp_LANSyncMDNSHelper_nativeOnPeerFound(
	JNIEnv *env, jclass, jstring jName, jstring jHost, jint jPort, jstring jServiceType)
{
	const char *name = env->GetStringUTFChars(jName, nullptr);
	const char *host = env->GetStringUTFChars(jHost, nullptr);
	const char *serviceType = env->GetStringUTFChars(jServiceType, nullptr);

	DiscoveredPeer peer;
	peer.deviceName = name;
	peer.host = host;
	peer.port = (int)jPort;
	peer.peerId = name;

	INFO_LOG(Log::System, "mDNS: Peer found '%s' @ %s:%d", name, host, (int)jPort);

	{
		std::lock_guard<std::mutex> lock(g_browserMutex);
		if (g_activeBrowser && g_activeBrowser->onFound_) {
			g_activeBrowser->onFound_(peer);
		}
	}

	env->ReleaseStringUTFChars(jServiceType, serviceType);
	env->ReleaseStringUTFChars(jHost, host);
	env->ReleaseStringUTFChars(jName, name);
}

extern "C" void JNICALL
Java_org_ppsspp_ppsspp_LANSyncMDNSHelper_nativeOnPeerLost(
	JNIEnv *env, jclass, jstring jName, jstring jHost, jint jPort, jstring jServiceType)
{
	const char *name = env->GetStringUTFChars(jName, nullptr);
	const char *host = env->GetStringUTFChars(jHost, nullptr);
	const char *serviceType = env->GetStringUTFChars(jServiceType, nullptr);

	DiscoveredPeer peer;
	peer.deviceName = name;
	peer.host = host;
	peer.port = (int)jPort;
	peer.peerId = name;

	INFO_LOG(Log::System, "mDNS: Peer lost '%s'", name);

	{
		std::lock_guard<std::mutex> lock(g_browserMutex);
		if (g_activeBrowser && g_activeBrowser->onLost_) {
			g_activeBrowser->onLost_(peer);
		}
	}

	env->ReleaseStringUTFChars(jServiceType, serviceType);
	env->ReleaseStringUTFChars(jHost, host);
	env->ReleaseStringUTFChars(jName, name);
}

extern "C" void JNICALL
Java_org_ppsspp_ppsspp_LANSyncMDNSHelper_nativeOnAnnounceResult(
	JNIEnv *env, jclass, jboolean jSuccess, jstring jMsg)
{
	const char *msg = env->GetStringUTFChars(jMsg, nullptr);
	INFO_LOG(Log::System, "mDNS: Announce result: %s - %s", jSuccess ? "success" : "fail", msg);
	env->ReleaseStringUTFChars(jMsg, msg);
}
}  // namespace LANSync

#endif  // PPSSPP_PLATFORM(ANDROID)
