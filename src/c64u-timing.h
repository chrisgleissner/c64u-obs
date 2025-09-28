#ifndef C64U_TIMING_H
#define C64U_TIMING_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64u_source;

// Frame timing strategies
typedef enum {
    C64U_TIMING_PASSTHROUGH,   // Direct pass-through (current behavior)
    C64U_TIMING_ADAPTIVE,      // Adaptive frame dropping/duplication
    C64U_TIMING_INTERPOLATION, // Frame interpolation buffer
    C64U_TIMING_VSYNC          // VSync-aware timing
} c64u_timing_strategy_t;

// Frame timing state
struct c64u_timing_state {
    // Configuration
    c64u_timing_strategy_t strategy;
    double target_fps; // OBS target frame rate (50.0 or 60.0)
    double source_fps; // C64 actual frame rate (50.125 or 59.826)

    // Timing tracking
    uint64_t last_obs_frame_time;
    uint64_t last_c64_frame_time;
    uint64_t obs_frame_interval_ns;
    uint64_t c64_frame_interval_ns;

    // Frame accumulator for rate conversion
    double frame_debt; // Accumulated timing difference
    uint32_t frames_delivered;
    uint32_t frames_received;

    // Interpolation buffer (for INTERPOLATION strategy)
    struct {
        uint32_t *frames[3]; // Triple buffer for interpolation
        uint64_t timestamps[3];
        uint8_t write_index;
        uint8_t read_index;
        bool buffer_ready;
    } interpolation;

    // Statistics
    uint32_t frames_dropped;
    uint32_t frames_duplicated;
    uint32_t frames_interpolated;
};

// Timing functions
void c64u_timing_init(struct c64u_timing_state *timing, c64u_timing_strategy_t strategy, double target_fps,
                      double source_fps, uint32_t frame_size);
void c64u_timing_destroy(struct c64u_timing_state *timing);
bool c64u_timing_should_deliver_frame(struct c64u_timing_state *timing, uint64_t now);
void c64u_timing_on_c64_frame_received(struct c64u_timing_state *timing, uint64_t now);
void c64u_timing_on_obs_frame_delivered(struct c64u_timing_state *timing, uint64_t now);
uint32_t *c64u_timing_get_frame_for_obs(struct c64u_timing_state *timing, struct c64u_source *context, uint64_t now);

#endif // C64U_TIMING_H
