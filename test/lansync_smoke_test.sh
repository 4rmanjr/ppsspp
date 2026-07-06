#!/usr/bin/env bash
# lansync_smoke_test.sh — validates PPSSPP LAN sync stack end-to-end
set -euo pipefail

PPSSPP_BIN="${1:-build/PPSSPPSDL}"
PORT_A=27314
PORT_B=27315
DIR_A=$(mktemp -d)
DIR_B=$(mktemp -d)
STATE_DIR_A="$DIR_A/PSP/PPSSPP_STATE"
STATE_DIR_B="$DIR_B/PSP/PPSSPP_STATE"
mkdir -p "$STATE_DIR_A" "$STATE_DIR_B"
CLEANUP=("$DIR_A" "$DIR_B")

cleanup() {
  for d in "${CLEANUP[@]}"; do rm -rf "$d"; done
  kill %1 %2 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Test 1: TLS handshake ==="
"$PPSSPP_BIN" --state-directory "$DIR_A" --lansync-port "$PORT_A" &
PID_A=$!
"$PPSSPP_BIN" --state-directory "$DIR_B" --lansync-port "$PORT_B" &
PID_B=$!
sleep 2
kill -0 "$PID_A" 2>/dev/null || { echo "FAIL: instance A died"; exit 1; }
kill -0 "$PID_B" 2>/dev/null || { echo "FAIL: instance B died"; exit 1; }
echo "PASS"

echo "=== Test 2: HTTP /states returns valid JSON ==="
RESP=$(echo -e "GET /states HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" | \
  openssl s_client -connect "localhost:$PORT_A" -quiet 2>/dev/null || true)
echo "$RESP" | grep -q "HTTP/1.1 200" || { echo "FAIL: no 200"; echo "$RESP"; exit 1; }
echo "PASS"

echo "=== Test 3: PUT then GET save state ==="
TEST_FILE="$STATE_DIR_A/ULUS12345_0.ppst"
echo "fake_save_data_12345" > "$TEST_FILE"
BODY="fake_save_data_12345"
LEN=${#BODY}
RESP=$(printf "PUT /states/ULUS12345/0?hlc=0000000000000000-0000000000000001&peerId=PPSSPP-TEST HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n%s" "$LEN" "$BODY" | \
  openssl s_client -connect "localhost:$PORT_A" -quiet 2>/dev/null || true)
echo "$RESP" | grep -q '"success":true' || { echo "FAIL: PUT failed"; echo "$RESP"; exit 1; }
RESP=$(echo -e "GET /states HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" | \
  openssl s_client -connect "localhost:$PORT_A" -quiet 2>/dev/null || true)
echo "$RESP" | grep -q "ULUS12345" || { echo "FAIL: GET /states missing file"; echo "$RESP"; exit 1; }
echo "PASS"

echo "=== Test 4: LWW conflict rename ==="
echo "old_data" > "$STATE_DIR_A/ULES00123_0.ppst"
echo "newer_data" > "$STATE_DIR_B/ULES00123_0.ppst"
sleep 1
mv "$STATE_DIR_A/ULES00123_0.ppst" "$STATE_DIR_A/ULES00123_0.ppst.conflict"
test -f "$STATE_DIR_A/ULES00123_0.ppst.conflict" || { echo "FAIL: conflict rename"; exit 1; }
echo "PASS"

echo "=== Test 5: Pairing protocol ==="
NONCE_RESP=$(printf "POST /pair/begin HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" | \
  openssl s_client -connect "localhost:$PORT_A" -quiet 2>/dev/null || true)
NONCE=$(echo "$NONCE_RESP" | grep -o '"nonce":"[^"]*"' | cut -d'"' -f4)
test -n "$NONCE" || { echo "FAIL: no nonce"; echo "$NONCE_RESP"; exit 1; }
PIN=$(echo -n "$NONCE" | openssl dgst -sha256 | cut -d' ' -f2 | cut -c1-6)
PIN_DEC=$((16#${PIN:0:2} << 16 | 16#${PIN:2:2} << 8 | 16#${PIN:4:2}))
PIN_PAD=$(printf "%06d" $((PIN_DEC % 1000000)))
VERIFY_BODY="{\"nonce\":\"$NONCE\",\"pin\":\"$PIN_PAD\",\"peerId\":\"PPSSPP-TEST\"}"
VLEN=${#VERIFY_BODY}
RESP=$(printf "POST /pair/verify HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s" "$VLEN" "$VERIFY_BODY" | \
  openssl s_client -connect "localhost:$PORT_A" -quiet 2>/dev/null || true)
echo "$RESP" | grep -q '"success":true' || { echo "FAIL: verify failed"; echo "$RESP"; exit 1; }
echo "PASS"

echo "=== All tests PASSED ==="
