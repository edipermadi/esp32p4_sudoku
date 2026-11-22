/**
 * @file bsp_audio.c
 * @ingroup bsp
 * @brief Speaker (ES8311 + NS4150) and microphones (ES7210) on I2S0.
 *
 * Speaker output: ES8311 codec (I2C 0x18 on the shared GPIO7/8 bus) fed by
 * I2S0, followed by an NS4150 class-D amp enabled on GPIO53 (active high).
 *
 * Pins and amp polarity come from Waveshare's esp32_p4_wifi6_touch_lcd_4b BSP.
 * The register sequence is a DAC-only, slave-mode subset of Espressif's
 * esp_codec_dev ES8311 driver (Apache-2.0); the Arduino core does not ship it.
 *
 * Microphones: ES7210 ADC (I2C 0x40) on the same I2S0 port, full duplex (its
 * SDOUT on GPIO11), clocked by our MCLK/BCLK/LRCK as a slave. Its setup is a
 * slave-mode, MIC1+MIC2, 16-bit subset of esp_codec_dev's ES7210 driver
 * (Apache-2.0). The RX channel is created with TX but only enabled while
 * something listens (bsp_mic_start/stop).
 *
 */
#include "bsp_board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_audio";   /**< the log's tag */

#define ES8311_ADDR        0x18      /**< CE pin low */
#define ES8311_I2C_HZ      100000       /**< the codec's I2C clock, Hz */
#define AUDIO_MCLK_MULT    I2S_MCLK_MULTIPLE_256  /**< MCLK = 256 x the sample rate */

#define ES7210_ADDR        0x40      /**< AD0/AD1 low */
#define ES7210_GAIN        10        /**< 30 dB (3 dB a step) */

static i2s_chan_handle_t s_tx = NULL;   /**< the speaker's I2S channel */
static i2s_chan_handle_t s_rx = NULL;   /**< the microphones' I2S channel (NULL: none) */
static i2c_master_dev_handle_t s_adc = NULL;  /**< the mic ADC (ES7210) on I2C */
static bool s_mic_on = false;           /**< the microphones are on (bsp_mic_start()) */
static i2c_master_dev_handle_t s_codec = NULL;  /**< the codec (ES8311) on I2C */
static SemaphoreHandle_t s_owner = NULL;   /**< one audio producer at a time (the MP3 player) */
static uint32_t s_rate = 0;             /**< the sample rate, Hz */

/**
 * @brief Writes a codec register.
 * @param[in] reg  the register
 * @param[in] val  its value
 * @return ESP_OK, or why not
 */
static esp_err_t es_wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_codec, buf, sizeof(buf), 100);
}

/**
 * @brief Reads a codec register.
 * @param[in] reg  the register
 * @param[out] val  gets its value
 * @return ESP_OK, or why not
 */
static esp_err_t es_rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_codec, &reg, 1, val, 1, 100);
}

/**
 * @brief Read-modify-write a codec register: the bits in keep kept, those in set set.
 * @param[in] reg  the register
 * @param[in] keep  the bits kept
 * @param[in] set  the bits set
 * @return ESP_OK, or why not
 */
static esp_err_t es_rmw(uint8_t reg, uint8_t keep, uint8_t set)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(es_rd(reg, &v), TAG, "read 0x%02x failed", reg);
    return es_wr(reg, (v & keep) | set);
}

/**
 * @brief A rate the codec's clocking supports.
 * @param[in] r  the rate, Hz
 * @return true if so
 */
static bool is_rate_supported(uint32_t r)
{
    static const uint32_t rates[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        if (rates[i] == r) {
            return true;
        }
    }
    return false;
}

/**
 * @brief
 * @param[in] rate  Hz
 * @return ESP_OK, or why not
 */
