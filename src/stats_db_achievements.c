/**
 * @file stats_db_achievements.c
 * @brief Achievements: their definitions, progress, and unlocking.
 */
#include "stats_db_internal.h"

static stats_achievements_t *s_ach;  /**< CMD_ACH result (PSRAM) */
static bool s_ach_new;               /**< s_ach is new */
static char s_unlocked[160];         /**< titles unlocked by the last solve */
static int s_unlocked_n;             /**< how many */

/* ------------------------------------------------------------- achievements */
/*
 * Each one is a number worked out from the player's solves (?1 = player id)
 * and a goal; it is unlocked once the number reaches the goal. The streak
 * ones are worked out in C (longest run of days with the daily solved).
 * The keys are stored in the achievements table: don't rename them.
 */
#define HARD_OR_MORE "(level LIKE '%Hard' OR level LIKE '%Expert')"   /**< SQL: a Hard or Expert level */

#define CLASSIC_9X9  "(puzzle < 2000 OR " OF_VARIANT(0) ")"         /**< SQL: a Classic 9x9 puzzle */

/** SQL: how many different variants the player (?1) solved (the built-in collections are Classic) */
#define VARIANTS_SOLVED \
    "SELECT COUNT(*) FROM (SELECT 0 FROM solves WHERE player_id = ?1 AND puzzle < 2000" \
    " UNION SELECT g.variant FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id WHERE s.player_id = ?1)"

/** SQL: the days the player (?1) solved the daily puzzle of, oldest first */
#define DAILY_DAYS \
    "SELECT DISTINCT g.day FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id" \
    " WHERE s.player_id = ?1 AND g.day IS NOT NULL ORDER BY g.day"

