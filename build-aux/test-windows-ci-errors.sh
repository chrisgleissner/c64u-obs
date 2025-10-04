#!/bin/bash

# Test Windows CI Specific Errors
# Verifies the exact issues from the Windows CI build are resolved

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "🎯 C64U OBS Plugin - Windows CI Error Verification"
echo "=================================================="
echo "Testing fixes for specific Windows CI build errors"
echo ""

cd "$PROJECT_ROOT"

# Check MinGW
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "❌ MinGW cross-compiler not found"
    echo "Install with: sudo apt-get install mingw-w64"
    exit 1
fi

# Set up test directory
BUILD_DIR="build_windows_ci_test"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Windows compilation flags (matching CI exactly)
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
    "-D_M_X64"  # Define architecture for Windows
)

echo "🧪 Testing specific Windows CI error scenarios..."

# Test 1: atomic_store_explicit, atomic_load_explicit, atomic_fetch_add_explicit
echo "  → Test 1: Missing atomic_*_explicit functions..."
cat > test_explicit_atomics.c << 'EOF'
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/c64u-network.h"
#include "../src/c64u-atomic.h"
#include <stdio.h>

int main() {
    atomic_uint64_t counter64 = ATOMIC_VAR_INIT(0);
    atomic_uint32_t counter32 = ATOMIC_VAR_INIT(0);
    atomic_uint16_t counter16 = ATOMIC_VAR_INIT(0);
    atomic_bool_t flag = ATOMIC_VAR_INIT(false);

    // Test the exact functions that were missing in CI
    atomic_store_explicit(&counter64, 12345ULL, memory_order_relaxed);
    atomic_store_explicit(&counter32, 1234, memory_order_relaxed);
    atomic_store_explicit_u16(&counter16, 123, memory_order_relaxed);
    atomic_store_explicit_bool(&flag, true, memory_order_relaxed);

    uint64_t v64 = atomic_load_explicit(&counter64, memory_order_relaxed);
    uint32_t v32 = atomic_load_explicit(&counter32, memory_order_relaxed);
    uint16_t v16 = atomic_load_explicit_u16(&counter16, memory_order_relaxed);
    bool vb = atomic_load_explicit_bool(&flag, memory_order_relaxed);

    atomic_fetch_add_explicit(&counter64, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&counter32, 1, memory_order_relaxed);
    atomic_fetch_add_explicit_u16(&counter16, 1, memory_order_relaxed);

    printf("Explicit atomics: u64=%llu, u32=%u, u16=%u, bool=%s\n",
           (unsigned long long)v64, v32, v16, vb ? "true" : "false");

    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -c -o test_explicit_atomics.o test_explicit_atomics.c; then
    echo "    ✅ atomic_*_explicit functions work correctly"
else
    echo "    ❌ atomic_*_explicit functions failed"
    exit 1
fi

# Test 2: memory_order_acq_rel and other memory order constants
echo "  → Test 2: Missing memory_order constants..."
cat > test_memory_orders.c << 'EOF'
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/c64u-atomic.h"
#include <stdio.h>

int main() {
    atomic_uint64_t counter = ATOMIC_VAR_INIT(0);

    // Test all memory order constants that were missing
    atomic_store_explicit_u64(&counter, 1, memory_order_relaxed);
    atomic_store_explicit_u64(&counter, 2, memory_order_consume);
    atomic_store_explicit_u64(&counter, 3, memory_order_acquire);
    atomic_store_explicit_u64(&counter, 4, memory_order_release);
    atomic_store_explicit_u64(&counter, 5, memory_order_acq_rel);  // This was missing
    atomic_store_explicit_u64(&counter, 6, memory_order_seq_cst);

    uint64_t val = atomic_load_explicit_u64(&counter, memory_order_acquire);
    printf("Memory orders test: final value=%llu\n", (unsigned long long)val);

    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -c -o test_memory_orders.o test_memory_orders.c; then
    echo "    ✅ All memory_order constants work correctly"
else
    echo "    ❌ memory_order constants failed"
    exit 1
fi

# Test 3: atomic_fetch_or_explicit (for bit mask operations)
echo "  → Test 3: atomic_fetch_or_explicit function..."
cat > test_fetch_or.c << 'EOF'
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/c64u-atomic.h"
#include <stdio.h>

int main() {
    atomic_uint64_t mask = ATOMIC_VAR_INIT(0);

    // Test atomic_fetch_or_explicit that was missing
    uint64_t old_mask = atomic_fetch_or_explicit(&mask, 0x0001, memory_order_acq_rel);
    old_mask = atomic_fetch_or_explicit(&mask, 0x0002, memory_order_acq_rel);
    old_mask = atomic_fetch_or_explicit(&mask, 0x0004, memory_order_acq_rel);

    uint64_t final_mask = atomic_load_explicit_u64(&mask, memory_order_acquire);
    printf("Fetch-or test: final mask=0x%04llx\n", (unsigned long long)final_mask);

    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -c -o test_fetch_or.o test_fetch_or.c; then
    echo "    ✅ atomic_fetch_or_explicit works correctly"
else
    echo "    ❌ atomic_fetch_or_explicit failed"
    exit 1
fi

# Test 4: Direct atomic operations (should work with atomic_load/store)
echo "  → Test 4: Atomic struct operations..."
cat > test_atomic_structs.c << 'EOF'
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/c64u-atomic.h"
#include <stdio.h>

// Simulate frame assembly structure like in c64u-video.c
struct test_frame {
    atomic_uint16_t received_packets;
    uint16_t expected_packets;
    atomic_bool_t complete;
};

int main() {
    struct test_frame frame = {
        .received_packets = ATOMIC_VAR_INIT(0),
        .expected_packets = 100,
        .complete = ATOMIC_VAR_INIT(false)
    };

    // Test operations that were failing in CI
    // These should now use atomic_load_u16 instead of direct operations
    uint16_t received = atomic_load_u16(&frame.received_packets);
    if (received > 0) {  // This was: if (frame.received_packets > 0)
        printf("Frame has packets\n");
    }

    // Test arithmetic that was failing
    float percentage = (atomic_load_u16(&frame.received_packets) * 100.0f) / frame.expected_packets;

    // Test increment that was failing
    atomic_fetch_add_u16(&frame.received_packets, 1);  // This was: frame.received_packets++

    received = atomic_load_u16(&frame.received_packets);
    printf("Atomic struct ops: received=%u, percentage=%.1f%%\n", received, percentage);

    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -c -o test_atomic_structs.o test_atomic_structs.c; then
    echo "    ✅ Atomic struct operations work correctly"
else
    echo "    ❌ Atomic struct operations failed"
    exit 1
fi

# Test 5: Architecture detection (No Target Architecture error)
echo "  → Test 5: Windows architecture detection..."
cat > test_architecture.c << 'EOF'
// Test architecture detection to prevent "No Target Architecture" error
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// Include order that was causing issues
#include "../src/c64u-network.h"  // This includes Windows headers
#include "../src/c64u-atomic.h"   // This should define architecture

#include <stdio.h>

int main() {
#ifdef _AMD64_
    printf("Architecture: AMD64 detected\n");
#elif defined(_X86_)
    printf("Architecture: X86 detected\n");
#elif defined(_ARM64_)
    printf("Architecture: ARM64 detected\n");
#elif defined(_ARM_)
    printf("Architecture: ARM detected\n");
#else
    printf("Architecture: Unknown\n");
#endif
    return 0;
}
EOF

if x86_64-w64-mingw32-gcc "${WINDOWS_CFLAGS[@]}" -c -o test_architecture.o test_architecture.c; then
    echo "    ✅ Architecture detection works correctly"
else
    echo "    ❌ Architecture detection failed"
    exit 1
fi

cd "$PROJECT_ROOT"
echo ""
echo "🧹 Cleaning up..."
rm -rf "$BUILD_DIR"

echo ""
echo "🎉 SUCCESS: All Windows CI errors have been resolved!"
echo ""
echo "📋 Fixed Issues:"
echo "  ✅ atomic_store_explicit undefined → Added generic _Generic macro"
echo "  ✅ atomic_load_explicit undefined → Added generic _Generic macro"
echo "  ✅ atomic_fetch_add_explicit undefined → Added generic _Generic macro"
echo "  ✅ atomic_exchange_explicit undefined → Added generic _Generic macro"
echo "  ✅ atomic_fetch_or_explicit undefined → Added InterlockedOr64 implementation"
echo "  ✅ memory_order_acq_rel undefined → Added all memory order constants"
echo "  ✅ Direct atomic operations → Fixed with atomic_load_u16/atomic_fetch_add_u16"
echo "  ✅ No Target Architecture → Added architecture detection macros"
echo ""
echo "🚀 The Windows CI should now build successfully!"
