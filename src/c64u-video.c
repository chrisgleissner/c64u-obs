#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include "c64u-network.h" // Include network header first to avoid Windows header conflicts

#include "c64u-logging.h"
#include "c64u-video.h"
#include "c64u-color.h"
#include "c64u-types.h"
#include "c64u-protocol.h"
#include "c64u-record.h"

#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib") // For timeBeginPeriod/timeEndPeriod
#endif

#include "c64u-protocol.h"

// Helper functions for frame assembly (updated to use lock-free implementation)
void c64u_init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
    // Delegate to lock-free implementation for performance
    c64u_init_frame_assembly_lockfree(frame, frame_num);
}

bool c64u_is_frame_complete(struct frame_assembly *frame)
{
    // Delegate to lock-free implementation for performance
    return c64u_is_frame_complete_lockfree(frame);
}

bool c64u_is_frame_timeout(struct frame_assembly *frame)
{
    uint64_t elapsed = (os_gettime_ns() - frame->start_time) / 1000000; // Convert to ms
    return elapsed > C64U_FRAME_TIMEOUT_MS;
}

void c64u_swap_frame_buffers(struct c64u_source *context)
{
    // Save frame to disk if enabled (before swap to avoid race conditions)
    if (context->save_frames) {
        c64u_save_frame_as_bmp(context, context->frame_buffer_back);
    }

    // Record frame to video file if recording is enabled
    if (context->record_video) {
        c64u_record_video_frame(context, context->frame_buffer_back);
    }

    // Swap front and back buffers
    uint32_t *temp = context->frame_buffer_front;
    context->frame_buffer_front = context->frame_buffer_back;
    context->frame_buffer_back = temp;
    context->frame_ready = true;
    context->last_frame_time = os_gettime_ns(); // Update timestamp for timeout detection
    context->buffer_swap_pending = false;
}

void c64u_assemble_frame_to_buffer(struct c64u_source *context, struct frame_assembly *frame)
{
    // Assemble complete frame into back buffer
    for (int i = 0; i < C64U_MAX_PACKETS_PER_FRAME; i++) {
        struct frame_packet *packet = &frame->packets[i];
        if (!packet->received)
            continue;

        uint16_t line_num = packet->line_num;
        uint8_t lines_per_packet = packet->lines_per_packet;

        for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height; line++) {
            uint32_t *dst_line = context->frame_buffer_back + ((line_num + line) * C64U_PIXELS_PER_LINE);
            uint8_t *src_line = packet->packet_data + (line * C64U_BYTES_PER_LINE);

            // Optimized color conversion using lookup table
            c64u_convert_pixels_optimized(src_line, dst_line, C64U_BYTES_PER_LINE);
        }
    }
}

// Delay queue management functions
void c64u_init_delay_queue(struct c64u_source *context)
{
    if (pthread_mutex_lock(&context->delay_mutex) == 0) {
        // Allocate delay queue buffers if needed (max delay + buffer)
        uint32_t needed_size = context->render_delay_frames + C64U_RENDER_BUFFER_SAFETY_MARGIN;

        if (context->delayed_frame_queue == NULL ||
            needed_size > (C64U_MAX_RENDER_DELAY_FRAMES + C64U_RENDER_BUFFER_SAFETY_MARGIN)) {
            if (context->delayed_frame_queue) {
                bfree(context->delayed_frame_queue);
            }
            if (context->delay_sequence_queue) {
                bfree(context->delay_sequence_queue);
            }

            size_t frame_size = context->width * context->height * 4; // RGBA
            context->delayed_frame_queue = bmalloc(frame_size * needed_size);
            context->delay_sequence_queue = bmalloc(sizeof(uint16_t) * needed_size);

            if (!context->delayed_frame_queue || !context->delay_sequence_queue) {
                C64U_LOG_ERROR("Failed to allocate delay queue buffers");
                if (context->delayed_frame_queue) {
                    bfree(context->delayed_frame_queue);
                    context->delayed_frame_queue = NULL;
                }
                if (context->delay_sequence_queue) {
                    bfree(context->delay_sequence_queue);
                    context->delay_sequence_queue = NULL;
                }
                pthread_mutex_unlock(&context->delay_mutex);
                return;
            }
        }

        context->delay_queue_size = 0;
        context->delay_queue_head = 0;
        context->delay_queue_tail = 0;

        pthread_mutex_unlock(&context->delay_mutex);
    }
}