/** The achievements (the keys are stored: don't rename them). */
static const ach_def_t ACH[] = {
    {
        "solve_1", "First steps", "Solve a puzzle.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1"
    },
    {
        "solve_10", "Getting the hang of it", "Solve 10 puzzles.", 10, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1"
    },
    {
        "solve_50", "Regular", "Solve 50 puzzles.", 50, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1"
    },
    {
        "solve_100", "Centurion", "Solve 100 puzzles.", 100, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1"
    },
    {
        "clean_1", "Clean sheet", "Solve a puzzle without a mistake or a hint.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0"
    },
    {
        "clean_25", "Steady hand", "Solve 25 puzzles without a mistake or a hint.", 25, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0"
    },
    {
        "hard_clean", "Hard and clean", "Solve a Hard, Very Hard or Expert puzzle without a mistake or a hint.", 1,
        ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0 AND " HARD_OR_MORE
    },
    {
        "expert", "Expert", "Solve an Expert puzzle (New puzzle or the Sunday daily).", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND level LIKE '%Expert'"
    },
    {
        "quick", "Quick thinker", "Solve a 9x9 puzzle in under 5 minutes.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND seconds < 300"
        " AND NOT " OF_VARIANT(2) " AND NOT " OF_VARIANT(11)          /* (not Mini 6x6, not Kids 4x4) */
    },
    {
        "fast_hard", "Speed demon", "Solve a Hard or harder puzzle in under 10 minutes, without hints.", 1,
        ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND seconds < 600 AND hints = 0 AND " HARD_OR_MORE
    },
    {
        "x_sudoku", "Crossed lines", "Solve an X-Sudoku.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(1)
    },
    {
        "mini", "Small wonder", "Solve a Mini 6x6.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(2)
    },
    {
        "variety", "Variety", "Solve a Classic, an X-Sudoku and a Mini 6x6.", 3, ACH_UNIT_COUNT,
        "SELECT EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " CLASSIC_9X9 ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(1) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(2) ")"
    },
    {
        "hyper", "Window cleaner", "Solve a Hyper Sudoku.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(3)
    },
    {
        "jigsaw", "Puzzle pieces", "Solve a Jigsaw Sudoku.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(4)
    },
    {
        "killer", "Killer instinct", "Solve a Killer Sudoku.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(5)
    },
    {
        "rule_bender", "Rule bender", "Solve an Anti-Knight, an Even-Odd and a Consecutive.", 3, ACH_UNIT_COUNT,
        "SELECT EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(6) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(7) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(8) ")"
    },
    {
        "thermo", "Feeling the heat", "Solve a Thermo Sudoku.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(9)
    },
    {
        "sandwich", "Lunch break", "Solve a Sandwich Sudoku.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(10)
    },
    {
        "variety_5", "Collector", "Solve each of the six original kinds: Classic, X-Sudoku, Hyper, Jigsaw, Killer and Mini 6x6.", 6,
        ACH_UNIT_COUNT,
        "SELECT EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " CLASSIC_9X9 ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(1) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(3) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(4) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(5) ")"
        " + EXISTS(SELECT 1 FROM solves WHERE player_id = ?1 AND " OF_VARIANT(2) ")"
    },
    {
        "fresh_10", "Fresh off the press", "Solve 10 puzzles made with New puzzle.", 10, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id"
        " WHERE s.player_id = ?1 AND g.day IS NULL AND g.source IS NULL"
    },
    {
        "daily_10", "Daily regular", "Solve the daily puzzle on 10 different days.", 10, ACH_UNIT_DAYS,
        "SELECT COUNT(*) FROM (" DAILY_DAYS ")"
    },
    {"streak_3", "On a roll", "Solve the daily puzzle 3 days in a row.", 3, ACH_UNIT_DAYS, NULL},
    {"streak_7", "Week streak", "Solve the daily puzzle 7 days in a row.", 7, ACH_UNIT_DAYS, NULL},
    {"streak_30", "Monthly habit", "Solve the daily puzzle 30 days in a row.", 30, ACH_UNIT_DAYS, NULL},
    {
        "explorer", "Explorer", "Solve a puzzle in each of the 16 built-in collections.", PUZZLE_COLLECTION_COUNT,
        ACH_UNIT_COUNT,
        "SELECT COUNT(DISTINCT level) FROM puzzle_stats WHERE player_id = ?1 AND puzzle < 2000"
    },
    {
        "collection", "Completionist", "Solve all 100 puzzles of one built-in collection.", 100, ACH_UNIT_COUNT,
        "SELECT COALESCE(MAX(n), 0) FROM (SELECT COUNT(*) AS n FROM puzzle_stats"
        " WHERE player_id = ?1 AND puzzle < 2000 GROUP BY level)"
    },
    {
        "marathon", "Marathon", "Spend 10 hours solving puzzles (solve times added up).", 600, ACH_UNIT_MINUTES,
        "SELECT CAST(TOTAL(seconds) / 60 AS INTEGER) FROM solves WHERE player_id = ?1"
    },
    {
        "night_owl", "Night owl", "Finish a puzzle between midnight and 5 am.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND finished_at IS NOT NULL"
        " AND CAST(strftime('%H', finished_at, 'unixepoch', 'localtime') AS INTEGER) < 5"
    },
    /* techniques: solves.flags bit k = a hint showed technique k (TECH_X_WING 10, TECH_SWORDFISH 11,
     * TECH_XY_WING 12); learned = Learn practices finished */
    {
        "graduate", "Graduate", "Finish a Learn practice of all ten techniques (place the digit each leads to).", 10,
        ACH_UNIT_COUNT, "SELECT COUNT(DISTINCT technique) FROM learned WHERE player_id = ?1 AND technique < 10"
    },
    {
        "fish", "Gone fishing", "Solve a puzzle where a hint showed an X-Wing or a Swordfish.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND (flags & 3072) != 0"
    },
    {
        "wing", "On a wing", "Solve a puzzle where a hint showed an XY-Wing.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND (flags & 4096) != 0"
    },
    {
        "expert_clean", "Unaided expert", "Solve an Expert puzzle without a mistake or a hint.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND mistakes = 0 AND hints = 0 AND level LIKE '%Expert'"
    },
    /* the no-notes challenge (GAME_FLAG_NO_NOTES = 1 << 20 = 1048576) */
    {
        "no_notes", "Bare hands", "Solve a puzzle in the no-notes challenge.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND (flags & 1048576) != 0"
    },
    {
        "no_notes_hard", "All in your head", "Solve a Hard or harder puzzle in the no-notes challenge.", 1,
        ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND (flags & 1048576) != 0 AND " HARD_OR_MORE
    },
    /* time attack: the runs (attack), checked again when one is recorded */
    {
        "attack_5", "Against the clock", "Solve 5 puzzles in one time attack.", 5, ACH_UNIT_COUNT,
        "SELECT COALESCE(MAX(solved), 0) FROM attack WHERE player_id = ?1"
    },
    {
        "attack_clean", "Cool under pressure", "Solve 8 puzzles in one time attack without a mistake.", 8,
        ACH_UNIT_COUNT,
        "SELECT COALESCE(MAX(solved), 0) FROM attack WHERE player_id = ?1 AND mistakes = 0"
    },
    {
        "variants_10", "Globetrotter", "Solve 10 different variants (Classic counts as one).", 10, ACH_UNIT_COUNT,
        VARIANTS_SOLVED
    },
    {
        "variants_all", "Rule book", "Solve every variant at least once.", VAR_COUNT, ACH_UNIT_COUNT,
        VARIANTS_SOLVED
    },
    {
        "kids", "Little steps", "Solve a Kids 4x4.", 1, ACH_UNIT_COUNT,
        "SELECT COUNT(*) FROM solves WHERE player_id = ?1 AND " OF_VARIANT(11)
    },
};

