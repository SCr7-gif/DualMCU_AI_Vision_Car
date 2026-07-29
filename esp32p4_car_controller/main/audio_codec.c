#include "audio_codec.h"

#include "board_config.h"
#include "board_i2c.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio_codec";

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static bool s_speaker_enabled;

#define ES8311_RESET_REG00       0x00
#define ES8311_CLK_MANAGER_REG01 0x01
#define ES8311_CLK_MANAGER_REG02 0x02
#define ES8311_CLK_MANAGER_REG03 0x03
#define ES8311_CLK_MANAGER_REG04 0x04
#define ES8311_CLK_MANAGER_REG05 0x05
#define ES8311_CLK_MANAGER_REG06 0x06
#define ES8311_CLK_MANAGER_REG07 0x07
#define ES8311_CLK_MANAGER_REG08 0x08
#define ES8311_SDPIN_REG09       0x09
#define ES8311_SDPOUT_REG0A      0x0A
#define ES8311_SYSTEM_REG0D      0x0D
#define ES8311_SYSTEM_REG0E      0x0E
#define ES8311_SYSTEM_REG12      0x12
#define ES8311_SYSTEM_REG13      0x13
#define ES8311_SYSTEM_REG14      0x14
#define ES8311_ADC_REG16         0x16
#define ES8311_ADC_REG17         0x17
#define ES8311_ADC_REG1C         0x1C
#define ES8311_DAC_REG32         0x32
#define ES8311_DAC_REG37         0x37

static esp_err_t es8311_write(uint8_t reg, uint8_t value)
{
    return board_i2c_write_reg8(BOARD_ES8311_I2C_ADDR, reg, value);
}

static esp_err_t es8311_read(uint8_t reg, uint8_t *value)
{
    return board_i2c_read_reg8(BOARD_ES8311_I2C_ADDR, reg, value);
}

void audio_codec_set_speaker_enabled(bool enabled)
{
    s_speaker_enabled = enabled;
    gpio_set_level(BOARD_ES8311_PA_CTRL_GPIO, enabled ? 1 : 0);
}

static esp_err_t audio_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(BOARD_AUDIO_BITS_PER_SAMPLE, BOARD_AUDIO_SLOT_MODE),
        .gpio_cfg = {
            .mclk = BOARD_ES8311_MCLK_GPIO,
            .bclk = BOARD_ES8311_BCLK_GPIO,
            .ws = BOARD_ES8311_LRCK_GPIO,
            .dout = BOARD_ES8311_DIN_GPIO,
            .din = BOARD_ES8311_DOUT_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* ES8311 使用 256fs MCLK；24 kHz 音频对应 6.144 MHz MCLK。 */
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "tx init std failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_chan, &std_cfg), TAG, "rx init std failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "tx enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "rx enable failed");

    ESP_LOGI(TAG,
             "I2S ready: %d Hz, 16-bit, stereo slot, MCLK=%d BCLK=%d LRCK=%d DIN=%d DOUT=%d",
             BOARD_AUDIO_SAMPLE_RATE_HZ,
             BOARD_ES8311_MCLK_GPIO, BOARD_ES8311_BCLK_GPIO, BOARD_ES8311_LRCK_GPIO,
             BOARD_ES8311_DIN_GPIO, BOARD_ES8311_DOUT_GPIO);
    return ESP_OK;
}

