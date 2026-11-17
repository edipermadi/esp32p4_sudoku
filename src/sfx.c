#include "sfx.h"
#include <math.h>
#include "bsp/bsp_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sfx";

#define SFX_RATE      24000
#define SFX_CHUNK     240           /* frames per write: 10 ms */
#define SFX_VOICES    4
#define SFX_VOLUME    70            /* codec volume, % */

typedef enum { W_SINE, W_TRI, W_SQUARE } wave_t;

typedef struct {
    uint16_t freq;                  /* Hz, 0 = rest */
    uint16_t ms;
    uint8_t  wave;
    uint8_t  amp;                   /* 0..100 */
} note_t;

#define END {0, 0, 0, 0}

/* A bright tick: barely a note, mostly attack. */
static const note_t s_touch[] = {
    {2400, 14, W_SINE, 28}, END,
};
/* Rising fifth: G5 -> C6. */
static const note_t s_digit_done[] = {
    {784, 90, W_TRI, 55}, {1047, 170, W_TRI, 55}, END,
};
/* Low two-step buzz. */
static const note_t s_error[] = {
    {196, 90, W_SQUARE, 22}, {0, 35, 0, 0}, {147, 160, W_SQUARE, 22}, END,
};
/* C major arpeggio and a held top note. */
static const note_t s_solved[] = {
    {523, 110, W_TRI, 55}, {659, 110, W_TRI, 55}, {784, 110, W_TRI, 55},
    {1047, 160, W_TRI, 60}, {0, 40, 0, 0}, {784, 110, W_TRI, 50}, {1047, 420, W_TRI, 65}, END,
};

static const note_t *const s_sounds[SFX_COUNT] = {
    [SFX_TOUCH] = s_touch,
    [SFX_DIGIT_DONE] = s_digit_done,
    [SFX_ERROR] = s_error,
    [SFX_SOLVED] = s_solved,
};

typedef struct {
    const note_t *note;             /* NULL = idle */
    uint32_t pos;                   /* frames into the current note */
    uint32_t len;                   /* frames in the current note */
    float    phase;                 /* 0..1 */
} voice_t;

static QueueHandle_t s_queue;
static volatile bool s_enabled = true;
static voice_t s_voices[SFX_VOICES];

static void voice_start_note(voice_t *v)
{
    v->pos = 0;
    v->len = (uint32_t)v->note->ms * SFX_RATE / 1000;
    v->phase = 0;
}

static void voice_start(sfx_id_t id)
{
    /* free voice, or steal the first one */
    voice_t *v = NULL;
    for (int i = 0; i < SFX_VOICES && !v; i++) {
        if (!s_voices[i].note) v = &s_voices[i];
    }
    if (!v) v = &s_voices[0];
    v->note = s_sounds[id];
    voice_start_note(v);
}

/* Envelope: 2 ms attack, then exponential decay with a short linear tail to 0. */
static float envelope(uint32_t pos, uint32_t len)
{
    const uint32_t attack = SFX_RATE / 500;
    float t = (float)pos / (float)len;
    float e = pos < attack ? (float)pos / attack : expf(-3.0f * t);
    if (t > 0.85f) e *= (1.0f - t) / 0.15f;
    return e;
}

static float osc(uint8_t wave, float ph)
{
    switch (wave) {
    case W_TRI:    return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case W_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    default:       return sinf(2.0f * (float)M_PI * ph);
    }
}

static bool render(int16_t *out, int frames)
{
    bool any = false;
    for (int f = 0; f < frames; f++) {
        float acc = 0;
        for (int i = 0; i < SFX_VOICES; i++) {
            voice_t *v = &s_voices[i];
            if (!v->note) continue;
            any = true;
            const note_t *n = v->note;
            if (n->freq) {
                acc += osc(n->wave, v->phase) * envelope(v->pos, v->len) * (n->amp / 100.0f);
                v->phase += (float)n->freq / SFX_RATE;
                if (v->phase >= 1.0f) v->phase -= 1.0f;
            }
            if (++v->pos >= v->len) {
                v->note++;
                if (v->note->ms == 0) v->note = NULL;
                else voice_start_note(v);
            }
        }
        if (acc > 1.0f) acc = 1.0f;
        if (acc < -1.0f) acc = -1.0f;
        int16_t s = (int16_t)(acc * 30000.0f);
        out[2 * f] = s;
        out[2 * f + 1] = s;
    }
    return any;
}

static void sfx_task(void *arg)
{
    static int16_t buf[SFX_CHUNK * 2];
    bool active = false;
    for (;;) {
        uint8_t id;
        /* idle: sleep until something is queued; playing: just drain the queue */
        while (xQueueReceive(s_queue, &id, active ? 0 : portMAX_DELAY) == pdTRUE) {
            if (id < SFX_COUNT) voice_start((sfx_id_t)id);
            active = true;
        }
        /* the chunk after the last note is all zeros: written too, so the tail plays out */
        active = render(buf, SFX_CHUNK);
        bsp_audio_write(buf, SFX_CHUNK, 100);
    }
}

esp_err_t sfx_init(void)
{
    esp_err_t err = bsp_audio_init(SFX_RATE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "speaker init failed: %s", esp_err_to_name(err));
        return err;
    }
    bsp_audio_set_volume(SFX_VOLUME);
    s_queue = xQueueCreate(8, sizeof(uint8_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sfx_task, "sfx", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void sfx_play(sfx_id_t id)
{
    if (!s_queue || !s_enabled) return;
    uint8_t b = (uint8_t)id;
    xQueueSend(s_queue, &b, 0);
}

void sfx_set_enabled(bool on)
{
    s_enabled = on;
}

bool sfx_enabled(void)
{
    return s_enabled;
}
