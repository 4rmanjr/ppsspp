package org.ppsspp.ppsspp.test;

import android.app.Activity;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.TextView;
import android.content.Context;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.InetSocketAddress;

/**
 * Minimal LAN Sync test app.
 * Tests: mDNS discovery, HTTP server, pairing, save state sync.
 */
public class LANSyncTestActivity extends Activity {
    private static final String TAG = "LANSyncTest";
    private static final String SERVICE_TYPE = "_ppsspp-sync._tcp.local.";
    
    private TextView logView;
    private ScrollView scrollView;
    private NsdManager nsdManager;
    private ServerSocket httpServer;
    private int serverPort = 0;
    private boolean isRunning = false;
    private String deviceId;
    private String deviceName;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Simple layout
        scrollView = new ScrollView(this);
        logView = new TextView(this);
        logView.setPadding(16, 16, 16, 16);
        logView.setTextSize(12);
        scrollView.addView(logView);
        
        android.widget.LinearLayout layout = new android.widget.LinearLayout(this);
        layout.setOrientation(android.widget.LinearLayout.VERTICAL);
        
        // Device name input
        EditText nameInput = new EditText(this);
        nameInput.setHint("Device Name");
        nameInput.setText("TestDevice-" + System.currentTimeMillis() % 1000);
        layout.addView(nameInput);
        
        // Buttons
        Button startBtn = new Button(this);
        startBtn.setText("Start Server");
        startBtn.setOnClickListener(v -> {
            deviceName = nameInput.getText().toString();
            startServer();
        });
        layout.addView(startBtn);
        
        Button discoverBtn = new Button(this);
        discoverBtn.setText("Discover Peers");
        discoverBtn.setOnClickListener(v -> discoverPeers());
        layout.addView(discoverBtn);
        
        Button syncBtn = new Button(this);
        syncBtn.setText("Sync Now");
        syncBtn.setOnClickListener(v -> syncTest());
        layout.addView(syncBtn);
        
        layout.addView(scrollView);
        setContentView(layout);
        
        deviceId = "test-" + System.currentTimeMillis() % 10000;
        log("Device ID: " + deviceId);
    }
    
    private void log(String msg) {
        runOnUiThread(() -> {
            logView.append(msg + "\n");
            scrollView.fullScroll(ScrollView.FOCUS_DOWN);
        });
        Log.d(TAG, msg);
    }
    
    private void startServer() {
        new Thread(() -> {
            try {
                httpServer = new ServerSocket(0); // Auto-assign port
                serverPort = httpServer.getLocalPort();
                isRunning = true;
                log("Server started on port " + serverPort);
                
                // Register mDNS service
                registerService();
                
                // Accept connections
                while (isRunning) {
                    Socket client = httpServer.accept();
                    handleClient(client);
                }
            } catch (Exception e) {
                log("Server error: " + e.getMessage());
            }
        }).start();
    }
    
    private void registerService() {
        nsdManager = (NsdManager) getSystemService(Context.NSD_SERVICE);
        
        NsdServiceInfo serviceInfo = new NsdServiceInfo();
        serviceInfo.setServiceName(deviceName);
        serviceInfo.setServiceType(SERVICE_TYPE);
        serviceInfo.setPort(serverPort);
        serviceInfo.setAttribute("id", deviceId);
        serviceInfo.setAttribute("name", deviceName);
        serviceInfo.setAttribute("device", "Android");
        
        nsdManager.registerService(serviceInfo, NsdManager.PROTOCOL_DNS_SD, 
            new NsdManager.RegistrationListener() {
                @Override
                public void onServiceRegistered(NsdServiceInfo info) {
                    log("mDNS registered: " + info.getServiceName());
                }
                
                @Override
                public void onRegistrationFailed(NsdServiceInfo info, int errorCode) {
                    log("mDNS registration failed: " + errorCode);
                }
                
                @Override
                public void onServiceUnregistered(NsdServiceInfo info) {
                    log("mDNS unregistered");
                }
                
                @Override
                public void onUnregistrationFailed(NsdServiceInfo info, int errorCode) {
                    log("mDNS unregistration failed: " + errorCode);
                }
            });
    }
    
    private void discoverPeers() {
        log("Discovering peers...");
        
        nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD,
            new NsdManager.DiscoveryListener() {
                @Override
                public void onDiscoveryStarted(String serviceType) {
                    log("Discovery started");
                }
                
                @Override
                public void onServiceFound(NsdServiceInfo serviceInfo) {
                    log("Found: " + serviceInfo.getServiceName());
                    
                    nsdManager.resolveService(serviceInfo, new NsdManager.ResolveListener() {
                        @Override
                        public void onResolveFailed(NsdServiceInfo info, int errorCode) {
                            log("Resolve failed: " + errorCode);
                        }
                        
                        @Override
                        public void onServiceResolved(NsdServiceInfo info) {
                            String host = info.getHost().getHostAddress();
                            int port = info.getPort();
                            log("Resolved: " + info.getServiceName() + " at " + host + ":" + port);
                        }
                    });
                }
                
                @Override
                public void onServiceLost(NsdServiceInfo serviceInfo) {
                    log("Lost: " + serviceInfo.getServiceName());
                }
                
                @Override
                public void onDiscoveryStopped(String serviceType) {
                    log("Discovery stopped");
                }
                
                @Override
                public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                    log("Discovery start failed: " + errorCode);
                }
                
                @Override
                public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                    log("Discovery stop failed: " + errorCode);
                }
            });
    }
    
    private void handleClient(Socket client) {
        new Thread(() -> {
            try {
                BufferedReader in = new BufferedReader(new InputStreamReader(client.getInputStream()));
                String line = in.readLine();
                log("Request: " + line);
                
                // Simple HTTP response
                String response = "{\"status\":\"ok\",\"device\":\"" + deviceName + "\",\"port\":" + serverPort + "}";
                String httpResponse = "HTTP/1.1 200 OK\r\n" +
                    "Content-Type: application/json\r\n" +
                    "Content-Length: " + response.length() + "\r\n" +
                    "Connection: close\r\n\r\n" + response;
                
                OutputStream out = client.getOutputStream();
                out.write(httpResponse.getBytes());
                out.flush();
                
                client.close();
            } catch (Exception e) {
                log("Client error: " + e.getMessage());
            }
        }).start();
    }
    
    private void syncTest() {
        log("Sync test: checking for peers...");
        // TODO: Implement actual sync test
        log("Sync test: not yet implemented");
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        isRunning = false;
        try {
            if (httpServer != null) httpServer.close();
        } catch (Exception e) {}
    }
}
