
#include <obs-module.h>
#include <util/platform.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include "c64u-logging.h"
#include "c64u-record.h"
#include "c64u-types.h"

#ifndef S_ISDIR
#ifdef _WIN32
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#endif

// Helper function to create directories recursively (cross-platform)
static bool create_directory_recursive(const char *path)
{
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\')
        tmp[len - 1] = 0;

    // Start from the beginning, but skip drive letters on Windows (e.g., "C:")
    p = tmp;
    if (len > 1 && tmp[1] == ':') {
        p = tmp + 2; // Skip "C:" part on Windows
    }
    if (*p == '/' || *p == '\\') {
        p++; // Skip leading slash
    }

    // Create each directory in the path
    for (; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            if (os_mkdir(tmp) != 0) {
                // Check if it already exists (ignore error if it does)
                struct stat st;
                if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    // Directory creation failed and it doesn't exist
                    return false;
                }
            }
            *p = '/'; // Use forward slash consistently (works on Windows too)
        }
    }

    // Create the final directory
    if (os_mkdir(tmp) != 0) {
        struct stat st;
        if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }

    return true;
}

// Helper function to write minimal, standard-compliant AVI header
static void write_avi_header(FILE *file, uint32_t width, uint32_t height, double fps)
{
    uint32_t frame_size = width * height * 3; // BGR24 bytes per frame
    uint32_t zero = 0;

    // Calculate precise frame period in microseconds
    uint32_t frame_period = (uint32_t)(1000000.0 / fps + 0.5); // Round to nearest microsecond

    // RIFF header (will update file size later)
    fwrite("RIFF", 1, 4, file);
    uint32_t file_size_placeholder = 0;
    fwrite(&file_size_placeholder, 4, 1, file);
    fwrite("AVI ", 1, 4, file);

    // LIST hdrl chunk
    fwrite("LIST", 1, 4, file);
    uint32_t hdrl_size = 4 + 56 + (4 + 48 + 4 + 40); // hdrl + avih + video_strl (NO AUDIO)
    fwrite(&hdrl_size, 4, 1, file);
    fwrite("hdrl", 1, 4, file);

    // Main AVI header (avih)
    fwrite("avih", 1, 4, file);
    uint32_t avih_size = 56;
    fwrite(&avih_size, 4, 1, file);

    uint32_t max_bytes_per_sec = (uint32_t)(frame_size * fps);

    fwrite(&frame_period, 4, 1, file);      // dwMicroSecPerFrame
    fwrite(&max_bytes_per_sec, 4, 1, file); // dwMaxBytesPerSec
    fwrite(&zero, 4, 1, file);              // dwPaddingGranularity
    fwrite(&zero, 4, 1, file);              // dwFlags
    fwrite(&zero, 4, 1, file);              // dwTotalFrames (update later)
    fwrite(&zero, 4, 1, file);              // dwInitialFrames
    uint32_t one = 1;
    fwrite(&one, 4, 1, file);        // dwStreams (video only)
    fwrite(&frame_size, 4, 1, file); // dwSuggestedBufferSize
    fwrite(&width, 4, 1, file);      // dwWidth
    fwrite(&height, 4, 1, file);     // dwHeight
    fwrite(&zero, 4, 1, file);       // dwReserved[0]
    fwrite(&zero, 4, 1, file);       // dwReserved[1]
    fwrite(&zero, 4, 1, file);       // dwReserved[2]
    fwrite(&zero, 4, 1, file);       // dwReserved[3]

    // LIST strl chunk (stream list)
    fwrite("LIST", 1, 4, file);
    uint32_t strl_size = 4 + 48 + 4 + 40; // strl + strh + strf + BITMAPINFOHEADER
    fwrite(&strl_size, 4, 1, file);
    fwrite("strl", 1, 4, file);

    // Stream header (strh)
    fwrite("strh", 1, 4, file);
    uint32_t strh_size = 48;
    fwrite(&strh_size, 4, 1, file);
    fwrite("vids", 1, 4, file);     // fccType (video)
    fwrite("\0\0\0\0", 1, 4, file); // fccHandler (uncompressed)
    fwrite(&zero, 4, 1, file);      // dwFlags
    uint16_t priority = 0;
    fwrite(&priority, 2, 1, file); // wPriority
    uint16_t language = 0;
    fwrite(&language, 2, 1, file); // wLanguage
    fwrite(&zero, 4, 1, file);     // dwInitialFrames
    // Use detected refresh rate for scale/rate
    uint32_t scale = 1000000;
    fwrite(&scale, 4, 1, file);                        // dwScale
    uint32_t rate = (uint32_t)(fps * 1000000.0 + 0.5); // Rate in scale units, rounded
    fwrite(&rate, 4, 1, file);                         // dwRate
    fwrite(&zero, 4, 1, file);                         // dwStart
    fwrite(&zero, 4, 1, file);                         // dwLength (update later)
    fwrite(&frame_size, 4, 1, file);                   // dwSuggestedBufferSize
    uint32_t quality = 0xFFFFFFFF;
    fwrite(&quality, 4, 1, file); // dwQuality (-1 = default)
    fwrite(&zero, 4, 1, file);    // dwSampleSize (0 for video)

    // Stream format (strf) - BITMAPINFOHEADER
    fwrite("strf", 1, 4, file);
    uint32_t strf_size = 40; // BITMAPINFOHEADER size
    fwrite(&strf_size, 4, 1, file);

    // BITMAPINFOHEADER
    fwrite(&strf_size, 4, 1, file); // biSize
    fwrite(&width, 4, 1, file);     // biWidth
    int32_t negative_height = -(int32_t)height;
    fwrite(&negative_height, 4, 1, file); // biHeight (negative = top-down)
    uint16_t planes = 1;
    fwrite(&planes, 2, 1, file); // biPlanes
    uint16_t bit_count = 24;
    fwrite(&bit_count, 2, 1, file); // biBitCount
    fwrite(&zero, 4, 1, file);      // biCompression (0 = uncompressed)
    uint32_t image_size = frame_size;
    fwrite(&image_size, 4, 1, file); // biSizeImage
    fwrite(&zero, 4, 1, file);       // biXPelsPerMeter
    fwrite(&zero, 4, 1, file);       // biYPelsPerMeter
    fwrite(&zero, 4, 1, file);       // biClrUsed
    fwrite(&zero, 4, 1, file);       // biClrImportant

    // LIST movi chunk (where frame data goes - VIDEO ONLY)
    fwrite("LIST", 1, 4, file);
    fwrite(&zero, 4, 1, file); // movi size (update later)
    fwrite("movi", 1, 4, file);
}

