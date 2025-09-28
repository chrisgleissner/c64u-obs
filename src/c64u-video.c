#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include "c64u-logging.h"
#include "c64u-video.h"
#include "c64u-types.h"
#include "c64u-protocol.h"
#include "c64u-network.h"
#include "c64u-timing.h"

// VIC color palette (BGRA values for OBS) - converted from grab.py RGB values
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

// Helper functions for frame assembly
void init_frame_assembly(struct frame_assembly *frame, uint16_t frame_num)
{
    memset(frame, 0, sizeof(struct frame_assembly));
    frame->frame_num = frame_num;
    frame->start_time = os_gettime_ns();
}

bool is_frame_complete(struct frame_assembly *frame)
{
    return frame->received_packets > 0 && frame->received_packets == frame->expected_packets;
}

bool is_frame_timeout(struct frame_assembly *frame)
{
    uint64_t elapsed = (os_gettime_ns() - frame->start_time) / 1000000; // Convert to ms
    return elapsed > C64U_FRAME_TIMEOUT_MS;
}

void swap_frame_buffers(struct c64u_source *context)
{
    // Atomically swap front and back buffers
    uint32_t *temp = context->frame_buffer_front;
    context->frame_buffer_front = context->frame_buffer_back;
    context->frame_buffer_back = temp;
    context->frame_ready = true;
    context->buffer_swap_pending = false;
}

void assemble_frame_to_buffer(struct c64u_source *context, struct frame_assembly *frame)
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

            // Convert 4-bit VIC colors to 32-bit RGBA
            for (int x = 0; x < C64U_BYTES_PER_LINE; x++) {
                uint8_t pixel_pair = src_line[x];
                uint8_t color1 = pixel_pair & 0x0F;
                uint8_t color2 = pixel_pair >> 4;

                dst_line[x * 2] = vic_colors[color1];
                dst_line[x * 2 + 1] = vic_colors[color2];
            }
        }
    }
}

