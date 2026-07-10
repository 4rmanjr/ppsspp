package org.ppsspp.ppsspp;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.util.Log;

class LANSyncMDNSHelper {
	private static final String TAG = "LANSyncMDNS";

	private static LANSyncMDNSHelper sInstance;

	private final NsdManager mNsdManager;
	private NsdManager.DiscoveryListener mDiscoveryListener;
	private NsdManager.RegistrationListener mRegistrationListener;

	private static native void nativeOnPeerFound(String name, String host, int port, String serviceType);
	private static native void nativeOnPeerLost(String name, String host, int port, String serviceType);
	private static native void nativeOnAnnounceResult(boolean success, String msg);

	private LANSyncMDNSHelper(Context context) {
		mNsdManager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
	}

	public static void init(Context ctx) {
		if (sInstance == null) {
			sInstance = new LANSyncMDNSHelper(ctx.getApplicationContext());
		}
	}

	public static void destroy() {
		if (sInstance != null) {
			sInstance.stopDiscoveryInternal();
			sInstance.stopAnnounceInternal();
			sInstance = null;
		}
	}

	public static void startDiscovery(String serviceType) {
		if (sInstance != null) sInstance.startDiscoveryInternal(serviceType);
	}

	public static void stopDiscovery() {
		if (sInstance != null) sInstance.stopDiscoveryInternal();
	}

	public static void startAnnounce(String serviceType, int port, String deviceName) {
		if (sInstance != null) sInstance.startAnnounceInternal(serviceType, port, deviceName, deviceName);
	}

	public static void stopAnnounce() {
		if (sInstance != null) sInstance.stopAnnounceInternal();
	}

	private String ensureDnsSdType(String type) {
		if (type == null || type.isEmpty()) return type;
		if (!type.endsWith(".")) return type + ".";
		return type;
	}

	private void startDiscoveryInternal(String serviceType) {
		if (mNsdManager == null) return;

		mDiscoveryListener = new NsdManager.DiscoveryListener() {
			@Override
			public void onDiscoveryStarted(String regType) {
				Log.d(TAG, "Discovery started: " + regType);
			}

			@Override
			public void onServiceFound(NsdServiceInfo info) {
				Log.d(TAG, "Service found: " + info.getServiceName());
				NsdManager.ResolveListener resolveListener = new NsdManager.ResolveListener() {
					@Override
					public void onResolveFailed(NsdServiceInfo resolveInfo, int errorCode) {
						Log.e(TAG, "Resolve failed for " + resolveInfo.getServiceName() + ": " + errorCode);
					}

					@Override
					public void onServiceResolved(NsdServiceInfo resolvedInfo) {
						String host = resolvedInfo.getHost() != null
							? resolvedInfo.getHost().getHostAddress()
							: "";
						String peerId = "";
						if (android.os.Build.VERSION.SDK_INT >= 16) {
							java.util.Map<String, byte[]> attrs = resolvedInfo.getAttributes();
							if (attrs != null && attrs.containsKey("id")) {
								peerId = new String(attrs.get("id"));
							}
						}
						Log.d(TAG, "Resolved: " + resolvedInfo.getServiceName()
							+ " @ " + host + ":" + resolvedInfo.getPort());
						nativeOnPeerFound(
							resolvedInfo.getServiceName(),
							host,
							resolvedInfo.getPort(),
							resolvedInfo.getServiceType()
						);
					}
				};
				mNsdManager.resolveService(info, resolveListener);
			}

			@Override
			public void onServiceLost(NsdServiceInfo info) {
				Log.d(TAG, "Service lost: " + info.getServiceName());
				nativeOnPeerLost(info.getServiceName(), "", 0, info.getServiceType());
			}

			@Override
			public void onDiscoveryStopped(String serviceType) {
				Log.d(TAG, "Discovery stopped: " + serviceType);
			}

			@Override
			public void onStartDiscoveryFailed(String serviceType, int errorCode) {
				Log.e(TAG, "Start discovery failed: " + serviceType + " error=" + errorCode);
			}

			@Override
			public void onStopDiscoveryFailed(String serviceType, int errorCode) {
				Log.e(TAG, "Stop discovery failed: " + serviceType + " error=" + errorCode);
			}
		};

		mNsdManager.discoverServices(ensureDnsSdType(serviceType),
			NsdManager.PROTOCOL_DNS_SD, mDiscoveryListener);
	}

	private void stopDiscoveryInternal() {
		if (mNsdManager == null || mDiscoveryListener == null) return;
		try {
			mNsdManager.stopServiceDiscovery(mDiscoveryListener);
		} catch (IllegalArgumentException e) {
			Log.w(TAG, "stopServiceDiscovery: " + e.getMessage());
		}
		mDiscoveryListener = null;
	}

	private void startAnnounceInternal(String serviceType, int port, String deviceName, String peerId) {
		if (mNsdManager == null) return;

		NsdServiceInfo serviceInfo = new NsdServiceInfo();
		serviceInfo.setServiceName(deviceName);
		serviceInfo.setServiceType(ensureDnsSdType(serviceType));
		serviceInfo.setPort(port);
		if (android.os.Build.VERSION.SDK_INT >= 21) {
			serviceInfo.setAttribute("id", peerId);
		}

		mRegistrationListener = new NsdManager.RegistrationListener() {
			@Override
			public void onServiceRegistered(NsdServiceInfo info) {
				Log.d(TAG, "Service registered: " + info.getServiceName());
				nativeOnAnnounceResult(true, info.getServiceName());
			}

			@Override
			public void onRegistrationFailed(NsdServiceInfo info, int errorCode) {
				Log.e(TAG, "Registration failed: " + errorCode);
				nativeOnAnnounceResult(false, "Registration failed: " + errorCode);
			}

			@Override
			public void onServiceUnregistered(NsdServiceInfo info) {
				Log.d(TAG, "Service unregistered: " + info.getServiceName());
			}

			@Override
			public void onUnregistrationFailed(NsdServiceInfo info, int errorCode) {
				Log.e(TAG, "Unregistration failed: " + errorCode);
			}
		};

		mNsdManager.registerService(serviceInfo, NsdManager.PROTOCOL_DNS_SD, mRegistrationListener);
	}

	private void stopAnnounceInternal() {
		if (mNsdManager == null || mRegistrationListener == null) return;
		try {
			mNsdManager.unregisterService(mRegistrationListener);
		} catch (IllegalArgumentException e) {
			Log.w(TAG, "unregisterService: " + e.getMessage());
		}
		mRegistrationListener = null;
	}
}