static void update_avi_header(FILE *file, uint32_t frame_count, uint32_t audio_samples_written)
{
    UNUSED_PARAMETER(audio_samples_written);

    if (!file)
        return;

    long current_pos = ftell(file);
    uint32_t file_size = current_pos - 8; // Total file size minus RIFF header

    // Update RIFF file size
    fseek(file, 4, SEEK_SET);
    fwrite(&file_size, 4, 1, file);

    // Update total frames in main AVI header (avih)
    // RIFF(4) + size(4) + AVI(4) + LIST(4) + size(4) + hdrl(4) + avih(4) + size(4) + period(4) + maxbytes(4) + pad(4) + flags(4) = 48
    fseek(file, 48, SEEK_SET);
    fwrite(&frame_count, 4, 1, file);

    // Seek back to end
    fseek(file, current_pos, SEEK_SET);
    fflush(file); // Ensure changes are written to disk
} // Helper function to convert RGBA to BGR24
static void convert_rgba_to_bgr24(uint32_t *rgba_buffer, uint8_t *bgr_buffer, uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < width * height; i++) {
        uint32_t pixel = rgba_buffer[i];
        // RGBA is typically: 0xAABBGGRR (little endian)
        // We need BGR24: B, G, R
        uint8_t r = pixel & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = (pixel >> 16) & 0xFF;

        bgr_buffer[i * 3 + 0] = b; // Blue
        bgr_buffer[i * 3 + 1] = g; // Green
        bgr_buffer[i * 3 + 2] = r; // Red
    }
}

