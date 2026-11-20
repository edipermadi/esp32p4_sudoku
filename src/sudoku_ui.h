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

/* Call once start-up went well (screens up, sudoku.db open): a firmware just
 * installed from update.bin is kept; until then, a restart goes back to the
 * previous one (the bootloader's rollback). */
void sudoku_ui_mark_ok(void);

#ifdef __cplusplus
}
#endif
