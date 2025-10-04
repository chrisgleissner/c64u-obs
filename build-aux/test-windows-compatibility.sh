#!/bin/bash

# Simple Windows Compatibility Verification
# Tests Windows-specific compilation without full Windows environment

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "🪟 C64U OBS Plugin - Windows Compatibility Test"
echo "==============================================="
echo "Verifying Windows-specific code compilation"
echo ""

cd "$PROJECT_ROOT"

# Check if we have MinGW cross-compiler
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "❌ MinGW cross-compiler not found"
    echo "Install with: sudo apt-get install mingw-w64"
    exit 1
fi

echo "✅ MinGW: $(x86_64-w64-mingw32-gcc --version | head -1)"

# Create a simple compilation test for Windows-specific code
BUILD_DIR="build_windows_test"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo ""
echo "🔍 Testing Windows-specific header compatibility..."

# Test atomic headers
echo "Testing c64u-atomic.h Windows compatibility..."
cat > "$BUILD_DIR/test_atomic.c" << 'EOF'
// Test Windows atomic compatibility
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/c64u-atomic.h"
#include <stdio.h>

int main() {
    atomic_uint32_t counter = ATOMIC_VAR_INIT(0);
    atomic_store(&counter, 42);
    uint32_t value = atomic_load(&counter);
    printf("Atomic test: %u\n", value);
    return 0;
}
EOF

# Test network headers
echo "Testing c64u-network.h Windows compatibility..."
cat > "$BUILD_DIR/test_network.c" << 'EOF'
// Test Windows network compatibility
#include "../src/c64u-network.h"
#include <stdio.h>

int main() {
    printf("Network header test passed\n");
    return 0;
}
EOF

# Test header include order (the critical issue we fixed)
echo "Testing header include order (network before atomic)..."
cat > "$BUILD_DIR/test_header_order.c" << 'EOF'
// Test the exact header order that was causing Windows CI failures
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// This is the order that was causing sockaddr redefinition errors:
#include "../src/c64u-network.h"  // This includes winsock2.h
#include "../src/c64u-atomic.h"   // This should not cause conflicts now
#include <stdio.h>

int main() {
    printf("Header include order test passed\n");
    return 0;
}
EOF

# Windows-specific compilation flags (matching the CI)
WINDOWS_CFLAGS=(
    "-std=c17"
    "-Wall"
    "-Werror"
    "-Wno-unknown-pragmas"  # MinGW doesn't support #pragma comment
    "-D_WIN32"
    "-DWIN32"
    "-D_WINDOWS"
    "-DUNICODE"
    "-D_UNICODE"
    "-D_CRT_SECURE_NO_WARNINGS"
    "-D_CRT_NONSTDC_NO_WARNINGS"
)

# Libraries needed (matching c64u-network.h pragmas)
WINDOWS_LIBS="-lws2_32 -liphlpapi -lwinmm"

echo ""
echo "🔨 Compiling Windows-specific tests..."

# Test atomic header
echo "  → Testing c64u-atomic.h..."
if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -o "$BUILD_DIR/test_atomic.exe" "$BUILD_DIR/test_atomic.c"; then
    echo "    ✅ c64u-atomic.h compiles successfully"
else
    echo "    ❌ c64u-atomic.h compilation failed"
    exit 1
fi

# Test network header
echo "  → Testing c64u-network.h..."
if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" $WINDOWS_LIBS -o "$BUILD_DIR/test_network.exe" "$BUILD_DIR/test_network.c"; then
    echo "    ✅ c64u-network.h compiles successfully"
else
    echo "    ❌ c64u-network.h compilation failed"
    exit 1
fi

# Test header include order
echo "  → Testing header include order..."
if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" $WINDOWS_LIBS -o "$BUILD_DIR/test_header_order.exe" "$BUILD_DIR/test_header_order.c"; then
    echo "    ✅ Header include order test passed"
else
    echo "    ❌ Header include order test failed"
    exit 1
fi

echo ""
echo "🧪 Testing Windows-specific code patterns..."

# Create test files that simulate the actual plugin code without OBS dependencies
echo "  → Testing c64u-color.c pattern (without OBS dependency)..."
cat > "$BUILD_DIR/test_color_pattern.c" << 'EOF'
// Simulate c64u-color.c without OBS dependency
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "../src/c64u-atomic.h"
#include <stdint.h>
#include <stdio.h>

// Simulate the VIC-II color conversion that c64u-color.c does
uint32_t test_rgb_conversion(uint8_t vic_color) {
    // Simple test of the RGB conversion logic
    return 0xFF000000 | (vic_color << 16) | (vic_color << 8) | vic_color;
}

