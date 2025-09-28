#ifndef C64U_TYPES_H
#define C64U_TYPES_H

#include <obs-module.h>
#include <media-io/audio-io.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "c64u-network.h"

// Forward declaration for timing system
struct c64u_timing_state;

// Frame packet structure for reordering
struct frame_packet {
    uint16_t line_num;
    uint8_t lines_per_packet;
    uint8_t packet_data[780 - 12]; // C64U_VIDEO_PACKET_SIZE - C64U_VIDEO_HEADER_SIZE
    bool received;
};

// Frame assembly structure
struct frame_assembly {
    uint16_t frame_num;
    uint16_t expected_packets;
    uint16_t received_packets;
    struct frame_packet packets[68]; // C64U_MAX_PACKETS_PER_FRAME
    bool complete;
    uint64_t start_time;
};

struct c64u_source {
    obs_source_t *source;

    // Configuration
    char ip_address[64];     // C64 IP Address (C64 Ultimate device)
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

    // Frame timing system
    struct c64u_timing_state *timing;
    uint64_t obs_target_fps_x1000; // OBS target FPS * 1000 for precision (50000 or 60000)
    bool timing_initialized;

    // Legacy frame timing (deprecated, kept for compatibility)
    uint64_t last_frame_time;
    uint64_t frame_interval_ns;

    // Auto-start control
    bool auto_start_attempted;
};

#endif // C64U_TYPES_H