// Helper function to write WAV file header
static void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample)
{
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;

    // WAV header (44 bytes) - we'll update sizes later
    fwrite("RIFF", 1, 4, file); // ChunkID
    uint32_t chunk_size = 36;   // ChunkSize (to be updated later)
    fwrite(&chunk_size, 4, 1, file);
    fwrite("WAVE", 1, 4, file);   // Format
    fwrite("fmt ", 1, 4, file);   // Subchunk1ID
    uint32_t subchunk1_size = 16; // Subchunk1Size (PCM)
    fwrite(&subchunk1_size, 4, 1, file);
    uint16_t audio_format = 1; // AudioFormat (PCM)
    fwrite(&audio_format, 2, 1, file);
    fwrite(&channels, 2, 1, file);        // NumChannels
    fwrite(&sample_rate, 4, 1, file);     // SampleRate
    fwrite(&byte_rate, 4, 1, file);       // ByteRate
    fwrite(&block_align, 2, 1, file);     // BlockAlign
    fwrite(&bits_per_sample, 2, 1, file); // BitsPerSample
    fwrite("data", 1, 4, file);           // Subchunk2ID
    uint32_t subchunk2_size = 0;          // Subchunk2Size (to be updated later)
    fwrite(&subchunk2_size, 4, 1, file);
}

// Helper function to finalize WAV header with correct file sizes
static void finalize_wav_header(FILE *file, uint32_t data_size)
{
    if (!file)
        return;

    // Update ChunkSize (file size - 8)
    fseek(file, 4, SEEK_SET);
    uint32_t chunk_size = data_size + 36;
    fwrite(&chunk_size, 4, 1, file);

    // Update Subchunk2Size (data size)
    fseek(file, 40, SEEK_SET);
    fwrite(&data_size, 4, 1, file);
}

// Fast BMP file saving function (minimal CPU overhead)
// Session management: Create session folder if none exists
static void ensure_recording_session(struct c64u_source *context)
{
    // If session already exists, do nothing
    if (context->session_folder[0] != '\0') {
        return;
    }

    // Create new session folder with timestamp
    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    time_t rawtime = timestamp_ms / 1000;
    struct tm *timeinfo = localtime(&rawtime);

    snprintf(context->session_folder, sizeof(context->session_folder), "%s/session_%04d%02d%02d_%02d%02d%02d",
             context->save_folder, timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour,
             timeinfo->tm_min, timeinfo->tm_sec);

    // Create the session directory recursively (cross-platform)
    C64U_LOG_INFO("Attempting to create session directory: %s", context->session_folder);
    if (!create_directory_recursive(context->session_folder)) {
        C64U_LOG_WARNING("Failed to create session directory: %s", context->session_folder);
        context->session_folder[0] = '\0'; // Clear on failure
    } else {
        C64U_LOG_INFO("Successfully created recording session: %s", context->session_folder);
    }
}

// Check if any recording is active
static bool any_recording_active(struct c64u_source *context)
{
    return context->save_frames || context->record_video;
}

// Clear session if no recording is active
static void cleanup_session_if_needed(struct c64u_source *context)
{
    if (!any_recording_active(context)) {
        context->session_folder[0] = '\0';
        C64U_LOG_INFO("Recording session ended");
    }
}

