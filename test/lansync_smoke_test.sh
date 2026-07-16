#!/usr/bin/env bash
# lansync_smoke_test.sh — validates PPSSPP LAN sync REST API + TLS end-to-end
# Usage: DISPLAY=:99 ./test/lansync_smoke_test.sh build/PPSSPPSDL
# Requires: Xvfb running on $DISPLAY, openssl

set -uo pipefail

PPSSPP_BIN="${1:-build/PPSSPPSDL}"
if [ ! -f "$PPSSPP_BIN" ]; then
    echo "Error: PPSSPPSDL binary not found at $PPSSPP_BIN"
    echo "Build first: cmake -B build -DPPSSPP_LANSYNC=ON && cmake --build build"
    exit 1
fi
PORT=${LANSYNC_PORT:-$((27314 + (RANDOM % 1000)))}
# [PPSSPP-FORK] Free any leftover server from a previously interrupted run so
# we don't talk to a stale PPSSPPSDL serving an old HOME state dir.
fuser -k "${PORT}/tcp" 2>/dev/null || true
sleep 1
TMP_HOME=$(mktemp -d)
STATE_DIR="$TMP_HOME/.config/ppsspp/PSP/PPSSPP_STATE"
mkdir -p "$STATE_DIR"
CERT_DIR="$TMP_HOME/certs"
mkdir -p "$CERT_DIR"
PASS_COUNT=0
FAIL_COUNT=0
PPSSPP_PID=""

# --- SSL cert setup ---
echo "--- Generating test certificates ---"

# Client cert A (the trusted client)
openssl ecparam -genkey -name prime256v1 -out "$CERT_DIR/clientA_key.pem" 2>/dev/null
openssl req -new -x509 -key "$CERT_DIR/clientA_key.pem" -out "$CERT_DIR/clientA_cert.pem" -days 3650 \
    -subj '/CN=PPSSPP Test Client A' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' 2>/dev/null

# Client cert B (the "evil" untrusted client)
openssl ecparam -genkey -name prime256v1 -out "$CERT_DIR/clientB_key.pem" 2>/dev/null
openssl req -new -x509 -key "$CERT_DIR/clientB_key.pem" -out "$CERT_DIR/clientB_cert.pem" -days 3650 \
    -subj '/CN=PPSSPP Evil Client B' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' 2>/dev/null

# Client cert C (a SECOND trusted client, for SR4 concurrent-verification test)
openssl ecparam -genkey -name prime256v1 -out "$CERT_DIR/clientC_key.pem" 2>/dev/null
openssl req -new -x509 -key "$CERT_DIR/clientC_key.pem" -out "$CERT_DIR/clientC_cert.pem" -days 3650 \
    -subj '/CN=PPSSPP Test Client C' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' 2>/dev/null

# Read clientC cert PEM, escape newlines for valid JSON
CLIENT_C_PEM=$(sed ':a;N;$!ba;s/\n/\\n/g' "$CERT_DIR/clientC_cert.pem")

# Read clientA cert PEM, escape newlines for valid JSON
# PEM_read_bio_X509 handles \n escape sequences via gason's decoder
CLIENT_A_PEM_RAW=$(cat "$CERT_DIR/clientA_cert.pem")
CLIENT_A_PEM=$(sed ':a;N;$!ba;s/\n/\\n/g' "$CERT_DIR/clientA_cert.pem")

echo "  clientA_cert: $CERT_DIR/clientA_cert.pem"
echo "  clientB_cert: $CERT_DIR/clientB_cert.pem"

