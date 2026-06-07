package org.ppsspp.ppsspp;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import android.util.Log;

import androidx.security.crypto.EncryptedSharedPreferences;
import androidx.security.crypto.MasterKey;

import java.io.IOException;
import java.security.GeneralSecurityException;

/**
 * Secure storage for LAN Sync tokens and secrets.
 * Uses Android Keystore + EncryptedSharedPreferences for API 23+.
 * Falls back to regular SharedPreferences (less secure) on older devices.
 */
public class LANSyncKeystore {
    private static final String TAG = "LANSyncKeystore";
    private static final String PREFS_NAME = "ppsspp_lansync_secrets";

    // ==================== Encrypted Storage (API 23+) ====================

    private static SharedPreferences getEncryptedPrefs(Context context) {
        try {
            MasterKey masterKey = new MasterKey.Builder(context)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build();

            return EncryptedSharedPreferences.create(
                context,
                PREFS_NAME,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            );
        } catch (GeneralSecurityException | IOException e) {
            Log.e(TAG, "Failed to create encrypted prefs", e);
            // Fallback to regular SharedPreferences
            return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        }
    }

    // ==================== Public API ====================

    public static boolean saveToken(Context context, String peerId, String token) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            prefs.edit().putString("token_" + peerId, token).apply();
            Log.d(TAG, "Token saved for peer: " + peerId);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to save token", e);
            return false;
        }
    }

    public static String loadToken(Context context, String peerId) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            return prefs.getString("token_" + peerId, null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to load token", e);
            return null;
        }
    }

    public static boolean deleteToken(Context context, String peerId) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            prefs.edit().remove("token_" + peerId).apply();
            Log.d(TAG, "Token deleted for peer: " + peerId);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to delete token", e);
            return false;
        }
    }

    // ==================== Certificate Fingerprint ====================

    public static boolean saveCertFingerprint(Context context, String peerId, String fingerprint) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            prefs.edit().putString("fp_" + peerId, fingerprint).apply();
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to save fingerprint", e);
            return false;
        }
    }

    public static String loadCertFingerprint(Context context, String peerId) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            return prefs.getString("fp_" + peerId, null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to load fingerprint", e);
            return null;
        }
    }

    // ==================== Paired Peers ====================

    public static boolean savePairedPeers(Context context, String peersJson) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            prefs.edit().putString("paired_peers", peersJson).apply();
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to save paired peers", e);
            return false;
        }
    }

    public static String loadPairedPeers(Context context) {
        try {
            SharedPreferences prefs = getEncryptedPrefs(context);
            return prefs.getString("paired_peers", "[]");
        } catch (Exception e) {
            Log.e(TAG, "Failed to load paired peers", e);
            return "[]";
        }
    }

    // ==================== Device Identity ====================

    public static String getDeviceId(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
        String id = prefs.getString("device_id", null);
        if (id == null) {
            id = java.util.UUID.randomUUID().toString();
            prefs.edit().putString("device_id", id).apply();
        }
        return id;
    }
}
