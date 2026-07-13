#!/usr/bin/env bash
# lansync_e2e_test.sh — end-to-end sync between two PPSSPP instances
# Usage: DISPLAY=:99 ./test/lansync_e2e_test.sh build/PPSSPPSDL
# Requires: Xvfb running on $DISPLAY, openssl

set -uo pipefail

PPSSPP_BIN="${1:-build/PPSSPPSDL}"
if [ ! -f "$PPSSPP_BIN" ]; then
    echo "Error: PPSSPPSDL binary not found at $PPSSPP_BIN"
    echo "Build first: cmake -B build -DPPSSPP_LANSYNC=ON && cmake --build build"
    exit 1
fi

PORT_A=${LANSYNC_PORT_A:-27314}
PORT_B=${LANSYNC_PORT_B:-27315}
TMP_ROOT=$(mktemp -d)
HOME_A="$TMP_ROOT/instance_A"
HOME_B="$TMP_ROOT/instance_B"
CERT_DIR="$TMP_ROOT/certs"
mkdir -p "$HOME_A/.config/ppsspp/PSP/PPSSPP_STATE"
mkdir -p "$HOME_B/.config/ppsspp/PSP/PPSSPP_STATE"
mkdir -p "$CERT_DIR"

PASS_COUNT=0
FAIL_COUNT=0
PID_A=""
PID_B=""

# --- Generate client certificates ---
echo "--- Generating test certificates ---"

openssl ecparam -genkey -name prime256v1 -out "$CERT_DIR/clientA_key.pem" 2>/dev/null
openssl req -new -x509 -key "$CERT_DIR/clientA_key.pem" -out "$CERT_DIR/clientA_cert.pem" -days 3650 \
    -subj '/CN=PPSSPP E2E Client A' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' 2>/dev/null

openssl ecparam -genkey -name prime256v1 -out "$CERT_DIR/clientB_key.pem" 2>/dev/null
openssl req -new -x509 -key "$CERT_DIR/clientB_key.pem" -out "$CERT_DIR/clientB_cert.pem" -days 3650 \
    -subj '/CN=PPSSPP E2E Client B' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' 2>/dev/null

CLIENTA_CERT_PEM=$(cat "$CERT_DIR/clientA_cert.pem")
CLIENTB_CERT_PEM=$(cat "$CERT_DIR/clientB_cert.pem")

