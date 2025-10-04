#ifndef C64U_VIDEO_H
#define C64U_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include "c64u-color.h"

// Rendering defaults
#define C64U_DEFAULT_RENDER_DELAY_FRAMES 3  // Default frame delay to smooth UDP packet loss/reordering
#define C64U_MAX_RENDER_DELAY_FRAMES 100    // Maximum allowed render delay frames
#define C64U_RENDER_BUFFER_SAFETY_MARGIN 10 // Extra buffer frames for queue safety

// Timing constants (nanoseconds)
#define C64U_FRAME_TIMEOUT_NS 100000000ULL       // 100ms - timeout for frame freshness (5 frames @ 50Hz)
#define C64U_DEBUG_LOG_INTERVAL_NS 2000000000ULL // 2 seconds - interval for debug logging

// Forward declarations
struct c64u_source;
struct frame_assembly;

// Helper functions for frame assembly
void c64u_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num);
bool c64u_is_frame_complete(struct frame_assembly *frame);
bool c64u_is_frame_timeout(struct frame_assembly *frame);
void c64u_swap_frame_buffers(struct c64u_source *context);
void c64u_assemble_frame_to_buffer(struct c64u_source *context, struct frame_assembly *frame);

// Delay queue management
void c64u_init_delay_queue(struct c64u_source *context);
bool c64u_enqueue_delayed_frame(struct c64u_source *context, struct frame_assembly *frame, uint16_t sequence_num);
bool c64u_dequeue_delayed_frame(struct c64u_source *context);
void c64u_clear_delay_queue(struct c64u_source *context);

// Performance optimization functions
void c64u_process_video_statistics_batch(struct c64u_source *context, uint64_t current_time);
void c64u_process_audio_statistics_batch(struct c64u_source *context, uint64_t current_time);

// Lock-free frame assembly functions
void c64u_init_frame_assembly_lockfree(struct frame_assembly *frame, uint16_t frame_num);
bool c64u_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index);
bool c64u_is_frame_complete_lockfree(struct frame_assembly *frame);

// Video thread function
void *c64u_video_thread_func(void *data);

#endif // C64U_VIDEO_H
