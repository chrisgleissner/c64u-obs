#ifndef C64U_PROTOCOL_H
#define C64U_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// C64U Stream constants
#define C64U_VIDEO_PACKET_SIZE 780
#define C64U_AUDIO_PACKET_SIZE 770
#define C64U_VIDEO_HEADER_SIZE 12
#define C64U_AUDIO_HEADER_SIZE 2
#define C64U_CONTROL_PORT 64
#define C64U_DEFAULT_VIDEO_PORT 11000
#define C64U_DEFAULT_AUDIO_PORT 11001
#define C64U_DEFAULT_HOST "c64u"

// Video format constants
#define C64U_PAL_WIDTH 384
#define C64U_PAL_HEIGHT 272
#define C64U_NTSC_WIDTH 384
#define C64U_NTSC_HEIGHT 240
#define C64U_PIXELS_PER_LINE 384
#define C64U_BYTES_PER_LINE 192 // 384 pixels / 2 (4-bit per pixel) - keeping original
#define C64U_LINES_PER_PACKET 4

// Frame assembly constants
#define C64U_MAX_PACKETS_PER_FRAME 68           // PAL: 272 lines ÷ 4 lines/packet = 68 packets
#define C64U_FRAME_TIMEOUT_MS 100               // Timeout for incomplete frames
#define C64U_PAL_FRAME_INTERVAL_NS 19950124ULL  // 19.95ms for 50.125Hz PAL (actual C64 timing)
#define C64U_NTSC_FRAME_INTERVAL_NS 16710875ULL // 16.71ms for 59.826Hz NTSC (actual C64 timing)

// Forward declaration
struct c64u_source;

// Protocol operations
void c64u_send_control_command(struct c64u_source *context, bool enable, uint8_t stream_id);

#endif // C64U_PROTOCOL_H
