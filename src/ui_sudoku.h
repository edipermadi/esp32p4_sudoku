#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the puzzle list and game screens and shows the list.
 * Call after bsp_lvgl_start() and store_init(). */
esp_err_t sudoku_ui_start(void);

/* Shows a message banner at the bottom of the screen for `ms` milliseconds. */
void sudoku_ui_notice(const char *text, uint32_t ms);
/* A banner that stays while something runs (drawn at once, so it shows even while the caller holds
 * the UI); called again, its text changes. sudoku_ui_progress_done() gives it its last text and
 * removes it after `ms` milliseconds (with no progress banner up: a plain notice). */
void sudoku_ui_progress(const char *text);
void sudoku_ui_progress_done(const char *text, uint32_t ms);

/* Writing the flash makes the panel flicker cyan (the display can't reach its frame buffer
 * meanwhile), so the screen is dark during it: sudoku_ui_flash_busy() covers the screen with a
 * card (title, text) that takes all taps; sudoku_ui_flash_dark() then turns the screen off (and
 * keeps the screen timeout from firing) until sudoku_ui_flash_done() brings it back and removes
 * the card. Call from the LVGL task. */
void sudoku_ui_flash_busy(const char *title, const char *text);
void sudoku_ui_flash_dark(void);
void sudoku_ui_flash_done(void);

/* Turns the screen off now, as its timeout does (and shows the lock screen
 * under it, if that's on); the next tap wakes it. For serial control. */
void sudoku_ui_screen_off(void);

/* Enters digits in the game on screen, as if typed on the pad (moves, undo, the
 * replay, sounds, mistakes, and the solve if it's finished): cells[k] gets
 * digits[k], cells 0..80 row by row. Checked first: nothing is entered if any
 * pair can't be (no game shown, solved or paused, a cell not on the board or a
 * given, a digit out of range); why says which. A cell that already holds its
 * digit is left as it is. Returns how many were entered, or -1. For serial
 * control (devctl "fill"). */
int sudoku_ui_fill(const uint8_t *cells, const uint8_t *digits, int n, char *why, size_t why_n);

/* The game on screen, for serial control (devctl "state"). board: 81 characters
 * and a terminator, row by row (cell 0 top left): '1'-'9' a given, 'a'-'i' a
 * digit entered (a = 1), '.' empty, '#' not on the board (Mini). false: no game
 * on screen. */
typedef struct {
    char name[48];               /* "Easy #15", "Daily Hyper Hard, 19 Nov", ... */
    const char *variant;         /* "Classic", "X-Sudoku", ... */
    int size;                    /* 9, or 6 (Mini) */
    int filled, cells;           /* digits on the board / cells on it */
    bool solved, paused, practice, attack;
    uint32_t seconds;            /* the game's clock */
    unsigned mistakes, hints;
} sudoku_ui_state_t;
bool sudoku_ui_state(sudoku_ui_state_t *st, char board[82]);

/* Where the board is, for scripts (devctl "screen"): "home1" / "home2" (the menu's page),
 * "list", "game", "stats", "settings", "sysmon", "players", "trophies", ... ("other" for
 * the rest), with "+dialog" when a dialog or overlay covers it. */
void sudoku_ui_screen_name(char *buf, size_t n);

/* A part of a puzzle sent by sound (devctl / Gibberlink "pz <id> <i>/<n> <part>",
 * another board's Share > Send by sound; the format is in ui_sudoku.c). When the
 * last part is in, the puzzle is checked (one solution), kept in Puzzles > Saved >
 * "Received" and offered to play. out: "OK pz 1/3", "OK pz done" or "ERR pz: ...". */
bool sudoku_ui_puzzle_part(const char *args, char *out, size_t n);

/* What's using the microSD card right now (an MP4 export, a screenshot, Files, a firmware update,
 * formatting, the USB drive, sudoku.db's worker), as a few words; NULL when nothing is.
 * (devctl genbench waits for none.) */
const char *sudoku_ui_io_busy(void);

/* A full-screen panel that blocks the screen (title "Benchmarking", text under it), or
 * updates its text; NULL takes it away. (devctl genbench) */
void sudoku_ui_busy(const char *text);

/* devctl genbench: the puzzle generator to itself while it runs (false: New puzzle or Learn
 * is using it; then nothing to end) */
bool gen_bench_begin(void);
void gen_bench_end(void);

#ifdef __cplusplus
}
#endif
