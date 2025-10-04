#ifndef C64U_TYPES_H
#define C64U_TYPES_H

#include <obs-module.h>
#include <media-io/audio-io.h>
#include <graphics/graphics.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "c64u-network.h"
#include "c64u-protocol.h"

// Frame packet structure for reordering
struct frame_packet {
    uint16_t line_num;
    uint8_t lines_per_packet;
    uint8_t packet_data[780 - 12]; // C64U_VIDEO_PACKET_SIZE - C64U_VIDEO_HEADER_SIZE
    bool received;
};

// Frame assembly structure (optimized for lock-free operations)
struct frame_assembly {
    uint16_t frame_num;
    struct frame_packet packets[C64U_MAX_PACKETS_PER_FRAME];
    uint16_t received_packets; // Number of packets received
    uint16_t expected_packets;
    bool complete;                  // Frame completion flag
    uint64_t start_time;            // When frame assembly started
    uint64_t packets_received_mask; // Bitmask of received packets (for 64 packets max)
};

struct c64u_source {
    obs_source_t *source;

    // Configuration
    char hostname[64];       // C64U hostname or IP as entered by user
    char ip_address[64];     // C64U IP Address (resolved from hostname)
    char obs_ip_address[64]; // OBS IP Address (this machine)
    bool auto_detect_ip;
    bool initial_ip_detected; // Flag to track if initial IP detection was done
    uint32_t video_port;
    uint32_t audio_port;
    bool streaming;

    // Video data
    uint32_t width;
    uint32_t height;
    uint8_t *video_buffer;

    // Double buffering for smooth video
    uint32_t *frame_buffer_front; // For rendering (OBS thread)
    uint32_t *frame_buffer_back;  // For UDP assembly (video thread)
    bool frame_ready;
    bool buffer_swap_pending;

    // Frame assembly and packet reordering
    struct frame_assembly current_frame;
    uint16_t last_completed_frame;
    uint32_t frame_drops;
    uint32_t packet_drops;

    // Frame diagnostic counters (Stats for Nerds style)
    uint32_t frames_expected;
    uint32_t frames_captured;
    uint32_t frames_delivered_to_obs;
    uint32_t frames_completed;
    uint32_t buffer_swaps;
    uint64_t last_capture_time;
    uint64_t total_capture_latency;
    uint64_t total_pipeline_latency;

    // Dynamic video format detection
    uint32_t detected_frame_height;
    bool format_detected;
    double expected_fps;

    // Audio data
    struct audio_output_info audio_info;

    // Network
    socket_t video_socket;
    socket_t audio_socket;
    socket_t control_socket;
    pthread_t video_thread;
    pthread_t audio_thread;
    bool thread_active;
    bool video_thread_active;
    bool audio_thread_active;

    // Synchronization
    pthread_mutex_t frame_mutex;
    pthread_mutex_t assembly_mutex;

    // Frame timing
    uint64_t last_frame_time;
    uint64_t frame_interval_ns; // Target frame interval (20ms for 50Hz PAL)

    // Render callback based timeout detection
    uint64_t last_udp_packet_time; // Timestamp of last UDP packet
    bool retry_in_progress;        // Flag to prevent redundant retry attempts
    uint32_t retry_count;          // Number of retry attempts
    uint32_t consecutive_failures; // Consecutive TCP failures for backoff

    // Rendering delay
    uint32_t render_delay_frames;   // Delay in frames before making buffer available to OBS
    uint32_t *delayed_frame_queue;  // Circular buffer for delayed frames
    uint32_t delay_queue_size;      // Current size of delay queue
    uint32_t delay_queue_head;      // Head position in delay queue
    uint32_t delay_queue_tail;      // Tail position in delay queue
    uint16_t *delay_sequence_queue; // Sequence numbers for delayed frames
    pthread_mutex_t delay_mutex;    // Mutex for delay queue access

    // Auto-start control
    bool auto_start_attempted;

    // Statistics counters (accessed from single threads, no atomics needed)
    uint64_t video_packets_received; // Total video packets received
    uint64_t video_bytes_received;   // Total video bytes received
    uint32_t video_sequence_errors;  // Sequence number errors (out-of-order, drops)
    uint32_t video_frames_processed; // Total video frames processed
    uint64_t audio_packets_received; // Total audio packets received
    uint64_t audio_bytes_received;   // Total audio bytes received
    uint64_t last_stats_log_time;    // Last time statistics were logged (non-atomic)

    // Logo display for network issues
    gs_texture_t *logo_texture; // Loaded logo texture
    bool logo_load_attempted;   // Have we tried to load the logo?

    // Frame saving for analysis
    bool save_frames;
    char save_folder[512];
    uint32_t saved_frame_count;

    // Video recording for analysis
    bool record_video;
    FILE *video_file;
    FILE *audio_file;
    FILE *timing_file;
    char session_folder[800]; // Current session folder path
    uint64_t recording_start_time;
    uint32_t recorded_frames;
    uint32_t recorded_audio_samples;
    pthread_mutex_t recording_mutex;
};

#endif // C64U_TYPES_H
