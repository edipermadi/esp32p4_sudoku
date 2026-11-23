/**
 * @file store.cpp
 * @brief The settings kept in RAM (their copy in sudoku.db), read from the old NVS keys once; reset.
 *        The statistics: store_stats.cpp; the game in progress: store_game.cpp.
 */
#include "store.h"
#include "store_internal.h"
#include <Preferences.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "stats_db.h"
#include "puzzles.h"
#include "esp_heap_caps.h"

/** @brief Legacy NVS layout, namespace "sudoku" (read once at start-up, never written):
 *   "best" : uint16_t[PUZZLE_COUNT] best time in seconds (0 = unsolved, capped at 65535)
 *   "stat" : store_stat_t: fewest mistakes + times solved per puzzle
 *   "game" : saved_game_t (older saves lack the trailing "mistakes")
 *   "coll" : uint8_t last collection
 *   "snd"  : bool sound effects on
 *   "dark" : bool dark colour scheme
 *   "sleep": uint8_t screen timeout in minutes (0 = never)
 *   "tz"   : string, IANA time zone name
 * All of it now lives in sudoku.db (stats_db.c); Wi-Fi keeps its own NVS
 * namespace (wifi_mgr.cpp). */
/** the settings on a fresh board */
static const store_prefs_t DEFAULT_PREFS = {
    .game = {
        .id = STORE_NO_GAME, .val = {}, .notes = {}, .elapsed_ms = 0, .mistakes = 0, .started_at = 0,
        .hints = 0, .colors = {}, .flags = 0
    },
    .last_collection = 0,
    .dark_mode = true,
    .sound = true,
    .screen_timeout_min = 5,
    .brightness = 100,
    .volume = 70,
    .assist = ASSIST_RULES,
    .show_timer = true,
    .accent = 0,
    .rules_seen = 0,
    .lock_mode = LOCK_OFF,
    .lock_pin = "",
    .saver = SAVER_MODE_DARK,
    .saver_min = 30,
    .cvd = false,
    .no_notes = false,
    .left_hand = false,
    .gibberlink = false,
    .big_digits = false,
    .hint_nudge = false,
    .night_mode = false,
    .night_start = 22,
    .night_end = 7,
    .night_brightness = 20,
    .timezone = "UTC",
    .player_id = 0,
    .player_name = "Player 1",
};
static store_prefs_t s_p = DEFAULT_PREFS;  /**< the settings and the game in progress */

/* (documented in store.h) */
void store_init(void)
{
    store_stats_init();                             /* (store_stats.cpp: "best", "stat") */
    store_game_init();                              /* (store_game.cpp: "game") */
    Preferences prefs;
    if (!prefs.begin("sudoku", true)) {
        return;    /* read only; absent on a fresh board */
    }
    uint8_t c = prefs.getUChar("coll", 0);
    s_p.last_collection = c < PUZZLE_COLLECTION_COUNT ? c : 0;
    s_p.dark_mode = prefs.getBool("dark", true);
    s_p.sound = prefs.getBool("snd", true);
    s_p.screen_timeout_min = prefs.getUChar("sleep", 5);
    if (!prefs.getString("tz", s_p.timezone, sizeof(s_p.timezone))) {
        strcpy(s_p.timezone, "UTC");
    }
    prefs.end();
}

/* (documented in store_internal.h) */
store_prefs_t *store_p(void)
{
    return &s_p;
}


/* (documented in store.h) */
void store_factory_reset(void)
{
    s_p = DEFAULT_PREFS;
    store_game_forget_moves();
    store_reset_stats();
    Preferences prefs;
    if (prefs.begin("sudoku", false)) {        /* the one NVS write left: erase the old keys */
        prefs.clear();
        prefs.end();
    }
}



/* (documented in store.h) */
void store_get_prefs(store_prefs_t *out)
{
    *out = s_p;
}

void store_apply_prefs(const store_prefs_t *in)
{
    s_p = *in;
    if (!puzzle_id_valid(s_p.game.id)) {
        s_p.game.id = STORE_NO_GAME;
    }
    store_load_game_moves();
    if (s_p.last_collection >= PUZZLE_COLLECTION_COUNT) {
        s_p.last_collection = 0;
    }
}