cleanup() {
  for pid in "$PID_A" "$PID_B"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null
      sleep 1
      kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  fuser -k "${PORT_A}/tcp" "${PORT_B}/tcp" 2>/dev/null || true
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

pass() { echo "  PASS"; ((PASS_COUNT++)); }
fail() { echo "  FAIL: $1"; ((FAIL_COUNT++)); }

OSSL_TIMEOUT=5

# HTTPS helpers using specific cert
https_get() {
  local path="$1" port="$2" cert_dir="$3"
  printf "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" "$path" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$port" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

https_put() {
  local path="$1" body="$2" port="$3" cert_dir="$4"
  local len=${#body}
  printf "PUT %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n%s" \
    "$path" "$len" "$body" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$port" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

https_post() {
  local path="$1" body="$2" port="$3" cert_dir="$4"
  local len=${#body}
  printf "POST %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s" \
    "$path" "$len" "$body" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$port" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

get_status() { echo "$1" | head -1; }
get_body() { echo "$1" | sed '1,/^\r\{0,1\}$/d'; }

# Pair client cert with server
# Usage: pair_client server_port cert_dir "peerId"
pair_client() {
  local port="$1" cert_dir="$2" peer_id="$3" cert_pem="$4"

  # POST /pair/begin
  BEGIN_BODY="{\"certPEM\":\"$cert_pem\"}"
  BEGIN_RESP=$(https_post "/pair/begin" "$BEGIN_BODY" "$port" "$cert_dir")
  NONCE=$(echo "$BEGIN_RESP" | grep -o '"nonce":"[^"]*"' | cut -d'"' -f4)
  if [ -z "$NONCE" ]; then
    fail "no nonce from server $port"
    return 1
  fi

  # Compute PIN
  PIN_HEX=$(printf '%s' "$NONCE" | openssl dgst -sha256 | cut -d' ' -f2 | cut -c1-6)
  PIN_DEC=$((16#${PIN_HEX:0:2} << 16 | 16#${PIN_HEX:2:2} << 8 | 16#${PIN_HEX:4:2}))
  PIN_PAD=$(printf "%06d" $((PIN_DEC % 1000000)))

  # POST /pair/verify
  VERIFY_BODY="{\"nonce\":\"$NONCE\",\"pin\":\"$PIN_PAD\",\"peerId\":\"$peer_id\"}"
  VERIFY_RESP=$(https_post "/pair/verify" "$VERIFY_BODY" "$port" "$cert_dir")
  if echo "$VERIFY_RESP" | grep -q '"success":true'; then
    return 0
  else
    fail "pair verify failed for peer $peer_id on port $port"
    return 1
  fi
}

echo "============================================"
echo "  LAN Sync End-to-End Test"
echo "============================================"
echo ""

# =================================================
echo "--- Test 1: Launch instance A (port $PORT_A) ---"
HOME="$HOME_A" "$PPSSPP_BIN" --lansync-enabled --lansync-port="$PORT_A" &>/dev/null &
PID_A=$!
sleep 3
if kill -0 "$PID_A" 2>/dev/null; then pass; else fail "instance A died"; exit 1; fi

echo "--- Test 2: Launch instance B (port $PORT_B) ---"
HOME="$HOME_B" "$PPSSPP_BIN" --lansync-enabled --lansync-port="$PORT_B" &>/dev/null &
PID_B=$!
sleep 3
if kill -0 "$PID_B" 2>/dev/null; then pass; else fail "instance B died"; exit 1; fi

# =================================================
echo "--- Test 3: Pair clientA with instance A ---"
pair_client "$PORT_A" "$CERT_DIR/clientA" "CLIENT-A" "$CLIENTA_CERT_PEM" && pass

echo "--- Test 4: Pair clientB with instance B ---"
pair_client "$PORT_B" "$CERT_DIR/clientB" "CLIENT-B" "$CLIENTB_CERT_PEM" && pass

echo "--- Test 5: Cross-pair: clientB trusted on instance A ---"
pair_client "$PORT_A" "$CERT_DIR/clientB" "CLIENT-B-ON-A" "$CLIENTB_CERT_PEM" && pass

echo "--- Test 6: Cross-pair: clientA trusted on instance B ---"
pair_client "$PORT_B" "$CERT_DIR/clientA" "CLIENT-A-ON-B" "$CLIENTA_CERT_PEM" && pass

# =================================================
echo "--- Test 7: PUT save state on A (older HLC=1) ---"
SAVE_A="data_from_instance_A_v1"
HLC_A="1000:0"
PUT_A=$(https_put "/states/E2EGAME/0?hlc=${HLC_A}&peerId=CLIENT-A" "$SAVE_A" "$PORT_A" "$CERT_DIR/clientA")
if echo "$PUT_A" | grep -q '"success":true'; then pass; else fail "PUT on A failed: $(echo "$PUT_A" | tail -1)"; fi

echo "--- Test 8: PUT save state on B (newer HLC=2) ---"
SAVE_B="data_from_instance_B_v2"
HLC_B="2000:0"
PUT_B=$(https_put "/states/E2EGAME/0?hlc=${HLC_B}&peerId=CLIENT-B" "$SAVE_B" "$PORT_B" "$CERT_DIR/clientB")
if echo "$PUT_B" | grep -q '"success":true'; then pass; else fail "PUT on B failed: $(echo "$PUT_B" | tail -1)"; fi

# =================================================
echo "--- Test 9: A lists B's states via cross-access ---"
LIST_B=$(https_get "/states" "$PORT_B" "$CERT_DIR/clientA")
if echo "$LIST_B" | grep -q "E2EGAME"; then
  pass
else
  fail "clientA cannot list B's states: $LIST_B"
fi

# =================================================
echo "--- Test 10: A downloads B's state (HLC=2 newer) ---"
GET_B=$(https_get "/states/E2EGAME/0" "$PORT_B" "$CERT_DIR/clientA")
GET_B_BODY=$(get_body "$GET_B")
if [ "$GET_B_BODY" = "$SAVE_B" ]; then
  pass
else
  fail "A downloaded wrong state from B: expected '$SAVE_B' got '$GET_B_BODY'"
fi

# =================================================
echo "--- Test 11: A updates local state with B's newer version ---"
# Simulate sync: A downloads from B, uploads to A using B's HLC
PUT_UPDATE=$(https_put "/states/E2EGAME/0?hlc=${HLC_B}&peerId=CLIENT-A" "$SAVE_B" "$PORT_A" "$CERT_DIR/clientA")
if echo "$PUT_UPDATE" | grep -q '"success":true'; then pass; else fail "A failed to update with B's data"; fi

# =================================================
echo "--- Test 12: Verify A's state now matches B's state ---"
GET_A_AFTER=$(https_get "/states/E2EGAME/0" "$PORT_A" "$CERT_DIR/clientA")
GET_A_BODY=$(get_body "$GET_A_AFTER")
if [ "$GET_A_BODY" = "$SAVE_B" ]; then
  pass
else
  fail "A's state doesn't match B's after sync: expected '$SAVE_B' got '$GET_A_BODY'"
fi

# =================================================
echo "--- Test 13: Both /states listings agree ---"
LIST_A=$(https_get "/states" "$PORT_A" "$CERT_DIR/clientA")
LIST_B_AFTER=$(https_get "/states" "$PORT_B" "$CERT_DIR/clientB")
echo "$LIST_A" | grep -o '"checksum":"[^"]*"' | cut -d'"' -f4 > "$TMP_ROOT/checksum_A.txt"
echo "$LIST_B_AFTER" | grep -o '"checksum":"[^"]*"' | cut -d'"' -f4 > "$TMP_ROOT/checksum_B.txt"
CHK_A=$(cat "$TMP_ROOT/checksum_A.txt")
CHK_B=$(cat "$TMP_ROOT/checksum_B.txt")
if [ "$CHK_A" = "$CHK_B" ] && [ -n "$CHK_A" ]; then
  pass
else
  fail "checksums differ after sync: A=$CHK_A B=$CHK_B"
fi

# =================================================
echo "--- Test 14: Multi-slot sync ---"
# A has slot 0 (already updated to B's data), put additional data on slot 1 on B
SAVE_SLOT1="slot1_data_from_B"
HLC_SLOT1="3000:0"
PUT_SLOT1=$(https_put "/states/E2EGAME/1?hlc=${HLC_SLOT1}&peerId=CLIENT-B" "$SAVE_SLOT1" "$PORT_B" "$CERT_DIR/clientB")
if echo "$PUT_SLOT1" | grep -q '"success":true'; then pass; else fail "PUT slot 1 on B failed"; fi

# Pull slot 1 from B and push to A
GET_SLOT1=$(https_get "/states/E2EGAME/1" "$PORT_B" "$CERT_DIR/clientA")
GET_SLOT1_BODY=$(get_body "$GET_SLOT1")
if [ "$GET_SLOT1_BODY" = "$SAVE_SLOT1" ]; then pass; else fail "A couldn't download slot 1 from B"; fi

PUT_SLOT1_A=$(https_put "/states/E2EGAME/1?hlc=${HLC_SLOT1}&peerId=CLIENT-A" "$SAVE_SLOT1" "$PORT_A" "$CERT_DIR/clientA")
if echo "$PUT_SLOT1_A" | grep -q '"success":true'; then pass; else fail "A couldn't upload slot 1"; fi

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