bool c64u_enqueue_delayed_frame(struct c64u_source *context, struct frame_assembly *frame, uint16_t sequence_num)
{
    if (pthread_mutex_lock(&context->delay_mutex) != 0) {
        return false;
    }

    // Initialize delay queue if not already done
    if (context->delayed_frame_queue == NULL) {
        pthread_mutex_unlock(&context->delay_mutex);
        c64u_init_delay_queue(context);
        if (pthread_mutex_lock(&context->delay_mutex) != 0) {
            return false;
        }
    }

    uint32_t max_queue_size = context->render_delay_frames + C64U_RENDER_BUFFER_SAFETY_MARGIN;

    // If queue is full, remove oldest frame
    if (context->delay_queue_size >= max_queue_size) {
        context->delay_queue_head = (context->delay_queue_head + 1) % max_queue_size;
        context->delay_queue_size--;
    }

    // Add frame to tail of queue
    size_t frame_size = context->width * context->height * 4;
    uint32_t tail_index = context->delay_queue_tail;

    // Assemble frame into delay queue slot
    uint32_t *queue_frame = context->delayed_frame_queue + (tail_index * context->width * context->height);

    // Clear the slot first
    memset(queue_frame, 0, frame_size);

    // Assemble frame data into queue slot
    for (int i = 0; i < C64U_MAX_PACKETS_PER_FRAME; i++) {
        struct frame_packet *packet = &frame->packets[i];
        if (!packet->received)
            continue;

        uint16_t line_num = packet->line_num;
        uint8_t lines_per_packet = packet->lines_per_packet;

        for (int line = 0; line < (int)lines_per_packet && (int)(line_num + line) < (int)context->height; line++) {
            uint32_t *dst_line = queue_frame + ((line_num + line) * C64U_PIXELS_PER_LINE);
            uint8_t *src_line = packet->packet_data + (line * C64U_BYTES_PER_LINE);

            // Optimized color conversion using lookup table
            c64u_convert_pixels_optimized(src_line, dst_line, C64U_BYTES_PER_LINE);
        }
    }

    context->delay_sequence_queue[tail_index] = sequence_num;
    context->delay_queue_tail = (context->delay_queue_tail + 1) % max_queue_size;
    context->delay_queue_size++;

    pthread_mutex_unlock(&context->delay_mutex);
    return true;
}

bool c64u_dequeue_delayed_frame(struct c64u_source *context)
{
    if (pthread_mutex_lock(&context->delay_mutex) != 0) {
        return false;
    }

    // Check if we have enough frames in queue to satisfy delay
    if (context->delay_queue_size < context->render_delay_frames) {
        pthread_mutex_unlock(&context->delay_mutex);
        return false;
    }

    // Copy frame from queue head to back buffer
    size_t frame_size = context->width * context->height * 4;
    uint32_t *queue_frame =
        context->delayed_frame_queue + (context->delay_queue_head * context->width * context->height);

    if (pthread_mutex_lock(&context->frame_mutex) == 0) {
        memcpy(context->frame_buffer_back, queue_frame, frame_size);
        pthread_mutex_unlock(&context->frame_mutex);

        // Remove frame from queue
        context->delay_queue_head =
            (context->delay_queue_head + 1) % (context->render_delay_frames + C64U_RENDER_BUFFER_SAFETY_MARGIN);
        context->delay_queue_size--;

        pthread_mutex_unlock(&context->delay_mutex);
        return true;
    }

    pthread_mutex_unlock(&context->delay_mutex);
    return false;
}

