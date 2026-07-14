package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

class LANSyncMDNSHelper {
	private static final String TAG = "LANSyncMDNS";

	private static LANSyncMDNSHelper sInstance;

	private final NsdManager mNsdManager;
	private final WifiManager mWifiManager;
	private final WifiManager.MulticastLock mMulticastLock;
	private int mMulticastRefs = 0;
	private NsdManager.DiscoveryListener mDiscoveryListener;
	private NsdManager.RegistrationListener mRegistrationListener;

	// Cache resolved peers so we can still report host/port/peerId on loss,
	// since onServiceLost often arrives without that info populated.
	private static class ResolvedPeer {
		String host;
		int port;
		String peerId;
	}
	private final Map<String, ResolvedPeer> mResolvedCache = new HashMap<>();

	// Activity reference (PpssppActivity) used to request runtime permissions
	// for NSD/mDNS. Normal permissions (e.g. ACCESS_LOCAL_NETWORK on API 34+)
	// are auto-granted; ACCESS_FINE_LOCATION (API 26-32) must be requested.
	private Activity mActivity;
	private static final int REQ_LANSYNC_PERMS = 0x4C41; // "LA"

	// Pending discovery/announce state, replayed after the runtime permission
	// is granted (onRequestPermissionsResult).
	private String mPendingDiscoveryType;
	private boolean mDiscoveryPending = false;
	private String mPendingAnnounceType;
	private int mPendingAnnouncePort;
	private String mPendingAnnounceName;
	private String mPendingAnnouncePeerId;
	private boolean mAnnouncePending = false;

	private static native void nativeOnPeerFound(String name, String host, int port, String serviceType, String peerId);
	private static native void nativeOnPeerLost(String name, String host, int port, String serviceType, String peerId);
	private static native void nativeOnAnnounceResult(boolean success, String msg);

	private LANSyncMDNSHelper(Context context) {
		mNsdManager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
		mWifiManager = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
		if (context instanceof Activity) {
			mActivity = (Activity) context;
		}
		WifiManager.MulticastLock lock = null;
		if (mWifiManager != null) {
			lock = mWifiManager.createMulticastLock("LANSync");
			lock.setReferenceCounted(true);
		}
		mMulticastLock = lock;
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
		if (sInstance != null) sInstance.startDiscoveryGuarded(serviceType);
	}

	public static void stopDiscovery() {
		if (sInstance != null) sInstance.stopDiscoveryInternal();
	}

	public static void startAnnounce(String serviceType, int port, String deviceName, String peerId) {
		if (sInstance != null) sInstance.startAnnounceGuarded(serviceType, port, deviceName, peerId);
	}

	public static void stopAnnounce() {
		if (sInstance != null) sInstance.stopAnnounceInternal();
	}

	private String ensureDnsSdType(String type) {
		if (type == null || type.isEmpty()) return type;
		if (!type.endsWith(".")) return type + ".";
		return type;
	}

	// mDNS (NsdManager) requires a held MulticastLock to receive multicast
	// packets on Wi-Fi; without it discovery finds nothing on most devices.
	// Ref-counted across discovery + announce so the lock is held only while
	// at least one of them is active.
	private void acquireMulticastLock() {
		if (mMulticastLock == null) return;
		try {
			if (mMulticastRefs == 0) mMulticastLock.acquire();
			mMulticastRefs++;
		} catch (SecurityException e) {
			Log.w(TAG, "acquireMulticastLock failed: " + e.getMessage());
		}
	}

	private void releaseMulticastLock() {
		if (mMulticastLock == null) return;
		if (mMulticastRefs > 0) mMulticastRefs--;
		if (mMulticastRefs == 0) {
			try {
				mMulticastLock.release();
			} catch (SecurityException e) {
				Log.w(TAG, "releaseMulticastLock failed: " + e.getMessage());
			}
		}
	}

