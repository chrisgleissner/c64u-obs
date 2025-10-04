#include "c64u-color.h"
#include "c64u-logging.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file c64u-color.c
 * @brief VIC-II color conversion and palette management implementation
 *
 * Provides optimized color conversion for C64 Ultimate video streams with
 * pre-computed lookup tables for maximum performance in hot path operations.
 */

// VIC-II color palette (16 colors) in BGRA format for OBS Studio
// Colors converted from C64 Ultimate grab.py RGB values to BGRA with full alpha
const uint32_t vic_colors[16] = {
    0xFF000000, // 0: Black
    0xFFEFEFEF, // 1: White
    0xFF342F8D, // 2: Red
    0xFFCDD46A, // 3: Cyan
    0xFFA43598, // 4: Purple/Magenta
    0xFF42B44C, // 5: Green
    0xFFB1292C, // 6: Blue
    0xFF5DEFEF, // 7: Yellow
    0xFF204E98, // 8: Orange
    0xFF00385B, // 9: Brown
    0xFF6D67D1, // 10: Light Red
    0xFF4A4A4A, // 11: Dark Grey
    0xFF7B7B7B, // 12: Mid Grey
    0xFF93EF9F, // 13: Light Green
    0xFFEF6A6D, // 14: Light Blue
    0xFFB2B2B2  // 15: Light Grey
};

// Color conversion optimization: Pre-computed lookup table for pixel pairs
static uint64_t color_pair_lut[256];
static bool color_lut_initialized = false;

void c64u_init_color_conversion_lut(void)
{
    if (color_lut_initialized) {
        return; // Already initialized
    }

    // Pre-compute all 256 possible 4-bit color pair combinations
    for (int i = 0; i < 256; i++) {
        uint8_t color1 = i & 0x0F;        // Lower 4 bits
        uint8_t color2 = (i >> 4) & 0x0F; // Upper 4 bits

        // Pack two 32-bit colors into a single 64-bit value for efficient memory writes
        // This allows writing both pixels with a single 64-bit store operation
        uint64_t packed_colors = ((uint64_t)vic_colors[color2] << 32) | vic_colors[color1];
        color_pair_lut[i] = packed_colors;
    }

    color_lut_initialized = true;
    C64U_LOG_INFO("🎨 Color conversion lookup table initialized (256 entries)");
}

void c64u_convert_pixels_optimized(const uint8_t *src, uint32_t *dst, int pixel_pairs)
{
    // Ensure LUT is initialized
    if (!color_lut_initialized) {
        c64u_init_color_conversion_lut();
    }

    // Process pixel pairs using optimized lookup table
    // Each src byte contains 2 pixels (4 bits each)
    // Each dst position gets 2 consecutive 32-bit RGBA values
    for (int i = 0; i < pixel_pairs; i++) {
        uint8_t pixel_pair = src[i];
        uint64_t colors = color_pair_lut[pixel_pair];

        // Write both pixels with a single 64-bit operation (where supported)
        // This is more cache-efficient than two separate 32-bit writes
        *(uint64_t *)(dst + i * 2) = colors;
    }
}
