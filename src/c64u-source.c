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
#include "c64u-network.h"
#include "c64u-video.h"
#include "c64u-audio.h"
#include "c64u-timing.h"
#include "plugin-support.h"

// Function to detect OBS target frame rate
static double c64u_detect_obs_fps(void)
{
    // Try to get OBS video info to detect target frame rate
    struct obs_video_info ovi;
    if (obs_get_video_info(&ovi)) {
        double fps = (double)ovi.fps_num / (double)ovi.fps_den;
        C64U_LOG_INFO("📺 Detected OBS frame rate: %.3f Hz (%u/%u)", fps, ovi.fps_num, ovi.fps_den);

        // Round to nearest standard frame rate
        if (fps >= 59.0 && fps <= 61.0) {
            return 60.0;
        } else if (fps >= 49.0 && fps <= 51.0) {
            return 50.0;
        } else if (fps >= 29.0 && fps <= 31.0) {
            return 30.0;
        } else if (fps >= 24.0 && fps <= 26.0) {
            return 25.0;
        }

        // Return detected rate if not a standard one
        C64U_LOG_WARNING("⚠️  Non-standard OBS frame rate detected: %.3f Hz", fps);
        return fps;
    }

    C64U_LOG_WARNING("⚠️  Could not detect OBS frame rate, defaulting to 50Hz");
    return 50.0; // Default fallback
}

