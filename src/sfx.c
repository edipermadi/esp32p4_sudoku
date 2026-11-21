/**
 * @file sfx.c
 * @brief Sound effects: a small synthesizer mixing notes into the speaker, and PCM clips.
 */
#include "sfx.h"
#include <math.h>
#include "bsp/bsp_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sfx";         /**< the log's tag */

#define SFX_RATE      24000             /**< the speaker's sample rate, Hz */
#define SFX_CHUNK     240           /**< frames per write: 10 ms */
#define SFX_VOICES    4                 /**< notes played at once */
#define SFX_VOLUME    70            /**< codec volume, % */

/** A note's waveform. */
typedef enum { W_SINE, W_TRI, W_SQUARE } wave_t;

/** A note of a sound: its pitch, length, waveform and loudness. */
typedef struct {
    uint16_t freq;                  /**< Hz, 0 = rest */
    uint16_t ms;                        /**< how long, ms */
    uint8_t  wave;                      /**< wave_t */
    uint8_t  amp;                   /**< 0..100 */
} note_t;

#define END {0, 0, 0, 0}                /**< a sound's last note */

/** @brief A bright tick: barely a note, mostly attack. */
static const note_t s_touch[] = {
    {2400, 14, W_SINE, 28}, END,
};
/** @brief Rising fifth: G5 -> C6. */
static const note_t s_digit_done[] = {
    {784, 90, W_TRI, 55}, {1047, 170, W_TRI, 55}, END,
};
/** @brief Quick bright sparkle, same key: E6 G6 C7. */
static const note_t s_unit_done[] = {
    {1319, 60, W_SINE, 40}, {1568, 60, W_SINE, 40}, {2093, 150, W_SINE, 38}, END,
};
/** @brief Longer for a cross of units: C6 E6 G6 C7, held. */
static const note_t s_units_done[] = {
    {1047, 55, W_SINE, 40}, {1319, 55, W_SINE, 40}, {1568, 55, W_SINE, 40}, {2093, 240, W_SINE, 40}, END,
};
/** @brief Low two-step buzz. */
static const note_t s_error[] = {
    {196, 90, W_SQUARE, 22}, {0, 35, 0, 0}, {147, 160, W_SQUARE, 22}, END,
};
/** @brief C major arpeggio and a held top note. */
static const note_t s_solved[] = {
    {523, 110, W_TRI, 55}, {659, 110, W_TRI, 55}, {784, 110, W_TRI, 55},
    {1047, 160, W_TRI, 60}, {0, 40, 0, 0}, {784, 110, W_TRI, 50}, {1047, 420, W_TRI, 65}, END,
};

/** each sound's notes, by sfx_id_t */
static const note_t *const s_sounds[SFX_COUNT] = {
    [SFX_TOUCH] = s_touch,
    [SFX_DIGIT_DONE] = s_digit_done,
    [SFX_ERROR] = s_error,
    [SFX_SOLVED] = s_solved,
    [SFX_UNIT_DONE] = s_unit_done,
    [SFX_UNITS_DONE] = s_units_done,
};

/** A voice: one sound playing, note by note. */
typedef struct {
    const note_t *note;             /**< NULL = idle */
    uint32_t pos;                   /**< frames into the current note */
    uint32_t len;                   /**< frames in the current note */
    float    phase;                 /**< 0..1 */
} voice_t;

#define SFX_PCM_ID    0xFE          /**< queued: the PCM in s_pcm starts */

static QueueHandle_t s_queue;           /**< the sounds to start (sfx_id_t, or SFX_PCM_ID) */
/** @brief one mono PCM clip at a time (sfx_play_pcm): the caller keeps it until done */
static const int16_t *volatile s_pcm;
static volatile size_t s_pcm_len;       /**< its length, frames */
static size_t s_pcm_pos;                /**< the next frame of it */
static volatile bool s_pcm_busy;        /**< a clip is playing (sfx_play_pcm() until it ends) */
static volatile bool s_enabled = true;  /**< sound is on */
static voice_t s_voices[SFX_VOICES];    /**< the voices */

/**
 * @brief Starts a voice's note: its length in frames, the phase from 0.
 * @param v  the voice
 */
static void voice_start_note(voice_t *v)
{
    v->pos = 0;
    v->len = (uint32_t)v->note->ms * SFX_RATE / 1000;
    v->phase = 0;
}

/**
 * @brief Starts a sound on a free voice (or the first one, stolen).
 * @param id  the sound
 */
static void voice_start(sfx_id_t id)
{
    /* free voice, or steal the first one */
    voice_t *v = NULL;
    for (int i = 0; i < SFX_VOICES && !v; i++) {
        if (!s_voices[i].note) {
            v = &s_voices[i];
        }
    }
    if (!v) {
        v = &s_voices[0];
    }
    v->note = s_sounds[id];
    voice_start_note(v);
}

/**
 * @brief
 * @param pos  frames into the note
 * @param len  the note's frames
 * @return the level, 0..1
 */
