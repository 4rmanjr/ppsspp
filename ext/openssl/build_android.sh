#!/usr/bin/env bash
# Build OpenSSL 3.3.0 for Android (all 4 ABIs) — for PPSSPP LANSync.
#
# Prerequisites:
#   - Android NDK installed, ANDROID_NDK_HOME set
#   - curl, tar, make, perl
#
# Usage:
#   export ANDROID_NDK_HOME=/path/to/android-ndk-r27
#   ./ext/openssl/build_android.sh
#
# Output: ext/openssl/android/<abi>/{include,lib}
#   Same layout as CMakeLists.txt expects.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_BASE="${SCRIPT_DIR}/android"

OPENSSL_VERSION="3.3.0"
OPENSSL_TAR="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TAR}"
OPENSSL_SRC_DIR="${SCRIPT_DIR}/openssl-${OPENSSL_VERSION}"

API_LEVEL="${API_LEVEL:-24}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "ERROR: ANDROID_NDK_HOME not set"
    echo "Usage: ANDROID_NDK_HOME=/path/to/android-ndk-r27 $0"
    exit 1
fi

# OpenSSL's Android config script (for 3.3.0) also checks ANDROID_NDK_ROOT
export ANDROID_NDK_ROOT="${ANDROID_NDK_HOME}"

TOOLCHAIN_DIR="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64"
if [ ! -d "$TOOLCHAIN_DIR" ]; then
    TOOLCHAIN_DIR="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64"
fi
if [ ! -d "$TOOLCHAIN_DIR" ]; then
    TOOLCHAIN_DIR="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/windows-x86_64"
fi
if [ ! -d "$TOOLCHAIN_DIR" ]; then
    echo "ERROR: Could not find NDK toolchain at ${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/"
    exit 1
fi

export PATH="${TOOLCHAIN_DIR}/bin:${PATH}"

echo "=== OpenSSL ${OPENSSL_VERSION} Android build ==="
echo "NDK:       ${ANDROID_NDK_HOME}"
echo "Toolchain: ${TOOLCHAIN_DIR}"
echo "API:       ${API_LEVEL}"
echo "Jobs:      ${JOBS}"
echo "Output:    ${OUTPUT_BASE}"
echo ""

# Download and extract OpenSSL source if needed
if [ ! -f "${SCRIPT_DIR}/${OPENSSL_TAR}" ] && [ ! -d "${OPENSSL_SRC_DIR}" ]; then
    echo "Downloading OpenSSL ${OPENSSL_VERSION}..."
    curl -L -o "${SCRIPT_DIR}/${OPENSSL_TAR}" "${OPENSSL_URL}"
fi

if [ ! -d "${OPENSSL_SRC_DIR}" ]; then
    echo "Extracting..."
    tar xzf "${SCRIPT_DIR}/${OPENSSL_TAR}" -C "${SCRIPT_DIR}"
fi

cd "${OPENSSL_SRC_DIR}"

# Build for each ABI
# Map: OpenSSL target → Android ABI
declare -A TARGETS=(
    ["android-arm64"]="arm64-v8a"
    ["android-arm"]="armeabi-v7a"
    ["android-x86_64"]="x86_64"
    ["android-x86"]="x86"
)

for openssl_target in "${!TARGETS[@]}"; do
    abi="${TARGETS[$openssl_target]}"
    prefix="${OUTPUT_BASE}/${abi}"

    echo "--- Building for ${abi} (${openssl_target}) ---"

    # Skip if already built
    if [ -f "${prefix}/lib/libssl.a" ] && [ -f "${prefix}/lib/libcrypto.a" ]; then
        echo "Already built, skipping."
        continue
    fi

    make clean 2>/dev/null || true

    perl Configure \
        "${openssl_target}" \
        "-D__ANDROID_API__=${API_LEVEL}" \
        "-DOPENSSL_NO_STDIO" \
        "-fvisibility=hidden" \
        "-fPIC" \
        no-shared \
        no-tests \
        no-docs \
        no-apps \
        no-ui-console \
        no-engine \
        no-cmp \
        --prefix="${prefix}" \
        --openssldir="${prefix}"

    make -j"${JOBS}"
    make install_sw

    echo "Done: ${prefix}"
    echo ""
done

cd "${SCRIPT_DIR}"

# Clean up source to save space
echo "Cleaning up source..."
rm -rf "${OPENSSL_SRC_DIR}"
rm -f "${OPENSSL_TAR}"

echo ""
echo "=== OpenSSL Android build complete ==="
echo "Libraries installed to: ${OUTPUT_BASE}"
echo ""
echo "Now build PPSSPP with:"
echo "  cmake -B build-android -DPPSSPP_LANSYNC=ON ..."