/* (documented in store.h) */
int32_t store_player_id(void)
{
    return s_p.player_id;
}

/* (documented in store.h) */
const char *store_player_name(void)
{
    return s_p.player_name;
}

/* (documented in store.h) */
void store_set_player(int32_t id, const char *name)
{
    s_p.player_id = id;
    snprintf(s_p.player_name, sizeof(s_p.player_name), "%s", name);
}

/* (documented in store.h) */
uint8_t store_last_collection(void)
{
    return s_p.last_collection;
}

/* (documented in store.h) */
void store_set_last_collection(uint8_t c)
{
    if (c >= PUZZLE_COLLECTION_COUNT || c == s_p.last_collection) {
        return;
    }
    s_p.last_collection = c;
    stats_db_set_text("last_level", puzzle_collection(c)->name);
}

/* (documented in store.h) */
bool is_store_dark_mode(void)
{
    return s_p.dark_mode;
}

/* (documented in store.h) */
void store_set_dark_mode(bool dark)
{
    s_p.dark_mode = dark;
    stats_db_set_int("dark_mode", dark);
}

/* (documented in store.h) */
bool is_store_sound_enabled(void)
{
    return s_p.sound;
}

/* (documented in store.h) */
void store_set_sound_enabled(bool on)
{
    s_p.sound = on;
    stats_db_set_int("sound", on);
}

/* (documented in store.h) */
const char *store_timezone(void)
{
    return s_p.timezone;
}

/* (documented in store.h) */
void store_set_timezone(const char *name)
{
    snprintf(s_p.timezone, sizeof(s_p.timezone), "%s", name);
    stats_db_set_text("time_zone", s_p.timezone);
}

/* (documented in store.h) */
uint8_t store_brightness(void)
{
    return s_p.brightness < 10 ? 10 : (s_p.brightness > 100 ? 100 : s_p.brightness);
}

/* (documented in store.h) */
void store_set_brightness(uint8_t percent)
{
    s_p.brightness = percent;
    stats_db_set_int("brightness", percent);
}

/* (documented in store.h) */
uint8_t store_volume(void)
{
    return s_p.volume > 100 ? 100 : s_p.volume;
}

/* (documented in store.h) */
void store_set_volume(uint8_t percent)
{
    s_p.volume = percent;
    stats_db_set_int("volume", percent);
}

/* (documented in store.h) */
uint8_t store_assist(void)
{
    return s_p.assist <= ASSIST_SOLUTION ? s_p.assist : (uint8_t)ASSIST_RULES;
}

/* (documented in store.h) */
void store_set_assist(uint8_t assist)
{
    s_p.assist = assist;
    stats_db_set_int("assist", assist);
}

/* (documented in store.h) */
bool is_store_show_timer(void)
{
    return s_p.show_timer;
}

/* (documented in store.h) */
void store_set_show_timer(bool on)
{
    s_p.show_timer = on;
    stats_db_set_int("show_timer", on);
}

/* (documented in store.h) */
uint8_t store_accent(void)
{
    return s_p.accent < ACCENT_COUNT ? s_p.accent : 0;
}

/* (documented in store.h) */
void store_set_accent(uint8_t accent)
{
    s_p.accent = accent;
    stats_db_set_int("accent", accent);
}

/* (documented in store.h) */
uint8_t store_lock_mode(void)
{
    if (s_p.lock_mode == LOCK_PIN && strlen(s_p.lock_pin) != 4) {
        return LOCK_SWIPE;    /* (no PIN set: swipe) */
    }
    return s_p.lock_mode <= LOCK_PIN ? s_p.lock_mode : (uint8_t)LOCK_OFF;
}

/* (documented in store.h) */
void store_set_lock_mode(uint8_t mode)
{
    s_p.lock_mode = mode;
    stats_db_set_int("lock_mode", mode);
}

const char *store_lock_pin(void)
{
    return s_p.lock_pin;
}

/* (documented in store.h) */
void store_set_lock_pin(const char *pin)
{
    snprintf(s_p.lock_pin, sizeof(s_p.lock_pin), "%s", pin ? pin : "");
    stats_db_set_text("lock_pin", s_p.lock_pin);
}

