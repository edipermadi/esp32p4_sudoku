/**
 * @file share_core.h
 * @ingroup share_core
 * @brief Sharing a puzzle, without LVGL: the QR code's text, the .sdx line sent by sound, and its parts.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup share_core Share core
 * @ingroup topic_game
 * @brief The texts the game's Share dialog shows and sends.
 * @{
 */

/*
 * Share > Send by sound plays the puzzle on screen as Gibberlink (ggwave) messages;
 * another board with Gibberlink on (Settings > Sound) takes it in, checks it has one
 * solution, keeps it in Puzzles > Saved > "Received" and offers to play it.
 *
 * The puzzle is its .sdx line (stats_db.c): variant|level|givens|regions|cages|marks|
 * thermos, the variant and level as numbers (level "-": the receiver rates it), the
 * givens shortened (a run of 1-26 empty cells is one letter, a-z), trailing empty
 * fields left out. A message carries at most 140 bytes, so the line goes in parts:
 *
 *   SU$pz <id> <i>/<n> <part>*<crc>
 *
 * id: 2 hex digits, the same in all parts of one puzzle; crc: CRC-16/CCITT-FALSE of
 * the text after "SU$" (gibberlink.h). A Classic one is one part (about 6 s); a
 * Killer one three or four. The receiver plays no answer to a part (devctl "pz").
 */
#define PZ_ID        "SU"                         /**< our Gibberlink sender id for puzzles */
#define PZ_PART      110                          /**< characters of the line per message */
#define PZ_MAX_PARTS 16                 /**< parts a puzzle may take */

/**
 * @brief The QR code's text: a Classic puzzle's 81 givens (0 = empty), which most Sudoku apps import; a
 *        variant's name, its givens and its layout ("Killer Sudoku 0..0 cages=...").
 * @param[in] G        the puzzle's board
 * @param[in] variant  sudoku_variant_t
 * @param[in] givens   81 cells, 0 = empty (a 6x6 or 4x4 puzzle: its cells by G->cells)
 * @param[out] buf     gets the text
 * @param[in] n        its size
 * @return false if it doesn't fit
 */
bool share_text(const sudoku_geom_t *G, int variant, const uint8_t *givens, char *buf, size_t n);

/**
 * @brief The puzzle as an .sdx line (sdx_format(), the givens packed): what Send by sound sends.
 * @param[in] G        the puzzle's board
 * @param[in] variant  sudoku_variant_t
 * @param[in] level    gen_level_t, -1 if not known (the receiver rates it)
 * @param[in] givens   81 cells, 0 = empty
 * @param[out] buf     gets the line
 * @param[in] n        its size
 * @return its length, 0 if it doesn't fit
 */
int share_pz_line(const sudoku_geom_t *G, int variant, int level, const uint8_t *givens, char *buf, size_t n);

/**
 * @brief CRC-16/CCITT-FALSE, the parts' check.
 * @param[in] p  the text
 * @param[in] n  its length
 * @return the CRC
 */
uint16_t share_crc16(const char *p, size_t n);

/**
 * @brief One part of a line, as its Gibberlink message: "SU$pz <id> <i>/<n> <part>*<crc>".
 * @param[in] id     the puzzle's id (2 hex digits), the same in all its parts
 * @param[in] i      the part, from 0
 * @param[in] parts  how many (len / PZ_PART rounded up)
 * @param[in] line   the whole line (share_pz_line())
 * @param[in] len    its length
 * @param[out] msg   gets the message
 * @param[in] n      its size (176 is enough)
 */
void share_pz_part(unsigned id, int i, int parts, const char *line, int len, char *msg, size_t n);

/**
 * @brief Reads a part as it arrives (devctl "pz": the message without "SU$pz" and the CRC).
 * @param[in] args    "<id> <i>/<n> <part>"
 * @param[out] id     gets the puzzle's id
 * @param[out] i      gets the part, from 1
 * @param[out] parts  gets how many
 * @param[out] data   gets the part's text (in args)
 * @return false if it doesn't read, or the part is too long
 */
bool share_pz_part_read(const char *args, unsigned *id, int *i, int *parts, const char **data);

/** @} */

#ifdef __cplusplus
}
#endif
