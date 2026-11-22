/**
 * @file ui_sudoku.h
 * @ingroup ui_sudoku
 * @brief The UI's public functions: start-up, notices, and what serial control asks of it.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_sudoku UI
 * @brief The touch UI (LVGL 9): start-up, notices, the screen saver, and the functions serial
 *        control (devctl) uses; each screen is a file of its own (ui_*.c, sharing ui_internal.h).
 *
 * | File          | What's in it |
 * |---------------|--------------|
 * | ui_sudoku.c   | start-up: the card and sudoku.db read, the settings applied, the screens built, the timers |
 * | ui_splash.c   | the start-up splash screen (extras/splashscreen.png, a JPEG in splash_data.c) |
 * | ui_notice.c   | notices: a banner for a while, or one that stays while something runs |
 * | ui_saver.c    | the screen saver: the dim clock, then dark, power saving; the waking tap |
 * | ui_lock.c     | the lock screen and Settings' PIN setup (@ref topic_lock) |
 * | ui_remote.c   | for devctl: digits into the game, the game as text, the screen's name, what uses the card, the Benchmarking panel |
 * @{
 */

/**
 * @brief Builds the screens and shows Home: the card and sudoku.db read first (the settings and
 *        the game in progress live there), Wi-Fi started, the settings applied, the timers started.
 *        Call after bsp_lvgl_start() and store_init().
 * @return ESP_OK, or ESP_ERR_NO_MEM
 */
esp_err_t sudoku_ui_start(void);

/**
 * @brief Shows a message banner at the bottom of the screen for a while.
 * @param[in] text  the message
 * @param[in] ms    how long, ms
 */
void sudoku_ui_notice(const char *text, uint32_t ms);
/**
 * @brief A banner that stays while something runs (drawn at once, so it shows even while the
 *        caller holds the UI); called again, its text changes.
 * @param[in] text  the message
 */
void sudoku_ui_progress(const char *text);
/**
 * @brief Gives the banner of sudoku_ui_progress() its last text and removes it after a while (with
 *        no progress banner up: a plain notice).
 * @param[in] text  the message
 * @param[in] ms    how long, ms
 */
void sudoku_ui_progress_done(const char *text, uint32_t ms);

/** @name Writing the flash
 *  Writing the flash makes the panel flicker cyan (the display can't reach its frame buffer
 *  meanwhile), so the screen is dark during it. Call from the LVGL task.
 *  @{ */
/**
 * @brief Covers the screen with a card that takes every tap, before the flash is written.
 * @param[in] title  the card's title
 * @param[in] text   what is happening (and that the screen goes dark)
 */
void sudoku_ui_flash_busy(const char *title, const char *text);
/** @brief Turns the screen off (and keeps the screen timeout from firing) while the flash is written. */
void sudoku_ui_flash_dark(void);
/** @brief The screen back on, and the card removed. */
void sudoku_ui_flash_done(void);
/** @} */

/** @brief Turns the screen off now, as its timeout does (with the lock screen under it, if that's on); the next tap wakes it. For serial control. */
void sudoku_ui_screen_off(void);

/**
 * @brief Enters digits in the game on screen, as if typed on the pad (moves, undo, the replay,
 *        sounds, mistakes, and the solve if it's finished). For serial control (devctl "fill").
 *
 * Checked first: nothing is entered if any pair can't be (no game shown, solved or paused, a cell
 * not on the board or a given, a digit out of range). A cell that already holds its digit is left
 * as it is.
 * @param[in] cells   the cells, 0..80 row by row
 * @param[in] digits  each one's digit
 * @param[in] n       how many
 * @param[out] why     gets why not, on -1
 * @param[in] why_n   its size
 * @return how many were entered, or -1
 */
int sudoku_ui_fill(const uint8_t *cells, const uint8_t *digits, int n, char *why, size_t why_n);