int main() {
    uint32_t color = test_rgb_conversion(5);
    printf("Color conversion test: 0x%08X\n", color);
    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -o "$BUILD_DIR/test_color_pattern.exe" "$BUILD_DIR/test_color_pattern.c"; then
    echo "    ✅ Color processing pattern compiles successfully"
else
    echo "    ❌ Color processing pattern compilation failed"
    exit 1
fi

echo "  → Testing c64u-network.c pattern (without OBS dependency)..."
cat > "$BUILD_DIR/test_network_pattern.c" << 'EOF'
// Simulate c64u-network.c without OBS dependency
#include "../src/c64u-network.h"
#include "../src/c64u-atomic.h"
#include <stdio.h>

// Simulate network socket operations
int test_socket_creation() {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return -1;
    }

    closesocket(sock);
    return 0;
}

int main() {
    if (test_socket_creation() == 0) {
        printf("Network socket test passed\n");
    } else {
        printf("Network socket test failed\n");
    }
    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" $WINDOWS_LIBS -o "$BUILD_DIR/test_network_pattern.exe" "$BUILD_DIR/test_network_pattern.c"; then
    echo "    ✅ Network processing pattern compiles successfully"
else
    echo "    ❌ Network processing pattern compilation failed"
    exit 1
fi

echo "  → OBS-dependent files will be tested by the actual CI build"
echo "    This test focuses on Windows-specific compatibility issues we fixed"

echo ""
echo "🎯 Testing critical Windows-specific scenarios..."

# Test the exact scenario that was failing in CI
echo "  → Testing sockaddr redefinition scenario..."
cat > "$BUILD_DIR/test_sockaddr.c" << 'EOF'
// Test the exact sockaddr issue that was failing
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// This is the order that was causing problems:
#include "../src/c64u-network.h"  // This includes winsock2.h
#include "../src/c64u-atomic.h"   // This should not conflict

int main() {
    printf("Sockaddr redefinition test passed\n");
    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" $WINDOWS_LIBS -o "$BUILD_DIR/test_sockaddr.exe" "$BUILD_DIR/test_sockaddr.c"; then
    echo "    ✅ Sockaddr redefinition test passed"
else
    echo "    ❌ Sockaddr redefinition test failed"
    exit 1
fi

# Test atomic type usage
echo "  → Testing atomic type compatibility..."
cat > "$BUILD_DIR/test_atomics_usage.c" << 'EOF'
// Test that our atomic types work as expected
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "../src/c64u-atomic.h"
#include "../src/c64u-types.h"

int main() {
    // Test the types we actually use in the plugin
    atomic_uint16_t frame_count = ATOMIC_VAR_INIT(0);
    atomic_uint32_t packet_count = ATOMIC_VAR_INIT(0);
    atomic_uint64_t byte_count = ATOMIC_VAR_INIT(0);
    atomic_bool_t is_running = ATOMIC_VAR_INIT(false);

    // Test operations
    atomic_store(&frame_count, 100);
    atomic_store(&packet_count, 1000);
    atomic_store(&byte_count, 10000);
    atomic_store(&is_running, true);

    uint16_t f = atomic_load(&frame_count);
    uint32_t p = atomic_load(&packet_count);
    uint64_t b = atomic_load(&byte_count);
    bool r = atomic_load(&is_running);

    printf("Atomics: frame=%u, packet=%u, bytes=%llu, running=%s\n",
           f, p, (unsigned long long)b, r ? "true" : "false");

    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -o "$BUILD_DIR/test_atomics_usage.exe" "$BUILD_DIR/test_atomics_usage.c"; then
    echo "    ✅ Atomic type usage test passed"
else
    echo "    ❌ Atomic type usage test failed"
    exit 1
fi

echo ""
echo "🧹 Cleaning up test artifacts..."
rm -rf "$BUILD_DIR"

echo ""
echo "🎉 SUCCESS: Windows compatibility verification completed!"
echo ""
echo "📋 Verification Results:"
echo "  ✅ All Windows-specific headers compile correctly"
echo "  ✅ Windows code patterns compile with Windows flags"
echo "  ✅ Sockaddr redefinition issue is resolved"
echo "  ✅ Atomic type compatibility works correctly"
echo "  ✅ Header include order issues are resolved"
echo "  ✅ Network socket operations compile correctly"
echo "  ✅ Critical Windows scenarios pass"
echo ""
echo "🚀 The plugin should build successfully on Windows CI!"
echo ""
echo "🔧 Tested with flags: ${WINDOWS_CFLAGS[*]}"
echo "🔗 Tested with libs: $WINDOWS_LIBS"
echo "📌 This matches the exact GitHub Actions windows-ci-x64 configuration."
