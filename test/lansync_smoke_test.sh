#!/usr/bin/env bash
# lansync_smoke_test.sh — validates PPSSPP LAN sync REST API + TLS end-to-end
# Usage: DISPLAY=:99 ./test/lansync_smoke_test.sh build/PPSSPPSDL
# Requires: Xvfb running on $DISPLAY, openssl

PPSSPP_BIN="${1:-build/PPSSPPSDL}"
if [ ! -f "$PPSSPP_BIN" ]; then
    echo "Error: PPSSPPSDL binary not found at $PPSSPP_BIN"
    echo "Build first: cmake -B build -DPPSSPP_LANSYNC=ON && cmake --build build"
    exit 1
fi
PORT=${LANSYNC_PORT:-27314}
TMP_HOME=$(mktemp -d)
STATE_DIR="$TMP_HOME/.config/ppsspp/PSP/PPSSPP_STATE"
mkdir -p "$STATE_DIR"
PASS_COUNT=0
FAIL_COUNT=0
TEST_COUNT=0
PPSSPP_PID=""

cleanup() {
  if [ -n "$PPSSPP_PID" ] && kill -0 "$PPSSPP_PID" 2>/dev/null; then
    kill "$PPSSPP_PID" 2>/dev/null
    wait "$PPSSPP_PID" 2>/dev/null || true
  fi
  fuser -k "${PORT}/tcp" 2>/dev/null || true
  rm -rf "$TMP_HOME"
}
trap cleanup EXIT

pass() {
  echo "  PASS"
  ((PASS_COUNT++))
}

fail() {
  echo "  FAIL: $1"
  ((FAIL_COUNT++))
}

# Send raw HTTP request via openssl, print response to stdout
# Usage: https_get "/path"
# Usage: https_put "/path" "body"
https_get() {
  local path="$1"
  printf "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" "$path" | \
    openssl s_client -connect "localhost:$PORT" -quiet 2>/dev/null || true
}

