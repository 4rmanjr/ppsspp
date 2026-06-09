#!/system/bin/sh
# LAN Sync E2E Test Script for Android
# Run on both devices to test discovery and sync
# Usage: sh /sdcard/PSP/test_lansync.sh

echo "=== LAN Sync E2E Test ==="
echo "Device: $(getprop ro.product.model)"
echo "IP: $(ip route get 1 | head -1 | awk '{print $7}')"
echo ""

# Test 1: Check if PSP save states exist
SAVE_DIR="/sdcard/PSP/PPSSPP_STATE"
if [ -d "$SAVE_DIR" ]; then
    echo "[1] Save directory: OK"
    ls -la "$SAVE_DIR"/*.ppst 2>/dev/null | head -5
else
    echo "[1] Save directory: NOT FOUND"
fi

# Test 2: Check network connectivity
echo ""
echo "[2] Network connectivity:"
ping -c 1 192.168.1.1 > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  Gateway: OK"
else
    echo "  Gateway: FAILED"
fi

# Test 3: Check if port 27313 is available
echo ""
echo "[3] Port check:"
netstat -tuln 2>/dev/null | grep 27313 || echo "  Port 27313: Available"

echo ""
echo "=== Ready for E2E test ==="
echo "1. Install PPSSPP APK with LAN sync"
echo "2. Enable LAN sync in Settings > Network"
echo "3. Pair devices using PIN or QR"
echo "4. Save state on device A"
echo "5. Sync from device B"
echo "6. Load state on device B"
