// [PPSSPP-FORK] LANSync: JNI bridge between C++ and LANSyncManager
package org.ppsspp.ppsspp;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Base64;
import android.util.Log;

import java.util.Map;

public class LANSyncActivity {
    private static final String TAG = "LANSyncActivity";
    private static boolean initialized = false;
    private static Context appContext = null;

    // Called by C++ JNI_OnLoad to cache method IDs
    public static native int registerNatives();

    // Called from PpssppActivity.onCreate() to set Context
    public static void init(Context context) {
        if (initialized) return;
        appContext = context.getApplicationContext();
        new LANSyncManager(appContext);
        initialized = true;
        Log.i(TAG, "LANSyncActivity initialized, LANSyncManager created");
    }

    // ==================== JNI bridge methods ====================

    public static void registerService(String name, int port, Map<String, String> txt) {
        Log.d(TAG, "registerService: " + name + " on port " + port);
        LANSyncManager.startSyncService(name, port);
    }

    public static void unregisterService() {
        Log.d(TAG, "unregisterService");
        LANSyncManager.stopSyncService();
    }

    public static void discoverServices() {
        Log.d(TAG, "discoverServices");
        LANSyncManager.startPeerDiscovery();
    }

    public static void stopServiceDiscovery() {
        Log.d(TAG, "stopServiceDiscovery");
        LANSyncManager.stopPeerDiscovery();
    }

    public static void startForegroundSync(String title, int max) {
        Log.d(TAG, "startForegroundSync: " + title);
        // Already handled by LANSyncService.startForeground() during registerService
    }

    public static void stopForegroundSync() {
        Log.d(TAG, "stopForegroundSync");
        LANSyncManager.stopSyncService();
    }

    public static void startQRScan() {
        Log.d(TAG, "startQRScan");
        LANSyncManager mgr = LANSyncManager.getInstance();
        if (mgr != null) mgr.startQRScan();
    }

    public static String keystoreStore(String alias, byte[] data) {
        Log.d(TAG, "keystoreStore: " + alias);
        if (appContext == null) return "";
        SharedPreferences prefs = appContext.getSharedPreferences("ppsspp_lansync_secrets", Context.MODE_PRIVATE);
        String encoded = Base64.encodeToString(data, Base64.DEFAULT);
        prefs.edit().putString("ks_" + alias, encoded).apply();
        return "ok";
    }

    public static byte[] keystoreLoad(String alias) {
        Log.d(TAG, "keystoreLoad: " + alias);
        if (appContext == null) return new byte[0];
        SharedPreferences prefs = appContext.getSharedPreferences("ppsspp_lansync_secrets", Context.MODE_PRIVATE);
        String encoded = prefs.getString("ks_" + alias, null);
        if (encoded == null) return new byte[0];
        return Base64.decode(encoded, Base64.DEFAULT);
    }

    public static void updateSyncProgress(int completed, int total, long completedBytes, long totalBytes) {
        LANSyncManager.updateSyncProgress(completed, total, completedBytes, totalBytes);
    }

    public static void completeSync(int uploaded, int downloaded) {
        LANSyncManager.completeSync(uploaded, downloaded);
    }
}
