/**
 * @file sdx.h
 * @brief The .sdx puzzle format: one puzzle a line, its variant, level, givens and extras.
 */
#pragma once
/*
 * One puzzle as a line of text: the .sdx format (Settings > Data > Import puzzles,
 * a saved collection's Export) and the form puzzles travel in by sound (Share >
 * Send by sound). Plain C, no hardware: tests/ runs it on a PC.
 *
 *   variant|level|givens|regions|cages|marks|thermos
 *
 * variant: a name (variant_name(): "Classic", "Killer", ...) or its number; level:
 * "Easy" .. "Expert" or 0-3, empty or "-" for none (the receiver rates it); givens: 81
 * cells row by row, a digit, '0' or '.' for an empty one, or a letter a-z for a run
 * of 1-26 empty cells (sdx_pack_givens; Mini 6x6 and Kids 4x4 use the top-left 6x6 /
 * 4x4); regions: Jigsaw's 81 digits 0-8; cages (Killer; Quadruple: its circles; Little
 * Killer: its diagonals), marks (Even-Odd, Consecutive, Kropki, XV, Greater-Than: 81
 * characters; Sandwich: its 18 sums; Skyscraper: its 18 clues), thermos (Thermo; Arrow, Renban, German Whispers: their lines): the texts
 * generator.h reads and writes. Fields a variant doesn't use are empty, trailing ones may be left out. A
 * line of just 81 digits (an .sdm line) is a Classic puzzle with no level.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A puzzle read from an .sdx line. */
typedef struct {
    int variant;                        /**< sudoku_variant_t */
    int level;                          /**< gen_level_t, -1: none given */
    uint8_t givens[81];                 /**< 0 = empty */
    const char *regions;                /**< Jigsaw's regions (into the parsed line; "" when not there) */
    const char *cages;                  /**< Killer's cages (the same) */
    const char *marks;                  /**< the marks: Kropki, XV, ... (the same) */
    const char *thermos;                /**< Thermo's lines (the same) */
} sdx_puzzle_t;

/**
 * @brief
 * @param[in,out] line  the line
 * @param[out] p  gets the puzzle
 * @return true if it is a puzzle line
 */
/** @brief Reads one line (changed in place: the fields are cut out of it). false: it isn't a
 * puzzle line (a comment, an unknown variant or level, givens that aren't 81 cells). */
bool sdx_parse(char *line, sdx_puzzle_t *p);

/**
 * @brief
 * @param[in] p  the puzzle
 * @param[out] out  gets its board
 * @return false: its layout doesn't read
 */
/** @brief The board of a parsed puzzle (Jigsaw's regions, Killer's cages, ...) into out.
 * false: its layout doesn't read. */
bool sdx_geom(const sdx_puzzle_t *p, sudoku_geom_t *out);

/**
 * @brief
 * @param[in] givens  the cells, 0 = empty
 * @param[out] out  gets the text
 * @param[in] n  its size
 * @return its length, or -1
 */
/** @brief givens as text, runs of empty cells packed into letters a-z; its length, or -1
 * if it doesn't fit (n >= 82 always does). */
int sdx_pack_givens(const uint8_t givens[81], char *out, size_t n);

/**
 * @brief
 * @param[in] variant  sudoku_variant_t
 * @param[in] level  gen_level_t, or -1
 * @param[in] givens  the cells, 0 = empty
 * @param[in] regions  Jigsaw's regions
 * @param[in] cages  Killer's cages
 * @param[in] marks  the marks (Kropki, XV, ...)
 * @param[in] thermos  Thermo's lines
 * @param[in] pack  pack the givens
 * @param[out] out  gets the line
 * @param[in] n  its size
 * @return its length, or -1
 */
/** @brief A line: the variant and level as numbers (level < 0: "-"), the givens packed (pack)
 * or as 81 digits, the texts (NULL or "" for none), trailing empty fields left out.
 * Its length, or -1 if it doesn't fit. */
int sdx_format(int variant, int level, const uint8_t givens[81], const char *regions, const char *cages,
               const char *marks, const char *thermos, bool pack, char *out, size_t n);

#ifdef __cplusplus
}
#endif