void c64u_save_frame_as_bmp(struct c64u_source *context, uint32_t *frame_buffer)
{
    if (!context->save_frames || !frame_buffer) {
        return;
    }

    // Ensure we have a recording session
    ensure_recording_session(context);
    if (context->session_folder[0] == '\0') {
        C64U_LOG_WARNING("Failed to create recording session for frame saving");
        return;
    }

    // Create frames subfolder within session folder
    char frames_folder[900];
    snprintf(frames_folder, sizeof(frames_folder), "%s/frames", context->session_folder);

    if (!create_directory_recursive(frames_folder)) {
        C64U_LOG_WARNING("Failed to create frames subfolder: %s", frames_folder);
        return;
    }

    // Create timestamped filename in frames subfolder
    uint64_t timestamp_ms = os_gettime_ns() / 1000000;
    char filename[900];
    snprintf(filename, sizeof(filename), "%s/frames/frame_%llu_%05u.bmp", context->session_folder,
             (unsigned long long)timestamp_ms, context->saved_frame_count++);

    FILE *file = fopen(filename, "wb");
    if (!file) {
        C64U_LOG_WARNING("Failed to create frame file: %s", filename);
        return;
    }

    uint32_t width = context->width;
    uint32_t height = context->height;
    uint32_t row_padded = (width * 3 + 3) & (~3); // 4-byte alignment for BMP
    uint32_t image_size = row_padded * height;
    uint32_t file_size = 54 + image_size; // BMP header + image data

    // BMP Header (54 bytes total)
    uint8_t header[54] = {
        'B',
        'M', // Signature
        file_size & 0xFF,
        (file_size >> 8) & 0xFF,
        (file_size >> 16) & 0xFF,
        (file_size >> 24) & 0xFF, // File size
        0,
        0,
        0,
        0, // Reserved
        54,
        0,
        0,
        0, // Data offset
        40,
        0,
        0,
        0, // Header size
        width & 0xFF,
        (width >> 8) & 0xFF,
        (width >> 16) & 0xFF,
        (width >> 24) & 0xFF, // Width
        height & 0xFF,
        (height >> 8) & 0xFF,
        (height >> 16) & 0xFF,
        (height >> 24) & 0xFF, // Height
        1,
        0, // Planes
        24,
        0, // Bits per pixel
        0,
        0,
        0,
        0, // Compression
        image_size & 0xFF,
        (image_size >> 8) & 0xFF,
        (image_size >> 16) & 0xFF,
        (image_size >> 24) & 0xFF, // Image size
        0,
        0,
        0,
        0, // X pixels per meter
        0,
        0,
        0,
        0, // Y pixels per meter
        0,
        0,
        0,
        0, // Colors used
        0,
        0,
        0,
        0 // Colors important
    };

    fwrite(header, 1, 54, file);

    // Write image data (BMP stores bottom-to-top, convert RGBA to BGR)
    uint8_t *row_buffer = malloc(row_padded);
    if (row_buffer) {
        for (int y = height - 1; y >= 0; y--) { // Bottom-to-top
            uint32_t *src_row = frame_buffer + (y * width);
            uint8_t *dst = row_buffer;

            for (uint32_t x = 0; x < width; x++) {
                uint32_t pixel = src_row[x];
                *dst++ = (pixel >> 16) & 0xFF; // B
                *dst++ = (pixel >> 8) & 0xFF;  // G
                *dst++ = pixel & 0xFF;         // R
            }

            // Pad to 4-byte boundary
            while ((dst - row_buffer) % 4 != 0) {
                *dst++ = 0;
            }

            fwrite(row_buffer, 1, row_padded, file);
        }
        free(row_buffer);
    }

    fclose(file);
}

