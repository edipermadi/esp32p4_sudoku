/**
 * @file devctl_game.c
 * @ingroup devctl
 * @brief state, fill: the game on screen, read as text and played by digits.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include "ui_sudoku.h"

/* (documented in devctl_internal.h) */
void devctl_state(void)
{
    sudoku_ui_state_t st;
    char board[82];
    if (!sudoku_ui_state(&st, board)) {
        devctl_reply("ERR state: no game on screen\n");
        return;
    }
    if (devctl_is_sound()) {                                /* over sound: what fits a message (140 bytes) */
        devctl_reply("OK state s%d %s\n", st.solved, board);
        return;
    }
    serial_printf("STATE name=\"%s\" variant=%s size=%d solved=%d paused=%d filled=%d/%d time=%u "
                  "mistakes=%u hints=%u%s%s\n", st.name, st.variant, st.size, st.solved, st.paused, st.filled,
                  st.cells, (unsigned)st.seconds, st.mistakes, st.hints, st.practice ? " practice=1" : "",
                  st.attack ? " attack=1" : "");
    serial_printf("BOARD %s\n", board);
    devctl_reply("OK state\n");
}

/* (documented in devctl_internal.h) */
void devctl_fill(const char *line)
{
    uint8_t cells[81], digits[81];
    int nums = 0, np = 0, v = -1;
    bool bad = false;
    for (const char *p = line + 4;; p++) {      /* the numbers after "fill", in pairs */
        if (*p >= '0' && *p <= '9') {
            v = (v < 0 ? 0 : v * 10) + (*p - '0');
            if (v > 255) {
                bad = true;
            }
            continue;
        }
        if (v >= 0) {
            if (nums % 2 == 0) {
                if (np >= 81) {
                    bad = true;
                } else {
                    cells[np] = (uint8_t)v;
                }
            } else if (np < 81) {
                digits[np++] = (uint8_t)v;
            }
            nums++;
            v = -1;
        }
        if (!*p) {
            break;
        }
    }
    if (bad || nums == 0 || nums % 2) {
        devctl_reply("ERR fill: pairs of <cell 0-80> <digit>, e.g. fill 12:5 13:7 (at most 81)\n");
        return;
    }
    char why[64];
    int placed = sudoku_ui_fill(cells, digits, np, why, sizeof(why));
    if (placed < 0) {
        devctl_reply("ERR fill: %s\n", why);
        return;
    }
    devctl_pump(SETTLE_MS);                            /* the board redraws (and the solve begins, if done) */
    devctl_reply("OK fill %d\n", placed);
}
