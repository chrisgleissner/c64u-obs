#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/threading.h>
#include <string.h>
#include <pthread.h>
#include "c64u-logging.h"
#include "c64u-source.h"
#include "c64u-types.h"
#include "c64u-protocol.h"
#include "c64u-video.h"
#include "c64u-network.h"
#include "c64u-audio.h"
#include "c64u-record.h"
#include "plugin-support.h"

// Load the C64U logo texture from module data directory
static gs_texture_t *load_logo_texture(void)
{
    C64U_LOG_INFO("Attempting to load logo texture...");

    // Use obs_module_file() to get the path to the logo in the data directory
    char *logo_path = obs_module_file("images/c64u-logo.png");
    if (!logo_path) {
        C64U_LOG_WARNING("Failed to locate logo file in module data directory");
        return NULL;
    }

    C64U_LOG_INFO("Logo path resolved to: %s", logo_path);

    // Load texture directly from the data directory
    gs_texture_t *logo_texture = gs_texture_create_from_file(logo_path);

    if (!logo_texture) {
        C64U_LOG_WARNING("Failed to load logo texture from: %s", logo_path);
    } else {
        uint32_t width = gs_texture_get_width(logo_texture);
        uint32_t height = gs_texture_get_height(logo_texture);
        C64U_LOG_INFO("Loaded logo texture from: %s (size: %ux%u)", logo_path, width, height);
    }

    bfree(logo_path);
    return logo_texture;
}

void *c64u_create(obs_data_t *settings, obs_source_t *source)
{
    C64U_LOG_INFO("🚀 Creating C64U source - settings: %p, source: %p", (void *)settings, (void *)source);

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

    struct c64u_source *context = bzalloc(sizeof(struct c64u_source));
    if (!context) {
        C64U_LOG_ERROR("Failed to allocate memory for source context");
        return NULL;
    }

    context->source = source;

    // Initialize configuration from settings
    const char *ip = obs_data_get_string(settings, "ip_address");
    strncpy(context->ip_address, ip ? ip : C64U_DEFAULT_IP, sizeof(context->ip_address) - 1);
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
            C64U_LOG_WARNING("Failed to detect OBS IP address, using fallback");
            strncpy(context->obs_ip_address, "192.168.1.100", sizeof(context->obs_ip_address) - 1);
            context->initial_ip_detected = false;
            obs_data_set_string(settings, "obs_ip_address", context->obs_ip_address);
        }
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
        context->render_delay_frames = 10; // Default value
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

    // Initialize recording module
    c64u_record_init(context);

    // Apply recording settings from OBS
    c64u_record_update_settings(context, settings);

    // Logo will be loaded lazily on first render (when graphics context is available)
    context->logo_texture = NULL;
    context->logo_load_attempted = false;

    C64U_LOG_INFO("C64U source created - C64 IP: %s, OBS IP: %s, Video: %u, Audio: %u", context->ip_address,
                  context->obs_ip_address, context->video_port, context->audio_port);

    // Auto-start streaming after plugin initialization
    C64U_LOG_INFO("🚀 Auto-starting C64U streaming after plugin initialization...");
    c64u_start_streaming(context);
    context->auto_start_attempted = true;

    C64U_LOG_INFO("✅ c64u_create returning context: %p (logo_load_attempted: %d, logo_texture: %p)", (void *)context,
                  context->logo_load_attempted, (void *)context->logo_texture);
    return context;
}