// Video recording functions (raw uncompressed format for minimal CPU overhead)
void c64u_start_video_recording(struct c64u_source *context)
{
    if (!context->record_video || context->video_file) {
        return; // Already recording or not enabled
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Ensure we have a recording session (creates if needed, joins if exists)
    ensure_recording_session(context);
    if (context->session_folder[0] == '\0') {
        C64U_LOG_ERROR("Failed to create recording session for video recording");
        pthread_mutex_unlock(&context->recording_mutex);
        return;
    }

    // Create filenames in the session folder
    char video_filename[950], audio_filename[950], timing_filename[950];
    snprintf(video_filename, sizeof(video_filename), "%s/video.avi", context->session_folder);
    snprintf(audio_filename, sizeof(audio_filename), "%s/audio.wav", context->session_folder);
    snprintf(timing_filename, sizeof(timing_filename), "%s/timing.txt", context->session_folder);

    // Open files for recording
    context->video_file = fopen(video_filename, "wb");
    context->audio_file = fopen(audio_filename, "wb");
    context->timing_file = fopen(timing_filename, "w");

    if (!context->video_file || !context->audio_file || !context->timing_file) {
        C64U_LOG_ERROR("Failed to create recording files");
        if (context->video_file) {
            fclose(context->video_file);
            context->video_file = NULL;
        }
        if (context->audio_file) {
            fclose(context->audio_file);
            context->audio_file = NULL;
        }
        if (context->timing_file) {
            fclose(context->timing_file);
            context->timing_file = NULL;
        }
    } else {
        uint64_t timestamp_ms = os_gettime_ns() / 1000000;
        context->recording_start_time = timestamp_ms;
        context->recorded_frames = 0;
        context->recorded_audio_samples = 0;

        // Write AVI header with detected frame rate
        write_avi_header(context->video_file, context->width, context->height, context->expected_fps);

        // Write WAV header to audio file
        write_wav_header(context->audio_file, 48000, 2, 16); // 48kHz stereo 16-bit

        // Write header info to timing file
        fprintf(context->timing_file, "# C64U Video Recording Session\n");
        fprintf(context->timing_file, "# Session Folder: %s\n", context->session_folder);
        fprintf(context->timing_file, "# Start Time: %llu ms\n", (unsigned long long)timestamp_ms);
        fprintf(context->timing_file, "# Video Format: AVI (Uncompressed BGR24), %ux%u pixels @ 50fps\n",
                context->width, context->height);
        fprintf(context->timing_file, "# Audio Format: WAV PCM 48kHz 16-bit stereo\n");
        fprintf(context->timing_file, "# Columns: frame_number, timestamp_ms, frame_size_bytes\n");
        fflush(context->timing_file);

        C64U_LOG_INFO("Started video recording: %s", video_filename);
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void c64u_record_video_frame(struct c64u_source *context, uint32_t *frame_buffer)
{
    if (!context->record_video || !context->video_file || !frame_buffer) {
        return;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Calculate consistent frame timestamp based on detected FPS
    // Each frame gets the exact timestamp it should have for perfectly regular timing
    double frame_interval_ms = 1000.0 / context->expected_fps;
    uint64_t calculated_timestamp_ms =
        context->recording_start_time + (uint64_t)(context->recorded_frames * frame_interval_ms);

    // Write AVI frame chunk with proper header
    size_t frame_size = context->width * context->height * 3; // BGR24
    uint8_t *bgr_buffer = malloc(frame_size);
    if (bgr_buffer) {
        // Check if frame_buffer has non-zero data
        uint32_t non_zero_pixels = 0;
        for (uint32_t i = 0; i < context->width * context->height && i < 100; i++) {
            if (frame_buffer[i] != 0)
                non_zero_pixels++;
        }

        // Log actual C64 video data stats (debug level)
        C64U_LOG_DEBUG("Recording frame %u: %ux%u, non_zero=%u/100, fps=%.3f", context->recorded_frames, context->width,
                       context->height, non_zero_pixels, context->expected_fps);

        convert_rgba_to_bgr24(frame_buffer, bgr_buffer, context->width, context->height);

        // For 60Hz sessions, dump first 16 bytes of BGR frame data for debugging
        if ((int)(context->expected_fps + 0.5) == 60) {
            char hexbuf[49]; // 16 bytes * 3 chars + null
            for (int i = 0; i < 16; i++) {
                sprintf(hexbuf + i * 3, "%02X ", bgr_buffer[i]);
            }
            hexbuf[48] = '\0';
            C64U_LOG_DEBUG("Frame %u BGR[0..15]: %s", context->recorded_frames, hexbuf);
        }

        // Write AVI frame chunk header ("00db" = stream 0, uncompressed DIB)
        fwrite("00db", 1, 4, context->video_file);
        uint32_t chunk_size = (uint32_t)frame_size;
        fwrite(&chunk_size, 4, 1, context->video_file);

        // Write frame data
        size_t written = fwrite(bgr_buffer, 1, frame_size, context->video_file);

        // Ensure word alignment (AVI requirement)
        if (frame_size % 2) {
            uint8_t pad = 0;
            fwrite(&pad, 1, 1, context->video_file);
        }

        free(bgr_buffer);

        if (written == frame_size) {
            context->recorded_frames++;

            // Update AVI header with current frame count (video-only)
            update_avi_header(context->video_file, context->recorded_frames, 0);

            // Log timing information with both actual and calculated timestamps
            if (context->timing_file) {
                uint64_t actual_timestamp_ms = os_gettime_ns() / 1000000;
                fprintf(context->timing_file, "%u,%llu,%llu,%zu,%.3f\n", context->recorded_frames,
                        (unsigned long long)calculated_timestamp_ms, (unsigned long long)actual_timestamp_ms,
                        frame_size, context->expected_fps);
                fflush(context->timing_file);
            }
        } else {
            C64U_LOG_WARNING("Failed to write video frame to recording");
        }
    } else {
        C64U_LOG_ERROR("Failed to allocate BGR conversion buffer");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void c64u_record_audio_data(struct c64u_source *context, const uint8_t *audio_data, size_t data_size)
{
    if (!context->record_video || !context->audio_file || !audio_data) {
        return;
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Write audio data to separate WAV file (preserve original behavior)
    size_t wav_written = fwrite(audio_data, 1, data_size, context->audio_file);

    // Note: Audio is only written to separate WAV file, not to AVI (video-only AVI)

    if (wav_written == data_size) {
        context->recorded_audio_samples += (uint32_t)(data_size / 4); // 16-bit stereo = 4 bytes per sample
        // Note: No AVI header update needed since audio is not in AVI file
    } else {
        C64U_LOG_WARNING("Failed to write audio data to WAV recording");
    }

    pthread_mutex_unlock(&context->recording_mutex);
}

void c64u_stop_video_recording(struct c64u_source *context)
{
    if (!context->video_file) {
        return; // Not recording
    }

    if (pthread_mutex_lock(&context->recording_mutex) != 0) {
        return;
    }

    // Close recording files and finalize formats
    if (context->video_file) {
        // Update AVI header with final frame count
        // Final header update is not needed since we update continuously
        fclose(context->video_file);
        context->video_file = NULL;
    }
    if (context->audio_file) {
        // Update WAV header with final file size
        finalize_wav_header(context->audio_file, context->recorded_audio_samples * 2); // samples * 2 bytes
        fclose(context->audio_file);
        context->audio_file = NULL;
    }
    if (context->timing_file) {
        fclose(context->timing_file);
        context->timing_file = NULL;
    }

    C64U_LOG_INFO("Recording stopped. Frames: %u, Audio samples: %llu", context->recorded_frames,
                  (unsigned long long)context->recorded_audio_samples);

    pthread_mutex_unlock(&context->recording_mutex);
}

// Recording initialization function
void c64u_record_init(struct c64u_source *context)
{
    // Initialize recording fields
    context->save_frames = false;
    context->saved_frame_count = 0;
    memset(context->save_folder, 0, sizeof(context->save_folder));
    strncpy(context->save_folder, "./recordings", sizeof(context->save_folder) - 1);

    // Initialize video recording
    context->record_video = false;
    context->video_file = NULL;
    context->audio_file = NULL;
    context->timing_file = NULL;
    context->recording_start_time = 0;
    context->recorded_frames = 0;
    context->recorded_audio_samples = 0;

    // Initialize recording mutex
    if (pthread_mutex_init(&context->recording_mutex, NULL) != 0) {
        C64U_LOG_ERROR("Failed to initialize recording mutex");
    }
}

// Recording cleanup function
void c64u_record_cleanup(struct c64u_source *context)
{
    // Stop recording if active
    if (context->record_video) {
        if (pthread_mutex_lock(&context->recording_mutex) == 0) {
            if (context->video_file) {
                fclose(context->video_file);
                context->video_file = NULL;
            }
            if (context->audio_file) {
                fclose(context->audio_file);
                context->audio_file = NULL;
            }
            if (context->timing_file) {
                fclose(context->timing_file);
                context->timing_file = NULL;
            }
            pthread_mutex_unlock(&context->recording_mutex);
        }
    }

    // Clean up recording mutex
    pthread_mutex_destroy(&context->recording_mutex);
}

// Recording settings update function
void c64u_record_update_settings(struct c64u_source *context, void *settings_ptr)
{
    obs_data_t *settings = (obs_data_t *)settings_ptr;

    // Update frame saving settings
    context->save_frames = obs_data_get_bool(settings, "save_frames");
    const char *new_save_folder = obs_data_get_string(settings, "save_folder");
    if (new_save_folder && strlen(new_save_folder) > 0) {
        if (strcmp(context->save_folder, new_save_folder) != 0) {
            strncpy(context->save_folder, new_save_folder, sizeof(context->save_folder) - 1);
            context->save_folder[sizeof(context->save_folder) - 1] = '\0';
            context->saved_frame_count = 0; // Reset counter for new folder
            C64U_LOG_INFO("Frame save folder updated: %s", context->save_folder);
        }
    }

    // Update frame saving (check if we need session cleanup)
    bool old_save_frames = context->save_frames;
    context->save_frames = obs_data_get_bool(settings, "save_frames");
    if (old_save_frames && !context->save_frames) {
        // Frame saving stopped, cleanup session if no other recording active
        cleanup_session_if_needed(context);
    }

    // Update video recording settings
    bool new_record_video = obs_data_get_bool(settings, "record_video");
    if (new_record_video != context->record_video) {
        context->record_video = new_record_video;

        if (new_record_video) {
            // Start recording (will join/create session)
            c64u_start_video_recording(context);
            C64U_LOG_INFO("Video recording started");
        } else {
            // Stop recording
            c64u_stop_video_recording(context);
            cleanup_session_if_needed(context);
        }
    }
}
