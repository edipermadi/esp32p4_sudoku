/**
 * @file sfx.h
 * @brief Sound effects: ticks, chimes, the buzz and the fanfare, and PCM clips.
 */
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

/** A sound effect. */
typedef enum {
    SFX_TOUCH = 0,      /**< short tick on any touch */
    SFX_DIGIT_DONE,     /**< all nine of a digit placed */
    SFX_ERROR,          /**< placed digit breaks a rule */
    SFX_SOLVED,         /**< puzzle complete */
    SFX_UNIT_DONE,      /**< a row, column or 3x3 box holds 1-9 */
    SFX_UNITS_DONE,     /**< two or three of them at once */
    SFX_COUNT
} sfx_id_t;

/**
 * @brief
 * @return ESP_OK, or why not
 */
/** @brief Brings up the speaker and starts the mixer task. */
esp_err_t sfx_init(void);
/**
 * @brief Plays a sound effect (mixed over any playing; nothing while sound is off).
 * @param id  which
 */
void sfx_play(sfx_id_t id);
/**
 * @brief Sound on or off (Settings).
 * @param on  on
 */
void sfx_set_enabled(bool on);
/**
 * @brief Sound is on.
 * @return true if so
 */
bool is_sfx_enabled(void);
/**
 * @brief
 * @param mono  the samples
 * @param frames  how many
 * @return true when it starts
 */
/** @brief Plays mono 16-bit PCM at sfx_rate() over the effects, whatever sfx_set_enabled
 * says (gibberlink's replies). One clip at a time; the caller keeps the samples
 * until is_sfx_pcm_busy() is false. false: one is still playing. */
bool sfx_play_pcm(const int16_t *mono, size_t frames);
/**
 * @brief A PCM clip (sfx_play_pcm()) is still playing.
 * @return true if so
 */
bool is_sfx_pcm_busy(void);
/**
 * @brief The speaker's sample rate (sfx_play_pcm()'s samples are at it).
 * @return Hz
 */
uint32_t sfx_rate(void);

#ifdef __cplusplus
}
#endif
