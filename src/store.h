/**
 * @file store.h
 * @brief The settings, the statistics in RAM, and the game in progress.
 */
#pragma once
/*
 * Game state the UI reads at once: statistics, the game in progress and the
 * settings. They are kept in sudoku.db on the microSD card (stats_db.h);
 * this is their RAM copy. Setters update RAM and queue the database write.
 *
 * Start-up: store_init() fills RAM from the NVS keys earlier versions wrote
 * (read only: nothing here writes NVS any more), then store_apply_prefs()
 * overrides that with what sudoku.db holds. Without a card, changes last
 * until power-off. (Wi-Fi keeps its network in NVS, see wifi_mgr.cpp.)
 */
#include <stdbool.h>
#include <stdint.h>
#include "moves.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_NO_GAME 0xFFFF            /**< no game (saved_game_t.id) */
#define STORE_MISTAKES_UNKNOWN 0xFF   /**< solved before mistakes were counted */

/** A game in progress: the board, pencil marks, clock, mistakes, hints and colours. */
typedef struct {
    uint16_t id;               /**< puzzle index, STORE_NO_GAME = none */
    uint8_t  val[81];          /**< 0 = empty; givens included */
    uint16_t notes[81];        /**< bit d (1..9) = pencil mark d */
    uint32_t elapsed_ms;                /**< the game's clock, ms (pauses excluded) */
    uint16_t mistakes;         /**< rule-breaking digits placed so far */
    int64_t  started_at;       /**< Unix time the puzzle was opened (or restarted), 0 = unknown */
    uint16_t hints;            /**< cells filled with Fill (the solution) so far */
    uint8_t  colors[81];       /**< cell colours (0 = none, 1..CELL_COLORS) */
    uint32_t flags;            /**< GAME_FLAG_*, and bit k = a hint showed technique k (tech_kind_t) */
} saved_game_t;

/** @brief saved_game_t.flags (also solves.flags in sudoku.db): bits 0..15 are the techniques
 * (1 << TECH_*, generator.h) the game's hints explained; above them: */
#define GAME_FLAG_NO_NOTES (1u << 20)   /**< the no-notes challenge: Notes and Auto off */
#define GAME_FLAG_TECHS    0xFFFFu      /**< the techniques' bits */
#define CELL_COLORS 6                /**< cell colours offered (colors: 1..CELL_COLORS) */

/**
 * @brief Reads what earlier versions kept in NVS (best times, statistics), if anything.
 */
void store_init(void);

/**
 * @brief Best time.
 * @param[in] id  the puzzle
 * @return seconds, 0 = never solved
 */
uint32_t store_best_time(uint16_t id);
/**
 * @brief Records a solve in RAM (stats_db_record_solve() saves it).
 * @param[in] id  the puzzle
 * @param[in] seconds  its time
 * @param[in] mistakes  its mistakes
 * @return true if it is a new best time (or the first solve)
 */
bool store_record_solve(uint16_t id, uint32_t seconds, uint32_t mistakes);
/**
 * @brief Merges per-puzzle statistics into the RAM copy (best: lower wins, fewest:
 * lower wins, solves: higher wins). Arrays of PUZZLE_COUNT; best 0 = unsolved.
 * @param[in] best  best times, s
 * @param[in] fewest  fewest mistakes
 * @param[in] solves  solve counts
 */
void store_merge_stats(const uint16_t *best, const uint8_t *fewest, const uint8_t *solves);
/**
 * @brief Copies the RAM statistics out (same layout).
 * @param[out] best  gets best times, s
 * @param[out] fewest  gets fewest mistakes
 * @param[out] solves  gets solve counts
 */
void store_copy_stats(uint16_t *best, uint8_t *fewest, uint8_t *solves);
/** @brief Forgets all statistics in RAM (the card was formatted). */
void store_reset_stats(void);
/**
 * @brief Fewest mistakes over all solves (capped at 254).
 * @param[in] id  the puzzle
 * @return them, STORE_MISTAKES_UNKNOWN if every solve predates mistake counting or the puzzle is unsolved
 */
uint8_t store_fewest_mistakes(uint16_t id);
/**
 * @brief Times solved.
 * @param[in] id  the puzzle
 * @return how many (capped at 255), 0 = never
 */