https_put() {
  local path="$1" body="$2"
  local len=${#body}
  printf "PUT %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n%s" \
    "$path" "$len" "$body" | \
    openssl s_client -connect "localhost:$PORT" -quiet 2>/dev/null || true
}

https_post() {
  local path="$1" body="$2"
  local len=${#body}
  printf "POST %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s" \
    "$path" "$len" "$body" | \
    openssl s_client -connect "localhost:$PORT" -quiet 2>/dev/null || true
}

# Extract status line and body from HTTP response
# Usage: resp=$(https_get "/foo"); status=$(get_status "$resp"); body=$(get_body "$resp")
get_status() { echo "$1" | head -1; }
get_body() { echo "$1" | sed '1,/^\r\{0,1\}$/d'; }

echo "============================================"
echo "  LAN Sync Integration Smoke Test"
echo "============================================"
echo ""

# =================================================
echo "--- Test 1: Process launch ---"
HOME="$TMP_HOME" "$PPSSPP_BIN" --lansync-enabled --lansync-port="$PORT" &>/dev/null &
PPSSPP_PID=$!
sleep 3
if kill -0 "$PPSSPP_PID" 2>/dev/null; then
  pass
else
  fail "PPSSPPSDL died on startup"
  exit 1
fi

# =================================================
echo "--- Test 2: GET /states returns empty JSON array ---"
RESP=$(https_get "/states")
STATUS=$(get_status "$RESP")
BODY=$(get_body "$RESP")
if echo "$STATUS" | grep -q "200"; then
  if echo "$BODY" | grep -q '^\[\]$'; then
    pass
  else
    fail "expected '[]' got: $BODY"
  fi
else
  fail "expected HTTP 200 got: $STATUS"
fi

# =================================================
echo "--- Test 3: PUT then GET save state ---"
SAVE_DATA="fake_save_data_12345"
PUT_RESP=$(https_put "/states/ULUS12345/0?hlc=0000000000000000-0000000000000001&peerId=PPSSPP-TEST" "$SAVE_DATA")
if echo "$PUT_RESP" | grep -q '"success":true'; then
  pass
else
  fail "PUT failed: $(echo "$PUT_RESP" | tail -1)"
fi

# GET it back
GET_RESP=$(https_get "/states/ULUS12345/0")
GET_STATUS=$(get_status "$GET_RESP")
GET_BODY=$(get_body "$GET_RESP")
if echo "$GET_STATUS" | grep -q "200"; then
  if [ "$GET_BODY" = "$SAVE_DATA" ]; then
    pass
  else
    fail "GET body mismatch: expected '$SAVE_DATA' got '$GET_BODY'"
  fi
else
  fail "GET failed: $GET_STATUS"
fi

# Verify /states lists it
LIST_RESP=$(https_get "/states")
LIST_BODY=$(get_body "$LIST_RESP")
if echo "$LIST_BODY" | grep -q "ULUS12345"; then
  pass
else
  fail "ULUS12345 not found in /states: $LIST_BODY"
fi

# =================================================
echo "--- Test 4: Conflict file exclusion ---"
echo "conflict_data" > "$STATE_DIR/ULUS12345_0.ppst.conflict"
CONF_RESP=$(https_get "/states")
CONF_BODY=$(get_body "$CONF_RESP")
if echo "$CONF_BODY" | grep -q "conflict"; then
  fail "conflict file appears in /states listing: $CONF_BODY"
else
  pass
fi
rm -f "$STATE_DIR/ULUS12345_0.ppst.conflict"

# =================================================
echo "--- Test 5: HTTP 404 for nonexistent file ---"
MISS_RESP=$(https_get "/states/NONEXIST/99")
MISS_BODY=$(get_body "$MISS_RESP")
if echo "$MISS_BODY" | grep -q "not_found"; then
  pass
else
  fail "expected 'not_found', got: $MISS_BODY"
fi

# =================================================
echo "--- Test 6: Pairing protocol ---"

# Step 6a: POST /pair/begin
BEGIN_RESP=$(https_post "/pair/begin" "")
NONCE=$(echo "$BEGIN_RESP" | grep -o '"nonce":"[^"]*"' | cut -d'"' -f4)
FINGERPRINT=$(echo "$BEGIN_RESP" | grep -o '"certFingerprint":"[^"]*"' | cut -d'"' -f4)
if [ -n "$NONCE" ]; then
  pass
else
  fail "no nonce in /pair/begin response: $(echo "$BEGIN_RESP" | tail -1)"
fi

# Step 6b: Compute PIN matching PairingManager::ComputePin()
#   SHA256(nonce) → first 3 bytes → (b0<<16 | b1<<8 | b2) % 1000000 → 6-digit
PIN_HEX=$(printf '%s' "$NONCE" | openssl dgst -sha256 | cut -d' ' -f2 | cut -c1-6)
PIN_DEC=$((16#${PIN_HEX:0:2} << 16 | 16#${PIN_HEX:2:2} << 8 | 16#${PIN_HEX:4:2}))
PIN_PAD=$(printf "%06d" $((PIN_DEC % 1000000)))

# Step 6c: POST /pair/verify with CORRECT pin
VERIFY_BODY="{\"nonce\":\"$NONCE\",\"pin\":\"$PIN_PAD\",\"peerId\":\"PPSSPP-TEST\"}"
VERIFY_RESP=$(https_post "/pair/verify" "$VERIFY_BODY")
if echo "$VERIFY_RESP" | grep -q '"success":true'; then
  pass
else
  fail "verify with correct pin failed: $(echo "$VERIFY_RESP" | tail -1)"
fi

# Step 6d: POST /pair/verify with WRONG pin
WRONG_BODY="{\"nonce\":\"$NONCE\",\"pin\":\"000000\",\"peerId\":\"PPSSPP-TEST\"}"
WRONG_RESP=$(https_post "/pair/verify" "$WRONG_BODY")
if echo "$WRONG_RESP" | grep -q '"success":false'; then
  pass
else
  fail "wrong pin should fail: $(echo "$WRONG_RESP" | tail -1)"
fi

# =================================================
echo ""
echo "--------------------------------------------"
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo "--------------------------------------------"

if [ "$FAIL_COUNT" -eq 0 ]; then
  echo "=== All tests PASSED ==="
  exit 0
else
  echo "=== Some tests FAILED ==="
  exit 1
fi