	// --- Runtime permission handling for NSD/mDNS ---
	// ACCESS_LOCAL_NETWORK (API 34+) is a normal permission -> auto-granted.
	// ACCESS_FINE_LOCATION (API 26-32) is REQUIRED at runtime for NSD; without
	// it discoverServices() silently finds nothing.
	private boolean ensurePermissionsGranted() {
		if (mActivity == null) return true;
		if (android.os.Build.VERSION.SDK_INT >= 34) {
			return mActivity.checkSelfPermission("android.permission.ACCESS_LOCAL_NETWORK")
				== PackageManager.PERMISSION_GRANTED;
		}
		if (android.os.Build.VERSION.SDK_INT >= 26 && android.os.Build.VERSION.SDK_INT <= 32) {
			return mActivity.checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION)
				== PackageManager.PERMISSION_GRANTED;
		}
		return true; // < API 26 or API 33: no runtime permission needed for NSD
	}

	private void maybeRequestPermissions() {
		if (mActivity == null) return;
		if (ensurePermissionsGranted()) return;
		final Activity a = mActivity;
		a.runOnUiThread(new Runnable() {
			@Override public void run() { requestPermissionsInternal(a); }
		});
	}

	// Call proactively (e.g. when the user enables LAN Sync) for a smoother
	// first-time prompt. Non-blocking; discovery/announce retries after grant.
	public static void ensurePermissions(Activity activity) {
		if (sInstance != null) sInstance.requestPermissionsInternal(activity);
	}

	private void requestPermissionsInternal(Activity activity) {
		if (activity == null) return;
		List<String> needed = new ArrayList<>();
		if (android.os.Build.VERSION.SDK_INT >= 34) {
			needed.add("android.permission.ACCESS_LOCAL_NETWORK");
		}
		if (android.os.Build.VERSION.SDK_INT >= 26 && android.os.Build.VERSION.SDK_INT <= 32) {
			needed.add(android.Manifest.permission.ACCESS_FINE_LOCATION);
		}
		if (needed.isEmpty()) return;
		if (activity.isFinishing() || activity.isDestroyed()) return;
		String[] arr = new String[needed.size()];
		needed.toArray(arr);
		activity.requestPermissions(arr, REQ_LANSYNC_PERMS);
	}

	// --- Deferred start: remember intent, request perm if needed, replay on grant ---
	private void startDiscoveryGuarded(String serviceType) {
		mPendingDiscoveryType = serviceType;
		mDiscoveryPending = true;
		if (ensurePermissionsGranted()) {
			startDiscoveryInternal(serviceType);
		} else if (mActivity != null) {
			maybeRequestPermissions();
		}
	}

	private void startAnnounceGuarded(String serviceType, int port, String deviceName, String peerId) {
		mPendingAnnounceType = serviceType;
		mPendingAnnouncePort = port;
		mPendingAnnounceName = deviceName;
		mPendingAnnouncePeerId = peerId;
		mAnnouncePending = true;
		if (ensurePermissionsGranted()) {
			startAnnounceInternal(serviceType, port, deviceName, peerId);
		} else if (mActivity != null) {
			maybeRequestPermissions();
		}
	}

	public static void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
		if (sInstance != null) sInstance.onPermissionResultInternal(requestCode, grantResults);
	}

	private void onPermissionResultInternal(int requestCode, int[] grantResults) {
		if (requestCode != REQ_LANSYNC_PERMS) return;
		boolean granted = (grantResults != null && grantResults.length > 0 &&
			grantResults[0] == PackageManager.PERMISSION_GRANTED);
		if (!granted) return;
		// Replay any discovery/announce that was deferred on the missing permission.
		if (mDiscoveryPending) {
			mDiscoveryPending = false;
			startDiscoveryInternal(mPendingDiscoveryType);
		}
		if (mAnnouncePending) {
			mAnnouncePending = false;
			startAnnounceInternal(mPendingAnnounceType, mPendingAnnouncePort, mPendingAnnounceName, mPendingAnnouncePeerId);
		}
	}

	private void startDiscoveryInternal(String serviceType) {
		if (mNsdManager == null) return;
		acquireMulticastLock();

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
					ResolvedPeer rp = new ResolvedPeer();
					rp.host = host;
					rp.port = resolvedInfo.getPort();
					rp.peerId = peerId;
					mResolvedCache.put(resolvedInfo.getServiceName(), rp);
					nativeOnPeerFound(
						resolvedInfo.getServiceName(),
						host,
						resolvedInfo.getPort(),
						resolvedInfo.getServiceType(),
						peerId
					);
					}
				};
				mNsdManager.resolveService(info, resolveListener);
			}

			@Override
			public void onServiceLost(NsdServiceInfo info) {
				Log.d(TAG, "Service lost: " + info.getServiceName());
				// Prefer the cached host/port/peerId from resolution; fall back
				// to whatever the loss callback carries (often empty).
				ResolvedPeer rp = mResolvedCache.remove(info.getServiceName());
				String host;
				int port;
				String peerId;
				if (rp != null) {
					host = rp.host;
					port = rp.port;
					peerId = rp.peerId;
				} else {
					host = info.getHost() != null ? info.getHost().getHostAddress() : "";
					port = info.getPort();
					peerId = "";
					if (android.os.Build.VERSION.SDK_INT >= 16) {
						java.util.Map<String, byte[]> attrs = info.getAttributes();
						if (attrs != null && attrs.containsKey("id")) {
							peerId = new String(attrs.get("id"));
						}
					}
				}
				nativeOnPeerLost(info.getServiceName(), host, port, info.getServiceType(), peerId);
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
		releaseMulticastLock();
		mDiscoveryPending = false;
	}

	private void startAnnounceInternal(String serviceType, int port, String deviceName, String peerId) {
		if (mNsdManager == null) return;
		acquireMulticastLock();

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
		releaseMulticastLock();
		mAnnouncePending = false;
	}
}
