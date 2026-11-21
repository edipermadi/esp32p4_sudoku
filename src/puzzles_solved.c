/**
 * @file puzzles_solved.c
 * @ingroup puzzles_solved
 * @brief Solved counts on the Puzzles screen: per collection, and the collection shown's puzzles.
 */
#include "puzzles_solved.h"
#include "collections.h"
#include "puzzles.h"
#include "store.h"

static uint8_t l_solved_bits[16];       /**< an imported or saved collection's solved puzzles (sudoku.db) */

/* (documented in puzzles_solved.h) */
uint16_t puzzles_solved_count(int c, int shown)
{
    int size = coll_size(c), k = coll_saved_index(c);
    return c < PUZZLE_COLLECTION_COUNT ? store_solved_count(coll_first(c), (uint16_t)size)
           : k >= 0 ? (uint16_t)stats_db_solved_list(ui_saved()[k].puzzles, size, c == shown ? l_solved_bits : NULL)
           : (uint16_t)stats_db_solved_in(coll_first(c), size, c == shown ? l_solved_bits : NULL);
}

/* (documented in puzzles_solved.h) */
bool is_puzzles_solved(int i)
{
    return i >= 0 && i < 128 && (l_solved_bits[i >> 3] >> (i & 7) & 1);
}

/* (documented in puzzles_solved.h) */
void puzzles_panel(int c, uint16_t *n, uint32_t *best, uint32_t *sum)
{
    const int size = coll_size(c);
    *n = 0;
    *best = 0;
    *sum = 0;
    for (int i = 0; c < PUZZLE_COLLECTION_COUNT && i < size; i++) {
        uint32_t s = store_best_time(coll_first(c) + i);
        if (!s) {
            continue;
        }
        (*n)++;
        *sum += s;
        if (!*best || s < *best) {
            *best = s;
        }
    }
    if (c >= PUZZLE_COLLECTION_COUNT) {
        for (int i = 0; i < size && i < 128; i++) {
            *n += l_solved_bits[i >> 3] >> (i & 7) & 1;
        }
    }
}