static esp_err_t audio_es8311_init(void)
{
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << BOARD_ES8311_PA_CTRL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG, "pa gpio config failed");
    gpio_set_level(BOARD_ES8311_PA_CTRL_GPIO, 0);

    uint8_t reg = 0;
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_RESET_REG00, 0x1F), TAG, "reset step1 failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_RESET_REG00, 0x00), TAG, "reset step2 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_RESET_REG00, 0x80), TAG, "power on failed");

    /*
     * Same ES8311 register plan as Espressif's component for:
     * MCLK=sample_rate*256, sample_rate=24kHz, 16-bit I2S slave.
     */
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG01, 0x3F), TAG, "clock reg01 failed");
    ESP_RETURN_ON_ERROR(es8311_read(ES8311_CLK_MANAGER_REG06, &reg), TAG, "clock reg06 read failed");
    reg &= (uint8_t)~BIT(5);
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG06, reg), TAG, "clock reg06 polarity failed");
    ESP_RETURN_ON_ERROR(es8311_read(ES8311_CLK_MANAGER_REG02, &reg), TAG, "clock reg02 read failed");
    reg &= 0x07;
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG02, reg), TAG, "clock reg02 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG03, 0x10), TAG, "clock reg03 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG04, 0x10), TAG, "clock reg04 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG05, 0x00), TAG, "clock reg05 failed");
    ESP_RETURN_ON_ERROR(es8311_read(ES8311_CLK_MANAGER_REG06, &reg), TAG, "clock reg06 read2 failed");
    reg &= 0xE0;
    reg |= 0x03;
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG06, reg), TAG, "clock reg06 divider failed");
    ESP_RETURN_ON_ERROR(es8311_read(ES8311_CLK_MANAGER_REG07, &reg), TAG, "clock reg07 read failed");
    reg &= 0xC0;
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG07, reg), TAG, "clock reg07 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_CLK_MANAGER_REG08, 0xFF), TAG, "clock reg08 failed");

    ESP_RETURN_ON_ERROR(es8311_read(ES8311_RESET_REG00, &reg), TAG, "serial mode read failed");
    reg &= 0xBF;
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_RESET_REG00, reg), TAG, "serial slave mode failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SDPIN_REG09, 0x0C), TAG, "sdp in failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SDPOUT_REG0A, 0x0C), TAG, "sdp out failed");

    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SYSTEM_REG0D, 0x01), TAG, "system reg0d failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SYSTEM_REG0E, 0x02), TAG, "system reg0e failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SYSTEM_REG12, 0x00), TAG, "system reg12 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SYSTEM_REG13, 0x10), TAG, "system reg13 failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_ADC_REG1C, 0x6A), TAG, "adc eq failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_DAC_REG37, 0x08), TAG, "dac ramp failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_DAC_REG32, 0xC6), TAG, "volume failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_ADC_REG17, 0xC8), TAG, "adc gain failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_SYSTEM_REG14, 0x1A), TAG, "analog mic failed");
    ESP_RETURN_ON_ERROR(es8311_write(ES8311_ADC_REG16, 0x04), TAG, "mic gain failed");

    audio_codec_set_speaker_enabled(false);
    ESP_LOGI(TAG,
             "ES8311 ready: addr=0x%02X PA_CTRL=%d i2s=%dHz/16bit/stereo-slot",
             BOARD_ES8311_I2C_ADDR,
             BOARD_ES8311_PA_CTRL_GPIO,
             BOARD_AUDIO_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t audio_codec_init(void)
{
    ESP_RETURN_ON_ERROR(audio_i2s_init(), TAG, "I2S init failed");
    ESP_RETURN_ON_ERROR(audio_es8311_init(), TAG, "ES8311 init failed");
    return ESP_OK;
}

esp_err_t audio_codec_read_pcm(int16_t *samples, size_t sample_count, size_t *samples_read, uint32_t timeout_ms)
{
    if ((s_rx_chan == NULL) || (samples == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(s_rx_chan,
                                     samples,
                                     sample_count * sizeof(int16_t),
                                     &bytes_read,
                                     pdMS_TO_TICKS(timeout_ms));
    if (samples_read != NULL) {
        *samples_read = bytes_read / sizeof(int16_t);
    }
    return ret;
}

esp_err_t audio_codec_play_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    if ((s_tx_chan == NULL) || (samples == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_speaker_enabled) {
        audio_codec_set_speaker_enabled(true);
    }
    /*
     * i2s_channel_write() 在超时或 DMA 繁忙时可能只写入部分字节。
     * 这里必须循环写完整个 PCM 块，否则上层会误以为整块已播放，听感就是跳读。
     */
    const uint8_t *p = (const uint8_t *)samples;
    size_t bytes_left = sample_count * sizeof(int16_t);
    while (bytes_left > 0) {
        size_t written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_chan,
                                          p,
                                          bytes_left,
                                          &written,
                                          pdMS_TO_TICKS(timeout_ms));
        if (ret != ESP_OK) {
            return ret;
        }
        if (written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        p += written;
        bytes_left -= written;
    }
    return ESP_OK;
}

esp_err_t audio_codec_play_silence(uint32_t duration_ms)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int16_t zeros[512] = {0};
    uint32_t frames_total = (BOARD_AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000u;
    uint32_t samples_total = frames_total * BOARD_AUDIO_CHANNEL_COUNT;

    while (samples_total > 0) {
        size_t chunk = samples_total;
        if (chunk > (sizeof(zeros) / sizeof(zeros[0]))) {
            chunk = sizeof(zeros) / sizeof(zeros[0]);
        }

        size_t written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_chan,
                                          zeros,
                                          chunk * sizeof(int16_t),
                                          &written,
                                          pdMS_TO_TICKS(200));
        if (ret != ESP_OK) {
            return ret;
        }

        size_t samples_written = written / sizeof(int16_t);
        if (samples_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        samples_total -= (uint32_t)samples_written;
    }

    return ESP_OK;
}

esp_err_t audio_codec_measure_mic_peak(int16_t *peak_out, size_t *samples_read_out, uint32_t timeout_ms)
{
    if (peak_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t samples[512];
    size_t samples_read = 0;
    esp_err_t ret = audio_codec_read_pcm(samples, sizeof(samples) / sizeof(samples[0]), &samples_read, timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    int16_t peak = 0;
    for (size_t i = 0; i < samples_read; i++) {
        int32_t v = samples[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = (int16_t)v;
        }
    }

    *peak_out = peak;
    if (samples_read_out != NULL) {
        *samples_read_out = samples_read;
    }
    return ESP_OK;
}

void audio_codec_play_1khz_test_tone(uint32_t duration_ms)
{
    if (s_tx_chan == NULL) {
        ESP_LOGW(TAG, "I2S tx is not ready");
        return;
    }

    if (duration_ms == 0) {
        duration_ms = 1000;
    }

    const size_t frames_per_chunk = BOARD_AUDIO_SAMPLE_RATE_HZ / 20; /* 50 ms */
    const size_t sample_count = frames_per_chunk * BOARD_AUDIO_CHANNEL_COUNT;
    int16_t *samples = calloc(sample_count, sizeof(int16_t));
    if (samples == NULL) {
        ESP_LOGE(TAG, "no memory for test tone");
        return;
    }

    const int period = BOARD_AUDIO_SAMPLE_RATE_HZ / 1000; /* 1 kHz square wave */
    for (int i = 0; i < (int)frames_per_chunk; i++) {
        int16_t v = ((i / (period / 2)) % 2) ? 9000 : -9000;
        for (int ch = 0; ch < BOARD_AUDIO_CHANNEL_COUNT; ch++) {
            samples[i * BOARD_AUDIO_CHANNEL_COUNT + ch] = v;
        }
    }

    ESP_LOGI(TAG, "play 1kHz local test tone start: %u ms", (unsigned)duration_ms);
    audio_codec_set_speaker_enabled(true);
    uint32_t chunks = (duration_ms + 49u) / 50u;
    for (uint32_t n = 0; n < chunks; n++) {
        esp_err_t ret = audio_codec_play_pcm(samples, sample_count, 300);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s write failed: %s", esp_err_to_name(ret));
            audio_codec_set_speaker_enabled(false);
            free(samples);
            return;
        }
    }
    audio_codec_play_silence(120);
    audio_codec_set_speaker_enabled(false);
    free(samples);
    ESP_LOGI(TAG, "play 1kHz local test tone done");
}

void audio_codec_play_test_tone(void)
{
    audio_codec_play_1khz_test_tone(1000);
}

void audio_codec_record_test_sample(void)
{
    int16_t peak = 0;
    size_t samples_read = 0;
    esp_err_t ret = audio_codec_measure_mic_peak(&peak, &samples_read, 500);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mic read failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "mic sample: samples=%u peak=%d", (unsigned)samples_read, peak);
}
