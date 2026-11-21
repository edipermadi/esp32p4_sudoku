/**
 * @file moves.h
 * @brief A game's move log: what each move did and when (the replay and the solves in sudoku.db).
 */
#pragma once
/*
 * Move log of a game: every change of a cell's digit, in order, with the game
 * clock at that moment and what made it (a tap on the number pad, Fill, Erase
 * or Undo). Pencil marks are not logged. Replaying the log over the givens
 * rebuilds the board at any point (the Replay screen).
 *
 * Stored in sudoku.db as a BLOB (saved_game.moves, solves.moves): 6 bytes per
 * move, little-endian: uint32 t_ms, uint8 cell (0..80), uint8 digit | kind << 4.
 * (Kind 4, Redo, is newer: a build from before it stops reading a log there.)
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_MOVES_MAX   2048            /**< a log stops growing here (still replayable) */
#define MOVE_BLOB_BYTES  6              /**< bytes per move in sudoku.db's blob */

/** What a move did (game_move_t.kind). */
typedef enum {
    MOVE_HAND = 0,                       /**< digit placed (or cleared by tapping it again) on the number pad */
    MOVE_FILL = 1,                       /**< Fill: the solution's digit (a hint) */
    MOVE_ERASE = 2,                      /**< Erase button */
    MOVE_UNDO = 3,                       /**< Undo put this cell back */
    MOVE_REDO = 4,                       /**< Redo put an undone change back */
} move_kind_t;

/** A move: what was done to which cell, and when. */
typedef struct {
    uint32_t t_ms;                       /**< game clock (pauses excluded) */
    uint8_t cell;                        /**< 0..80, row by row */
    uint8_t digit;                       /**< 0 = cleared */
    uint8_t kind;                        /**< move_kind_t */
} game_move_t;

/**
 * @brief Packs moves into sudoku.db's blob (MOVE_BLOB_BYTES each).
 * @param m  the moves
 * @param n  how many
 * @param out  gets n * MOVE_BLOB_BYTES bytes
 * @return the bytes written
 */
static inline size_t moves_pack(const game_move_t *m, int n, uint8_t *out)
{
    for (int i = 0; i < n; i++, out += MOVE_BLOB_BYTES) {
        out[0] = (uint8_t)m[i].t_ms;
        out[1] = (uint8_t)(m[i].t_ms >> 8);
        out[2] = (uint8_t)(m[i].t_ms >> 16);
        out[3] = (uint8_t)(m[i].t_ms >> 24);
        out[4] = m[i].cell;
        out[5] = (uint8_t)((m[i].digit & 0x0F) | (m[i].kind << 4));
    }
    return (size_t)n * MOVE_BLOB_BYTES;
}

/**
 * @brief Unpacks a blob into moves.
 * @param in  the blob
 * @param bytes  its size
 * @param out  gets the moves
 * @param max  out's room
 * @return the number of moves read (bad cells/digits end the log there)
 */
/** @brief Returns the number of moves read (bad cells/digits end the log there). */
static inline int moves_unpack(const uint8_t *in, int bytes, game_move_t *out, int max)
{
    int n = 0;
    for (; n < max && (n + 1) * MOVE_BLOB_BYTES <= bytes; n++, in += MOVE_BLOB_BYTES) {
        game_move_t *m = &out[n];
        m->t_ms = in[0] | (in[1] << 8) | (in[2] << 16) | ((uint32_t)in[3] << 24);
        m->cell = in[4];
        m->digit = in[5] & 0x0F;
        m->kind = in[5] >> 4;
        if (m->cell > 80 || m->digit > 9 || m->kind > MOVE_REDO) {
            break;
        }
    }
    return n;
}

#ifdef __cplusplus
}
#endif
