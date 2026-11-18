#pragma once
/*
 * Synthesised sound effects on the board speaker. sfx_play() only queues
 * the effect; a background task mixes up to SFX_VOICES effects at once, so
 * it is safe to call from LVGL callbacks.
 */
#include <stdbool.h>
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

#ifdef __cplusplus
}
#endif
