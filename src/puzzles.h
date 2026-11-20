#pragma once
/* Puzzles from the OpenSudoku collections, stored in flash (see tools/gen_puzzles.py). */
#include <stdint.h>
#include "puzzles_count.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PUZZLE_PACKED_BYTES 41   /* 81 cells, 4 bits each, 0 = empty */

/* Puzzles made on the board (generator.h) have ids from PUZZLE_GEN_BASE:
 * PUZZLE_GEN_BASE + generated.id in sudoku.db, or, without a card,
 * PUZZLE_GEN_RAM_BASE and up (kept in RAM until power-off). */
#define PUZZLE_GEN_BASE      2000
#define PUZZLE_GEN_RAM_BASE  0xF000
static inline int puzzle_is_generated(uint16_t id)
{
    return id >= PUZZLE_GEN_BASE && id != 0xFFFF;
}
/* a puzzle this firmware can show: built-in or generated */
static inline int puzzle_id_valid(int id)
{
    return (id >= 0 && id < PUZZLE_COUNT) || (id >= PUZZLE_GEN_BASE && id < 0xFFFF);
}

typedef struct {
    const char *name;
    uint16_t first;              /* global index of the collection's first puzzle */
    uint16_t count;
} puzzle_collection_t;

const puzzle_collection_t *puzzle_collection(int c);   /* 0..PUZZLE_COLLECTION_COUNT-1 */

/* Unpacks puzzle `id` (0..PUZZLE_COUNT-1) into 81 cells, row-major, 0 = empty. */
void puzzle_load(uint16_t id, uint8_t cells[81]);

/* Collection that holds puzzle `id`. */
int puzzle_collection_of(uint16_t id);

#ifdef __cplusplus
}
#endif
