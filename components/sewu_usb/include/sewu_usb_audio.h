#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sewu_usb_audio_init(void);
void sewu_usb_audio_update(void);

bool   sewu_usb_audio_read_frame(float *left, float *right);
size_t sewu_usb_audio_read_batch(float *left_buf, float *right_buf, size_t num_frames);
void sewu_usb_audio_write_frames(const int16_t *interleaved_stereo, size_t frames);
void sewu_usb_audio_write_pcm16_bytes(const uint8_t *pcm_bytes, size_t bytes, uint8_t channels);

size_t sewu_usb_audio_buffered_frames(void);
size_t sewu_usb_audio_capacity_frames(void);
void sewu_usb_audio_reset_stats(void);

#ifdef __cplusplus
}
#endif
