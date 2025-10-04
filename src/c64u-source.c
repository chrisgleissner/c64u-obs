#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/threading.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include "c64u-network.h" // Include network header first to avoid Windows header conflicts

#include "c64u-logging.h"
#include "c64u-source.h"
#include "c64u-types.h"
#include "c64u-protocol.h"
#include "c64u-video.h"
#include "c64u-color.h"
#include "c64u-audio.h"
#include "c64u-record.h"
#include "c64u-version.h"
#include "c64u-properties.h"
#include "plugin-support.h"

// Forward declarations
static void close_and_reset_sockets(struct c64u_source *context);

// Async retry task - runs in OBS thread pool (NOT render thread)
// Based on working 0.4.3 approach but simplified
static void c64u_async_retry_task(void *data)
{
    struct c64u_source *context = (struct c64u_source *)data;

    if (!context) {
        C64U_LOG_WARNING("Async retry task called with NULL context");
        return;
    }

    C64U_LOG_INFO("Async retry attempt %u - %s", context->retry_count,
                  context->streaming ? "sending start commands" : "starting streaming");

    bool tcp_success = false;

    if (!context->streaming) {
        // Not streaming - need to create UDP sockets and threads (like 0.4.3)
        c64u_start_streaming(context);
        tcp_success = true; // c64u_start_streaming handles TCP commands internally
    } else {
        // Already streaming - test connectivity and send start commands (like 0.4.3)
        // Use quick connectivity test (250ms timeout) instead of blocking TCP socket creation
        if (c64u_test_connectivity(context->ip_address, C64U_CONTROL_PORT)) {
            c64u_send_control_command(context, true, 0); // Video
            c64u_send_control_command(context, true, 1); // Audio
            tcp_success = true;
            context->consecutive_failures = 0; // Reset failure counter on success
        } else {
            tcp_success = false;
            context->consecutive_failures++;
        }
    }

    context->retry_count++;

    if (!tcp_success) {
        C64U_LOG_DEBUG("TCP connection failed (%u consecutive failures)", context->consecutive_failures);
    }

    // Clear retry state - allows future retries
    context->retry_in_progress = false;
}