/* (documented in store.h) */
uint8_t store_saver_mode(void)
{
    return s_p.saver;
}

/* (documented in store.h) */
void store_set_saver_mode(uint8_t mode)
{
    s_p.saver = mode < SAVER_MODE_COUNT ? mode : SAVER_MODE_DARK;
    stats_db_set_int("saver_clock", s_p.saver);   /* (the key from when it was the clock or not) */
}

/* (documented in store.h) */
uint8_t store_saver_minutes(void)
{
    return s_p.saver_min;
}

/* (documented in store.h) */
void store_set_saver_minutes(uint8_t min)
{
    s_p.saver_min = min;
    stats_db_set_int("saver_minutes", min);
}

/* (documented in store.h) */
bool is_store_cvd(void)
{
    return s_p.cvd;
}

/* (documented in store.h) */
void store_set_cvd(bool on)
{
    s_p.cvd = on;
    stats_db_set_int("cvd", on);
}

/* (documented in store.h) */
bool is_store_no_notes(void)
{
    return s_p.no_notes;
}

/* (documented in store.h) */
void store_set_no_notes(bool on)
{
    s_p.no_notes = on;
    stats_db_set_int("no_notes", on);
}

/* (documented in store.h) */
bool is_store_left_hand(void)
{
    return s_p.left_hand;
}

/* (documented in store.h) */
void store_set_left_hand(bool on)
{
    s_p.left_hand = on;
    stats_db_set_int("left_hand", on);
}

/* (documented in store.h) */
bool is_store_gibberlink(void)
{
    return s_p.gibberlink;
}

/* (documented in store.h) */
void store_set_gibberlink(bool on)
{
    s_p.gibberlink = on;
    stats_db_set_int("gibberlink", on);
}

/* (documented in store.h) */
bool is_store_hint_nudge(void)
{
    return s_p.hint_nudge;
}

/* (documented in store.h) */
void store_set_hint_nudge(bool on)
{
    s_p.hint_nudge = on;
    stats_db_set_int("hint_nudge", on);
}

/* (documented in store.h) */
bool is_store_big_digits(void)
{
    return s_p.big_digits;
}

/* (documented in store.h) */
void store_set_big_digits(bool on)
{
    s_p.big_digits = on;
    stats_db_set_int("big_digits", on);
}

/* (documented in store.h) */
bool is_store_night_mode(void)
{
    return s_p.night_mode;
}

/* (documented in store.h) */
void store_set_night_mode(bool on)
{
    s_p.night_mode = on;
    stats_db_set_int("night_mode", on);
}

/* (documented in store.h) */
uint8_t store_night_start(void)
{
    return s_p.night_start < 24 ? s_p.night_start : 22;
}

/* (documented in store.h) */
uint8_t store_night_end(void)
{
    return s_p.night_end < 24 ? s_p.night_end : 7;
}

/* (documented in store.h) */
void store_set_night_hours(uint8_t start, uint8_t end)
{
    s_p.night_start = start;
    s_p.night_end = end;
    stats_db_set_int("night_start", start);
    stats_db_set_int("night_end", end);
}

/* (documented in store.h) */
uint8_t store_night_brightness(void)
{
    return s_p.night_brightness;
}

/* (documented in store.h) */
void store_set_night_brightness(uint8_t percent)
{
    s_p.night_brightness = percent;
    stats_db_set_int("night_brightness", percent);
}

/* (documented in store.h) */
uint64_t store_rules_seen(void)
{
    return s_p.rules_seen;
}

/* (documented in store.h) */
void store_set_rules_seen(uint64_t mask)
{
    s_p.rules_seen = mask;
    stats_db_set_int("rules_seen", (int64_t)mask);
}

/* (documented in store.h) */
uint8_t store_screen_timeout_min(void)
{
    return s_p.screen_timeout_min;
}

/* (documented in store.h) */
void store_set_screen_timeout_min(uint8_t min)
{
    s_p.screen_timeout_min = min;
    stats_db_set_int("screen_off_minutes", min);
}
