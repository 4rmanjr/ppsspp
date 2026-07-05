package org.ppsspp.ppsspp;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Binder;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import java.util.Map;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

/**
 * ForegroundService for LAN Save State Sync.
 * Handles NsdManager registration/discovery and sync operations.
 *
 * Lifecycle:
 *   Start → Register mDNS service → Discover peers → Accept connections → Stop
 */
public class LANSyncService extends Service {
    private static final String TAG = "LANSyncService";
    private static final String CHANNEL_ID = "ppsspp_lansync";
    private static final int NOTIFICATION_ID = 27313;

    private static final String SERVICE_TYPE = "_ppsspp-sync._tcp.local.";

    private NsdManager nsdManager;
    private NsdManager.RegistrationListener registrationListener;
    private NsdManager.DiscoveryListener discoveryListener;

    private String deviceId;
    private String deviceName;
    private int serverPort;
    private boolean isRegistered = false;
    private boolean isDiscovering = false;
    private boolean isSyncing = false;
    private int syncProgress = 0;
    private int syncTotal = 0;

    private final IBinder binder = new LocalBinder();

    // Callback for discovered peers
    public interface PeerDiscoveryCallback {
        void onPeerFound(String id, String name, String host, int port, String fingerprint, String device);
        void onPeerLost(String id);
    }

    private PeerDiscoveryCallback peerCallback;

    public class LocalBinder extends Binder {
        public LANSyncService getService() {
            return LANSyncService.this;
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && "STOP".equals(intent.getAction())) {
            stopForeground(true);
            stopSelf();
            return START_NOT_STICKY;
        }
        // Restart if killed - important for long-running sync
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopDiscovery();
        unregisterService();
        super.onDestroy();
    }