static esp_err_t es8311_set_clock(uint32_t rate)
{
    esp_err_t r = ESP_OK;
    r |= es_rmw(0x02, 0x07, 0x00);                         /* pre_div 1, pre_multi x1 */
    r |= es_wr(0x05, 0x00);                                /* adc_div = dac_div = 1 */
    r |= es_rmw(0x03, 0x80, 0x10);                         /* single speed, adc_osr */
    r |= es_rmw(0x04, 0x80, rate <= 16000 ? 0x20 : 0x10);  /* dac_osr */
    r |= es_rmw(0x07, 0xC0, 0x00);                         /* LRCK div high */
    r |= es_wr(0x08, 0xFF);                                /* LRCK div low */
    r |= es_rmw(0x06, 0xE0, 4 - 1);                        /* BCLK div 4 */
    return r == ESP_OK ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Brings up the codec: DAC only, slave mode, at the rate.
 * @param[in] sample_rate  Hz
 * @return ESP_OK, or why not
 */
static esp_err_t es8311_init(uint32_t sample_rate)
{
    uint8_t id1 = 0, id2 = 0;
    ESP_RETURN_ON_ERROR(es_rd(0xFD, &id1), TAG, "ES8311 not responding at 0x%02x", ES8311_ADDR);
    es_rd(0xFE, &id2);
    ESP_LOGI(TAG, "ES8311 chip id %02x%02x", id1, id2);

    esp_err_t r = ESP_OK;
    /* open: base configuration */
    r |= es_wr(0x0D, 0xFA);
    r |= es_wr(0x44, 0x08);              /* I2C noise immunity (written twice on purpose) */
    r |= es_wr(0x44, 0x08);
    r |= es_wr(0x01, 0x30);
    r |= es_wr(0x02, 0x00);
    r |= es_wr(0x03, 0x10);
    r |= es_wr(0x16, 0x24);
    r |= es_wr(0x04, 0x10);
    r |= es_wr(0x05, 0x00);
    r |= es_wr(0x0B, 0x00);
    r |= es_wr(0x0C, 0x00);
    r |= es_wr(0x10, 0x1F);
    r |= es_wr(0x11, 0x7F);
    r |= es_wr(0x00, 0x80);              /* slave mode */
    r |= es_wr(0x01, 0x3F);              /* MCLK from MCLK pin, not inverted */
    r |= es_rmw(0x06, (uint8_t)~0x20, 0x00); /* SCLK not inverted */
    r |= es_wr(0x13, 0x10);
    r |= es_wr(0x1B, 0x0A);
    r |= es_wr(0x1C, 0x6A);
    r |= es_wr(0x44, 0x58);

    /* format: I2S, 16-bit */
    r |= es_rmw(0x09, 0xFC, 0x0C);
    r |= es_rmw(0x0A, 0xFC, 0x0C);

    /* sample-rate clocking */
    r |= es8311_set_clock(sample_rate);

    /* start (DAC path) */
    r |= es_wr(0x00, 0x80);
    r |= es_wr(0x01, 0x3F);
    r |= es_rmw(0x09, 0xBF, 0x00);       /* SDP in: unmute */
    r |= es_wr(0x17, 0xBF);
    r |= es_wr(0x0E, 0x02);
    r |= es_wr(0x12, 0x00);              /* power up DAC */
    r |= es_wr(0x14, 0x1A);
    r |= es_wr(0x0D, 0x01);
    r |= es_wr(0x15, 0x40);
    r |= es_wr(0x37, 0x08);
    r |= es_wr(0x45, 0x00);
    r |= es_rmw(0x31, 0x9F, 0x00);       /* DAC soft-unmute */

    ESP_RETURN_ON_FALSE(r == ESP_OK, ESP_FAIL, TAG, "ES8311 register setup failed");
    return ESP_OK;
}

/* (documented in bsp_board.h) */
void bsp_audio_set_volume(int percent)
{
    if (!s_codec) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    /* Reg 0x32: 0x00 = -95.5 dB, 0xBF = 0 dB, 0.5 dB/step. Map 1..100 % to -47.5..0 dB. */
    uint8_t reg = percent == 0 ? 0x00 : (uint8_t)(0x5F + (0xBF - 0x5F) * percent / 100);
    es_wr(0x32, reg);
}

/* (documented in bsp_board.h) */
esp_err_t bsp_audio_init(uint32_t sample_rate)
{
    if (s_tx) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(is_rate_supported(sample_rate), ESP_ERR_NOT_SUPPORTED, TAG,
                        "unsupported sample rate %lu", (unsigned long)sample_rate);
    if (!s_owner) {
        s_owner = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_owner, ESP_ERR_NO_MEM, TAG, "mutex alloc failed");
    }

    /* Amp off while the codec comes up, to avoid pops */
    gpio_config_t pa = {
        .pin_bit_mask = (1ULL << BSP_AUDIO_PA_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "PA gpio failed");
    gpio_set_level(BSP_AUDIO_PA_GPIO, 0);

    /* I2S first so MCLK is running when the codec is configured */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;          /* underrun -> silence, not a buzz */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "I2S channel failed");   /* full duplex */

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK_GPIO,
            .bclk = BSP_I2S_BCLK_GPIO,
            .ws = BSP_I2S_WS_GPIO,
            .dout = BSP_I2S_DOUT_GPIO,
            .din = BSP_I2S_DIN_GPIO,
        },
    };
    std_cfg.clk_cfg.mclk_multiple = AUDIO_MCLK_MULT;
    esp_err_t err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s_rx, &std_cfg);   /* (enabled by bsp_mic_start) */
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        i2s_del_channel(s_rx);
        s_tx = NULL;
        s_rx = NULL;
        return err;
    }

    /* Codec on the shared I2C bus */
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(&bus), TAG, "I2C bus failed");
    if (!s_codec) {
        i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ES8311_ADDR,
            .scl_speed_hz = ES8311_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &s_codec), TAG, "add ES8311 failed");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(es8311_init(sample_rate), TAG, "ES8311 init failed");
    bsp_audio_set_volume(70);

    s_rate = sample_rate;

    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(BSP_AUDIO_PA_GPIO, 1);
    ESP_LOGI(TAG, "speaker ready, %lu Hz", (unsigned long)sample_rate);
    return ESP_OK;
}

