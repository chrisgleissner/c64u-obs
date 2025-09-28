#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <math.h>
#include "c64u-timing.h"
#include "c64u-types.h"
#include "c64u-logging.h"

void c64u_timing_init(struct c64u_timing_state *timing, c64u_timing_strategy_t strategy, double target_fps,
                      double source_fps, uint32_t frame_size)
{
    memset(timing, 0, sizeof(struct c64u_timing_state));

    timing->strategy = strategy;
    timing->target_fps = target_fps;
    timing->source_fps = source_fps;
    timing->obs_frame_interval_ns = (uint64_t)(1000000000.0 / target_fps);
    timing->c64_frame_interval_ns = (uint64_t)(1000000000.0 / source_fps);

    C64U_LOG_INFO("🎯 Timing initialized: C64 %.3f Hz -> OBS %.1f Hz (strategy %d)", source_fps, target_fps, strategy);
    C64U_LOG_INFO("   Frame intervals: C64 %.3f ms, OBS %.3f ms", timing->c64_frame_interval_ns / 1000000.0,
                  timing->obs_frame_interval_ns / 1000000.0);

    // Initialize interpolation buffers if needed
    if (strategy == C64U_TIMING_INTERPOLATION) {
        for (int i = 0; i < 3; i++) {
            timing->interpolation.frames[i] = bmalloc(frame_size);
            if (!timing->interpolation.frames[i]) {
                C64U_LOG_ERROR("Failed to allocate interpolation buffer %d", i);
                // Clean up already allocated buffers
                for (int j = 0; j < i; j++) {
                    bfree(timing->interpolation.frames[j]);
                    timing->interpolation.frames[j] = NULL;
                }
                timing->strategy = C64U_TIMING_ADAPTIVE; // Fallback
                break;
            }
        }
        if (timing->strategy == C64U_TIMING_INTERPOLATION) {
            C64U_LOG_INFO("   ✓ Interpolation buffers allocated");
        }
    }
}

void c64u_timing_destroy(struct c64u_timing_state *timing)
{
    if (timing->strategy == C64U_TIMING_INTERPOLATION) {
        for (int i = 0; i < 3; i++) {
            if (timing->interpolation.frames[i]) {
                bfree(timing->interpolation.frames[i]);
                timing->interpolation.frames[i] = NULL;
            }
        }
    }

    // Log final statistics
    if (timing->frames_received > 0) {
        double drop_rate = (100.0 * timing->frames_dropped) / timing->frames_received;
        double dup_rate = (100.0 * timing->frames_duplicated) / timing->frames_delivered;
        double interp_rate = (100.0 * timing->frames_interpolated) / timing->frames_delivered;

        C64U_LOG_INFO("📊 Final timing stats: %u received, %u delivered", timing->frames_received,
                      timing->frames_delivered);
        C64U_LOG_INFO("   Drops: %u (%.1f%%), Duplicates: %u (%.1f%%), Interpolated: %u (%.1f%%)",
                      timing->frames_dropped, drop_rate, timing->frames_duplicated, dup_rate,
                      timing->frames_interpolated, interp_rate);
    }
}

void c64u_timing_on_c64_frame_received(struct c64u_timing_state *timing, uint64_t now)
{
    timing->frames_received++;
    timing->last_c64_frame_time = now;
}

bool c64u_timing_should_deliver_frame(struct c64u_timing_state *timing, uint64_t now)
{
    switch (timing->strategy) {
    case C64U_TIMING_PASSTHROUGH:
        // Always deliver immediately (current behavior)
        return true;

    case C64U_TIMING_ADAPTIVE:
    case C64U_TIMING_INTERPOLATION:
    case C64U_TIMING_VSYNC: {
        // Always deliver first few frames to avoid black screen
        if (timing->frames_delivered < 10) {
            return true;
        }

        // Calculate timing debt: how far ahead/behind are we?
        if (timing->last_obs_frame_time == 0 || timing->frames_received < 5) {
            timing->last_obs_frame_time = now;
            return true; // Initial frames, always deliver
        }

        uint64_t obs_elapsed = now - timing->last_obs_frame_time;

        // Be more permissive - deliver frame if it's been more than half the expected interval
        uint64_t min_interval = timing->obs_frame_interval_ns / 2;
        if (obs_elapsed >= min_interval) {
            return true;
        }

        // Also deliver based on expected frame count, but be more generous
        uint64_t expected_obs_frames = obs_elapsed / timing->obs_frame_interval_ns;
        bool should_deliver = (timing->frames_delivered <= expected_obs_frames);

        // Update frame debt for rate adaptation (only if we have enough data)
        if (timing->frames_received > 10 && obs_elapsed > timing->obs_frame_interval_ns) {
            double c64_rate = timing->frames_received * 1000000000.0 / obs_elapsed;
            double obs_rate = timing->target_fps;
            timing->frame_debt += (c64_rate / obs_rate) - 1.0;
        }

        return should_deliver;
    }
    }

    return true; // Always fallback to delivering
}

void c64u_timing_on_obs_frame_delivered(struct c64u_timing_state *timing, uint64_t now)
{
    timing->frames_delivered++;

    if (timing->last_obs_frame_time > 0) {
        uint64_t interval = now - timing->last_obs_frame_time;

        // Detect duplicated frames (delivered too quickly)
        if (interval < timing->obs_frame_interval_ns / 2) {
            timing->frames_duplicated++;
        }
    }

    timing->last_obs_frame_time = now;
}

uint32_t *c64u_timing_get_frame_for_obs(struct c64u_timing_state *timing, struct c64u_source *context, uint64_t now)
{
    switch (timing->strategy) {
    case C64U_TIMING_PASSTHROUGH:
        // Return front buffer directly (current behavior)
        return context->frame_buffer_front;

    case C64U_TIMING_ADAPTIVE: {
        // Adaptive strategy: decide whether to drop, duplicate, or deliver frame
        // Note: We still return a frame but adjust timing debt for next decision
        if (timing->frame_debt > 2.0) {
            // We're significantly ahead, note the drop but still deliver
            timing->frame_debt -= 1.0;
            timing->frames_dropped++;
        } else if (timing->frame_debt < -2.0) {
            // We're behind, note duplication for stats
            timing->frame_debt += 1.0;
            timing->frames_duplicated++;
        }

        // Always return a frame to prevent black screen
        return context->frame_buffer_front;
    }

    case C64U_TIMING_INTERPOLATION: {
        // Store the new frame in interpolation buffer
        uint8_t next_write = (timing->interpolation.write_index + 1) % 3;
        if (timing->interpolation.frames[next_write]) {
            uint32_t frame_size = context->width * context->height * 4;
            memcpy(timing->interpolation.frames[next_write], context->frame_buffer_front, frame_size);
            timing->interpolation.timestamps[next_write] = now;
            timing->interpolation.write_index = next_write;
            timing->interpolation.buffer_ready = true;
        }

        // Calculate which frame to return based on timing
        if (timing->interpolation.buffer_ready) {
            // For now, just return the most recent frame
            // TODO: Implement true temporal interpolation
            return timing->interpolation.frames[timing->interpolation.write_index];
        }

        return context->frame_buffer_front; // Fallback
    }

    case C64U_TIMING_VSYNC: {
        // VSync strategy: align with OBS render timing
        // Be more conservative to prevent black screen
        return context->frame_buffer_front;
    }
    }

    return context->frame_buffer_front; // Fallback
}