void c64u_clear_delay_queue(struct c64u_source *context)
{
    if (pthread_mutex_lock(&context->delay_mutex) == 0) {
        context->delay_queue_size = 0;
        context->delay_queue_head = 0;
        context->delay_queue_tail = 0;
        pthread_mutex_unlock(&context->delay_mutex);
    }
}

// Performance optimization: Batch process video statistics to reduce hot path overhead
void c64u_process_video_statistics_batch(struct c64u_source *context, uint64_t current_time)
{
    // Only process statistics every 5 seconds to minimize overhead
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL; // 5 seconds

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return; // Not time yet for statistics processing
    }

    // Read and reset counters (no atomics needed - single threaded access per counter type)
    uint64_t packets_received = context->video_packets_received;
    uint64_t bytes_received = context->video_bytes_received;
    uint32_t sequence_errors = context->video_sequence_errors;
    uint32_t frames_processed = context->video_frames_processed;
    context->video_packets_received = 0;
    context->video_bytes_received = 0;
    context->video_sequence_errors = 0;
    context->video_frames_processed = 0;

    // Calculate rates and statistics
    double duration_seconds = time_since_last_log / 1000000000.0;
    double packets_per_second = packets_received / duration_seconds;
    double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);
    double frames_per_second = frames_processed / duration_seconds;
    double loss_percentage = packets_received > 0 ? (100.0 * sequence_errors) / packets_received : 0.0;

    // Calculate frame delivery metrics
    double expected_fps = context->format_detected ? context->expected_fps : 50.0; // Default to PAL
    double frame_delivery_rate = context->frames_delivered_to_obs / duration_seconds;
    double frame_completion_rate = context->frames_completed / duration_seconds;
    double capture_drop_pct =
        context->frames_expected > 0
            ? (100.0 * (context->frames_expected - context->frames_captured)) / context->frames_expected
            : 0.0;
    double delivery_drop_pct =
        context->frames_completed > 0
            ? (100.0 * (context->frames_completed - context->frames_delivered_to_obs)) / context->frames_completed
            : 0.0;
    double avg_pipeline_latency = context->frames_delivered_to_obs > 0
                                      ? context->total_pipeline_latency / (context->frames_delivered_to_obs * 1000000.0)
                                      : 0.0; // Convert to ms

    // Log comprehensive statistics (only if we received packets)
    if (packets_received > 0) {
        C64U_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Loss: %.1f%% | Frames: %u", frames_per_second,
                      bandwidth_mbps, packets_per_second, loss_percentage, (uint32_t)frames_processed);
        C64U_LOG_INFO("🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                      expected_fps, context->frames_captured / duration_seconds, frame_delivery_rate,
                      frame_completion_rate);
        C64U_LOG_INFO(
            "📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms | Buffer swaps %u",
            capture_drop_pct, delivery_drop_pct, avg_pipeline_latency, context->buffer_swaps);
    }

    // Reset diagnostic counters and update timestamp
    context->frames_expected = 0;
    context->frames_captured = 0;
    context->frames_delivered_to_obs = 0;
    context->frames_completed = 0;
    context->buffer_swaps = 0;
    context->total_pipeline_latency = 0;
    context->last_stats_log_time = current_time;
}

// Performance optimization: Batch process audio statistics
void c64u_process_audio_statistics_batch(struct c64u_source *context, uint64_t current_time)
{
    static const uint64_t STATS_INTERVAL_NS = 5000000000ULL; // 5 seconds

    uint64_t time_since_last_log = current_time - context->last_stats_log_time;
    if (time_since_last_log < STATS_INTERVAL_NS) {
        return; // Not time yet for statistics processing
    }

    // Read and reset audio counters
    uint64_t packets_received = context->audio_packets_received;
    uint64_t bytes_received = context->audio_bytes_received;
    context->audio_packets_received = 0;
    context->audio_bytes_received = 0;

    if (packets_received > 0) {
        double duration_seconds = time_since_last_log / 1000000000.0;
        double packets_per_second = packets_received / duration_seconds;
        double bandwidth_mbps = (bytes_received * 8.0) / (duration_seconds * 1000000.0);

        C64U_LOG_INFO("🔊 AUDIO: %.2f Mbps | %.0f pps | Packets: %llu", bandwidth_mbps, packets_per_second,
                      (unsigned long long)packets_received);
    }
}