uint8_t store_solve_count(uint16_t id);
/**
 * @brief Solved puzzles among [first, first+count).
 * @param[in] first  the first puzzle
 * @param[in] count  how many
 * @return the solved ones
 */
uint16_t store_solved_count(uint16_t first, uint16_t count);

/**
 * @brief The game being played (sudoku.db keeps a few more unfinished ones, see
 * stats_db_list_games(); without a card this is the only one).
 * @return it (id == STORE_NO_GAME if none)
 */
const saved_game_t *store_saved_game(void);
/**
 * @brief Updates the in-memory copy only; store_commit_game() queues the save.
 * @param[in] g  the game
 */
void store_set_game(const saved_game_t *g);
/**
 * @brief Queues the game in progress (and its moves) to be saved in sudoku.db.
 */
void store_commit_game(void);
void store_clear_game(void);                  /**< memory + queued delete */
/**
 * @brief Move log of the game in progress (RAM; store_commit_game() saves it with
 * the game). store_load_game_moves(): the log sudoku.db has for the saved
 * game, after the game itself came from there (start-up, another player).
 * @param[in] m  the moves
 * @param[in] n  how many
 */
void store_set_game_moves(const game_move_t *m, int n);
/**
 * @brief The move log of the game in progress.
 * @param[out] out  gets it
 * @return its moves
 */
int store_game_moves(const game_move_t **out);
/**
 * @brief Takes the move log sudoku.db has for the saved game.
 */
void store_load_game_moves(void);

#define STORE_TZ_LEN 48                       /**< a time zone's name, incl. the terminator */
#define STORE_NAME_LEN 24                     /**< player name, incl. the terminator */
/** @brief Everything except statistics, as one block: what sudoku.db is seeded with
 * on first use, and what it hands back at start-up. */
typedef struct {
    saved_game_t game;                        /**< game.id == STORE_NO_GAME: none */
    uint8_t last_collection;            /**< the collection last shown in Puzzles */
    bool dark_mode;                           /**< the dark theme */
    bool sound;                               /**< sound effects on */
    uint8_t screen_timeout_min;         /**< minutes without a touch before the screen turns off, 0 = never */
    uint8_t brightness;                       /**< backlight, 10..100 % */
    uint8_t volume;                           /**< speaker, 0..100 % */
    uint8_t assist;                           /**< store_assist_t: what turns red */
    bool show_timer;                          /**< the game's clock is shown (it always runs) */
    uint8_t accent;                           /**< accent colour, ACCENT_* (0 = blue) */
    uint64_t rules_seen;                      /**< bit v: variant v's rules card has been shown */
    uint8_t lock_mode;                        /**< store_lock_t: the lock screen after a screen timeout */
    char lock_pin[8];                         /**< 4 digits (PIN mode), "" = none set */
    uint8_t saver;                            /**< store_saver_t: what shows before the screen goes dark */
    bool cvd;                                 /**< colour-blind friendly colours (Okabe-Ito) */
    bool no_notes;                            /**< new games start in the no-notes challenge */
    bool left_hand;                           /**< the game's number pad on the left of the board */
    bool gibberlink;                          /**< listen for sound commands (gibberlink.h) */
    bool big_digits;                          /**< larger digits on the board (and pencil marks) */
    bool hint_nudge;                          /**< Hints: only where to look (no More / Place it) */
    bool night_mode;                          /**< dark and dimmer between night_start and night_end */
    uint8_t night_start;                      /**< night starts, hour 0..23, local time */
    uint8_t night_end;                        /**< night ends, hour 0..23, local time */
    uint8_t night_brightness;                 /**< backlight at night, 10..100 % (at most the day's) */
    char timezone[STORE_TZ_LEN];        /**< the time zone, IANA ("UTC" until one is chosen) */
    int32_t player_id;                        /**< who is playing (players.id in sudoku.db) */
    char player_name[STORE_NAME_LEN];   /**< who is playing: the name */
} store_prefs_t;
/**
 * @brief The settings and the game in progress, as one block.
 * @param[out] out  gets them
 */
void store_get_prefs(store_prefs_t *out);
/**
 * @brief Takes the settings from sudoku.db (RAM only).
 * @param[in] in  them
 */
void store_apply_prefs(const store_prefs_t *in);