// Video thread function
void *video_thread_func(void *data)
{
    struct c64u_source *context = data;
    uint8_t packet[C64U_VIDEO_PACKET_SIZE];
    static int packet_count = 0;

    C64U_LOG_INFO("Video receiver thread started on port %u", context->video_port);
    // Video receiver thread initialized
    C64U_LOG_INFO("🔥 VIDEO THREAD FUNCTION STARTED - Our custom statistics code will run here!");

    while (context->thread_active) {
        ssize_t received = recv(context->video_socket, (char *)packet, (int)sizeof(packet), 0);

        if (received < 0) {
            int error = c64u_get_socket_error();
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
#endif
                os_sleep_ms(1); // 1ms delay
                continue;
            }
            C64U_LOG_ERROR("Video socket error: %s", c64u_get_socket_error_string(error));
            break;
        }

        if (received != C64U_VIDEO_PACKET_SIZE) {
            C64U_LOG_WARNING("Received incomplete video packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                             SSIZE_T_CAST(received), C64U_VIDEO_PACKET_SIZE);
            continue;
        }

        // Debug: Count received packets
        packet_count++;
        // Technical statistics tracking - Video
        static uint64_t last_video_log = 0;
        static uint32_t video_bytes_period = 0;
        static uint32_t video_packets_period = 0;
        static uint16_t last_video_seq = 0;
        static uint32_t video_drops = 0;
        static uint32_t video_frames = 0;
        static bool first_video = true;

        // Parse packet header
        uint16_t seq_num = *(uint16_t *)(packet + 0);
        uint16_t frame_num = *(uint16_t *)(packet + 2);
        uint16_t line_num = *(uint16_t *)(packet + 4);
        uint16_t pixels_per_line = *(uint16_t *)(packet + 6);
        uint8_t lines_per_packet = packet[8];
        uint8_t bits_per_pixel = packet[9];
        uint16_t encoding = *(uint16_t *)(packet + 10);

        UNUSED_PARAMETER(frame_num);
        UNUSED_PARAMETER(pixels_per_line);
        UNUSED_PARAMETER(bits_per_pixel);
        UNUSED_PARAMETER(encoding);

        bool last_packet = (line_num & 0x8000) != 0;
        line_num &= 0x7FFF;

        // Update video statistics
        video_bytes_period += (uint32_t)received; // Cast ssize_t to uint32_t for Windows
        video_packets_period++;

        uint64_t now = os_gettime_ns();
        if (last_video_log == 0) {
            last_video_log = now;
            C64U_LOG_INFO("� Video statistics tracking initialized");
        }

        // Track packet drops (seq_num should increment by 1)
        if (!first_video && seq_num != (uint16_t)(last_video_seq + 1)) {
            video_drops++;
        }
        last_video_seq = seq_num;
        first_video = false;

        // NOTE: Frame counting is now done only in frame assembly completion logic
        // Do not count frames here based on last_packet flag as it creates duplicate counting

        // Log comprehensive video statistics every 5 seconds
        uint64_t time_diff = now - last_video_log;
        if (time_diff >= 5000000000ULL) {
            double duration = time_diff / 1000000000.0;
            double bandwidth_mbps = (video_bytes_period * 8.0) / (duration * 1000000.0);
            double pps = video_packets_period / duration;
            double fps = video_frames / duration;
            double loss_pct = video_packets_period > 0 ? (100.0 * video_drops) / video_packets_period : 0.0;

            // Calculate frame delivery metrics (Stats for Nerds style)
            double expected_fps = context->format_detected ? context->expected_fps
                                                           : 50.0; // Default to PAL if not detected yet
            double frame_delivery_rate = context->frames_delivered_to_obs / duration;
            double frame_completion_rate = context->frames_completed / duration;
            double capture_drop_pct =
                context->frames_expected > 0
                    ? (100.0 * (context->frames_expected - context->frames_captured)) / context->frames_expected
                    : 0.0;
            double delivery_drop_pct = context->frames_completed > 0
                                           ? (100.0 * (context->frames_completed - context->frames_delivered_to_obs)) /
                                                 context->frames_completed
                                           : 0.0;
            double avg_pipeline_latency =
                context->frames_delivered_to_obs > 0
                    ? context->total_pipeline_latency / (context->frames_delivered_to_obs * 1000000.0)
                    : 0.0; // Convert to ms

            C64U_LOG_INFO("📺 VIDEO: %.1f fps | %.2f Mbps | %.0f pps | Loss: %.1f%% | Frames: %u", fps, bandwidth_mbps,
                          pps, loss_pct, video_frames);
            C64U_LOG_INFO(
                "🎯 DELIVERY: Expected %.0f fps | Captured %.1f fps | Delivered %.1f fps | Completed %.1f fps",
                expected_fps, context->frames_captured / duration, frame_delivery_rate, frame_completion_rate);
            C64U_LOG_INFO(
                "📊 PIPELINE: Capture drops %.1f%% | Delivery drops %.1f%% | Avg latency %.1f ms | Buffer swaps %u",
                capture_drop_pct, delivery_drop_pct, avg_pipeline_latency, context->buffer_swaps);

            // Reset period counters
            video_bytes_period = 0;
            video_packets_period = 0;
            video_frames = 0;
            // Reset diagnostic counters
            context->frames_expected = 0;
            context->frames_captured = 0;
            context->frames_delivered_to_obs = 0;
            context->frames_completed = 0;
            context->buffer_swaps = 0;
            context->total_pipeline_latency = 0;
            last_video_log = now;
        }

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
                // Count expected and captured frames only on new frame start
                if (context->last_capture_time > 0) {
                    context->frames_expected++;
                }
                context->frames_captured++;
                context->last_capture_time = capture_time;
                // Complete previous frame if it exists and is reasonably complete
                if (context->current_frame.received_packets > 0) {
                    if (is_frame_complete(&context->current_frame) || is_frame_timeout(&context->current_frame)) {
                        if (is_frame_complete(&context->current_frame)) {
                            // Assemble complete frame and swap buffers (only if not already completed)
                            if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                                if (context->last_completed_frame != context->current_frame.frame_num) {
                                    assemble_frame_to_buffer(context, &context->current_frame);
                                    swap_frame_buffers(context);
                                    context->last_completed_frame = context->current_frame.frame_num;
                                    // Track diagnostics consistently
                                    context->frames_completed++;
                                    context->buffer_swaps++;
                                    context->frames_delivered_to_obs++;
                                    context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                                    // video_frames++ removed - counted in main completion section
                                }
                                pthread_mutex_unlock(&context->frame_mutex);
                            }
                        } else {
                            // Frame timeout - log drop and continue
                            context->frame_drops++;
                        }
                    }
                }

                // Start new frame
                init_frame_assembly(&context->current_frame, frame_num);
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
                            C64U_LOG_INFO("🎥 Detected PAL format: 384x%u @ %.3f Hz", frame_height,
                                          context->expected_fps);
                        } else if (frame_height == C64U_NTSC_HEIGHT) {
                            context->expected_fps = 59.826; // NTSC: 59.826 Hz (actual C64 timing)
                            C64U_LOG_INFO("🎥 Detected NTSC format: 384x%u @ %.3f Hz", frame_height,
                                          context->expected_fps);
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
                if (is_frame_complete(&context->current_frame)) {
                    // Assemble complete frame and swap buffers (only if not already completed)
                    if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                        if (context->last_completed_frame != context->current_frame.frame_num) {
                            assemble_frame_to_buffer(context, &context->current_frame);
                            swap_frame_buffers(context);
                            context->last_completed_frame = context->current_frame.frame_num;

                            // Notify timing system of new C64 frame
                            if (context->timing && context->timing_initialized) {
                                c64u_timing_on_c64_frame_received(context->timing, os_gettime_ns());
                            }

                            // Track diagnostics (only once per completed frame!)
                            context->frames_completed++;
                            context->buffer_swaps++;
                            context->frames_delivered_to_obs++;
                            context->total_pipeline_latency += (os_gettime_ns() - capture_time);
                            video_frames++; // Count completed frames for statistics (primary location)
                        }
                        pthread_mutex_unlock(&context->frame_mutex);
                    }

                    // Reset for next frame
                    init_frame_assembly(&context->current_frame, 0);
                }
            }

            pthread_mutex_unlock(&context->assembly_mutex);
        }
    }

    C64U_LOG_INFO("Video receiver thread stopped");
    return NULL;
}