/** @brief The game on screen, for serial control (devctl "state"). */
typedef struct {
    char name[48];               /**< "Easy #15", "Daily Hyper Hard, 19 Nov", ... */
    const char *variant;         /**< "Classic", "X-Sudoku", ... */
    int size;                    /**< 9, or 6 (Mini), or 4 (Kids) */
    int filled;                  /**< digits on the board */
    int cells;                   /**< cells on it */
    bool solved;                 /**< solved */
    bool paused;                 /**< paused */
    bool practice;               /**< a Learn practice */
    bool attack;                 /**< a time attack */
    uint32_t seconds;            /**< the game's clock */
    unsigned mistakes;           /**< mistakes made */
    unsigned hints;              /**< hints used */
} sudoku_ui_state_t;
/**
 * @brief The game on screen.
 * @param[out] st     gets its state
 * @param[out] board  gets 81 characters and a terminator, row by row (cell 0 top left): '1'-'9' a given,
 *               'a'-'i' a digit entered (a = 1), '.' empty, '#' not on the board (Mini)
 * @return false: no game on screen
 */
bool sudoku_ui_state(sudoku_ui_state_t *st, char board[82]);

/**
 * @brief Where the board is, for scripts (devctl "screen").
 * @param[out] buf  gets "home1" / "home2" (the menu's page), "list", "game", "stats", "settings",
 *             "sysmon", "players", "trophies", ... ("other" for the rest), with "+dialog" when a
 *             dialog or overlay covers it
 * @param[in] n    its size
 */
void sudoku_ui_screen_name(char *buf, size_t n);

/**
 * @brief Deletes a saved or imported collection, as the Puzzles screen's Delete collection does (devctl
 *        "colldel"): its puzzles stay on the board, with their solves, History and Gameplays; an
 *        imported one's file can be imported again. The screens are brought up to date; the Puzzles
 *        screen keeps the collection it showed (by name), or its tab's first if that one went.
 * @param[in] tab    its tab: 1 saved (made on the board), 2 imported (COLL_TAB_*, collections.h)
 * @param[in] id     its id (coll_id(): its row in sudoku.db)
 * @param[out] msg    gets what happened ("variant-examples deleted", or why not)
 * @param[in] n      its size
 * @return true if it was deleted
 */
bool sudoku_ui_coll_delete(int tab, int id, char *msg, size_t n);

/**
 * @brief Opens a puzzle of a collection on the game screen, as tapping it on the Puzzles screen does (devctl
 *        "open"): the game in progress, an unfinished one resumed, or a new start; the game's List goes back to
 *        that collection. Refused while a dialog or overlay is open (the lock screen among them) or a time attack
 *        runs. Without a card, a game in progress makes a dialog ask first, as on the screen.
 * @param[in] tab     the collection's tab: 0 built-in, 1 saved, 2 imported (COLL_TAB_*, collections.h)
 * @param[in] id      its id (coll_id(): built-in 1-16, else its row in sudoku.db; devctl "colls" lists them)
 * @param[in] number  the puzzle's number in it, from 1
 * @param[out] msg    gets the puzzle's name ("Easy #5", with " (asking first)" when a dialog asks), or why not
 * @param[in] n       its size
 * @return true if it was opened (or a dialog asks first)
 */
bool sudoku_ui_open_puzzle(int tab, int id, int number, char *msg, size_t n);

/**
 * @brief A part of a puzzle sent by sound (devctl / Gibberlink "pz \<id\> \<i\>/\<n\> \<part\>",
 *        another board's Share > Send by sound). When the last part is in, the puzzle is checked
 *        (one solution), kept in Puzzles > Saved > "Received" and offered to play.
 * @param[in] args  what follows "pz"
 * @param[out] out   gets "OK pz 1/3", "OK pz done" or "ERR pz: ..."
 * @param[in] n     its size
 * @return true if it went in
 */
bool sudoku_ui_puzzle_part(const char *args, char *out, size_t n);

/**
 * @brief What's using the microSD card right now (an MP4 export, a screenshot, Files, a firmware
 *        update, formatting, the USB drive, sudoku.db's worker). devctl genbench waits for none.
 * @return a few words, or NULL when nothing is
 */
const char *sudoku_ui_io_busy(void);

/**
 * @brief A full-screen panel that blocks the screen (title "Benchmarking", the text under it), or
 *        updates its text (devctl genbench).
 * @param[in] text  the text; NULL takes the panel away
 */
void sudoku_ui_busy(const char *text);

/**
 * @brief devctl genbench: the puzzle generator to itself while it runs.
 * @return false: New puzzle or Learn is using it (then nothing to end)
 */
bool gen_bench_begin(void);
/** @brief devctl genbench is done with the generator. */
void gen_bench_end(void);

/** @} */

#ifdef __cplusplus
}
#endif