/** @brief Factory reset of the board side: settings back to the defaults, no game in
 * progress, no statistics in RAM, and the NVS keys earlier versions wrote
 * erased (so they can't come back). sudoku.db: stats_db_factory_reset(). */
void store_factory_reset(void);

/**
 * @brief Who is playing (RAM; stats_db_switch_player() etc. change it in sudoku.db).
 * @return players.id in sudoku.db
 */
int32_t store_player_id(void);
/**
 * @brief Who is playing: the name.
 * @return it
 */
const char *store_player_name(void);
/**
 * @brief Who is playing (RAM only).
 * @param[in] id  players.id
 * @param[in] name  the name
 */
void store_set_player(int32_t id, const char *name);

/**
 * @brief The collection last shown in Puzzles.
 * @return it
 */
uint8_t store_last_collection(void);
/**
 * @brief Sets it.
 * @param[in] c  the collection
 */
void store_set_last_collection(uint8_t c);

/**
 * @brief The dark theme is on.
 * @return true if so
 */
bool is_store_dark_mode(void);
/**
 * @brief Turns the dark theme on or off.
 * @param[in] dark  on
 */
void store_set_dark_mode(bool dark);

/**
 * @brief Sound effects are on.
 * @return true if so
 */
bool is_store_sound_enabled(void);
/**
 * @brief Turns sound effects on or off.
 * @param[in] on  on
 */
void store_set_sound_enabled(bool on);

/**
 * @brief IANA time zone name, e.g. "Asia/Dubai"; "UTC" until one is chosen.
 * @return the name
 */
const char *store_timezone(void);
/**
 * @brief Sets the time zone.
 * @param[in] name  its IANA name
 */
void store_set_timezone(const char *name);

/**
 * @brief Minutes without a touch before the screen turns off; 0 = never.
 * @return minutes
 */
uint8_t store_screen_timeout_min(void);
/**
 * @brief Sets it.
 * @param[in] min  minutes, 0 = never
 */
void store_set_screen_timeout_min(uint8_t min);

/**
 * @brief Backlight.
 * @return 10..100 %
 */
uint8_t store_brightness(void);
/**
 * @brief Sets it.
 * @param[in] percent  10..100
 */
void store_set_brightness(uint8_t percent);
/**
 * @brief Speaker volume.
 * @return 0..100 %
 */
uint8_t store_volume(void);
/**
 * @brief Sets it.
 * @param[in] percent  0..100
 */
void store_set_volume(uint8_t percent);

/** @brief Mistake check: which digits turn red (and buzz) in the game. */
typedef enum {
    ASSIST_OFF = 0,                           /**< none: no red, no buzz (mistakes still counted) */
    ASSIST_RULES = 1,                         /**< digits that break a row/column/box rule (default) */
    ASSIST_SOLUTION = 2,                      /**< also digits that aren't the solution's */
} store_assist_t;
/**
 * @brief The mistake check.
 * @return store_assist_t
 */
uint8_t store_assist(void);
/**
 * @brief Sets it.
 * @param[in] assist  store_assist_t
 */
void store_set_assist(uint8_t assist);

/**
 * @brief Show the game's clock (Settings > Show timer); it runs and is recorded either way.
 * @return true if shown
 */
bool is_store_show_timer(void);
/**
 * @brief Shows or hides it.
 * @param[in] on  shown
 */
void store_set_show_timer(bool on);

/** @brief Accent colour (Settings): 0 blue, 1 teal, 2 green, 3 purple, 4 pink. */
#define ACCENT_COUNT 5
/**
 * @brief The accent colour.
 * @return 0..ACCENT_COUNT-1
 */
uint8_t store_accent(void);
/**
 * @brief Sets it.
 * @param[in] accent  0..ACCENT_COUNT-1
 */
void store_set_accent(uint8_t accent);

/**
 * @brief Which variants' rules cards were shown already (bit = variant).
 * @return bit v: variant v's card was shown
 */
uint64_t store_rules_seen(void);
/**
 * @brief Sets them.
 * @param[in] mask  bit v: variant v's card was shown
 */
void store_set_rules_seen(uint64_t mask);

/** @brief Lock screen (Settings > Display): shown when the screen wakes from its
 * timeout. Not security: RESET starts the board unlocked. */
typedef enum { LOCK_OFF = 0, LOCK_SWIPE = 1, LOCK_PIN = 2 } store_lock_t;
/**
 * @brief The lock screen.
 * @return store_lock_t (LOCK_PIN without a PIN set: LOCK_SWIPE)
 */
