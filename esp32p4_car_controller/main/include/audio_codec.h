#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t audio_codec_init(void);
esp_err_t audio_codec_read_pcm(int16_t *samples, size_t sample_count, size_t *samples_read, uint32_t timeout_ms);
esp_err_t audio_codec_play_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t audio_codec_play_silence(uint32_t duration_ms);
void audio_codec_set_speaker_enabled(bool enabled);
esp_err_t audio_codec_measure_mic_peak(int16_t *peak_out, size_t *samples_read_out, uint32_t timeout_ms);
void audio_codec_play_1khz_test_tone(uint32_t duration_ms);
void audio_codec_play_test_tone(void);
void audio_codec_record_test_sample(void);