#define ACH_COUNT ((int)(sizeof(ACH) / sizeof(ACH[0])))   /**< how many */

_Static_assert(sizeof(ACH) / sizeof(ACH[0]) <= STATS_ACH_MAX, "raise STATS_ACH_MAX");

/**
 * @brief Days since 1970-01-01 of a date (proleptic Gregorian).
 * @param[in] d  'YYYY-MM-DD'
 * @return the days, or INT32_MIN if it isn't a date
 */
static int32_t day_number(const char *d)
{
    int y, m, dd;
    if (!d || sscanf(d, "%d-%d-%d", &y, &m, &dd) != 3) {
        return INT32_MIN;
    }
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* (documented in stats_db_internal.h) */
int sdb_longest_daily_streak(int32_t player)
{
    sqlite3_stmt *st;
    int best = 0, run = 0;
    int32_t prev = INT32_MIN;
    if (!sdb_db() || sqlite3_prepare_v2(sdb_db(), DAILY_DAYS, -1, &st, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(st, 1, player);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int32_t d = day_number((const char *)sqlite3_column_text(st, 0));
        if (d == INT32_MIN) {
            continue;
        }
        run = prev != INT32_MIN && d == prev + 1 ? run + 1 : 1;
        if (run > best) {
            best = run;
        }
        prev = d;
    }
    sqlite3_finalize(st);
    return best;
}

/* (documented in stats_db_internal.h) */
void sdb_ach_values(int32_t player, int32_t *val)
{
    int streak = -1;
    for (int i = 0; i < ACH_COUNT; i++) {
        val[i] = 0;
        if (!ACH[i].sql) {
            if (streak < 0) {
                streak = sdb_longest_daily_streak(player);
            }
            val[i] = streak;
            continue;
        }
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(sdb_db(), ACH[i].sql, -1, &st, NULL) != SQLITE_OK) {
            printf("stats_db: achievement %s: %s\n", ACH[i].key, sqlite3_errmsg(sdb_db()));
            continue;
        }
        sqlite3_bind_int(st, 1, player);
        if (sqlite3_step(st) == SQLITE_ROW) {
            val[i] = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
}

/* (documented in stats_db_internal.h) */
void sdb_ach_unlocked(int32_t player, int64_t *when)
{
    for (int i = 0; i < ACH_COUNT; i++) {
        when[i] = 0;
    }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT key, unlocked_at FROM achievements WHERE player_id = ?1", -1, &st,
                           NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(st, 1, player);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(st, 0);
        for (int i = 0; key && i < ACH_COUNT; i++) {
            if (strcmp(ACH[i].key, key) != 0) {
                continue;
            }
            int64_t t = sqlite3_column_type(st, 1) == SQLITE_NULL ? 0 : sqlite3_column_int64(st, 1);
            when[i] = t > 1 ? t : 1;
        }
    }
    sqlite3_finalize(st);
}

/**
 * @brief Writes a player's progress into the cache (achievement_progress).
 * @param[in] player  players.id
 * @param[in] val     sdb_ach_count() values, from sdb_ach_values()
 */
static void ach_cache_put(int32_t player, const int32_t *val)
{
    sqlite3_stmt *st = NULL;
    if (!sdb_exec("SAVEPOINT ach_cache")) {
        return;
    }
    bool ok = sqlite3_prepare_v2(sdb_db(), "INSERT OR REPLACE INTO achievement_progress(player_id, key, value)"
                                 " VALUES(?1, ?2, ?3)", -1, &st, NULL) == SQLITE_OK;
    for (int i = 0; ok && i < ACH_COUNT; i++) {
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, player);
        sqlite3_bind_text(st, 2, ACH[i].key, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 3, val[i]);
        ok = sdb_step_done(st, "achievement progress");
    }
    sqlite3_finalize(st);                                  /* (NULL: nothing) */
    if (!ok) {
        sdb_exec("ROLLBACK TO ach_cache");
    }
    sdb_exec("RELEASE ach_cache");
}

/**
 * @brief Reads a player's progress from the cache (achievement_progress).
 * @param[in] player  players.id
 * @param[out] val     gets sdb_ach_count() values
 * @return true if every achievement was there (false: a new firmware's, a new player: work them out)
 */
static bool ach_cache_get(int32_t player, int32_t *val)
{
    bool have[ACH_COUNT] = {false};
    int n = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT key, value FROM achievement_progress WHERE player_id = ?1", -1, &st,
                           NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(st, 1, player);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(st, 0);
        for (int i = 0; key && i < ACH_COUNT; i++) {
            if (!have[i] && strcmp(ACH[i].key, key) == 0) {
                val[i] = sqlite3_column_int(st, 1);
                have[i] = true;
                n++;
                break;
            }
        }
    }
    sqlite3_finalize(st);
    return n == ACH_COUNT;
}