// Lock-free frame assembly optimization functions
void c64u_init_frame_assembly_lockfree(struct frame_assembly *frame, uint16_t frame_num)
{
    // Initialize frame structure
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
    frame->received_packets = 0;
    frame->complete = false;
    frame->packets_received_mask = 0;
}

bool c64u_try_add_packet_lockfree(struct frame_assembly *frame, uint16_t packet_index)
{
    // Check if packet index is valid
    if (packet_index >= C64U_MAX_PACKETS_PER_FRAME) {
        return false;
    }

    // Set the packet bit and increment counter
    uint64_t packet_mask = 1ULL << packet_index;
    uint64_t old_mask = frame->packets_received_mask;
    frame->packets_received_mask |= packet_mask;

    // If this bit was already set, this is a duplicate packet
    if (old_mask & packet_mask) {
        return false; // Duplicate packet
    }

    // Increment packet counter
    frame->received_packets++;

    return true; // Successfully added new packet
}

bool c64u_is_frame_complete_lockfree(struct frame_assembly *frame)
{
    // Load current packet count
    uint16_t received = frame->received_packets;

    // Frame is complete when we have all expected packets
    // For PAL: 68 packets (272 lines / 4 lines per packet)
    // For NTSC: 60 packets (240 lines / 4 lines per packet)
    uint16_t expected = frame->expected_packets;
    if (expected == 0) {
        // Auto-detect based on received packet pattern
        expected = 68; // Default to PAL, could be refined based on actual data
    }

    bool complete = (received >= expected);

    // Update completion flag if frame is complete
    if (complete) {
        frame->complete = true;
    }

    return complete;
}