void c64u_destroy(void *data)
{
    struct c64u_source *context = data;
    if (!context)
        return;

    C64U_LOG_INFO("Destroying C64U source");

    // Stop streaming if active
    if (context->streaming) {
        C64U_LOG_DEBUG("Stopping active streaming during destruction");
        context->streaming = false;
        context->thread_active = false;

        // Send stop commands
        send_control_command(context, false, 0); // Stop video
        send_control_command(context, false, 1); // Stop audio

        // Close sockets
        if (context->video_socket != INVALID_SOCKET_VALUE) {
            close(context->video_socket);
            context->video_socket = INVALID_SOCKET_VALUE;
        }
        if (context->audio_socket != INVALID_SOCKET_VALUE) {
            close(context->audio_socket);
            context->audio_socket = INVALID_SOCKET_VALUE;
        }

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

    // Cleanup recording module
    c64u_record_cleanup(context);

    // Cleanup logo texture
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
    const char *new_ip = obs_data_get_string(settings, "ip_address");
    const char *new_obs_ip = obs_data_get_string(settings, "obs_ip_address");
    uint32_t new_video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    uint32_t new_audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");

    // Set defaults
    if (!new_ip)
        new_ip = C64U_DEFAULT_IP;
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

    // Update configuration
    strncpy(context->ip_address, new_ip, sizeof(context->ip_address) - 1);
    context->ip_address[sizeof(context->ip_address) - 1] = '\0';
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

    // If already streaming, just send start commands again (no need to recreate sockets/threads)
    if (context->streaming) {
        C64U_LOG_INFO("Already streaming - sending start commands with current config");
        send_control_command(context, true, 0); // Start video
        send_control_command(context, true, 1); // Start audio
        return;
    }

    C64U_LOG_INFO("Starting C64U streaming to C64 %s (OBS IP: %s, video:%u, audio:%u)...", context->ip_address,
                  context->obs_ip_address, context->video_port, context->audio_port);

    // Create UDP sockets
    context->video_socket = create_udp_socket(context->video_port);
    context->audio_socket = create_udp_socket(context->audio_port);

    if (context->video_socket == INVALID_SOCKET_VALUE || context->audio_socket == INVALID_SOCKET_VALUE) {
        C64U_LOG_ERROR("Failed to create UDP sockets for streaming");
        if (context->video_socket != INVALID_SOCKET_VALUE) {
            close(context->video_socket);
            context->video_socket = INVALID_SOCKET_VALUE;
        }
        if (context->audio_socket != INVALID_SOCKET_VALUE) {
            close(context->audio_socket);
            context->audio_socket = INVALID_SOCKET_VALUE;
        }
        return;
    }

    // Send start commands to C64U
    send_control_command(context, true, 0); // Start video
    send_control_command(context, true, 1); // Start audio

    // Start worker threads
    context->thread_active = true;
    context->streaming = true;
    context->video_thread_active = false;
    context->audio_thread_active = false;

    if (pthread_create(&context->video_thread, NULL, video_thread_func, context) != 0) {
        C64U_LOG_ERROR("Failed to create video receiver thread");
        context->streaming = false;
        context->thread_active = false;
        close(context->video_socket);
        close(context->audio_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
        context->audio_socket = INVALID_SOCKET_VALUE;
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
        close(context->video_socket);
        close(context->audio_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
        context->audio_socket = INVALID_SOCKET_VALUE;
        return;
    }
    context->audio_thread_active = true;

    // Initialize delay queue for rendering delay
    init_delay_queue(context);

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

    // Send stop commands
    send_control_command(context, false, 0);
    send_control_command(context, false, 1);

    // Close sockets to wake up threads
    if (context->video_socket != INVALID_SOCKET_VALUE) {
        close(context->video_socket);
        context->video_socket = INVALID_SOCKET_VALUE;
    }
    if (context->audio_socket != INVALID_SOCKET_VALUE) {
        close(context->audio_socket);
        context->audio_socket = INVALID_SOCKET_VALUE;
    }

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
    clear_delay_queue(context);

    C64U_LOG_INFO("C64U streaming stopped");
}

void c64u_render(void *data, gs_effect_t *effect)
{
    struct c64u_source *context = data;
    UNUSED_PARAMETER(effect);

    static bool first_render = true;
    if (first_render) {
        C64U_LOG_INFO("🎨 First render called - context: %p", (void *)context);
        first_render = false;
    }

    if (!context) {
        C64U_LOG_ERROR("Render called with NULL context!");
        return;
    }

    // Lazy load logo texture on first render (when graphics context is available)
    if (!context->logo_load_attempted) {
        C64U_LOG_INFO("Logo load not yet attempted, loading now...");
        context->logo_texture = load_logo_texture();
        context->logo_load_attempted = true;
        if (context->logo_texture) {
            C64U_LOG_INFO("Logo texture loaded successfully");
        } else {
            C64U_LOG_WARNING("Logo texture is NULL after loading attempt");
        }
    }

    // Track render timing for diagnostic purposes
    static uint64_t last_render_time = 0;
    static uint32_t render_calls = 0;
    uint64_t render_start = os_gettime_ns();

    render_calls++;

    // Note: Auto-start streaming now happens in c64u_create, not on first render

    // Check if we have streaming data and frame ready
    static bool was_rendering_video = false;
    static uint64_t last_state_log = 0;
    bool should_render_video = (context->streaming && context->frame_ready && context->frame_buffer_front);

    // Log state changes
    if (should_render_video != was_rendering_video) {
        C64U_LOG_INFO("🎬 Render mode switch: %s (streaming:%d, frame_ready:%d)",
                      should_render_video ? "VIDEO" : "LOGO", context->streaming, context->frame_ready);
        was_rendering_video = should_render_video;
        last_state_log = os_gettime_ns();
    }

    // Periodically log state when in LOGO mode to help debug why logo might not show
    if (!should_render_video) {
        uint64_t now = os_gettime_ns();
        if (last_state_log == 0 || (now - last_state_log) >= 5000000000ULL) { // Every 5 seconds
            C64U_LOG_INFO("📺 Rendering LOGO mode (streaming:%d, frame_ready:%d, logo_texture:%p)", context->streaming,
                          context->frame_ready, (void *)context->logo_texture);
            last_state_log = now;
        }
    }

    if (should_render_video) {
        // Render actual C64U video frame from front buffer

        // Lock the frame buffer to ensure thread safety
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
    } else {
        // Log render starvation - OBS is requesting frames but we have none
        static uint64_t last_starvation_log = 0;
        static uint32_t starvation_count = 0;
        uint64_t now = os_gettime_ns();

        if (context->streaming && !context->frame_ready) {
            starvation_count++;
            if (last_starvation_log == 0 || (now - last_starvation_log) >= 1000000000ULL) { // Log every second
                C64U_LOG_WARNING(
                    "🍽️ RENDER STARVATION: OBS requesting frames but none ready (%u starved renders in last period)",
                    starvation_count);
                starvation_count = 0;
                last_starvation_log = now;
            }
        }

        // Show C64U logo centered on black background
        if (context->logo_texture) {
            static bool logged_logo_render = false;
            if (!logged_logo_render) {
                C64U_LOG_INFO("📺 Rendering logo (texture: %p, streaming: %d, frame_ready: %d)",
                              (void *)context->logo_texture, context->streaming, context->frame_ready);
                logged_logo_render = true;
            }

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
            static bool logged_effect = false;
            if (!logged_effect) {
                C64U_LOG_INFO("🖼️ Logo render: default_effect=%p", (void *)default_effect);
                logged_effect = true;
            }
            if (default_effect) {
                gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
                static bool logged_tech = false;
                if (!logged_tech) {
                    C64U_LOG_INFO("🖼️ Logo render: technique=%p", (void *)tech);
                    logged_tech = true;
                }
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

                    static bool logged_draw = false;
                    if (!logged_draw) {
                        C64U_LOG_INFO("🖼️ Drawing logo: %ux%u scaled to %.0fx%.0f at (%.1f,%.1f) on canvas %ux%u",
                                      logo_width, logo_height, scaled_width, scaled_height, x, y, context->width,
                                      context->height);
                        logged_draw = true;
                    }

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
                } else {
                    static bool logged_no_tech = false;
                    if (!logged_no_tech) {
                        C64U_LOG_WARNING("❌ Failed to get 'Draw' technique from default effect!");
                        logged_no_tech = true;
                    }
                }
            } else {
                static bool logged_no_effect = false;
                if (!logged_no_effect) {
                    C64U_LOG_WARNING("❌ Failed to get OBS_EFFECT_DEFAULT!");
                    logged_no_effect = true;
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
    }

    // Log render timing diagnostics every 5 seconds
    if (last_render_time > 0) {
        uint64_t render_end = os_gettime_ns();
        uint64_t render_duration = render_end - render_start;
        uint64_t render_interval = render_start - last_render_time;

        static uint64_t last_render_log = 0;
        static uint32_t total_render_calls = 0;
        static uint64_t total_render_time = 0;
        static uint64_t min_render_interval = UINT64_MAX;
        static uint64_t max_render_interval = 0;
        static uint32_t irregular_renders = 0;

        total_render_calls++;
        total_render_time += render_duration;

        // Track render call timing irregularities
        if (render_interval > 0) {
            uint64_t expected_render_interval = 16666667; // ~60Hz display refresh

            min_render_interval = (render_interval < min_render_interval) ? render_interval : min_render_interval;
            max_render_interval = (render_interval > max_render_interval) ? render_interval : max_render_interval;

            // Detect irregular render calls that could cause perceived choppiness
            if (render_interval < expected_render_interval * 0.5 || render_interval > expected_render_interval * 1.5) {
                irregular_renders++;
                if (irregular_renders % 10 == 1) { // Log every 10th irregular call to avoid spam
                    double interval_ms = render_interval / 1000000.0;
                    C64U_LOG_WARNING("🎭 RENDER IRREGULAR: %.2f ms interval (expected ~16.7ms for 60Hz display)",
                                     interval_ms);
                }
            }
        }

        if (last_render_log == 0)
            last_render_log = render_end;

        uint64_t log_diff = render_end - last_render_log;
        if (log_diff >= 5000000000ULL) { // Every 5 seconds
            double duration = log_diff / 1000000000.0;
            double render_fps = total_render_calls / duration;
            double avg_render_time_ms = total_render_time / (total_render_calls * 1000000.0);
            double min_interval_ms = min_render_interval / 1000000.0;
            double max_interval_ms = max_render_interval / 1000000.0;

            C64U_LOG_INFO("🎨 RENDER: %.1f fps | %.2f ms avg render time | %u total calls", render_fps,
                          avg_render_time_ms, render_calls);
            C64U_LOG_INFO("🎭 RENDER TIMING: Min interval %.2f ms | Max interval %.2f ms | Irregular calls: %u",
                          min_interval_ms, max_interval_ms, irregular_renders);

            // Reset counters
            total_render_calls = 0;
            total_render_time = 0;
            min_render_interval = UINT64_MAX;
            max_render_interval = 0;
            irregular_renders = 0;
            last_render_log = render_end;
        }
    }
    last_render_time = render_start;
}

uint32_t c64u_get_width(void *data)
{
    struct c64u_source *context = data;
    static bool first_call = true;
    if (first_call) {
        C64U_LOG_INFO("📏 get_width called: %u", context->width);
        first_call = false;
    }
    return context->width;
}

uint32_t c64u_get_height(void *data)
{
    struct c64u_source *context = data;
    static bool first_call = true;
    if (first_call) {
        C64U_LOG_INFO("📏 get_height called: %u", context->height);
        first_call = false;
    }
    return context->height;
}

const char *c64u_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("C64U");
}

obs_properties_t *c64u_properties(void *data)
{
    // C64U properties setup
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();

    // Debug logging toggle
    obs_property_t *debug_prop = obs_properties_add_bool(props, "debug_logging", "Debug Logging");
    obs_property_set_long_description(debug_prop, "Enable detailed logging for debugging connection issues");

    // Network Configuration Group
    obs_property_t *network_group = obs_properties_add_group(props, "network_group", "Network Configuration",
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *network_props = obs_property_group_content(network_group);

    // C64 IP Address
    obs_property_t *ip_prop = obs_properties_add_text(network_props, "ip_address", "C64 Ultimate IP", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(
        ip_prop, "IP address or DNS name of C64 Ultimate device (use 0.0.0.0 to skip control commands)");

    // OBS IP Address (editable)
    obs_property_t *obs_ip_prop =
        obs_properties_add_text(network_props, "obs_ip_address", "OBS Server IP", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(obs_ip_prop, "IP address of this OBS server (where C64 Ultimate sends streams)");

    // Auto-detect IP toggle
    obs_property_t *auto_ip_prop = obs_properties_add_bool(network_props, "auto_detect_ip", "Auto-detect OBS IP");
    obs_property_set_long_description(auto_ip_prop, "Automatically detect and use OBS server IP in streaming commands");

    // UDP Ports within the same network group
    obs_property_t *video_port_prop =
        obs_properties_add_int(network_props, "video_port", "Video Port (11000)", 1024, 65535, 1);
    obs_property_set_long_description(video_port_prop, "UDP port for video stream from C64 Ultimate");

    obs_property_t *audio_port_prop =
        obs_properties_add_int(network_props, "audio_port", "Audio Port (11001)", 1024, 65535, 1);
    obs_property_set_long_description(audio_port_prop, "UDP port for audio stream from C64 Ultimate");

    // Rendering Delay
    obs_property_t *delay_prop =
        obs_properties_add_int_slider(props, "render_delay_frames", "Render Delay (frames)", 0, 100, 1);
    obs_property_set_long_description(
        delay_prop, "Delay frames before rendering to smooth UDP packet loss/reordering (default: 10)");

    // Recording Group (compact layout)
    obs_property_t *recording_group =
        obs_properties_add_group(props, "recording_group", "Recording", OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *recording_props = obs_property_group_content(recording_group);

    obs_property_t *save_frames_prop = obs_properties_add_bool(recording_props, "save_frames", "☐ Save BMP Frames");
    obs_property_set_long_description(
        save_frames_prop, "Save each frame as BMP in frames/ subfolder (for debugging - impacts performance)");

    obs_property_t *record_video_prop = obs_properties_add_bool(recording_props, "record_video", "☐ Record AVI + WAV");
    obs_property_set_long_description(record_video_prop,
                                      "Record uncompressed AVI video + WAV audio (for debugging - high disk usage)");

    // Save Folder (applies to both frame saving and video recording) - now properly in Recording group
    obs_property_t *save_folder_prop =
        obs_properties_add_path(recording_props, "save_folder", "Output Folder", OBS_PATH_DIRECTORY, NULL, NULL);
    obs_property_set_long_description(
        save_folder_prop,
        "Directory where session folders with frames, video, audio, and timing files will be created");

    return props;
}

void c64u_defaults(obs_data_t *settings)
{
    // C64U defaults initialization

    obs_data_set_default_bool(settings, "debug_logging", true);
    obs_data_set_default_bool(settings, "auto_detect_ip", true);
    obs_data_set_default_string(settings, "ip_address", C64U_DEFAULT_IP);
    obs_data_set_default_string(settings, "obs_ip_address", ""); // Empty by default, will be auto-detected
    obs_data_set_default_int(settings, "video_port", C64U_DEFAULT_VIDEO_PORT);
    obs_data_set_default_int(settings, "audio_port", C64U_DEFAULT_AUDIO_PORT);
    obs_data_set_default_int(settings, "render_delay_frames", 10); // Default 10 frames delay

    // Frame saving defaults
    obs_data_set_default_bool(settings, "save_frames", false); // Disabled by default

    // Platform-specific default recording folder
#ifdef _WIN32
    const char *default_folder = "%USERPROFILE%\\Documents\\obs-studio\\c64u\\recordings";
#elif defined(__APPLE__)
    const char *default_folder = "~/Documents/obs-studio/c64u/recordings";
#else // Linux and other Unix-like systems
    const char *default_folder = "~/Documents/obs-studio/c64u/recordings";
#endif
    obs_data_set_default_string(settings, "save_folder", default_folder);

    // Video recording defaults
    obs_data_set_default_bool(settings, "record_video", false); // Disabled by default
}