/* (documented in stats_db_internal.h) */
void sdb_ach_sync(int32_t player, bool announce)
{
    if (!sdb_db()) {
        return;
    }
    int32_t val[ACH_COUNT];
    int64_t when[ACH_COUNT];
    sdb_ach_values(player, val);
    ach_cache_put(player, val);                            /* what the Trophies screen reads */
    sdb_ach_unlocked(player, when);
    time_t now = time(NULL);
    char titles[sizeof(s_unlocked)] = "";
    int n = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "INSERT OR IGNORE INTO achievements(player_id, key, unlocked_at) VALUES(?1, ?2, ?3)",
                           -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    for (int i = 0; i < ACH_COUNT; i++) {
        if (when[i] || val[i] < ACH[i].goal) {
            continue;
        }
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, player);
        sqlite3_bind_text(st, 2, ACH[i].key, -1, SQLITE_STATIC);
        if (announce && now > 1700000000) {
            sqlite3_bind_int64(st, 3, now);
        } else {
            sqlite3_bind_null(st, 3);
        }
        if (!sdb_step_done(st, "achievement")) {
            continue;
        }
        if (!announce) {
            continue;
        }
        size_t len = strlen(titles);
        snprintf(titles + len, sizeof(titles) - len, "%s%s", n ? ", " : "", ACH[i].title);
        n++;
    }
    sqlite3_finalize(st);
    if (n) {
        printf("stats_db: unlocked %s\n", titles);
        xSemaphoreTake(sdb_lock(), portMAX_DELAY);
        snprintf(s_unlocked, sizeof(s_unlocked), "%s", titles);
        s_unlocked_n = n;
        xSemaphoreGive(sdb_lock());
    }
}

/* (documented in stats_db_internal.h) */
void sdb_ach_backfill(void)
{
    int32_t ids[STATS_MAX_PLAYERS];
    int n = 0;
    sqlite3_stmt *st;
    if (!sdb_db() || sqlite3_prepare_v2(sdb_db(), "SELECT id FROM players", -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    while (n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
        ids[n++] = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    for (int i = 0; i < n; i++) {
        sdb_ach_sync(ids[i], false);
    }
}

/* (documented in stats_db_internal.h) */
void sdb_do_achievements(void)
{
    static stats_achievements_t *ap;
    if (!sdb_psram_buf((void **)&ap, sizeof(*ap)) || !sdb_psram_buf((void **)&s_ach, sizeof(*s_ach))) {
        return;
    }
    stats_achievements_t *const a = ap;
    memset(a, 0, sizeof(*a));
    a->player_id = sdb_player();
    if (sdb_db()) {
        /* the progress cached by the last sdb_ach_sync() (at open, after a solve, a Learn practice, a
         * time attack run); only when it's missing are they worked out (and recorded) here */
        int32_t val[ACH_COUNT];
        int64_t when[ACH_COUNT];
        if (!ach_cache_get(sdb_player(), val)) {
            sdb_ach_sync(sdb_player(), false);
            if (!ach_cache_get(sdb_player(), val)) {
                sdb_ach_values(sdb_player(), val);
            }
        }
        sdb_ach_unlocked(sdb_player(), when);
        for (int i = 0; i < ACH_COUNT; i++) {
            stats_ach_t *x = &a->a[a->n++];
            x->title = ACH[i].title;
            x->desc = ACH[i].desc;
            x->goal = ACH[i].goal;
            x->unit = ACH[i].unit;
            x->unlocked = when[i] != 0;
            x->unlocked_at = when[i] > 1 ? when[i] : 0;
            x->value = x->unlocked || val[i] > ACH[i].goal ? ACH[i].goal : val[i];
            a->unlocked += x->unlocked;
        }
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_ach = *a;
    s_ach_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db.h) */
void stats_db_query_achievements(void)
{
    cmd_t c = {.type = CMD_ACH};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_achievements(stats_achievements_t *out)
{
    if (!sdb_lock() || !s_ach) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_ach_new;
    if (fresh) {
        *out = *s_ach;
    }
    s_ach_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db.h) */
bool stats_db_take_unlocked(char *buf, int len, int *count)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool any = s_unlocked_n > 0;
    if (any) {
        snprintf(buf, len, "%s", s_unlocked);
        if (count) {
            *count = s_unlocked_n;
        }
    }
    s_unlocked_n = 0;
    xSemaphoreGive(sdb_lock());
    return any;
}

/* (documented in stats_db_internal.h) */
const ach_def_t *sdb_ach(int i)
{
    return &ACH[i];
}

/* (documented in stats_db_internal.h) */
int sdb_ach_count(void)
{
    return ACH_COUNT;
}