/* (documented in bsp_board.h) */
esp_err_t bsp_audio_write(const int16_t *stereo_frames, size_t frames, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_INVALID_STATE, TAG, "audio not initialised");
    size_t written = 0;
    return i2s_channel_write(s_tx, stereo_frames, frames * 2 * sizeof(int16_t), &written, timeout_ms);
}

/* (documented in bsp_board.h) */
esp_err_t bsp_audio_set_sample_rate(uint32_t sample_rate)
{
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_INVALID_STATE, TAG, "audio not initialised");
    if (sample_rate == s_rate) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(is_rate_supported(sample_rate), ESP_ERR_NOT_SUPPORTED, TAG,
                        "unsupported sample rate %lu", (unsigned long)sample_rate);

    es_rmw(0x31, 0x9F, 0x60);                     /* DAC soft-mute during the switch */
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clk.mclk_multiple = AUDIO_MCLK_MULT;
    if (s_mic_on) {
        i2s_channel_disable(s_rx);
    }
    esp_err_t err = i2s_channel_disable(s_tx);
    if (err == ESP_OK) {
        err = i2s_channel_reconfig_std_clock(s_tx, &clk);
    }
    if (err == ESP_OK && s_rx) {
        err = i2s_channel_reconfig_std_clock(s_rx, &clk);   /* (one clock for both directions) */
    }
    esp_err_t en = i2s_channel_enable(s_tx);
    if (s_mic_on) {
        i2s_channel_enable(s_rx);
    }
    if (err == ESP_OK) {
        err = en;
    }
    if (err == ESP_OK) {
        err = es8311_set_clock(sample_rate);
    }
    es_rmw(0x31, 0x9F, 0x00);                     /* unmute */
    ESP_RETURN_ON_ERROR(err, TAG, "rate switch to %lu failed", (unsigned long)sample_rate);
    s_rate = sample_rate;
    ESP_LOGI(TAG, "sample rate %lu Hz", (unsigned long)sample_rate);
    return ESP_OK;
}

/* (documented in bsp_board.h) */
bool bsp_audio_acquire(uint32_t timeout_ms)
{
    return s_owner && xSemaphoreTake(s_owner, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/* (documented in bsp_board.h) */
void bsp_audio_release(void)
{
    if (s_owner) {
        xSemaphoreGive(s_owner);
    }
}

/**
 * @brief Writes a mic ADC register.
 * @param[in] reg  the register
 * @param[in] val  its value
 * @return ESP_OK, or why not
 */
static esp_err_t ad_wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_adc, buf, sizeof(buf), 100);
}

/**
 * @brief Reads a mic ADC register.
 * @param[in] reg  the register
 * @param[out] val  gets its value
 * @return ESP_OK, or why not
 */
static esp_err_t ad_rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_adc, &reg, 1, val, 1, 100);
}

/**
 * @brief Updates a mic ADC register: the bits in mask from val.
 * @param[in] reg  the register
 * @param[in] mask  the bits changed
 * @param[in] val  their values
 * @return ESP_OK, or why not
 */
static esp_err_t ad_upd(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(ad_rd(reg, &v), TAG, "ES7210 read 0x%02x failed", reg);
    return ad_wr(reg, (uint8_t)((v & ~mask) | (val & mask)));
}

/**
 * @brief MIC1 and MIC2 on, at ES7210_GAIN, two slots (not TDM).
 * @return ESP_OK, or why not
 */
