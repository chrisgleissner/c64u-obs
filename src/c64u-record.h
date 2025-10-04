#ifndef C64U_RECORD_H
#define C64U_RECORD_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct c64u_source;

// Frame saving and recording functions
void c64u_save_frame_as_bmp(struct c64u_source *context, uint32_t *frame_buffer);
void c64u_start_video_recording(struct c64u_source *context);
void c64u_record_video_frame(struct c64u_source *context, uint32_t *frame_buffer);
void c64u_record_audio_data(struct c64u_source *context, const uint8_t *audio_data, size_t data_size);
void c64u_stop_video_recording(struct c64u_source *context);

// Recording initialization and cleanup functions
void c64u_record_init(struct c64u_source *context);
void c64u_record_cleanup(struct c64u_source *context);
void c64u_record_update_settings(struct c64u_source *context, void *settings);

#endif // C64U_RECORD_H
