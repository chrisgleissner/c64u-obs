#include <obs-module.h>
#include <util/platform.h>
#include "c64u-network.h" // Include network header first to avoid Windows header conflicts

#include "c64u-logging.h"
#include "c64u-audio.h"
#include "c64u-types.h"
#include "c64u-protocol.h"
#include "c64u-video.h"  // For batch statistics processing
#include "c64u-record.h" // For recording functions

// Audio thread function
void *audio_thread_func(void *data)
{
    struct c64u_source *context = data;
    uint8_t packet[C64U_AUDIO_PACKET_SIZE];

    C64U_LOG_DEBUG("Audio receiver thread started on port %u", context->audio_port);

    while (context->thread_active) {
        ssize_t received = recv(context->audio_socket, (char *)packet, (int)sizeof(packet), 0);

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
            C64U_LOG_ERROR("Audio socket error: %s", c64u_get_socket_error_string(error));
            break;
        }

        if (received != C64U_AUDIO_PACKET_SIZE) {
            C64U_LOG_WARNING("Received incomplete audio packet: " SSIZE_T_FORMAT " bytes (expected %d)",
                             SSIZE_T_CAST(received), C64U_AUDIO_PACKET_SIZE);
            continue;
        }

        // Update timestamp for timeout detection - UDP packet received successfully
        context->last_udp_packet_time = os_gettime_ns();

        // Parse audio packet
        uint16_t seq_num = *(uint16_t *)(packet);
        int16_t *audio_data = (int16_t *)(packet + C64U_AUDIO_HEADER_SIZE);

        // Update audio statistics
        context->audio_packets_received++;
        context->audio_bytes_received += received;

        // Streamlined audio sequence tracking (keep for important audio sync issues)
        static uint16_t last_audio_seq = 0;
        static bool first_audio = true;

        if (!first_audio && seq_num != (uint16_t)(last_audio_seq + 1)) {
            C64U_LOG_WARNING("🔊 AUDIO OUT-OF-SEQUENCE: Expected %u, got %u", (uint16_t)(last_audio_seq + 1), seq_num);
        }
        last_audio_seq = seq_num;
        first_audio = false;

        // Batch process audio statistics (moved out of hot path)
        uint64_t audio_now = os_gettime_ns();
        c64u_process_audio_statistics_batch(context, audio_now);

        // Send audio to OBS (192 stereo samples = 384 16-bit values)
        struct obs_source_audio audio_frame = {0};
        audio_frame.data[0] = (uint8_t *)audio_data;
        audio_frame.frames = 192;
        audio_frame.speakers = SPEAKERS_STEREO;
        audio_frame.format = AUDIO_FORMAT_16BIT;
        audio_frame.samples_per_sec = 48000; // Will be adjusted for PAL/NTSC
        audio_frame.timestamp = os_gettime_ns();

        // Record audio data if recording is enabled
        if (context->record_video) {
            c64u_record_audio_data(context, (const uint8_t *)audio_data,
                                   192 * 2 * 2); // 192 stereo samples * 2 bytes per sample
        }

        obs_source_output_audio(context->source, &audio_frame);
    }

    C64U_LOG_DEBUG("Audio thread stopped for C64U source '%s'", obs_source_get_name(context->source));
    return NULL;
}