// Helper function to safely close and reset sockets
static void close_and_reset_sockets(struct c64u_source *context)
{
    if (context->video_socket != INVALID_SOCKET_VALUE) {
        close(context->video_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
    }
    if (context->audio_socket != INVALID_SOCKET_VALUE) {
        close(context->audio_socket);
        context->audio_socket = INVALID_SOCKET_VALUE;
    }
}

// Load the C64U logo texture from module data directory
static gs_texture_t *load_logo_texture(void)
{
    C64U_LOG_DEBUG("Attempting to load logo texture...");

    // Use obs_module_file() to get the path to the logo in the data directory
    char *logo_path = obs_module_file("images/c64u-logo.png");
    if (!logo_path) {
        C64U_LOG_WARNING("Failed to locate logo file in module data directory");
        return NULL;
    }

    C64U_LOG_DEBUG("Logo path resolved to: %s", logo_path);

    // Load texture directly from the data directory
    gs_texture_t *logo_texture = gs_texture_create_from_file(logo_path);

    if (!logo_texture) {
        C64U_LOG_WARNING("Failed to load logo texture from: %s", logo_path);
    } else {
        uint32_t width = gs_texture_get_width(logo_texture);
        uint32_t height = gs_texture_get_height(logo_texture);
        C64U_LOG_DEBUG("Loaded logo texture from: %s (size: %ux%u)", logo_path, width, height);
    }

    bfree(logo_path);
    return logo_texture;
}

void *c64u_create(obs_data_t *settings, obs_source_t *source)
{
    C64U_LOG_INFO("Creating C64U source");

    // C64U source creation

    // Initialize networking on first use
    static bool networking_initialized = false;
    if (!networking_initialized) {
        if (!c64u_init_networking()) {
            C64U_LOG_ERROR("Failed to initialize networking");
            return NULL;
        }
        networking_initialized = true;
    }

    // Initialize color conversion optimization on first use
    static bool color_lut_initialized = false;
    if (!color_lut_initialized) {
        c64u_init_color_conversion_lut();
        color_lut_initialized = true;
    }

    struct c64u_source *context = bzalloc(sizeof(struct c64u_source));
    if (!context) {
        C64U_LOG_ERROR("Failed to allocate memory for source context");
        return NULL;
    }

    context->source = source;

    // Initialize configuration from settings
    const char *host = obs_data_get_string(settings, "c64u_host");
    const char *hostname = host ? host : C64U_DEFAULT_HOST;

    // Store the original hostname/IP as entered by user
    strncpy(context->hostname, hostname, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Get configured DNS server IP
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");

    // Resolve hostname to IP address for actual connections
    if (!c64u_resolve_hostname_with_dns(hostname, dns_server_ip, context->ip_address, sizeof(context->ip_address))) {
        // If hostname resolution fails, store the hostname as-is (might be invalid IP like 0.0.0.0)
        strncpy(context->ip_address, hostname, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
        C64U_LOG_WARNING("Could not resolve hostname '%s', using as-is: %s", hostname, context->ip_address);
    } else {
        C64U_LOG_INFO("Resolved C64U host '%s' to IP: %s", hostname, context->ip_address);
    }

    context->auto_detect_ip = obs_data_get_bool(settings, "auto_detect_ip");
    context->video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    context->audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    context->streaming = false;

    // Initialize OBS IP address from settings or auto-detect on first run
    memset(context->obs_ip_address, 0, sizeof(context->obs_ip_address));
    const char *saved_obs_ip = obs_data_get_string(settings, "obs_ip_address");

    if (saved_obs_ip && strlen(saved_obs_ip) > 0) {
        // Use previously saved/configured OBS IP address
        strncpy(context->obs_ip_address, saved_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->initial_ip_detected = true;
        C64U_LOG_INFO("Using saved OBS IP address: %s", context->obs_ip_address);
    } else {
        // First time - detect local IP address
        if (c64u_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
            C64U_LOG_INFO("Successfully detected OBS IP address: %s", context->obs_ip_address);
            context->initial_ip_detected = true;
            // Save the detected IP to settings for future use
            obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
        } else {
            C64U_LOG_WARNING("Failed to detect OBS IP address, will use configured value");
            context->initial_ip_detected = false;
        }
    }

    // Ensure we have a valid OBS IP address - use localhost as last resort
    if (strlen(context->obs_ip_address) == 0) {
        C64U_LOG_INFO("No OBS IP configured, using localhost as fallback");
        strncpy(context->obs_ip_address, "127.0.0.1", sizeof(context->obs_ip_address) - 1);
        obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
    }

    // Set default ports if not configured
    if (context->video_port == 0)
        context->video_port = C64U_DEFAULT_VIDEO_PORT;
    if (context->audio_port == 0)
        context->audio_port = C64U_DEFAULT_AUDIO_PORT;

    // Initialize video format (start with PAL, will be detected from stream)
    context->width = C64U_PAL_WIDTH;
    context->height = C64U_PAL_HEIGHT;

    // Allocate video buffers (double buffering)
    size_t frame_size = context->width * context->height * 4; // RGBA
    context->frame_buffer_front = bmalloc(frame_size);
    context->frame_buffer_back = bmalloc(frame_size);
    if (!context->frame_buffer_front || !context->frame_buffer_back) {
        C64U_LOG_ERROR("Failed to allocate video frame buffers");
        if (context->frame_buffer_front)
            bfree(context->frame_buffer_front);
        if (context->frame_buffer_back)
            bfree(context->frame_buffer_back);
        bfree(context);
        return NULL;
    }
    memset(context->frame_buffer_front, 0, frame_size);
    memset(context->frame_buffer_back, 0, frame_size);
    context->frame_ready = false;
    context->last_frame_time = 0; // Initialize frame timeout detection

    // Initialize video format detection
    context->detected_frame_height = 0;
    context->format_detected = false;
    context->expected_fps = 50.125; // Default to PAL timing until detected

    // Initialize mutexes
    if (pthread_mutex_init(&context->frame_mutex, NULL) != 0) {
        C64U_LOG_ERROR("Failed to initialize frame mutex");
        bfree(context->frame_buffer_front);
        bfree(context->frame_buffer_back);
        bfree(context);
        return NULL;
    }
    if (pthread_mutex_init(&context->assembly_mutex, NULL) != 0) {
        C64U_LOG_ERROR("Failed to initialize assembly mutex");
        pthread_mutex_destroy(&context->frame_mutex);
        bfree(context->frame_buffer_front);
        bfree(context->frame_buffer_back);
        bfree(context);
        return NULL;
    }

    // Initialize delay queue mutex
    if (pthread_mutex_init(&context->delay_mutex, NULL) != 0) {
        C64U_LOG_ERROR("Failed to initialize delay mutex");
        pthread_mutex_destroy(&context->frame_mutex);
        pthread_mutex_destroy(&context->assembly_mutex);
        bfree(context->frame_buffer_front);
        bfree(context->frame_buffer_back);
        bfree(context);
        return NULL;
    }

    // Initialize rendering delay from settings
    context->render_delay_frames = (uint32_t)obs_data_get_int(settings, "render_delay_frames");
    if (context->render_delay_frames == 0) {
        context->render_delay_frames = C64U_DEFAULT_RENDER_DELAY_FRAMES;
    }

    // Initialize delay queue - allocate for maximum delay + some extra buffer
    context->delay_queue_size = 0;
    context->delay_queue_head = 0;
    context->delay_queue_tail = 0;
    context->delayed_frame_queue = NULL;
    context->delay_sequence_queue = NULL;

    C64U_LOG_INFO("Rendering delay initialized: %u frames", context->render_delay_frames);

    // Initialize sockets to invalid
    context->video_socket = INVALID_SOCKET_VALUE;
    context->audio_socket = INVALID_SOCKET_VALUE;
    context->control_socket = INVALID_SOCKET_VALUE;
    context->thread_active = false;
    context->video_thread_active = false;
    context->audio_thread_active = false;
    context->auto_start_attempted = false;

    // Initialize statistics counters
    context->video_packets_received = 0;
    context->video_bytes_received = 0;
    context->video_sequence_errors = 0;
    context->video_frames_processed = 0;
    context->audio_packets_received = 0;
    context->audio_bytes_received = 0;
    context->last_stats_log_time = os_gettime_ns();

    // Initialize render callback timeout system
    uint64_t now = os_gettime_ns();
    context->last_udp_packet_time = now;
    context->retry_in_progress = false;
    context->retry_count = 0;
    context->consecutive_failures = 0;

    // Initialize logo display
    context->logo_texture = NULL;
    context->logo_load_attempted = false;

    // Initialize recording for this source
    c64u_record_init(context);

    // Start initial connection asynchronously to avoid blocking OBS startup
    C64U_LOG_INFO("C64U source created successfully - queuing async initial connection");
    context->retry_in_progress = true; // Prevent render thread from also starting retry
    obs_queue_task(OBS_TASK_UI, c64u_async_retry_task, context, false);

    return context;
}

void c64u_destroy(void *data)
{
    struct c64u_source *context = data;
    if (!context)
        return;

    C64U_LOG_INFO("Destroying C64U source");

    // No retry thread to shutdown - using async delegation approach

    // Stop streaming if active
    if (context->streaming) {
        C64U_LOG_DEBUG("Stopping active streaming during destruction");
        context->streaming = false;
        context->thread_active = false;

        // Note: No TCP calls in destroy - async system handles cleanup

        // Close sockets
        close_and_reset_sockets(context);

        // Wait for threads to finish
        if (context->video_thread_active) {
            pthread_join(context->video_thread, NULL);
            context->video_thread_active = false;
        }
        if (context->audio_thread_active) {
            pthread_join(context->audio_thread, NULL);
            context->audio_thread_active = false;
        }
    }

    c64u_record_cleanup(context);

    if (context->logo_texture) {
        gs_texture_destroy(context->logo_texture);
        context->logo_texture = NULL;
    }

    // Cleanup resources
    pthread_mutex_destroy(&context->frame_mutex);
    pthread_mutex_destroy(&context->assembly_mutex);
    pthread_mutex_destroy(&context->delay_mutex);
    if (context->frame_buffer_front) {
        bfree(context->frame_buffer_front);
    }
    if (context->frame_buffer_back) {
        bfree(context->frame_buffer_back);
    }
    if (context->delayed_frame_queue) {
        bfree(context->delayed_frame_queue);
    }
    if (context->delay_sequence_queue) {
        bfree(context->delay_sequence_queue);
    }

    bfree(context);
    C64U_LOG_INFO("C64U source destroyed");
}

void c64u_update(void *data, obs_data_t *settings)
{
    struct c64u_source *context = data;
    if (!context)
        return;

    // Update debug logging setting
    c64u_debug_logging = obs_data_get_bool(settings, "debug_logging");
    C64U_LOG_DEBUG("Debug logging %s", c64u_debug_logging ? "enabled" : "disabled"); // Update IP detection setting
    bool new_auto_detect = obs_data_get_bool(settings, "auto_detect_ip");
    if (new_auto_detect != context->auto_detect_ip || new_auto_detect) {
        context->auto_detect_ip = new_auto_detect;
        if (new_auto_detect) {
            // Re-detect IP address
            if (c64u_detect_local_ip(context->obs_ip_address, sizeof(context->obs_ip_address))) {
                C64U_LOG_INFO("Updated OBS IP address: %s", context->obs_ip_address);
                // Save the updated IP to settings
                obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
            } else {
                C64U_LOG_WARNING("Failed to update OBS IP address");
            }
        }
    }

    // Update configuration
    const char *new_host = obs_data_get_string(settings, "c64u_host");
    const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
    uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");

    // Set defaults
    if (!new_host)
        new_host = C64U_DEFAULT_HOST;
    if (new_video_port == 0)
        new_video_port = C64U_DEFAULT_VIDEO_PORT;
    if (new_audio_port == 0)
        new_audio_port = C64U_DEFAULT_AUDIO_PORT;

    // Check if ports have changed (requires socket recreation)
    bool ports_changed = (new_video_port != context->video_port) || (new_audio_port != context->audio_port);

    if (ports_changed && context->streaming) {
        C64U_LOG_INFO("Port configuration changed (video: %u->%u, audio: %u->%u), recreating sockets",
                      context->video_port, new_video_port, context->audio_port, new_audio_port);

        // Stop streaming and close existing sockets
        c64u_stop_streaming(context);

        // Give the C64U device time to process stop commands
        os_sleep_ms(100);
    }

    // Update configuration - hostname and IP resolution
    strncpy(context->hostname, new_host, sizeof(context->hostname) - 1);
    context->hostname[sizeof(context->hostname) - 1] = '\0';

    // Get configured DNS server IP
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");

    // Resolve hostname to IP address for connections
    if (!c64u_resolve_hostname_with_dns(new_host, dns_server_ip, context->ip_address, sizeof(context->ip_address))) {
        // If hostname resolution fails, store the hostname as-is (might be invalid IP like 0.0.0.0)
        strncpy(context->ip_address, new_host, sizeof(context->ip_address) - 1);
        context->ip_address[sizeof(context->ip_address) - 1] = '\0';
        C64U_LOG_WARNING("Could not resolve hostname '%s' during update, using as-is: %s", new_host,
                         context->ip_address);
    } else {
        C64U_LOG_DEBUG("Resolved C64U host '%s' to IP: %s", new_host, context->ip_address);
    }
    if (new_obs_ip) {
        strncpy(context->obs_ip_address, new_obs_ip, sizeof(context->obs_ip_address) - 1);
        context->obs_ip_address[sizeof(context->obs_ip_address) - 1] = '\0';
    }
    context->video_port = new_video_port;
    context->audio_port = new_audio_port;

    // Update rendering delay setting
    uint32_t new_delay_frames = (uint32_t)obs_data_get_int(settings, "render_delay_frames");
    if (new_delay_frames != context->render_delay_frames) {
        C64U_LOG_INFO("Rendering delay changed from %u to %u frames", context->render_delay_frames, new_delay_frames);

        if (pthread_mutex_lock(&context->delay_mutex) == 0) {
            context->render_delay_frames = new_delay_frames;

            // Reset delay queue when delay changes
            context->delay_queue_size = 0;
            context->delay_queue_head = 0;
            context->delay_queue_tail = 0;

            // Force reallocation of delay buffers on next frame
            if (context->delayed_frame_queue) {
                bfree(context->delayed_frame_queue);
                context->delayed_frame_queue = NULL;
            }
            if (context->delay_sequence_queue) {
                bfree(context->delay_sequence_queue);
                context->delay_sequence_queue = NULL;
            }

            pthread_mutex_unlock(&context->delay_mutex);
        }
    }

    // Update recording settings
    c64u_record_update_settings(context, settings);

    // Start streaming with current configuration (will create new sockets if needed)
    C64U_LOG_INFO("Applying configuration and starting streaming");
    c64u_start_streaming(context);
}

void c64u_start_streaming(struct c64u_source *context)
{
    if (!context) {
        C64U_LOG_WARNING("Cannot start streaming - invalid context");
        return;
    }

    C64U_LOG_INFO("Starting C64U streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", context->ip_address,
                  context->obs_ip_address, context->video_port, context->audio_port);

    // Always close existing sockets before creating new ones (handles reconnection case)
    close_and_reset_sockets(context);

    // Create fresh UDP sockets (critical for reconnection after C64U restart)
    context->video_socket = c64u_create_udp_socket(context->video_port);
    context->audio_socket = c64u_create_udp_socket(context->audio_port);

    if (context->video_socket == INVALID_SOCKET_VALUE || context->audio_socket == INVALID_SOCKET_VALUE) {
        C64U_LOG_ERROR("Failed to create UDP sockets for streaming");
        close_and_reset_sockets(context);
        return;
    }

    // Send start commands to C64U
    c64u_send_control_command(context, true, 0); // Start video
    c64u_send_control_command(context, true, 1); // Start audio

    // Stop existing threads before creating new ones (handles reconnection case)
    if (context->streaming) {
        context->streaming = false;
        context->thread_active = false;

        // Wait for existing threads to finish
        if (context->video_thread_active && pthread_join(context->video_thread, NULL) != 0) {
            C64U_LOG_WARNING("Failed to join existing video thread during reconnection");
        }
        if (context->audio_thread_active && pthread_join(context->audio_thread, NULL) != 0) {
            C64U_LOG_WARNING("Failed to join existing audio thread during reconnection");
        }
        context->video_thread_active = false;
        context->audio_thread_active = false;
    }

    // Start fresh worker threads
    context->thread_active = true;
    context->streaming = true;

    if (pthread_create(&context->video_thread, NULL, c64u_video_thread_func, context) != 0) {
        C64U_LOG_ERROR("Failed to create video receiver thread");
        context->streaming = false;
        context->thread_active = false;
        close_and_reset_sockets(context);
        return;
    }
    context->video_thread_active = true;

    if (pthread_create(&context->audio_thread, NULL, audio_thread_func, context) != 0) {
        C64U_LOG_ERROR("Failed to create audio receiver thread");
        context->streaming = false;
        context->thread_active = false;
        if (context->video_thread_active) {
            pthread_join(context->video_thread, NULL);
            context->video_thread_active = false;
        }
        close_and_reset_sockets(context);
        return;
    }
    context->audio_thread_active = true;

    // Initialize delay queue for rendering delay
    c64u_init_delay_queue(context);

    C64U_LOG_INFO("C64U streaming started successfully");
}

void c64u_stop_streaming(struct c64u_source *context)
{
    if (!context || !context->streaming) {
        C64U_LOG_WARNING("Cannot stop streaming - invalid context or not streaming");
        return;
    }

    C64U_LOG_INFO("Stopping C64U streaming...");

    context->streaming = false;
    context->thread_active = false;

    // Note: No TCP stop commands in OBS callback thread - async system handles cleanup

    // Close sockets to wake up threads
    close_and_reset_sockets(context);

    // Wait for threads to finish
    if (context->video_thread_active && pthread_join(context->video_thread, NULL) != 0) {
        C64U_LOG_WARNING("Failed to join video thread");
    }
    context->video_thread_active = false;

    if (context->audio_thread_active && pthread_join(context->audio_thread, NULL) != 0) {
        C64U_LOG_WARNING("Failed to join audio thread");
    }
    context->audio_thread_active = false;

    // Reset frame state and clear buffers
    if (pthread_mutex_lock(&context->frame_mutex) == 0) {
        context->frame_ready = false;
        context->buffer_swap_pending = false;

        // Clear frame buffers to prevent yellow screen
        if (context->frame_buffer_front && context->frame_buffer_back) {
            uint32_t frame_size = context->width * context->height * 4;
            memset(context->frame_buffer_front, 0, frame_size);
            memset(context->frame_buffer_back, 0, frame_size);
        }

        pthread_mutex_unlock(&context->frame_mutex);
    }

    // Reset frame assembly state
    if (pthread_mutex_lock(&context->assembly_mutex) == 0) {
        memset(&context->current_frame, 0, sizeof(context->current_frame));
        context->last_completed_frame = 0;
        context->frame_drops = 0;
        context->packet_drops = 0;
        context->frames_expected = 0;
        context->frames_captured = 0;
        context->frames_delivered_to_obs = 0;
        context->frames_completed = 0;
        pthread_mutex_unlock(&context->assembly_mutex);
    }

    // Clear delay queue
    c64u_clear_delay_queue(context);

    C64U_LOG_INFO("C64U streaming stopped");
}

void c64u_render(void *data, gs_effect_t *effect)
{
    struct c64u_source *context = data;
    UNUSED_PARAMETER(effect);

    if (!context) {
        C64U_LOG_ERROR("Render called with NULL context!");
        return;
    }

    // Lazy load logo texture on first render (when graphics context is available)
    if (!context->logo_load_attempted) {
        context->logo_texture = load_logo_texture();
        context->logo_load_attempted = true;
    }

    // Check if we should show logo:
    // 1. Not streaming, OR
    // 2. No frames ready, OR
    // 3. No frame buffer, OR
    // 4. Frames have timed out (no new frames for timeout period)
    uint64_t now = os_gettime_ns();
    bool frames_timed_out = false;
    bool needs_initial_connection = false;

    if (context->last_frame_time > 0) {
        // We have received frames before - check for timeout regardless of frame_ready state
        uint64_t frame_age = now - context->last_frame_time;
        frames_timed_out = (frame_age > C64U_FRAME_TIMEOUT_NS);
    } else {
        // No frames received yet - need initial connection (regardless of streaming flag)
        // This handles both: OBS started first, and failed connection attempts
        needs_initial_connection = true;
    }

    // Simple retry detection: render thread detects need, delegates to async task
    if (frames_timed_out || needs_initial_connection) {
        static uint64_t last_retry_time = 0;
        uint64_t time_since_last_retry = now - last_retry_time;

        // Simple timing: 1 second between retries, no complex backoff needed
        // The async task will handle the actual TCP timeouts and failures
        bool should_retry = (time_since_last_retry >= 1000000000ULL) &&  // 1 second between retries
                            !context->retry_in_progress &&               // Don't overlap retries
                            strcmp(context->ip_address, "0.0.0.0") != 0; // Valid IP configured

        if (should_retry) {
            last_retry_time = now;
            context->retry_in_progress = true;

            // Just delegate to async task - keep render thread fast
            C64U_LOG_INFO("� Connection needed - delegating to async task");
            obs_queue_task(OBS_TASK_UI, c64u_async_retry_task, context, false);
        }
    } else if (context->frame_ready && context->last_frame_time > 0) {
        // Frames are arriving normally - reset retry counters
        context->retry_in_progress = false;
        context->retry_count = 0;
        context->consecutive_failures = 0;
    }

    bool should_show_logo = !context->streaming || !context->frame_ready || !context->frame_buffer_front ||
                            frames_timed_out;

    // Debug logging (only when debug logging is enabled)
    if (c64u_debug_logging) {
        static uint64_t last_frame_debug_log = 0;
        if (last_frame_debug_log == 0 || (now - last_frame_debug_log) >= C64U_DEBUG_LOG_INTERVAL_NS) {
            uint64_t frame_age = (context->last_frame_time > 0) ? (now - context->last_frame_time) / 1000000000ULL
                                                                : 999;
            C64U_LOG_DEBUG(
                "🎬 Frame state: should_show_logo=%d, streaming=%d, frame_ready=%d, frames_timed_out=%d, frame_age=%" PRIu64
                "s",
                should_show_logo, context->streaming, context->frame_ready, frames_timed_out, frame_age);
            last_frame_debug_log = now;
        }
    }

    // Additional debug when logo should be showing
    static uint64_t last_debug_log = 0;
    if (should_show_logo && (last_debug_log == 0 || (now - last_debug_log) >= C64U_DEBUG_LOG_INTERVAL_NS)) {
        const char *reason = !context->streaming            ? "not_streaming"
                             : !context->frame_ready        ? "no_frames"
                             : !context->frame_buffer_front ? "no_buffer"
                             : frames_timed_out             ? "frame_timeout"
                                                            : "unknown";
        C64U_LOG_DEBUG("🖼️ Showing logo (%s): streaming=%d, frame_ready=%d, frames_timed_out=%d, C64_IP='%s'", reason,
                       context->streaming, context->frame_ready, frames_timed_out, context->ip_address);
        last_debug_log = now;
    }

    // Clear frame ready state immediately when frames timeout to show logo
    if (frames_timed_out) {
        context->frame_ready = false;
    }

    // REMOVED: Self-healing TCP calls from render thread (causes UI blocking)
    // Self-healing will be handled by a separate async mechanism
    // For now, just detect timeout conditions without making network calls

    if (should_show_logo) {
        // Show C64U logo centered on black background
        if (context->logo_texture) {
            // First draw black background
            gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
            gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

            if (solid && color) {
                gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
                if (tech) {
                    gs_technique_begin(tech);
                    gs_technique_begin_pass(tech, 0);

                    // Black background
                    struct vec4 black = {0.0f, 0.0f, 0.0f, 1.0f};
                    gs_effect_set_vec4(color, &black);
                    gs_draw_sprite(NULL, 0, context->width, context->height);

                    gs_technique_end_pass(tech);
                    gs_technique_end(tech);
                }
            }

            // Then draw centered logo
            gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
            if (default_effect) {
                gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
                if (tech) {
                    uint32_t logo_width = gs_texture_get_width(context->logo_texture);
                    uint32_t logo_height = gs_texture_get_height(context->logo_texture);

                    // Calculate scale to fit logo within canvas while maintaining aspect ratio
                    float scale_x = (float)context->width / (float)logo_width;
                    float scale_y = (float)context->height / (float)logo_height;
                    float scale = (scale_x < scale_y) ? scale_x : scale_y;

                    // Limit scale to max 1.0 (don't enlarge small logos)
                    if (scale > 1.0f) {
                        scale = 1.0f;
                    }

                    // Calculate scaled dimensions
                    float scaled_width = logo_width * scale;
                    float scaled_height = logo_height * scale;

                    // Center the scaled logo
                    float x = (context->width - scaled_width) / 2.0f;
                    float y = (context->height - scaled_height) / 2.0f;

                    gs_matrix_push();
                    gs_matrix_translate3f(x, y, 0.0f);
                    gs_matrix_scale3f(scale, scale, 1.0f);

                    gs_technique_begin(tech);
                    gs_technique_begin_pass(tech, 0);

                    gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
                    gs_effect_set_texture(image, context->logo_texture);

                    gs_draw_sprite(context->logo_texture, 0, logo_width, logo_height);

                    gs_technique_end_pass(tech);
                    gs_technique_end(tech);

                    gs_matrix_pop();
                }
            }
        } else {
            // Fallback to colored background if logo failed to load
            gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
            gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

            if (solid && color) {
                gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
                if (tech) {
                    gs_technique_begin(tech);
                    gs_technique_begin_pass(tech, 0);

                    if (context->streaming) {
                        // Show yellow if streaming but no frame ready yet
                        struct vec4 yellow = {0.8f, 0.8f, 0.2f, 1.0f};
                        gs_effect_set_vec4(color, &yellow);
                    } else {
                        // Show dark blue to indicate plugin loaded but no streaming
                        struct vec4 dark_blue = {0.1f, 0.2f, 0.4f, 1.0f};
                        gs_effect_set_vec4(color, &dark_blue);
                    }

                    gs_draw_sprite(NULL, 0, context->width, context->height);

                    gs_technique_end_pass(tech);
                    gs_technique_end(tech);
                }
            }
        }
    } else {
        // Render actual C64U video frame from front buffer
        if (pthread_mutex_lock(&context->frame_mutex) == 0) {
            // Create texture from front buffer data
            gs_texture_t *texture = gs_texture_create(context->width, context->height, GS_RGBA, 1,
                                                      (const uint8_t **)&context->frame_buffer_front, 0);
            if (texture) {
                // Use default effect for texture rendering
                gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
                if (default_effect) {
                    gs_eparam_t *image_param = gs_effect_get_param_by_name(default_effect, "image");
                    if (image_param) {
                        gs_effect_set_texture(image_param, texture);

                        gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
                        if (tech) {
                            gs_technique_begin(tech);
                            gs_technique_begin_pass(tech, 0);
                            gs_draw_sprite(texture, 0, context->width, context->height);
                            gs_technique_end_pass(tech);
                            gs_technique_end(tech);
                        }
                    }
                }

                // Clean up texture
                gs_texture_destroy(texture);
            }

            pthread_mutex_unlock(&context->frame_mutex);
        }
    }
}

uint32_t c64u_get_width(void *data)
{
    struct c64u_source *context = data;
    return context->width;
}

uint32_t c64u_get_height(void *data)
{
    struct c64u_source *context = data;
    return context->height;
}

const char *c64u_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("C64UDisplay");
}

obs_properties_t *c64u_properties(void *data)
{
    return c64u_create_properties(data);
}

void c64u_defaults(obs_data_t *settings)
{
    c64u_set_property_defaults(settings);
}