// Function to initialize timing system based on detected formats
static void c64u_init_timing_system(struct c64u_source *context)
{
    if (context->timing_initialized || !context->format_detected) {
        return;
    }

    // Get timing strategy from settings
    obs_data_t *settings = obs_source_get_settings(context->source);
    int strategy_int = (int)obs_data_get_int(settings, "timing_strategy");
    c64u_timing_strategy_t strategy = (c64u_timing_strategy_t)strategy_int;
    obs_data_release(settings);

    // Detect OBS target frame rate
    double obs_fps = c64u_detect_obs_fps();
    context->obs_target_fps_x1000 = (uint64_t)(obs_fps * 1000.0);

    // Use detected C64 frame rate
    double c64_fps = context->expected_fps;

    // Allocate and initialize timing system
    context->timing = bzalloc(sizeof(struct c64u_timing_state));
    if (context->timing) {
        uint32_t frame_size = context->width * context->height * 4; // RGBA
        c64u_timing_init(context->timing, strategy, obs_fps, c64_fps, frame_size);
        context->timing_initialized = true;

        C64U_LOG_INFO("🎯 Frame timing system initialized: C64 %.3f Hz -> OBS %.1f Hz", c64_fps, obs_fps);

        // Log potential timing issues
        double rate_diff = fabs(c64_fps - obs_fps) / obs_fps;
        if (rate_diff > 0.01) { // More than 1% difference
            C64U_LOG_WARNING("⚠️  Significant frame rate mismatch detected (%.1f%% difference)", rate_diff * 100.0);
            C64U_LOG_INFO("   This may cause frame drops/duplicates without proper timing strategy");
        }
    } else {
        C64U_LOG_ERROR("❌ Failed to allocate memory for timing system");
    }
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

    // Initialize timing system
    context->timing = NULL;
    context->timing_initialized = false;
    context->obs_target_fps_x1000 = 50000; // Default to 50.000 Hz, will be detected

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

    // Initialize sockets to invalid
    context->video_socket = INVALID_SOCKET_VALUE;
    context->audio_socket = INVALID_SOCKET_VALUE;
    context->control_socket = INVALID_SOCKET_VALUE;
    context->thread_active = false;
    context->video_thread_active = false;
    context->audio_thread_active = false;
    context->auto_start_attempted = false;

    C64U_LOG_INFO("C64U source created - C64 IP: %s, OBS IP: %s, Video: %u, Audio: %u", context->ip_address,
                  context->obs_ip_address, context->video_port, context->audio_port);

    // Auto-start streaming after plugin initialization
    C64U_LOG_INFO("🚀 Auto-starting C64U streaming after plugin initialization...");
    c64u_start_streaming(context);
    context->auto_start_attempted = true;

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

    // Cleanup resources
    // Clean up timing system
    if (context->timing) {
        c64u_timing_destroy(context->timing);
        bfree(context->timing);
        context->timing = NULL;
    }

    pthread_mutex_destroy(&context->frame_mutex);
    pthread_mutex_destroy(&context->assembly_mutex);
    if (context->frame_buffer_front) {
        bfree(context->frame_buffer_front);
    }
    if (context->frame_buffer_back) {
        bfree(context->frame_buffer_back);
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

    C64U_LOG_INFO("C64U streaming stopped");
}

void c64u_render(void *data, gs_effect_t *effect)
{
    struct c64u_source *context = data;
    UNUSED_PARAMETER(effect);

    // Track render timing for diagnostic purposes
    static uint64_t last_render_time = 0;
    static uint32_t render_calls = 0;
    uint64_t render_start = os_gettime_ns();

    render_calls++;

    // Note: Auto-start streaming now happens in c64u_create, not on first render

    // Initialize timing system if format is detected but timing not yet initialized
    if (context->format_detected && !context->timing_initialized) {
        c64u_init_timing_system(context);
    }

    // Check if we have streaming data and should deliver frame
    if (context->streaming && context->frame_ready) {
        uint32_t *frame_to_render = NULL;

        // Use timing system to determine which frame to render
        if (context->timing && context->timing_initialized && context->timing->strategy != C64U_TIMING_PASSTHROUGH) {
            // Check if we should deliver a frame now
            if (c64u_timing_should_deliver_frame(context->timing, render_start)) {
                frame_to_render = c64u_timing_get_frame_for_obs(context->timing, context, render_start);

                if (frame_to_render) {
                    c64u_timing_on_obs_frame_delivered(context->timing, render_start);
                }
            }

            // Safety fallback: if timing system doesn't provide a frame, use direct rendering
            if (!frame_to_render) {
                frame_to_render = context->frame_buffer_front;
            }
        } else {
            // Direct rendering (original behavior) - always works
            frame_to_render = context->frame_buffer_front;
        }

        // Render frame if we have one to render
        if (frame_to_render) {
            // Lock the frame buffer to ensure thread safety
            if (pthread_mutex_lock(&context->frame_mutex) == 0) {
                // Create texture from frame data
                gs_texture_t *texture = gs_texture_create(context->width, context->height, GS_RGBA, 1,
                                                          (const uint8_t **)&frame_to_render, 0);
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
    } else {
        // Show colored background to indicate plugin state
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

    // Log render timing diagnostics every 5 seconds
    if (last_render_time > 0) {
        uint64_t render_end = os_gettime_ns();
        uint64_t render_duration = render_end - render_start;
        static uint64_t last_render_log = 0;
        static uint32_t total_render_calls = 0;
        static uint64_t total_render_time = 0;

        total_render_calls++;
        total_render_time += render_duration;

        if (last_render_log == 0)
            last_render_log = render_end;

        uint64_t log_diff = render_end - last_render_log;
        if (log_diff >= 5000000000ULL) { // Every 5 seconds
            double duration = log_diff / 1000000000.0;
            double render_fps = total_render_calls / duration;
            double avg_render_time_ms = total_render_time / (total_render_calls * 1000000.0);

            C64U_LOG_INFO("🎨 RENDER: %.1f fps | %.2f ms avg render time | %u total calls", render_fps,
                          avg_render_time_ms, render_calls);

            // Reset counters
            total_render_calls = 0;
            total_render_time = 0;
            last_render_log = render_end;
        }
    }
    last_render_time = render_start;
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
    return "C64U Display";
}

obs_properties_t *c64u_properties(void *data)
{
    // C64U properties setup
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();

    // Debug logging toggle
    obs_property_t *debug_prop = obs_properties_add_bool(props, "debug_logging", "Enable Debug Logging");
    obs_property_set_long_description(debug_prop, "Enable detailed logging for debugging C64U connection issues");

    // C64 IP Address
    obs_property_t *ip_prop = obs_properties_add_text(props, "ip_address", "C64 IP Address", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(
        ip_prop, "IP address or DNS name of the C64 Ultimate device. Leave as 0.0.0.0 to skip control commands.");

    // OBS IP Address (editable)
    obs_property_t *obs_ip_prop = obs_properties_add_text(props, "obs_ip_address", "OBS IP Address", OBS_TEXT_DEFAULT);
    obs_property_set_long_description(
        obs_ip_prop, "IP address of this OBS machine. C64 Ultimate will stream video/audio to this address.");

    // Auto-detect IP toggle
    obs_property_t *auto_ip_prop = obs_properties_add_bool(props, "auto_detect_ip", "Use Auto-detected OBS IP");
    obs_property_set_long_description(auto_ip_prop,
                                      "Use the automatically detected OBS IP address in streaming commands");

    // Video Port
    obs_property_t *video_port_prop = obs_properties_add_int(props, "video_port", "Video Port", 1024, 65535, 1);
    obs_property_set_long_description(video_port_prop, "UDP port for video stream (default: 11000)");

    // Audio Port
    obs_property_t *audio_port_prop = obs_properties_add_int(props, "audio_port", "Audio Port", 1024, 65535, 1);
    obs_property_set_long_description(audio_port_prop, "UDP port for audio stream (default: 11001)");

    // Frame Timing Strategy
    obs_property_t *timing_prop = obs_properties_add_list(props, "timing_strategy", "Frame Timing Strategy",
                                                          OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(timing_prop, "Pass-through (original behavior)", 0);
    obs_property_list_add_int(timing_prop, "Adaptive (recommended)", 1);
    obs_property_list_add_int(timing_prop, "Interpolation (experimental)", 2);
    obs_property_list_add_int(timing_prop, "VSync aware", 3);
    obs_property_set_long_description(timing_prop,
                                      "Frame timing strategy to handle C64 50.125Hz vs OBS 50Hz mismatch:\n"
                                      "• Pass-through: Direct frames (may cause stuttering)\n"
                                      "• Adaptive: Smart frame dropping/duplication (recommended)\n"
                                      "• Interpolation: Frame blending (experimental)\n"
                                      "• VSync aware: Align with OBS rendering");

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
    obs_data_set_default_int(settings, "timing_strategy", 0); // Default to passthrough (original behavior)
}
