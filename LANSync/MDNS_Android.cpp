#include "LANSync/MDNS.h"
#include "Common/Log.h"

#if PPSSPP_PLATFORM(ANDROID)
#include <jni.h>
#include <mutex>
#include "app-android.h"

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
		static JNICache c;
		static std::once_flag initFlag;
		std::call_once(initFlag, [&c]() {
			JNIEnv *env = EnsureAttached();
			jclass local = findClass("org/ppsspp/ppsspp/LANSyncMDNSHelper");
			if (!local) {
				ERROR_LOG(Log::System, "mDNS: Failed to find LANSyncMDNSHelper class");
				return;
			}
			c.clazz = (jclass)env->NewGlobalRef(local);
			env->DeleteLocalRef(local);

			c.startDiscovery = env->GetStaticMethodID(c.clazz, "startDiscovery", "(Ljava/lang/String;)V");
			c.stopDiscovery = env->GetStaticMethodID(c.clazz, "stopDiscovery", "()V");
			c.startAnnounce = env->GetStaticMethodID(c.clazz, "startAnnounce", "(Ljava/lang/String;ILjava/lang/String;)V");
			c.stopAnnounce = env->GetStaticMethodID(c.clazz, "stopAnnounce", "()V");

			if (!c.startDiscovery || !c.stopDiscovery || !c.startAnnounce || !c.stopAnnounce) {
				ERROR_LOG(Log::System, "mDNS: Failed to find JNI methods");
			}
		});
		return c;
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
extern "C" void JNICALL
Java_org_ppsspp_ppsspp_LANSyncMDNSHelper_nativeOnPeerFound(
	JNIEnv *env, jclass, jstring jName, jstring jHost, jint jPort, jstring jServiceType)
{
	const char *name = env->GetStringUTFChars(jName, nullptr);
	const char *host = env->GetStringUTFChars(jHost, nullptr);
	const char *serviceType = env->GetStringUTFChars(jServiceType, nullptr);

	LANSync::DiscoveredPeer peer;
	peer.deviceName = name;
	peer.host = host;
	peer.port = (int)jPort;

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

	LANSync::DiscoveredPeer peer;
	peer.deviceName = name;
	peer.host = host;
	peer.port = (int)jPort;

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

#endif  // PPSSPP_PLATFORM(ANDROID)