cleanup() {
  if [ -n "$PPSSPP_PID" ] && kill -0 "$PPSSPP_PID" 2>/dev/null; then
    kill "$PPSSPP_PID" 2>/dev/null
    sleep 1
    kill -0 "$PPSSPP_PID" 2>/dev/null && kill -9 "$PPSSPP_PID" 2>/dev/null || true
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

OSSL_TIMEOUT=5

# Send raw HTTP request via openssl, print response to stdout
# Usage: https_get "/path" "cert_dir" ["cert_dir"]
# If cert_dir omitted, uses clientA certs.
https_get() {
  local path="$1" cert_dir="${2:-$CERT_DIR/clientA}"
  printf "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" "$path" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$PORT" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

https_put() {
  local path="$1" body="$2" cert_dir="${3:-$CERT_DIR/clientA}"
  local len=${#body}
  printf "PUT %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n%s" \
    "$path" "$len" "$body" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$PORT" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

# Send PUT with only headers (no body), sets a fake Content-Length
# Used for testing size limit without sending 100MB of data
https_put_oversized() {
  local path="$1" content_length="$2" cert_dir="${3:-$CERT_DIR/clientA}"
  printf "PUT %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n" \
    "$path" "$content_length" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$PORT" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

https_post() {
  local path="$1" body="$2" cert_dir="${3:-$CERT_DIR/clientA}"
  local len=${#body}
  printf "POST %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s" \
    "$path" "$len" "$body" | \
    timeout $OSSL_TIMEOUT openssl s_client -connect "localhost:$PORT" -quiet \
      -cert "${cert_dir}_cert.pem" -key "${cert_dir}_key.pem" 2>/dev/null || true
}

# Extract status line and body from HTTP response
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
echo "--- Test 2: Pair clientA (first-use: any cert accepted for TLS) ---"

# Step 2a: POST /pair/begin with clientA cert + certPEM in body
BEGIN_BODY="{\"certPEM\":\"$CLIENT_A_PEM\"}"
BEGIN_RESP=$(https_post "/pair/begin" "$BEGIN_BODY")
NONCE=$(echo "$BEGIN_RESP" | grep -o '"nonce":"[^"]*"' | cut -d'"' -f4)
FINGERPRINT=$(echo "$BEGIN_RESP" | grep -o '"certFingerprint":"[^"]*"' | cut -d'"' -f4)
if [ -n "$NONCE" ]; then
  pass
else
  fail "no nonce in /pair/begin response: $(echo "$BEGIN_RESP" | tail -1)"
fi

# Step 2b: Compute PIN matching PairingManager::ComputePin()
#   SHA256(nonce) -> first 3 bytes -> (b0<<16 | b1<<8 | b2) % 1000000 -> 6-digit
PIN_HEX=$(printf '%s' "$NONCE" | openssl dgst -sha256 | cut -d' ' -f2 | cut -c1-6)
PIN_DEC=$((16#${PIN_HEX:0:2} << 16 | 16#${PIN_HEX:2:2} << 8 | 16#${PIN_HEX:4:2}))
PIN_PAD=$(printf "%06d" $((PIN_DEC % 1000000)))

# Step 2c: POST /pair/verify with CORRECT pin
VERIFY_BODY="{\"nonce\":\"$NONCE\",\"pin\":\"$PIN_PAD\",\"peerId\":\"PPSSPP-TEST-A\"}"
VERIFY_RESP="$(https_post "/pair/verify" "$VERIFY_BODY")"
if echo "$VERIFY_RESP" | grep -q '"success":true'; then
  pass
else
  fail "verify with correct pin failed: $(echo "$VERIFY_RESP" | tail -1)"
fi

# Step 2d: POST /pair/verify with WRONG pin
WRONG_BODY="{\"nonce\":\"$NONCE\",\"pin\":\"000000\",\"peerId\":\"PPSSPP-TEST-A\"}"
WRONG_RESP="$(https_post "/pair/verify" "$WRONG_BODY")"
if echo "$WRONG_RESP" | grep -q '"success":false'; then
  pass
else
  fail "wrong pin should fail: $(echo "$WRONG_RESP" | tail -1)"
fi

# =================================================
echo "--- Test 3: GET /states returns empty JSON array (paired clientA) ---"
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
echo "--- Test 4: PUT then GET save state (paired clientA) ---"
SAVE_DATA="fake_save_data_12345"
HLC_VALUE=$(date +%s)000:0
PUT_RESP=$(https_put "/states/ULUS12345/0?hlc=${HLC_VALUE}&peerId=PPSSPP-TEST-A" "$SAVE_DATA")
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
echo "--- Test 5: Conflict file exclusion ---"
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
echo "--- Test 6: HTTP 404 for nonexistent file ---"
MISS_RESP=$(https_get "/states/NONEXIST/99")
MISS_BODY=$(get_body "$MISS_RESP")
if echo "$MISS_BODY" | grep -q "not_found"; then
  pass
else
  fail "expected 'not_found', got: $MISS_BODY"
fi

# =================================================
echo "--- Test 7: 403 pairing enforcement (untrusted clientB) ---"

# Step 7a: GET /states with clientB -> 403
RESP_B=$(https_get "/states" "$CERT_DIR/clientB")
STATUS_B=$(get_status "$RESP_B")
BODY_B=$(get_body "$RESP_B")
if echo "$STATUS_B" | grep -q "403"; then
  pass
else
  fail "expected 403 for untrusted GET, got: $STATUS_B"
fi

# Step 7b: PUT with clientB -> 403
PUT_B=$(https_put "/states/ULUS12345/0?hlc=1111:0&peerId=EVIL" "evil_data" "$CERT_DIR/clientB")
if echo "$PUT_B" | grep -q "403\|forbidden"; then
  pass
else
  fail "expected 403 for untrusted PUT, got: $PUT_B"
fi

# =================================================
echo "--- Test 7.5 (SR4): concurrent connections — trusted peers pass, untrusted rejected ---"
# Pair a SECOND trusted client (clientC) exactly like clientA was paired in Test 2.
# Then fire 3 concurrent TLS connections (clientA trusted, clientC trusted,
# clientB untrusted) and assert the trust gate is evaluated PER-CONNECTION
# (the old shared currentSSL_ member could let an untrusted peer pass if a
# trusted peer's handshake happened to populate it last).

BEGIN_C_BODY="{\"certPEM\":\"$CLIENT_C_PEM\"}"
BEGIN_C_RESP=$(https_post "/pair/begin" "$BEGIN_C_BODY")
NONCE_C=$(echo "$BEGIN_C_RESP" | grep -o '"nonce":"[^"]*"' | cut -d'"' -f4)
PIN_C_HEX=$(printf '%s' "$NONCE_C" | openssl dgst -sha256 | cut -d' ' -f2 | cut -c1-6)
PIN_C_DEC=$((16#${PIN_C_HEX:0:2} << 16 | 16#${PIN_C_HEX:2:2} << 8 | 16#${PIN_C_HEX:4:2}))
PIN_C_PAD=$(printf "%06d" $((PIN_C_DEC % 1000000)))
VERIFY_C_BODY="{\"nonce\":\"$NONCE_C\",\"pin\":\"$PIN_C_PAD\",\"peerId\":\"PPSSPP-TEST-C\"}"
VERIFY_C_RESP="$(https_post "/pair/verify" "$VERIFY_C_BODY")"
if echo "$VERIFY_C_RESP" | grep -q '"success":true'; then
  pass
else
  fail "failed to pair second trusted client clientC: $(echo "$VERIFY_C_RESP" | tail -1)"
fi

# Fire 3 concurrent GET /states: clientA (trusted), clientC (trusted), clientB (untrusted)
RESP_A=$(https_get "/states" "$CERT_DIR/clientA") &
RESP_C=$(https_get "/states" "$CERT_DIR/clientC") &
RESP_B=$(https_get "/states" "$CERT_DIR/clientB") &
wait
STATUS_A=$(get_status "$RESP_A")
STATUS_C=$(get_status "$RESP_C")
STATUS_B=$(get_status "$RESP_B")

if echo "$STATUS_A" | grep -q "200"; then pass; else fail "trusted clientA should get 200, got: $STATUS_A"; fi
if echo "$STATUS_C" | grep -q "200"; then pass; else fail "trusted clientC should get 200, got: $STATUS_C"; fi
if echo "$STATUS_B" | grep -q "403"; then pass; else fail "untrusted clientB should get 403, got: $STATUS_B"; fi

# =================================================
echo "--- Test 8: PUT payload too large (413) ---"
# Server checks Content-Length > 100MB (104857600)
# Send only headers with oversized CL — server rejects before reading body
OVER_CL=$((104857600 + 1))
PUT_OVER=$(https_put_oversized "/states/ULUS12345/1?hlc=2222:0&peerId=PPSSPP-TEST-A" "$OVER_CL")
if echo "$PUT_OVER" | grep -q "413\|payload_too_large"; then
  pass
else
  fail "expected 413 for oversized PUT, got: $PUT_OVER"
fi

# =================================================
echo "--- Test 9: Path validation ---"

# Step 9a: Empty gameId
RESP_EMPTY=$(https_get "/states//0")
if echo "$RESP_EMPTY" | grep -q "invalid_path\|error"; then
  pass
else
  fail "expected error for empty gameId, got: $(echo "$RESP_EMPTY" | tail -1)"
fi

# Step 9b: Non-numeric slot (slot "abc" -> atoi returns 0, writes to game_0.ppst, but test basic)
RESP_NONUM=$(https_get "/states/GAME/abc")
if echo "$RESP_NONUM" | grep -q "not_found\|200"; then
  # atoi("abc")=0, so it looks for GAME_0.ppst which doesn't exist -> not_found
  pass
else
  fail "unexpected response for non-numeric slot: $(echo "$RESP_NONUM" | tail -1)"
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
