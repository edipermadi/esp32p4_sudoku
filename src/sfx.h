#pragma once
/*
 * Synthesised sound effects on the board speaker. sfx_play() only queues
 * the effect; a background task mixes up to SFX_VOICES effects at once, so
 * it is safe to call from LVGL callbacks.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SFX_TOUCH = 0,      /* short tick on any touch */
    SFX_DIGIT_DONE,     /* all nine of a digit placed */
    SFX_ERROR,          /* placed digit breaks a rule */
    SFX_SOLVED,         /* puzzle complete */
    SFX_UNIT_DONE,      /* a row, column or 3x3 box holds 1-9 */
    SFX_UNITS_DONE,     /* two or three of them at once */
    SFX_COUNT
} sfx_id_t;

/* Brings up the speaker and starts the mixer task. */
esp_err_t sfx_init(void);
void sfx_play(sfx_id_t id);
void sfx_set_enabled(bool on);
bool sfx_enabled(void);
/* Plays mono 16-bit PCM at sfx_rate() over the effects, whatever sfx_set_enabled
 * says (gibberlink's replies). One clip at a time; the caller keeps the samples
 * until sfx_pcm_busy() is false. false: one is still playing. */
bool sfx_play_pcm(const int16_t *mono, size_t frames);
bool sfx_pcm_busy(void);
uint32_t sfx_rate(void);

#ifdef __cplusplus
}
#endif