static esp_err_t es7210_mics(void)
{
    esp_err_t r = ESP_OK;
    for (int i = 0; i < 4; i++) {
        r |= ad_upd(0x43 + i, 0x10, 0x00);         /* MIC1..4 gain off */
    }
    r |= ad_wr(0x4B, 0xFF);
    r |= ad_wr(0x4C, 0xFF);
    r |= ad_upd(0x01, 0x0B, 0x00);                  /* MIC1/2 clocks on */
    r |= ad_wr(0x4B, 0x00);                         /* MIC1/2 bias, ADC, PGA power */
    r |= ad_upd(0x43, 0x10, 0x10);
    r |= ad_upd(0x43, 0x0F, ES7210_GAIN);
    r |= ad_upd(0x44, 0x10, 0x10);
    r |= ad_upd(0x44, 0x0F, ES7210_GAIN);
    r |= ad_wr(0x12, 0x00);                         /* SDP: 2 slots */
    return r == ESP_OK ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Brings up the mic ADC (slave mode, 16-bit), powered down until es7210_start().
 * @return ESP_OK, or why not
 */
static esp_err_t es7210_init(void)
{
    esp_err_t r = ESP_OK;
    r |= ad_wr(0x00, 0xFF);                         /* reset */
    r |= ad_wr(0x00, 0x41);
    r |= ad_wr(0x01, 0x3F);                         /* clocks off */
    r |= ad_wr(0x09, 0x30);                         /* chip state cycle */
    r |= ad_wr(0x0A, 0x30);                         /* power-on state cycle */
    r |= ad_wr(0x23, 0x2A);                         /* high-pass filters */
    r |= ad_wr(0x22, 0x0A);
    r |= ad_wr(0x20, 0x0A);
    r |= ad_wr(0x21, 0x2A);
    r |= ad_upd(0x08, 0x01, 0x00);                  /* slave */
    r |= ad_wr(0x40, 0x43);                         /* analog: VDDA 3.3 V, VMID 5 kOhm */
    r |= ad_wr(0x41, 0x70);                         /* mic bias 2.87 V */
    r |= ad_wr(0x42, 0x70);
    r |= ad_wr(0x07, 0x20);                         /* OSR */
    r |= ad_wr(0x02, 0xC1);                         /* clock divider, DLL; clears the state */
    r |= es7210_mics();
    r |= ad_upd(0x11, 0xE0, 0x60);                  /* 16-bit */
    r |= ad_upd(0x11, 0x03, 0x00);                  /* I2S format */
    return r == ESP_OK ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Powers the microphones up.
 * @return ESP_OK, or why not
 */
static esp_err_t es7210_start(void)
{
    esp_err_t r = ESP_OK;
    r |= ad_wr(0x01, 0x34);                         /* clocks: MIC1/2 on (as after es7210_mics) */
    r |= ad_wr(0x06, 0x00);                         /* power up */
    r |= ad_wr(0x40, 0x43);
    r |= ad_wr(0x47, 0x08);                         /* MIC1..4 power */
    r |= ad_wr(0x48, 0x08);
    r |= ad_wr(0x49, 0x08);
    r |= ad_wr(0x4A, 0x08);
    r |= es7210_mics();
    r |= ad_wr(0x40, 0x43);
    r |= ad_wr(0x00, 0x71);
    r |= ad_wr(0x00, 0x41);
    return r == ESP_OK ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Powers the microphones down.
 */
static void es7210_stop(void)
{
    ad_wr(0x47, 0xFF);
    ad_wr(0x48, 0xFF);
    ad_wr(0x49, 0xFF);
    ad_wr(0x4A, 0xFF);
    ad_wr(0x4B, 0xFF);
    ad_wr(0x4C, 0xFF);
    ad_wr(0x40, 0xC0);
    ad_wr(0x01, 0x7F);
    ad_wr(0x06, 0x07);
}

/* (documented in bsp_board.h) */
esp_err_t bsp_mic_start(void)
{
    ESP_RETURN_ON_FALSE(s_tx && s_rx, ESP_ERR_INVALID_STATE, TAG, "audio not initialised");
    if (s_mic_on) {
        return ESP_OK;
    }
    if (!s_adc) {
        i2c_master_bus_handle_t bus = NULL;
        ESP_RETURN_ON_ERROR(bsp_i2c_init(&bus), TAG, "I2C bus failed");
        i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ES7210_ADDR,
            .scl_speed_hz = ES8311_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &s_adc), TAG, "add ES7210 failed");
        ESP_RETURN_ON_ERROR(es7210_init(), TAG, "ES7210 not responding at 0x%02x", ES7210_ADDR);
    }
    ESP_RETURN_ON_ERROR(es7210_start(), TAG, "ES7210 start failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "I2S RX enable failed");
    s_mic_on = true;
    ESP_LOGI(TAG, "microphones on, %lu Hz", (unsigned long)s_rate);
    return ESP_OK;
}

/* (documented in bsp_board.h) */
void bsp_mic_stop(void)
{
    if (!s_mic_on) {
        return;
    }
    i2s_channel_disable(s_rx);
    es7210_stop();
    s_mic_on = false;
}

/* (documented in bsp_board.h) */
esp_err_t bsp_mic_read(int16_t *stereo_frames, size_t frames, size_t *got_frames, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_mic_on, ESP_ERR_INVALID_STATE, TAG, "microphones off");
    size_t got = 0;
    esp_err_t err = i2s_channel_read(s_rx, stereo_frames, frames * 2 * sizeof(int16_t), &got, timeout_ms);
    if (got_frames) {
        *got_frames = got / (2 * sizeof(int16_t));
    }
    return err;
}

/* (documented in bsp_board.h) */
uint32_t bsp_audio_sample_rate(void)
{
    return s_rate;
}