    // ==================== Notification ====================

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "PPSSPP LAN Sync",
                NotificationManager.IMPORTANCE_LOW
            );
            channel.setDescription("Syncing save states over local network");
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private Notification buildNotification(String status) {
        return new NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle("PPSSPP LAN Sync")
            .setContentText(status)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .build();
    }

    public void updateNotification(String status) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, buildNotification(status));
        }
    }

    public void updateSyncProgress(int completed, int total, long completedBytes, long totalBytes) {
        this.syncProgress = completed;
        this.syncTotal = total;
        this.isSyncing = true;

        String status;
        if (totalBytes > 0) {
            String progressBytes = formatBytes(completedBytes);
            String totalBytesStr = formatBytes(totalBytes);
            status = String.format("Syncing: %d/%d slots (%s/%s)", completed, total, progressBytes, totalBytesStr);
        } else {
            status = String.format("Syncing: %d/%d slots", completed, total);
        }

        Notification notification = new NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle("PPSSPP LAN Sync")
            .setContentText(status)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .setProgress(total, completed, false)
            .build();

        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, notification);
        }
    }

    private String formatBytes(long bytes) {
        if (bytes < 1024) {
            return bytes + " B";
        } else if (bytes < 1024 * 1024) {
            return String.format("%.1f KB", bytes / 1024.0);
        } else if (bytes < 1024L * 1024 * 1024) {
            return String.format("%.1f MB", bytes / (1024.0 * 1024.0));
        } else {
            return String.format("%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
        }
    }

    public void completeSync(int uploaded, int downloaded) {
        this.isSyncing = false;
        String status = String.format("Sync complete: %d up, %d down", uploaded, downloaded);
        updateNotification(status);
    }

    // ==================== NsdManager Registration ====================

    public void registerService(String name, int port, PeerDiscoveryCallback callback, String deviceId) {
        this.deviceName = name;
        this.serverPort = port;
        this.peerCallback = callback;
        this.deviceId = deviceId;
        this.nsdManager = (NsdManager) getSystemService(Context.NSD_SERVICE);

        if (nsdManager == null) {
            Log.e(TAG, "NsdManager not available");
            return;
        }

        NsdServiceInfo serviceInfo = new NsdServiceInfo();
        serviceInfo.setServiceName(name);
        serviceInfo.setServiceType(SERVICE_TYPE);
        serviceInfo.setPort(port);

        // TXT records
        serviceInfo.setAttribute("version", "1");
        serviceInfo.setAttribute("device", "Android");
        serviceInfo.setAttribute("name", name);
        serviceInfo.setAttribute("id", deviceId);

        registrationListener = new NsdManager.RegistrationListener() {
            @Override
            public void onServiceRegistered(NsdServiceInfo info) {
                Log.i(TAG, "Service registered: " + info.getServiceName());
                isRegistered = true;
                // Service name may be modified by NsdManager
                LANSyncService.this.deviceId = info.getServiceName();
            }

            @Override
            public void onRegistrationFailed(NsdServiceInfo info, int errorCode) {
                Log.e(TAG, "Registration failed: " + errorCode);
                isRegistered = false;
            }

            @Override
            public void onServiceUnregistered(NsdServiceInfo info) {
                Log.i(TAG, "Service unregistered");
                isRegistered = false;
            }

            @Override
            public void onUnregistrationFailed(NsdServiceInfo info, int errorCode) {
                Log.e(TAG, "Unregistration failed: " + errorCode);
            }
        };

        nsdManager.registerService(serviceInfo, NsdManager.PROTOCOL_DNS_SD, registrationListener);

        // Start foreground notification (Android 13+ requires POST_NOTIFICATIONS permission)
        try {
            startForeground(NOTIFICATION_ID, buildNotification("LAN Sync active - port " + port));
        } catch (SecurityException e) {
            Log.w(TAG, "Cannot show notification — POST_NOTIFICATIONS permission not granted");
            // Service still runs, just without visible notification
        }
    }

    public void unregisterService() {
        if (nsdManager != null && registrationListener != null && isRegistered) {
            try {
                nsdManager.unregisterService(registrationListener);
            } catch (Exception e) {
                Log.w(TAG, "Failed to unregister service", e);
            }
            isRegistered = false;
        }
    }

    // ==================== NsdManager Discovery ====================

    public void startDiscovery() {
        if (nsdManager == null) {
            nsdManager = (NsdManager) getSystemService(Context.NSD_SERVICE);
        }
        if (nsdManager == null || isDiscovering) return;

        discoveryListener = new NsdManager.DiscoveryListener() {
            @Override
            public void onDiscoveryStarted(String serviceType) {
                Log.i(TAG, "Discovery started");
                isDiscovering = true;
            }

            @Override
            public void onServiceFound(NsdServiceInfo serviceInfo) {
                Log.d(TAG, "Service found: " + serviceInfo.getServiceName());
                // Don't resolve our own service
                if (serviceInfo.getServiceName().equals(deviceId)) return;

                nsdManager.resolveService(serviceInfo, new NsdManager.ResolveListener() {
                    @Override
                    public void onResolveFailed(NsdServiceInfo info, int errorCode) {
                        Log.w(TAG, "Resolve failed: " + errorCode);
                    }

                    @Override
                    public void onServiceResolved(NsdServiceInfo info) {
                        String host = info.getHost() != null ? info.getHost().getHostAddress() : "";
                        int port = info.getPort();

                        // Extract TXT records
                        String name = info.getServiceName();
                        String id = "";
                        String fingerprint = "";
                        String device = "Unknown";

                        Map<String, byte[]> attributes = info.getAttributes();
                        if (attributes != null) {
                            byte[] idBytes = attributes.get("id");
                            if (idBytes != null) id = new String(idBytes, java.nio.charset.StandardCharsets.UTF_8);
                            byte[] fpBytes = attributes.get("fp");
                            if (fpBytes != null) fingerprint = new String(fpBytes, java.nio.charset.StandardCharsets.UTF_8);
                            byte[] devBytes = attributes.get("device");
                            if (devBytes != null) device = new String(devBytes, java.nio.charset.StandardCharsets.UTF_8);
                        }

                        // Notify callback
                        if (peerCallback != null) {
                            peerCallback.onPeerFound(id, name, host, port, fingerprint, device);
                        }
                    }
                });
            }

            @Override
            public void onServiceLost(NsdServiceInfo serviceInfo) {
                Log.d(TAG, "Service lost: " + serviceInfo.getServiceName());
                if (peerCallback != null) {
                    peerCallback.onPeerLost(serviceInfo.getServiceName());
                }
            }

            @Override
            public void onDiscoveryStopped(String serviceType) {
                Log.i(TAG, "Discovery stopped");
                isDiscovering = false;
            }

            @Override
            public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "Discovery start failed: " + errorCode);
                isDiscovering = false;
            }

            @Override
            public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                Log.e(TAG, "Discovery stop failed: " + errorCode);
            }
        };

        try {
            nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener);
        } catch (SecurityException e) {
            Log.e(TAG, "Cannot discover services — NEARBY_WIFI_DEVICES permission needed (Android 13+)");
            isDiscovering = false;
        }
    }

    public void stopDiscovery() {
        if (nsdManager != null && discoveryListener != null && isDiscovering) {
            try {
                nsdManager.stopServiceDiscovery(discoveryListener);
            } catch (Exception e) {
                Log.w(TAG, "Failed to stop discovery", e);
            }
            isDiscovering = false;
        }
    }

    // ==================== State ====================

    public boolean isRegistered() { return isRegistered; }
    public boolean isDiscovering() { return isDiscovering; }
    public String getDeviceName() { return deviceName; }
    public int getServerPort() { return serverPort; }
}
