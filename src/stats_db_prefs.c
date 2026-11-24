/**
 * @file stats_db_prefs.c
 * @brief The settings table: the game's preferences, read at open and written as they change.
 */
#include "stats_db_internal.h"

static store_prefs_t s_prefs_out;       /**< the settings read at open (stats_db_take_prefs()) */
static bool s_prefs_new;                /**< they are new */

/* (documented in stats_db_internal.h) */
void sdb_set_setting(const char *key, bool is_text, int64_t ival, const char *text)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "INSERT INTO settings(key, value) VALUES(?1, ?2)"
                           " ON CONFLICT(key) DO UPDATE SET value = excluded.value", -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    if (is_text) {
        sqlite3_bind_text(st, 2, text, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_int64(st, 2, ival);
    }
    sdb_step_done(st, "setting");
    sqlite3_finalize(st);
}

/* (documented in stats_db_internal.h) */
void sdb_load_prefs(store_prefs_t *p)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT key, value FROM settings", -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *k = (const char *)sqlite3_column_text(st, 0);
            if (!k) {
                continue;
            }
            int v = sqlite3_column_int(st, 1);
            const char *t = (const char *)sqlite3_column_text(st, 1);
            if (!strcmp(k, "dark_mode")) {
                p->dark_mode = v != 0;
            } else if (!strcmp(k, "sound")) {
                p->sound = v != 0;
            } else if (!strcmp(k, "screen_off_minutes")) {
                p->screen_timeout_min = v < 0 ? 0 : (v > 255 ? 255 : v);
            } else if (!strcmp(k, "brightness")) {
                p->brightness = v < 10 ? 10 : (v > 100 ? 100 : v);
            } else if (!strcmp(k, "volume")) {
                p->volume = v < 0 ? 0 : (v > 100 ? 100 : v);
            } else if (!strcmp(k, "assist")) {
                p->assist = v >= ASSIST_OFF && v <= ASSIST_SOLUTION ? v : ASSIST_RULES;
            } else if (!strcmp(k, "show_timer")) {
                p->show_timer = v != 0;
            } else if (!strcmp(k, "accent")) {
                p->accent = v >= 0 && v < ACCENT_COUNT ? v : 0;
            } else if (!strcmp(k, "rules_seen")) {
                /* (older builds kept 32 bits, as an int: a negative one comes back with its top 32 bits all set.
                 * A 64-bit mask with variant 63's bit is negative too, but keeps its own top bits) */
                int64_t m = sqlite3_column_int64(st, 1);
                p->rules_seen = m < 0 && (uint64_t)m >> 32 == 0xFFFFFFFFu ? (uint64_t)m & 0xFFFFFFFFu : (uint64_t)m;
            } else if (!strcmp(k, "rules_seen_hi")) {
                p->rules_seen_hi = (uint64_t)sqlite3_column_int64(st, 1);
            } else if (!strcmp(k, "lock_mode")) {
                p->lock_mode = v >= LOCK_OFF && v <= LOCK_PIN ? v : LOCK_OFF;
            } else if (!strcmp(k, "lock_pin") && t) {
                snprintf(p->lock_pin, sizeof(p->lock_pin), "%s", t);
            } else if (!strcmp(k, "saver_clock")) {
                p->saver = v >= SAVER_MODE_DARK && v < SAVER_MODE_COUNT ? v : SAVER_MODE_DARK;
            } else if (!strcmp(k, "saver_minutes")) {
                p->saver_min = v < 0 ? 0 : (v > 255 ? 255 : v);
            } else if (!strcmp(k, "cvd")) {
                p->cvd = v != 0;
            } else if (!strcmp(k, "no_notes")) {
                p->no_notes = v != 0;
            } else if (!strcmp(k, "left_hand")) {
                p->left_hand = v != 0;
            } else if (!strcmp(k, "gibberlink")) {
                p->gibberlink = v != 0;
            } else if (!strcmp(k, "big_digits")) {
                p->big_digits = v != 0;
            } else if (!strcmp(k, "hint_nudge")) {
                p->hint_nudge = v != 0;
            } else if (!strcmp(k, "night_mode")) {
                p->night_mode = v != 0;
            } else if (!strcmp(k, "night_start")) {
                p->night_start = v >= 0 && v < 24 ? v : 22;
            } else if (!strcmp(k, "night_end")) {
                p->night_end = v >= 0 && v < 24 ? v : 7;
            } else if (!strcmp(k, "night_brightness")) {
                p->night_brightness = v < 10 ? 10 : (v > 100 ? 100 : v);
            } else if (!strcmp(k, "time_zone") && t) {
                snprintf(p->timezone, sizeof(p->timezone), "%s", t);
            } else if (!strcmp(k, "last_level") && t) {
                for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
                    if (!strcmp(puzzle_collection(c)->name, t)) {
                        p->last_collection = c;
                    }
                }
            }
        }
        sqlite3_finalize(st);
    }
    sdb_load_game(&p->game);
    p->player_id = sdb_player();
    snprintf(p->player_name, sizeof(p->player_name), "%s", sdb_player_name());
}