uint8_t store_lock_mode(void);
/**
 * @brief Sets it.
 * @param[in] mode  store_lock_t
 */
void store_set_lock_mode(uint8_t mode);
/**
 * @brief The lock screen's PIN.
 * @return 4 digits, or ""
 */
const char *store_lock_pin(void);
/**
 * @brief Sets it.
 * @param[in] pin  4 digits, or ""
 */
void store_set_lock_pin(const char *pin);

/** @brief Screen saver (Settings > Display): what shows at the screen timeout until a touch; Dark saves
 * power instead (sudoku.db's setting saver_clock: 0 to 7). */
typedef enum {
    SAVER_MODE_DARK = 0,                    /**< nothing: dark at once, saving power */
    SAVER_MODE_CLOCK = 1,                   /**< a dim clock moving about */
    SAVER_MODE_LIFE = 2,                    /**< Conway's Game of Life in a VFD's colours */
    SAVER_MODE_ANALOG = 3,                  /**< an analog clock moving about */
    SAVER_MODE_RAIN = 4,                    /**< digital rain of the digits 1 to 9, in a VFD's colours */
    SAVER_MODE_ATTRACTOR = 5,               /**< strange attractors building up, in a VFD's colours */
    SAVER_MODE_BOIDS = 6,                   /**< a flock of boids with fading trails, in a VFD's colours */
    SAVER_MODE_LISSAJOUS = 7,               /**< turning Lissajous figures, in a VFD's colours */
    SAVER_MODE_COUNT                        /**< how many */
} store_saver_t;
/**
 * @brief The screen saver.
 * @return store_saver_t
 */
uint8_t store_saver_mode(void);
/**
 * @brief Sets it.
 * @param[in] mode  store_saver_t
 */
void store_set_saver_mode(uint8_t mode);
/**
 * @brief Colour-blind friendly colours (Okabe-Ito) are on.
 * @return true if so
 */
bool is_store_cvd(void);
/**
 * @brief Turns them on or off.
 * @param[in] on  on
 */
void store_set_cvd(bool on);
/**
 * @brief New games start in the no-notes challenge.
 * @return true if so
 */
bool is_store_no_notes(void);
/**
 * @brief Turns it on or off.
 * @param[in] on  on
 */
void store_set_no_notes(bool on);
/**
 * @brief The game's number pad is on the left of the board.
 * @return true if so
 */
bool is_store_left_hand(void);
/**
 * @brief Puts it on the left or right.
 * @param[in] on  left
 */
void store_set_left_hand(bool on);

/**
 * @brief Gibberlink: the microphones listen for remote-control commands as sound.
 * @return true if on
 */
bool is_store_gibberlink(void);
/**
 * @brief Turns it on or off.
 * @param[in] on  on
 */
void store_set_gibberlink(bool on);
/**
 * @brief Larger digits on the board.
 * @return true if so
 */
bool is_store_big_digits(void);
/**
 * @brief Hints only say where to look (no More / Place it).
 * @return true if so
 */
bool is_store_hint_nudge(void);
/**
 * @brief Turns that on or off.
 * @param[in] on  on
 */
void store_set_hint_nudge(bool on);
/**
 * @brief Turns larger digits on or off.
 * @param[in] on  on
 */
void store_set_big_digits(bool on);
/**
 * @brief Night mode: dark and dimmer between the night hours.
 * @return true if on
 */
bool is_store_night_mode(void);
/**
 * @brief Turns it on or off.
 * @param[in] on  on
 */
void store_set_night_mode(bool on);
/**
 * @brief Night starts.
 * @return hour 0..23, local time
 */
uint8_t store_night_start(void);
/**
 * @brief Night ends.
 * @return hour 0..23, local time
 */
uint8_t store_night_end(void);
/**
 * @brief Sets the night hours.
 * @param[in] start  hour 0..23
 * @param[in] end  hour 0..23
 */
void store_set_night_hours(uint8_t start, uint8_t end);
/**
 * @brief The backlight at night.
 * @return 10..100 % (at most the day's)
 */
uint8_t store_night_brightness(void);
/**
 * @brief Sets it.
 * @param[in] percent  10..100
 */
void store_set_night_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif
