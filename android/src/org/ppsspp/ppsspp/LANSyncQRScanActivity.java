package org.ppsspp.ppsspp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

/**
 * QR Code scanning activity for LAN sync pairing.
 * Uses simple intent-based approach without CameraX dependency.
 */
public class LANSyncQRScanActivity extends Activity {
    private static final String TAG = "LANSyncQRScan";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Try to launch external QR scanner
        try {
            Intent intent = new Intent("com.google.zxing.client.android.SCAN");
            intent.putExtra("SCAN_MODE", "QR_CODE_MODE");
            startActivityForResult(intent, 0);
        } catch (Exception e) {
            Log.e(TAG, "No QR scanner found", e);
            Toast.makeText(this, "请安装QR码扫描应用", Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        
        if (requestCode == 0 && resultCode == RESULT_OK && data != null) {
            String contents = data.getStringExtra("SCAN_RESULT");
            if (contents != null && contents.startsWith("ppsspp-sync://")) {
                Log.i(TAG, "QR code scanned: " + contents);
                LANSyncManager.onQRCodeScanned(contents);
                Toast.makeText(this, "QR码扫描成功", Toast.LENGTH_SHORT).show();
            } else {
                Toast.makeText(this, "无效的QR码", Toast.LENGTH_SHORT).show();
            }
        }
        finish();
    }
}