// Video thread function
void *c64u_video_thread_func(void *data)
{
    struct c64u_source *context = data;
    uint8_t packet[C64U_VIDEO_PACKET_SIZE];

    C64U_LOG_DEBUG("Video receiver thread started on port %u", context->video_port);

#ifdef _WIN32
    // Windows: Increase thread priority for video receiver to reduce scheduling delays
    // High-frequency UDP packet reception (3400+ packets/sec) benefits from higher priority
    HANDLE thread_handle = GetCurrentThread();
    if (SetThreadPriority(thread_handle, THREAD_PRIORITY_ABOVE_NORMAL)) {
        C64U_LOG_DEBUG("Set video receiver thread to above-normal priority on Windows");
    } else {
        C64U_LOG_WARNING("Failed to set video receiver thread priority on Windows");
    }

    // Windows: Set thread to use high-resolution timing for better scheduling precision
    timeBeginPeriod(1); // Request 1ms timer resolution
#endif

    // Video receiver thread initialized
    C64U_LOG_DEBUG("Video thread function started with optimized scheduling");

    while (context->thread_active) {
        ssize_t received = recv(context->video_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64u_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
                // Windows: Use shorter sleep for high-frequency packet streams
                // C64U sends 3400+ packets/sec, so 1ms sleep can miss multiple packets
                Sleep(0); // Yield to other threads, then retry immediately
                continue;
            }
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
                os_sleep_ms(1); // 1ms delay on Linux (usually works fine)
                continue;
            }
#endif
            C64U_LOG_ERROR("Video socket error: %s", c64u_get_socket_error_string(error));
            break;
        }

        if (received != C64U_VIDEO_PACKET_SIZE) {
            C64U_LOG_WARNING("Received incomplete video packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                             SSIZE_T_CAST(received), C64U_VIDEO_PACKET_SIZE);
            continue;
        }

        // Update timestamp for timeout detection - UDP packet received successfully
        context->last_udp_packet_time = os_gettime_ns();

        // Update statistics counters (video thread only access)
        context->video_packets_received++;
        context->video_bytes_received += received;

        // Parse packet header (streamlined - only what we need for processing)
        uint16_t seq_num = *(uint16_t *)(packet + 0);
        uint16_t frame_num = *(uint16_t *)(packet + 2);
        uint16_t line_num = *(uint16_t *)(packet + 4);
        uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
        uint8_t lines_per_packet = packet[8];
        uint8_t bits_per_pixel = packet[9];

        bool last_packet = (line_num & 0x8000) != 0;
        line_num &= 0x7FFF;

        // Streamlined sequence error tracking
        static uint16_t last_video_seq = 0;
        static bool first_video = true;

        if (!first_video && seq_num != (uint16_t)(last_video_seq + 1)) {
            // Increment sequence error counter
            context->video_sequence_errors++;

            // Log sequence errors (keep for debugging, but move details out of hot path)
            uint16_t expected_seq = (uint16_t)(last_video_seq + 1);
            int16_t seq_diff = (int16_t)(seq_num - expected_seq);

            if (seq_diff > 0) {
                C64U_LOG_WARNING("🔴 UDP OUT-OF-SEQUENCE: Expected seq %u, got %u (skipped %d packets)", expected_seq,
                                 seq_num, seq_diff);
            } else {
                C64U_LOG_WARNING("🔄 UDP OUT-OF-ORDER: Expected seq %u, got %u (reorder offset %d)", expected_seq,
                                 seq_num, seq_diff);
            }
        }
        last_video_seq = seq_num;
        first_video = false;

        // Batch process statistics every N packets or time interval (moved out of hot path)
        uint64_t now = os_gettime_ns();
        c64u_process_video_statistics_batch(context, now);

        // Validate packet data
        if (lines_per_packet != C64U_LINES_PER_PACKET || pixels_per_line != C64U_PIXELS_PER_LINE ||
            bits_per_pixel != 4) {
            C64U_LOG_WARNING("Invalid packet format: lines=%u, pixels=%u, bits=%u", lines_per_packet, pixels_per_line,
                             bits_per_pixel);
            continue;
        }

        // Process packet with frame assembly and double buffering
        if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
            // Track frame capture timing for diagnostics (per-frame, not per-packet)
            uint64_t capture_time = os_gettime_ns();

            // Check if this is a new frame
            if (context->current_frame.frame_num != frame_num) {
                // Log frame transitions to detect skips and duplicates
                if (context->current_frame.frame_num != 0) {
                    uint16_t expected_next = context->current_frame.frame_num + 1;
                    int16_t frame_diff = (int16_t)(frame_num - expected_next);

                    if (frame_diff > 0) {
                        C64U_LOG_WARNING("📽️ FRAME SKIP: Expected frame %u, got %u (skipped %d frames)", expected_next,
                                         frame_num, frame_diff);
                    } else if (frame_diff < 0) {
                        C64U_LOG_WARNING("🔄 FRAME REVERT: Expected frame %u, got %u (went back %d frames)",
                                         expected_next, frame_num, -frame_diff);
                    }
                }

                // Count expected and captured frames only on new frame start
                if (context->last_capture_time > 0) {
                    context->frames_expected++;
                }
                context->frames_captured++;
                context->last_capture_time = capture_time;
                // Complete previous frame if it exists and is reasonably complete
                if (context->current_frame.received_packets > 0) {
                    if (c64u_is_frame_complete(&context->current_frame) ||
                        c64u_is_frame_timeout(&context->current_frame)) {
                        if (c64u_is_frame_complete(&context->current_frame)) {
                            // Handle frame completion with delay queue for timeout case
                            if (context->last_completed_frame != context->current_frame.frame_num) {
                                C64U_LOG_DEBUG(
                                    "✅ FRAME COMPLETE: Frame %u assembled with %u/%u packets (%.1f%% complete)",
                                    context->current_frame.frame_num, context->current_frame.received_packets,
                                    context->current_frame.expected_packets,
                                    (context->current_frame.received_packets * 100.0f) /
                                        context->current_frame.expected_packets);

                                // If no delay configured, process frame immediately
                                if (context->render_delay_frames == 0) {
                                    if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                                        c64u_assemble_frame_to_buffer(context, &context->current_frame);
                                        c64u_swap_frame_buffers(context);
                                        context->last_completed_frame = context->current_frame.frame_num;
                                        // Track diagnostics consistently
                                        context->frames_completed++;
                                        context->buffer_swaps++;
                                        context->frames_delivered_to_obs++;
                                        context->total_pipeline_latency += (os_gettime_ns() - capture_time);

                                        C64U_LOG_DEBUG(
                                            "🚀 IMMEDIATE DELIVERY: Frame %u delivered to OBS (latency: %llu ms)",
                                            context->current_frame.frame_num,
                                            (unsigned long long)((os_gettime_ns() - capture_time) / 1000000));
                                        pthread_mutex_unlock(&context->frame_mutex);
                                    }
                                } else {
                                    // Add frame to delay queue
                                    if (c64u_enqueue_delayed_frame(context, &context->current_frame, seq_num)) {
                                        context->last_completed_frame = context->current_frame.frame_num;
                                        context->frames_completed++;

                                        C64U_LOG_DEBUG("⏳ DELAY QUEUE: Frame %u enqueued (queue size: %u/%u)",
                                                       context->current_frame.frame_num, context->delay_queue_size,
                                                       context->render_delay_frames);

                                        // Try to dequeue a delayed frame if queue has enough frames
                                        if (c64u_dequeue_delayed_frame(context)) {
                                            // Successfully dequeued a frame, make it available to OBS
                                            if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                                                c64u_swap_frame_buffers(context);
                                                context->buffer_swaps++;
                                                context->frames_delivered_to_obs++;
                                                context->total_pipeline_latency += (os_gettime_ns() - capture_time);

                                                C64U_LOG_DEBUG(
                                                    "📺 DELAYED DELIVERY: Frame delivered from delay queue to OBS");
                                                pthread_mutex_unlock(&context->frame_mutex);
                                            }
                                        } else {
                                            C64U_LOG_DEBUG("⏸️ DELAY WAIT: Queue not full yet, waiting for more frames");
                                        }
                                    } else {
                                        C64U_LOG_WARNING("❌ DELAY QUEUE FULL: Failed to enqueue frame %u",
                                                         context->current_frame.frame_num);
                                    }
                                }
                            }
                        } else {
                            // Frame timeout - log drop and continue
                            C64U_LOG_WARNING(
                                "⏰ FRAME TIMEOUT: Frame %u dropped with %u/%u packets (%.1f%% complete, age: %llu ms)",
                                context->current_frame.frame_num, context->current_frame.received_packets,
                                context->current_frame.expected_packets,
                                context->current_frame.expected_packets > 0
                                    ? (context->current_frame.received_packets * 100.0f) /
                                          context->current_frame.expected_packets
                                    : 0.0f,
                                (unsigned long long)((os_gettime_ns() - context->current_frame.start_time) / 1000000));
                            context->frame_drops++;
                        }
                    }
                }

                // Start new frame
                c64u_init_frame_assembly(&context->current_frame, frame_num);
            }

            // Add packet to current frame (calculate packet index from line number)
            uint16_t packet_index = line_num / lines_per_packet;
            if (packet_index < C64U_MAX_PACKETS_PER_FRAME) {
                struct frame_packet *fp = &context->current_frame.packets[packet_index];
                if (!fp->received) {
                    fp->line_num = line_num;
                    fp->lines_per_packet = lines_per_packet;
                    fp->received = true;
                    memcpy(fp->packet_data, packet + C64U_VIDEO_HEADER_SIZE,
                           C64U_VIDEO_PACKET_SIZE - C64U_VIDEO_HEADER_SIZE);
                    context->current_frame.received_packets++;
                } else {
                    // Duplicate packet within same frame - indicates severe packet reordering or duplication
                    C64U_LOG_WARNING("📦 DUPLICATE PACKET: Frame %u, Line %u (packet_index %u) - seq %u", frame_num,
                                     line_num, packet_index, seq_num);
                    context->packet_drops++; // Count as a drop since we can't use it
                }
            } else {
                // Invalid packet index - packet line number is out of range
                C64U_LOG_WARNING("❌ INVALID PACKET: Frame %u, Line %u out of range (packet_index %u >= %d) - seq %u",
                                 frame_num, line_num, packet_index, C64U_MAX_PACKETS_PER_FRAME, seq_num);
                context->packet_drops++;
            }

            // Update expected packet count and detect video format based on last packet
            if (last_packet && context->current_frame.expected_packets == 0) {
                context->current_frame.expected_packets = packet_index + 1;

                // Detect PAL vs NTSC format from frame height
                uint32_t frame_height = line_num + lines_per_packet;
                if (!context->format_detected || context->detected_frame_height != frame_height) {
                    context->detected_frame_height = frame_height;
                    context->format_detected = true;

                    // Calculate expected FPS based on detected format
                    if (frame_height == C64U_PAL_HEIGHT) {
                        context->expected_fps = 50.125; // PAL: 50.125 Hz (actual C64 timing)
                        C64U_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                    } else if (frame_height == C64U_NTSC_HEIGHT) {
                        context->expected_fps = 59.826; // NTSC: 59.826 Hz (actual C64 timing)
                        C64U_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.3f Hz", frame_height, context->expected_fps);
                    } else {
                        // Unknown format, estimate based on packet count
                        context->expected_fps = (frame_height <= 250) ? 59.826 : 50.125;
                        C64U_LOG_WARNING("⚠️ Unknown video format: 384x%u, assuming %.3f Hz", frame_height,
                                         context->expected_fps);
                    }

                    // Update context dimensions if they changed
                    if (context->height != frame_height) {
                        context->height = frame_height;
                        context->width = C64U_PIXELS_PER_LINE; // Always 384
                    }
                }
            }

            // Check if frame is complete
            if (c64u_is_frame_complete(&context->current_frame)) {
                // Handle frame completion with delay queue
                if (context->last_completed_frame != context->current_frame.frame_num) {

                    // If no delay configured, process frame immediately
                    if (context->render_delay_frames == 0) {
                        if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                            c64u_assemble_frame_to_buffer(context, &context->current_frame);
                            c64u_swap_frame_buffers(context);
                            context->last_completed_frame = context->current_frame.frame_num;
                            // Track diagnostics (only once per completed frame!)
                            context->frames_completed++;
                            context->buffer_swaps++;
                            context->frames_delivered_to_obs++;
                            context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                            context->video_frames_processed++;
                            pthread_mutex_unlock(&context->frame_mutex);
                        }
                    } else {
                        // Add frame to delay queue
                        if (c64u_enqueue_delayed_frame(context, &context->current_frame, seq_num)) {
                            context->last_completed_frame = context->current_frame.frame_num;
                            context->frames_completed++;
                            context->video_frames_processed++;

                            // Try to dequeue a delayed frame if queue has enough frames
                            if (c64u_dequeue_delayed_frame(context)) {
                                // Successfully dequeued a frame, make it available to OBS
                                if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                                    c64u_swap_frame_buffers(context);
                                    context->buffer_swaps++;
                                    context->frames_delivered_to_obs++;
                                    context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                                    pthread_mutex_unlock(&context->frame_mutex);
                                }
                            }
                        }
                    }
                }

                // Reset for next frame
                c64u_init_frame_assembly(&context->current_frame, 0);
            }
        }

        pthread_mutex_unlock(&context->assembly_mutex);
    }

    C64U_LOG_DEBUG("Video receiver thread stopped");

#ifdef _WIN32
    // Windows: Restore default timer resolution
    timeEndPeriod(1);
#endif

    return NULL;
}
