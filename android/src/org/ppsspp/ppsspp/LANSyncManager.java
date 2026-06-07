package org.ppsspp.ppsspp;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

/**
 * Manages LAN Sync service lifecycle and peer discovery.
 * Bridges between JNI C++ code and Android NsdManager/ForegroundService.
 */
public class LANSyncManager {
    private static final String TAG = "LANSyncManager";

    private final Context context;
    private LANSyncService syncService;
    private boolean serviceBound = false;

    // JNI callbacks
    private static LANSyncManager instance;

    public interface PeerCallback {
        void onPeerDiscovered(String id, String name, String host, int port, String fingerprint, String device);
        void onPeerLost(String id);
    }

    private PeerCallback peerCallback;

    public LANSyncManager(Context context) {
        this.context = context.getApplicationContext();
        instance = this;
    }

    public static LANSyncManager getInstance() {
        return instance;
    }

    // ==================== Service Lifecycle ====================

    private ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            LANSyncService.LocalBinder binder = (LANSyncService.LocalBinder) service;
            syncService = binder.getService();
            serviceBound = true;
            Log.i(TAG, "LAN Sync service connected");
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            syncService = null;
            serviceBound = false;
            Log.i(TAG, "LAN Sync service disconnected");
        }
    };

    public void startService(String deviceName, int port) {
        Intent intent = new Intent(context, LANSyncService.class);
        intent.putExtra("deviceName", deviceName);
        intent.putExtra("port", port);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }

        context.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);
    }

    public void stopService() {
        if (serviceBound) {
            context.unbindService(serviceConnection);
            serviceBound = false;
        }
        Intent intent = new Intent(context, LANSyncService.class);
        intent.setAction("STOP");
        context.startService(intent);
    }

    // ==================== Peer Discovery ====================

    public void startDiscovery(PeerCallback callback) {
        this.peerCallback = callback;
        if (syncService != null) {
            syncService.startDiscovery();
        }
    }

    public void stopDiscovery() {
        if (syncService != null) {
            syncService.stopDiscovery();
        }
    }

    // Called from NsdManager callbacks via LANSyncService
    public void onPeerFound(String id, String name, String host, int port, String fingerprint, String device) {
        if (peerCallback != null) {
            peerCallback.onPeerDiscovered(id, name, host, port, fingerprint, device);
        }
        // Notify JNI
        nativeOnPeerFound(id, name, host, port, fingerprint, device);
    }

    public void onPeerLost(String id) {
        if (peerCallback != null) {
            peerCallback.onPeerLost(id);
        }
        // Notify JNI
        nativeOnPeerLost(id);
    }

    // ==================== QR Code ====================

    public void startQRScan() {
        // Launch QR scan activity
        Intent intent = new Intent(context, LANSyncQRScanActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);
    }

    // Called from QR scan activity when result is obtained
    public static void onQRCodeScanned(String payload) {
        Log.i(TAG, "QR code scanned: " + payload);
        // Parse payload: ppsspp-sync://pair?host=...&port=...&fp=...&pin=...&name=...
        nativeOnQRScanned(payload);
    }

    // ==================== Keystore ====================

    public String loadToken(String peerId) {
        return LANSyncKeystore.loadToken(context, peerId);
    }

    public boolean saveToken(String peerId, String token) {
        return LANSyncKeystore.saveToken(context, peerId, token);
    }

    public boolean deleteToken(String peerId) {
        return LANSyncKeystore.deleteToken(context, peerId);
    }

    // ==================== JNI Bridge ====================

    // Called from C++ via JNI
    public static native void nativeOnPeerFound(String id, String name, String host, int port, String fingerprint, String device);
    public static native void nativeOnPeerLost(String id);
    public static native void nativeOnQRScanned(String payload);

    // Called from C++ to control service
    public static void startSyncService(String deviceName, int port) {
        if (instance != null) {
            instance.startService(deviceName, port);
        }
    }

    public static void stopSyncService() {
        if (instance != null) {
            instance.stopService();
        }
    }

    public static void startPeerDiscovery() {
        if (instance != null) {
            instance.startDiscovery(null);
        }
    }

    public static void stopPeerDiscovery() {
        if (instance != null) {
            instance.stopDiscovery();
        }
    }
}