/** @brief Envelope: 2 ms attack, then exponential decay with a short linear tail to 0. */
static float envelope(uint32_t pos, uint32_t len)
{
    const uint32_t attack = SFX_RATE / 500;
    float t = (float)pos / (float)len;
    float e = pos < attack ? (float)pos / attack : expf(-3.0f * t);
    if (t > 0.85f) {
        e *= (1.0f - t) / 0.15f;
    }
    return e;
}

/**
 * @brief One oscillator sample.
 * @param wave  wave_t
 * @param ph  the phase, 0..1
 * @return -1..1
 */
static float osc(uint8_t wave, float ph)
{
    switch (wave) {
    case W_TRI:
        return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case W_SQUARE:
        return ph < 0.5f ? 1.0f : -1.0f;
    default:
        return sinf(2.0f * (float)M_PI * ph);
    }
}

/**
 * @brief Mixes the voices and the PCM clip into stereo frames.
 * @param out  gets frames * 2 samples
 * @param frames  how many
 * @return false: all silent
 */
static bool render(int16_t *out, int frames)
{
    bool any = false;
    for (int f = 0; f < frames; f++) {
        float acc = 0;
        for (int i = 0; i < SFX_VOICES; i++) {
            voice_t *v = &s_voices[i];
            if (!v->note) {
                continue;
            }
            any = true;
            const note_t *n = v->note;
            if (n->freq) {
                acc += osc(n->wave, v->phase) * envelope(v->pos, v->len) * (n->amp / 100.0f);
                v->phase += (float)n->freq / SFX_RATE;
                if (v->phase >= 1.0f) {
                    v->phase -= 1.0f;
                }
            }
            if (++v->pos >= v->len) {
                v->note++;
                if (v->note->ms == 0) {
                    v->note = NULL;
                } else {
                    voice_start_note(v);
                }
            }
        }
        if (s_pcm && s_pcm_pos < s_pcm_len) {   /* a clip (gibberlink's replies) */
            acc += s_pcm[s_pcm_pos++] / 32768.0f;
            any = true;
            if (s_pcm_pos >= s_pcm_len) {
                s_pcm = NULL;
                s_pcm_busy = false;
            }
        }
        if (acc > 1.0f) {
            acc = 1.0f;
        }
        if (acc < -1.0f) {
            acc = -1.0f;
        }
        int16_t s = (int16_t)(acc * 30000.0f);
        out[2 * f] = s;
        out[2 * f + 1] = s;
    }
    return any;
}

/**
 * @brief The mixer task: starts what's queued, writes 10 ms chunks to the speaker, silence when idle.
 * @param arg  unused
 */
static void sfx_task(void *arg)
{
    static int16_t buf[SFX_CHUNK * 2];
    bool active = false;
    for (;;) {
        uint8_t id;
        /* idle: sleep until something is queued; playing: just drain the queue */
        while (xQueueReceive(s_queue, &id, active ? 0 : portMAX_DELAY) == pdTRUE) {
            if (id < SFX_COUNT) {
                voice_start((sfx_id_t)id);
            } else if (id == SFX_PCM_ID) {
                s_pcm_pos = 0;
            }
            active = true;
        }
        /* the chunk after the last note is all zeros: written too, so the tail plays out */
        active = render(buf, SFX_CHUNK);
        bsp_audio_write(buf, SFX_CHUNK, 100);
    }
}

/* (documented in sfx.h) */
esp_err_t sfx_init(void)
{
    esp_err_t err = bsp_audio_init(SFX_RATE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "speaker init failed: %s", esp_err_to_name(err));
        return err;
    }
    bsp_audio_set_volume(SFX_VOLUME);
    s_queue = xQueueCreate(8, sizeof(uint8_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    /* core 0, away from LVGL (loopTask, core 1). Unpinned would not stay so: on the
     * P4 a task is pinned to the core it is on when it first uses the FPU. */
    if (xTaskCreatePinnedToCore(sfx_task, "sfx", 4096, NULL, 5, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* (documented in sfx.h) */
void sfx_play(sfx_id_t id)
{
    if (!s_queue || !s_enabled) {
        return;
    }
    uint8_t b = (uint8_t)id;
    xQueueSend(s_queue, &b, 0);
}

/* (documented in sfx.h) */
void sfx_set_enabled(bool on)
{
    s_enabled = on;
}

/* (documented in sfx.h) */
bool is_sfx_enabled(void)
{
    return s_enabled;
}

/* (documented in sfx.h) */
bool sfx_play_pcm(const int16_t *mono, size_t frames)
{
    if (!s_queue || s_pcm_busy || !frames) {
        return false;
    }
    s_pcm_len = frames;
    s_pcm = mono;
    s_pcm_busy = true;
    uint8_t b = SFX_PCM_ID;
    if (xQueueSend(s_queue, &b, pdMS_TO_TICKS(50)) != pdTRUE) {
        s_pcm = NULL;
        s_pcm_busy = false;
        return false;
    }
    return true;
}

/* (documented in sfx.h) */
bool is_sfx_pcm_busy(void)
{
    return s_pcm_busy;
}

/* (documented in sfx.h) */
uint32_t sfx_rate(void)
{
    return SFX_RATE;
}
