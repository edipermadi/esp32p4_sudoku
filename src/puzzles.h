#pragma once
/* Puzzles from the OpenSudoku collections, stored in flash (see tools/gen_puzzles.py). */
#include <stdint.h>
#include "puzzles_count.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PUZZLE_PACKED_BYTES 41   /* 81 cells, 4 bits each, 0 = empty */

typedef struct {
    const char *name;
    uint16_t first;              /* global index of the collection's first puzzle */
    uint16_t count;
} puzzle_collection_t;

extern const puzzle_collection_t puzzle_collections[PUZZLE_COLLECTION_COUNT];
extern const uint8_t puzzle_data[PUZZLE_COUNT][PUZZLE_PACKED_BYTES];

/* Unpacks puzzle `id` (0..PUZZLE_COUNT-1) into 81 cells, row-major, 0 = empty. */
static inline void puzzle_load(uint16_t id, uint8_t cells[81])
{
    const uint8_t *p = puzzle_data[id];
    for (int i = 0; i < 81; i++) {
        uint8_t b = p[i >> 1];
        cells[i] = (i & 1) ? (b & 0x0F) : (b >> 4);
    }
}

/* Collection that holds puzzle `id`. */
static inline int puzzle_collection_of(uint16_t id)
{
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        if (id < puzzle_collections[c].first + puzzle_collections[c].count) return c;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
