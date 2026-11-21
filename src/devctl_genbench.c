/**
 * @file devctl_genbench.c
 * @ingroup devctl
 * @brief genbench: the puzzle generator's time on the board.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp32-hal.h"
#include <lvgl.h>
#include "bsp/bsp_board.h"
#include "ui_sudoku.h"
#include "generator.h"

/**
 * @brief A variant or level by number, name or the start of a name (any case).
 * @param s  the word
 * @param name  its names
 * @param n  how many there are
 * @return its number; -1 none, -2 ambiguous
 */
static int gb_pick(const char *s, const char *(*name)(int), int n)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s && !*end) {
        return v >= 0 && v < n ? (int)v : -1;
    }
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(s, name(i))) {
            return i;
        }
    }
    int hit = -1, hits = 0;
    for (int i = 0; i < n; i++) {
        if (!strncasecmp(s, name(i), strlen(s))) {
            hit = i;
            hits++;
        }
    }
    return hits == 1 ? hit : hits ? -2 : -1;
}

/** The genbench puzzle being made: what to make, then (gb_task) how it went. */
static struct {
    int variant;                                    /**< the variant asked for */
    int level;                                      /**< the level asked for */
    uint32_t seed;                                  /**< the seed (fixed: builds compare) */
    uint32_t us;                                    /**< how long it took, microseconds */
    int got;                                        /**< the level it came out at */
    int clues;                                      /**< its givens */
    volatile bool done;                             /**< made */
} s_gb;
static sudoku_geom_t *s_gb_board;                   /**< the board of the puzzle made (PSRAM) */

/**
 * @brief One puzzle, on the worker core like New puzzle's (with its yields: the task watchdog).
 * @param arg  unused (the request is in s_gb)
 */
static void gb_task(void *arg)
{
    uint8_t givens[81];
    gen_level_t got = (gen_level_t)s_gb.level;
    int64_t t0 = esp_timer_get_time();
    sudoku_generate(s_gb.variant, (gen_level_t)s_gb.level, s_gb.seed, givens, s_gb_board, &got);
    s_gb.us = (uint32_t)(esp_timer_get_time() - t0);
    s_gb.got = got;
    s_gb.clues = 0;
    for (int i = 0; i < 81; i++) {
        s_gb.clues += givens[i] != 0;
    }
    s_gb.done = true;
    vTaskDelete(NULL);
}

/* (documented in devctl_internal.h) */
void devctl_genbench(const char *line)
{
    char vs[24] = "", ls[16] = "all";
    int n = 5;
    if (sscanf(line, "%*s %23s %15s %d", vs, ls, &n) < 1) {
        devctl_reply("ERR genbench: genbench <variant|all> [level|all] [n], e.g. genbench greater expert 10\n");
        return;
    }
    if (n < 1) {
        n = 1;
    }
    if (n > 200) {
        n = 200;
    }
    bool all_v = !strcasecmp(vs, "all"), all_l = !strcasecmp(ls, "all");
    int v0 = all_v ? 0 : gb_pick(vs, variant_name, VAR_COUNT), l0 = all_l ? 0 : gb_pick(ls, gen_level_name, GEN_LEVELS);
    if (v0 < 0 || l0 < 0) {
        devctl_reply("ERR genbench: %s %s: %s\n", v0 < 0 ? "variant" : "level", v0 < 0 ? vs : ls,
                     (v0 < 0 ? v0 : l0) == -2 ? "more than one match" : "no such one");
        return;
    }
    if (!s_gb_board && !(s_gb_board = (sudoku_geom_t *)heap_caps_malloc(sizeof(*s_gb_board), MALLOC_CAP_SPIRAM))) {
        devctl_reply("ERR genbench: no memory\n");
        return;
    }
    {                                               /* not over a puzzle being solved */
        sudoku_ui_state_t st;
        char board[82];
        if (sudoku_ui_state(&st, board) && !st.solved) {
            devctl_reply("ERR genbench: a puzzle is being solved (the game screen is open): leave it first\n");
            return;
        }
    }
    const char *io = sudoku_ui_io_busy();          /* nothing on the card under way meanwhile */
    if (io) {
        devctl_reply("ERR genbench: %s: try again when it's done\n", io);
        return;
    }
    if (!gen_bench_begin()) {
        devctl_reply("ERR genbench: a puzzle is being generated (New puzzle or Learn): try again when it's done\n");
        return;
    }
    bsp_power_cpu_full(true);                       /* (screen dark: 180 MHz otherwise) */
    serial_printf("GENBENCH CPU %u MHz, %d puzzle%s each\n", (unsigned)getCpuFrequencyMhz(), n, n == 1 ? "" : "s");
    for (int v = v0; v < (all_v ? VAR_COUNT : v0 + 1); v++) {
        for (int l = l0; l < (all_l ? GEN_LEVELS : l0 + 1); l++) {
            if (all_l && v == VAR_KIDS && l >= GEN_HARD) {
                continue;    /* (Kids 4x4: Easy and Medium only) */
            }
            uint32_t sum = 0, mn = UINT32_MAX, mx = 0;
            int on = 0, clues = 0;
            for (int i = 0; i < n; i++) {
                char what[96];                      /* the screen says what's running */
                snprintf(what, sizeof(what), "devctl genbench: puzzle generation\n%s %s, %d of %d", variant_name(v),
                         gen_level_name(l), i + 1, n);
                sudoku_ui_busy(what);
                s_gb.variant = v;
                s_gb.level = l;
                s_gb.seed = 0x5EED0000u + (uint32_t)v * 1000u + (uint32_t)l * 100u + (uint32_t)i;
                s_gb.done = false;
                if (xTaskCreatePinnedToCore(gb_task, "genbench", 16384, NULL, 2, NULL, 0) != pdPASS) {
                    devctl_reply("ERR genbench: no task\n");
                    goto out;
                }
                while (!s_gb.done) {
                    lv_display_trigger_activity(NULL);   /* (no screen timeout meanwhile) */
                    devctl_pump(5);
                }
                sum += s_gb.us;
                if (s_gb.us < mn) {
                    mn = s_gb.us;
                }
                if (s_gb.us > mx) {
                    mx = s_gb.us;
                }
                on += s_gb.got == l;
                clues += s_gb.clues;
            }
            serial_printf("GENBENCH %s %s: avg %u ms, min %u, max %u; on level %d/%d; %u.%u clues\n", variant_name(v),
                          gen_level_name(l), (unsigned)(sum / n / 1000), (unsigned)(mn / 1000), (unsigned)(mx / 1000), on,
                          n, (unsigned)(clues / n), (unsigned)(clues * 10 / n % 10));
        }
    }
    devctl_reply("OK genbench\n");
out:
    bsp_power_cpu_full(false);                      /* (back to 180 MHz if still dark) */
    sudoku_ui_busy(NULL);
    gen_bench_end();
}