/* (documented in stats_db_internal.h) */
void sdb_migrate_prefs(const store_prefs_t *p)
{
    sdb_exec("BEGIN");
    sdb_set_setting("dark_mode", false, p->dark_mode, NULL);
    sdb_set_setting("sound", false, p->sound, NULL);
    sdb_set_setting("screen_off_minutes", false, p->screen_timeout_min, NULL);
    sdb_set_setting("brightness", false, p->brightness, NULL);
    sdb_set_setting("volume", false, p->volume, NULL);
    sdb_set_setting("assist", false, p->assist, NULL);
    sdb_set_setting("show_timer", false, p->show_timer, NULL);
    sdb_set_setting("accent", false, p->accent, NULL);
    sdb_set_setting("rules_seen", false, (int64_t)p->rules_seen, NULL);
    sdb_set_setting("rules_seen_hi", false, (int64_t)p->rules_seen_hi, NULL);
    sdb_set_setting("lock_mode", false, p->lock_mode, NULL);
    sdb_set_setting("lock_pin", true, 0, p->lock_pin);
    sdb_set_setting("saver_clock", false, p->saver, NULL);
    sdb_set_setting("saver_minutes", false, p->saver_min, NULL);
    sdb_set_setting("cvd", false, p->cvd, NULL);
    sdb_set_setting("no_notes", false, p->no_notes, NULL);
    sdb_set_setting("left_hand", false, p->left_hand, NULL);
    sdb_set_setting("gibberlink", false, p->gibberlink, NULL);
    sdb_set_setting("big_digits", false, p->big_digits, NULL);
    sdb_set_setting("hint_nudge", false, p->hint_nudge, NULL);
    sdb_set_setting("night_mode", false, p->night_mode, NULL);
    sdb_set_setting("night_start", false, p->night_start, NULL);
    sdb_set_setting("night_end", false, p->night_end, NULL);
    sdb_set_setting("night_brightness", false, p->night_brightness, NULL);
    sdb_set_setting("time_zone", true, 0, p->timezone);
    sdb_set_setting("last_level", true, 0, puzzle_collection(p->last_collection < PUZZLE_COLLECTION_COUNT
                    ? p->last_collection : 0)->name);
    if (puzzle_id_valid(p->game.id)) {
        sdb_save_game(&p->game, NULL, 0);    /* (none after a factory reset) */
    }
    sdb_set_setting("migrated_from_nvs", false, 1, NULL);
    sdb_exec("COMMIT");
}

/* (documented in stats_db.h) */
bool stats_db_take_prefs(store_prefs_t *out)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_prefs_new;
    if (fresh) {
        *out = s_prefs_out;
    }
    s_prefs_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
void stats_db_set_int(const char *key, int64_t value)
{
    cmd_t c = {.type = CMD_SET, .is_text = false, .ival = value};
    snprintf(c.key, sizeof(c.key), "%s", key);
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_set_text(const char *key, const char *value)
{
    cmd_t c = {.type = CMD_SET, .is_text = true};
    snprintf(c.key, sizeof(c.key), "%s", key);
    snprintf(c.text, sizeof(c.text), "%s", value);
    sdb_send(&c);
}

/* (documented in stats_db_internal.h) */
void sdb_prefs_post(const store_prefs_t *p)
{
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_prefs_out = *p;
    s_prefs_new = true;
    xSemaphoreGive(sdb_lock());
}
