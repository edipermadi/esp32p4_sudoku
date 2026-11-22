/**
 * @file generator.h
 * @ingroup generator
 * @brief The puzzle generator, solver, grader and hints, for every variant (plain C: no ESP-IDF, no
 *        LVGL; the PC tests build it too).
 */
/**
 * @defgroup generator Puzzle generator
 * @ingroup topic_game
 * @brief Boards for every variant, solving and counting solutions, grading the way a person would,
 *        generating puzzles at a level, and hints; split by what it does:
 *
 * | File            | What |
 * |-----------------|------|
 * | gen_geometry.c  | the names, each variant's board, the variants' texts read and written |
 * | gen_solver.c    | counting and filling: every variant's rules, the search |
 * | gen_grade.c     | human-style grading: the techniques and the level they give |
 * | gen_make.c      | generating: digging to a level; Jigsaw regions, Killer cages, lines, ... |
 * | gen_hints.c     | the next step a person would take |
 *
 * generator.h is what the rest of the firmware uses; the files share gen_internal.h. How it all
 * works: docs/internals/generator.md.
 */
#pragma once
/*
 * Puzzle generator, solver and hints for the variants:
 *
 *   VAR_CLASSIC  9x9, rows, columns and 3x3 boxes hold 1-9 once each
 *   VAR_X        X-Sudoku: also both main diagonals hold 1-9 once each
 *   VAR_MINI     6x6 with 2x3 boxes (2 rows, 3 columns), digits 1-6
 *   VAR_HYPER    Hyper (Windoku): also four 3x3 windows, at rows and
 *                columns 2-4 and 6-8, hold 1-9 once each
 *   VAR_JIGSAW   9x9 with nine irregular regions of 9 connected cells in
 *                place of the boxes; every puzzle has its own regions
 *   VAR_KILLER   Classic rules, plus cages: groups of 2-5 connected cells
 *                whose digits add up to the cage's sum, none repeated;
 *                few or no givens; every puzzle has its own cages
 *   VAR_KNIGHT   Anti-Knight: Classic rules, and two cells a chess knight's
 *                move apart never hold the same digit
 *   VAR_EVENODD  Even-Odd: Classic rules, and some cells are marked even
 *                or odd; every puzzle has its own marks
 *   VAR_CONSEC   Consecutive: Classic rules, and every pair of side-by-side
 *                cells whose digits differ by 1 has a bar between them (no
 *                bar: they don't); every puzzle has its own bars
 *   VAR_THERMO   Thermo: Classic rules, and along each thermometer the
 *                digits rise from its bulb; every puzzle has its own
 *   VAR_SANDWICH Sandwich: Classic rules, and a clue by each row and column
 *                gives the sum of the digits between its 1 and its 9
 *   VAR_KING     Anti-King: Classic rules, and two cells that touch, also
 *                diagonally (a chess king's move), never hold the same digit
 *   VAR_NONCONSEC Non-Consecutive: Classic rules, and side-by-side cells
 *                never hold digits 1 apart (Consecutive with no bars at all)
 *   VAR_DISJOINT Disjoint Groups: Classic rules, and the nine cells in the
 *                same place in each box also hold 1-9 once each
 *   VAR_ASTERISK Asterisk: Classic rules, and nine cells in an asterisk
 *                shape around the centre also hold 1-9 once each
 *   VAR_KROPKI   Kropki: Classic rules, and a white dot between two side-by-side
 *                cells means their digits are 1 apart, a black dot that one is
 *                double the other; no dot: neither (1 and 2 get either one)
 *   VAR_XV       XV: Classic rules, and an X between two side-by-side cells
 *                means they add up to 10, a V to 5; no mark: neither
 *   VAR_GT       Greater-Than: Classic rules, and between every two side-by-side
 *                cells in a box a sign says which one is bigger
 *   VAR_QUAD     Quadruple: Classic rules, and a circle where four cells meet
 *                lists digits that must be among those four cells
 *   VAR_ARROW    Arrow: Classic rules, and the digits along an arrow's shaft add
 *                up to the digit in its circle (shaft digits may repeat)
 *   VAR_RENBAN   Renban: Classic rules, and the digits on each line are a set of
 *                consecutive digits, in any order (3-5-4-6)
 *   VAR_WHISPER  German Whispers: Classic rules, and neighbours along a line
 *                differ by at least 5 (so no 5 on a line)
 *   VAR_SKY      Skyscraper: Classic rules, and a clue left of some rows and above
 *                some columns: how many "buildings" (digits) are seen from there,
 *                a taller one hiding the shorter ones behind it
 *   VAR_LK       Little Killer: Classic rules, and clues outside the grid with an
 *                arrow: the sum of the digits along that diagonal (repeats allowed)
 *   VAR_PALINDROME Palindrome: Classic rules, and each grey line reads the same
 *                from both ends (its first and last cells hold the same digit, ...)
 *   VAR_REGIONSUM Region Sum Lines: Classic rules, and on each blue line the digits
 *                in each box it passes through add up to the same total
 *   VAR_XSUM     X-Sums: Classic rules, and a clue left of some rows and above some
 *                columns: the sum of the first X digits from there, X being the
 *                first of them
 *   VAR_ZIPPER   Zipper Lines: Classic rules, and on each pink line, two cells the
 *                same distance from its middle add up to the same total (on a line
 *                of odd length: the digit in its middle cell)
 *   VAR_FRAME    Frame: Classic rules, and a clue left of some rows and above some
 *                columns: the sum of the first three digits from there
 *   VAR_BETWEEN  Between Lines: Classic rules, and the digits on a line between two
 *                circles lie strictly between the circles' digits
 *   VAR_ENTROPIC Entropic Lines: Classic rules, and any three cells in a row on a
 *                line hold a low (1-3), a middle (4-6) and a high (7-9) digit
 *   VAR_MODULAR  Modular Lines: Classic rules, and any three cells in a row on a
 *                line hold one digit of each of 1 4 7, 2 5 8 and 3 6 9
 *   VAR_CENTERDOT Center Dot: Classic rules, and the nine box centres also hold
 *                1-9 once each
 *   VAR_OUTSIDE  Outside Sudoku: Classic rules, and a clue left of some rows and
 *                above some columns lists 1-3 digits that are among the first three
 *                cells from there
 *   VAR_ROSSINI  Rossini: Classic rules, and an arrow left of a row or above a
 *                column says its first three digits rise in the arrow's direction;
 *                no arrow: they neither rise nor fall
 *   VAR_DIAGNC   Diagonal Non-Consecutive: Classic rules, and two cells that touch
 *                at a corner never hold digits 1 apart
 *
 * The functions take the board's geometry (sudoku_geom_t): sudoku_geom()
 * for the fixed variants, sudoku_geom_jigsaw() for a Jigsaw region map,
 * sudoku_geom_killer_text() for a Killer puzzle's cages,
 * sudoku_geom_marks() for an Even-Odd or Consecutive puzzle's marks,
 * sudoku_geom_thermo_text() for a Thermo puzzle's thermometers.
 *
 * Every board is kept in 81 cells, row by row (cell = row * 9 + column); a
 * 6x6 board uses the top-left 6x6 of them and leaves the rest 0. Saved games,
 * move logs and the database then work the same for every variant.
 *
 * Units have fixed numbers whatever the variant (43: the Center Dot): 0..8 rows, 9..17 columns,
 * 18..26 boxes (left to right, top to bottom; on Jigsaw the regions), 27 the
 * main diagonal (top left to bottom right), 28 the other diagonal, 29..32
 * the Hyper windows (top left, top right, bottom left, bottom right), 33..41
 * the Disjoint groups (by the place in the box, row by row), 42 the
 * Asterisk. A 6x6
 * board has rows 0..5, columns 9..14 and boxes 18..23; each unit has n cells.
 *
 * Generating: a random complete grid, then clues taken away in symmetric
 * pairs (180-degree rotation) as long as exactly one solution remains.
 * Graded by a solver that works like a person:
 *
 *   GEN_EASY    singles only (naked + hidden), 36 or more clues (6x6: 16)
 *   GEN_MEDIUM  singles only, fewer clues
 *   GEN_HARD    also needs locked candidates (pointing / claiming) or
 *               naked / hidden pairs
 *   GEN_EXPERT  those aren't enough (needs chains or trial and error)
 *
 * Runs in well under a second for Easy to Hard on the ESP32-P4; Expert can
 * take a few tries. Call from a worker task, not the LVGL task.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A puzzle's level (the generator's and the grader's). */
typedef enum {
    GEN_EASY = 0,                       /**< Easy */
    GEN_MEDIUM,                         /**< Medium */
    GEN_HARD,                           /**< Hard */
    GEN_EXPERT,                         /**< Expert */
    GEN_LEVELS                          /**< how many */
} gen_level_t;

/**
 * @brief A level's name.
 * @param level  0..GEN_LEVELS-1
 * @return "Easy", "Medium", "Hard", "Expert"
 */
const char *gen_level_name(int level);

/** @brief The variants. The numbers are stored in sudoku.db (generated.variant): don't reorder. */
typedef enum {
    VAR_CLASSIC = 0,                    /**< Classic */
    VAR_X,                              /**< X-Sudoku: the diagonals too */
    VAR_MINI,                           /**< Mini 6x6 */
    VAR_HYPER,                          /**< Hyper: four windows too */
    VAR_JIGSAW,                         /**< Jigsaw: regions instead of boxes */
    VAR_KILLER,                         /**< Killer: cages and their sums */
    VAR_KNIGHT,                         /**< Anti-Knight */
    VAR_EVENODD,                        /**< Even-Odd */
    VAR_CONSEC,                         /**< Consecutive */
    VAR_THERMO,                         /**< Thermo */
    VAR_SANDWICH,                       /**< Sandwich */
    VAR_KIDS,                           /**< Kids 4x4 */
    VAR_KING,                           /**< Anti-King */
    VAR_NONCONSEC,                      /**< Non-Consecutive */
    VAR_DISJOINT,                       /**< Disjoint Groups */
    VAR_ASTERISK,                       /**< Asterisk */
    VAR_KROPKI,                         /**< Kropki */
    VAR_XV,                             /**< XV */
    VAR_GT,                             /**< Greater-Than */
    VAR_QUAD,                           /**< Quadruple */
    VAR_ARROW,                          /**< Arrow */
    VAR_RENBAN,                         /**< Renban */
    VAR_WHISPER,                        /**< German Whispers */
    VAR_SKY,                            /**< Skyscraper */
    VAR_LK,                             /**< Little Killer */
    VAR_PALINDROME,                     /**< Palindrome */
    VAR_REGIONSUM,                      /**< Region Sum Lines */
    VAR_XSUM,                           /**< X-Sums */
    VAR_ZIPPER,                         /**< Zipper Lines */
    VAR_FRAME,                          /**< Frame */
    VAR_BETWEEN,                        /**< Between Lines */
    VAR_ENTROPIC,                       /**< Entropic Lines */
    VAR_MODULAR,                        /**< Modular Lines */
    VAR_CENTERDOT,                      /**< Center Dot */
    VAR_OUTSIDE,                        /**< Outside Sudoku */
    VAR_ROSSINI,                        /**< Rossini */
    VAR_DIAGNC,                         /**< Diagonal Non-Consecutive */
    VAR_COUNT                           /**< how many */
} sudoku_variant_t;

/**
 * @brief A variant's name.
 * @param variant  0..VAR_COUNT-1
 * @return "Classic", "X-Sudoku", "Mini 6x6", "Hyper", "Jigsaw", "Killer", "Anti-Knight", "Even-Odd",
 *         "Consecutive", "Thermo", "Sandwich", "Kids 4x4", "Anti-King", "Non-Consecutive",
 *         "Disjoint Groups", "Asterisk", "Kropki", "XV", "Greater-Than", "Quadruple", "Arrow",
 *         "Renban", "German Whispers", "Skyscraper", "Little Killer", "Palindrome", "Region Sum Lines",
 *         "X-Sums", "Zipper Lines", "Frame", "Between Lines", "Entropic Lines", "Modular Lines",
 *         "Center Dot", "Outside Sudoku", "Rossini", "Diagonal Non-Consecutive"
 */
const char *variant_name(int variant);

/** @name Units: 0..8 rows, 9..17 columns, 18..26 boxes (or regions), then these
 *  @{ */
#define UNIT_DIAG_MAIN 27               /**< the top-left to bottom-right diagonal */
#define UNIT_DIAG_ANTI 28               /**< the top-right to bottom-left diagonal */
#define UNIT_WINDOW    29               /**< Hyper's windows: 29..32 */
#define UNIT_GROUP     33               /**< Disjoint groups: 33..41 */
#define UNIT_ASTERISK  42               /**< Asterisk's nine cells */
#define UNIT_CENTERDOT 43               /**< Center Dot's nine box centres */
#define UNITS_MAX      44               /**< units at most */
/** @} */
#define CAGES_MAX      81               /**< Killer: cages per board (1 cell each at most) */
#define CAGE_NONE      0xFF             /**< a cell in no cage */
#define THERMOS_MAX    16               /**< lines per board */
#define THERMO_NONE    0xFF             /**< a cell on no line */
/** @brief The kind of lines (sudoku_geom_t.line_kind). */
enum {
    LINE_THERMO = 0,                    /**< Thermo: digits rise from the bulb */
    LINE_ARROW,                         /**< Arrow: the shaft adds up to the circle */
    LINE_RENBAN,                        /**< Renban: a run of consecutive digits, any order */
    LINE_WHISPER,                       /**< German Whispers: neighbours differ by 5 or more */
    LINE_PALINDROME,                    /**< Palindrome: the same digits read from either end */
    LINE_REGIONSUM,                     /**< Region Sum Lines: the same sum in each box it crosses */
    LINE_ZIPPER,                        /**< Zipper Lines: cells mirrored about the middle add up the same */
    LINE_BETWEEN,                       /**< Between Lines: digits strictly between the two end circles' */
    LINE_ENTROPIC,                      /**< Entropic Lines: any three in a row: a low, a middle and a high digit */
    LINE_MODULAR                        /**< Modular Lines: any three in a row: one of each remainder mod 3 */
};
#define LK_MAX         16               /**< Little Killer: diagonals per board */
#define LK_NONE        0xFF             /**< a cell on no diagonal */
/** @brief Little Killer: a diagonal's direction from its clue: a clue above a column points down-right or
 * down-left, one left of a row down-right or up-right. The board draws a clue on the line between
 * two cells, half a cell before its first cell (the generator keeps one clue per place). */
enum {
    LK_DOWN_RIGHT = 0,                  /**< down and right */
    LK_DOWN_LEFT,                       /**< down and left */
    LK_UP_RIGHT                         /**< up and right */
};
#define QUADS_MAX      32               /**< Quadruple: circles per board */
#define QUAD_NONE      0xFF             /**< no circle */

/** @brief Kropki, XV, Greater-Than: the kind of edge marks (sudoku_geom_t.edge_kind). */
enum {
    EDGE_NONE = 0,                      /**< none */
    EDGE_KROPKI,                        /**< Kropki's dots */
    EDGE_XV,                            /**< XV's letters */
    EDGE_GT                             /**< Greater-Than's signs */
};
/** @brief Kropki's marks. */
enum {
    MARK_NONE = 0,                      /**< no mark */
    MARK_WHITE = 1,                     /**< a white dot: 1 apart */
    MARK_BLACK = 2                      /**< a black dot: one is double the other */
};
/** @brief XV's marks. */
enum {
    MARK_V = 1,                         /**< V: they add up to 5 */
    MARK_X = 2                          /**< X: they add up to 10 */
};
/** @brief Greater-Than's marks. */
enum {
    MARK_LESS = 1,                      /**< the left / top cell is smaller */
    MARK_MORE = 2                       /**< the left / top cell is bigger */
};
#define EDGE_RIGHT(e) ((e) & 3)                 /**< the mark on the line to the right of a cell */
#define EDGE_DOWN(e)  (((e) >> 2) & 3)          /**< ... below it */

/** @brief A board: its size, units, peers and every variant's extras (cages, marks, lines, clues). */
typedef struct {
    uint8_t variant;                    /**< sudoku_variant_t */
    uint8_t n;                          /**< digits and side: 9, 6 or 4 */
    uint8_t box_h;                      /**< a box's height: 3, or 2 (6x6, 4x4) */
    uint8_t box_w;                      /**< a box's width: 3 (9x9, 6x6), or 2 (4x4) */
    bool diag;                          /**< the two diagonals are units too */
    bool windows;                       /**< the four Hyper windows are units too */
    uint8_t region[81];                 /**< box (Jigsaw: region) of each cell, 0..n-1 */
    uint16_t all;                       /**< candidate bits 1..n */
    uint8_t ncells;                     /**< the cells in use */
    uint8_t cells[81];                  /**< which */
    bool used[81];                      /**< a cell is in use (Mini, Kids: the top-left n x n) */
    uint8_t nunits;                     /**< the units in use */
    uint8_t unit_ids[UNITS_MAX];        /**< which */
    bool active[UNITS_MAX];             /**< a unit is in use */
    uint8_t unit[UNITS_MAX][9];         /**< cells of each unit in use (n of them) */
    uint8_t ncu[81];                    /**< units of each cell: how many */
    uint8_t cu[81][5];                  /**< units of each cell: row, column, box (+ diagonals, window, group...) */
    uint8_t npeers[81];                 /**< peers of each cell: how many */
    uint8_t peers[81][32];              /**< cells sharing a unit */
    uint32_t peer_bits[81][3];          /**< the same as bits (cell / 32, cell % 32) */
    /* Killer: cages (their cells are peers too: no digit twice in a cage) */
    uint8_t ncages;                     /**< Killer: cages */
    uint8_t cage_of[81];                /**< each cell's cage, CAGE_NONE = not in a cage */
    uint8_t cage_sum[CAGES_MAX];        /**< each cage's sum */
    uint8_t cage_n[CAGES_MAX];          /**< each cage's cells: how many */
    uint8_t cage_cell[CAGES_MAX][9];    /**< cells of each cage, the top-left one first */
    uint16_t allow[81];                 /**< digits each cell may hold (candidate bits; Even-Odd narrows its marked cells) */
    uint8_t parity[81];                 /**< Even-Odd: PARITY_NONE / _EVEN / _ODD */
    bool consec_on;                     /**< Consecutive: the bars are rules */
    uint8_t consec[81];                 /**< Consecutive: bars, bit CONSEC_RIGHT = to the cell on the right, CONSEC_DOWN = below */
    uint8_t nxpeers[81];                /**< peers that share no unit (Anti-Knight: the knight's moves; Anti-King: the touching cells): how many */
    uint8_t xpeers[81][8];              /**< which */
    /* Thermo, Arrow, Renban, German Whispers, Palindrome, Region Sum, Zipper, Between Lines: lines,
     * each a path of cells (side by side or diagonal), of the kind line_kind: a thermometer from its
     * bulb (digits rise along it), an arrow from its circle (the rest, its shaft, adds up to it), a
     * Renban, a Whispers, a Palindrome, a Region Sum, a Zipper, an Entropic or a Modular line, or a
     * Between line (a circle at each end) */
    uint8_t line_kind;                  /**< the lines' kind, LINE_* */
    uint8_t nthermos;                   /**< lines */
    uint8_t thermo_n[THERMOS_MAX];      /**< each line's cells: how many */
    uint8_t thermo_cell[THERMOS_MAX][9];/**< each line's cells, from its bulb (or circle) */
    uint8_t thermo_of[81];              /**< each cell's line, THERMO_NONE = on none (a cell is on one at most) */
    uint8_t thermo_pos[81];             /**< place along it, 0 = the bulb */
    uint8_t between_need[THERMOS_MAX];  /**< Between Lines: the most of a line's cells between its circles that share a row,
                                         *   column or box (different digits: the circles at least this + 1 apart) */
    bool sandwich;                      /**< Sandwich: the sums are rules */
    uint8_t sandwich_sum[18];           /**< per row (0..8) and column (9..17), the sum between its 1 and 9 */
    bool skyscraper;                    /**< Skyscraper: the clues are rules */
    uint8_t sky[18];                    /**< per row (0..8, seen from the left) and column (9..17, from the top), the buildings seen, 0 = no clue */
    bool xsums;                         /**< X-Sums: the clues are rules */
    uint8_t xsum[18];                   /**< per row (0..8, from the left) and column (9..17, from the top), the sum of the first X digits, 0 = no clue */
    bool frame;                         /**< Frame: the clues are rules */
    uint8_t frame_sum[18];              /**< per row (0..8, from the left) and column (9..17, from the top), the sum of the first 3 digits, 0 = no clue */
    bool outside;                       /**< Outside Sudoku: the clues are rules */
    uint16_t outside_set[18];           /**< per row and column (as frame_sum), the digits among its first 3 (bit d = d), 0 = no clue */
    bool rossini;                       /**< Rossini: the arrows are rules (every row and column has one, or none: neither) */
    uint8_t rossini_dir[18];            /**< per row and column (as frame_sum), its first 3 digits: ROSSINI_* */
    bool diag_nc;                       /**< Diagonal Non-Consecutive: cells touching at a corner aren't 1 apart */
    /* Little Killer: diagonals, each from its first cell (on the top row or the left column) in
     * its direction (LK_*), with the sum of its cells; a cell is on two at most (lk_of: the
     * down-right one, then the other way) */
    uint8_t nlk;                        /**< Little Killer: diagonals */
    uint8_t lk_dir[LK_MAX];             /**< each one's direction, LK_* */
    uint8_t lk_sum[LK_MAX];             /**< its sum */
    uint8_t lk_n[LK_MAX];               /**< its cells: how many */
    uint8_t lk_cell[LK_MAX][9];         /**< its cells, from the first */
    bool lk_from_top[LK_MAX];           /**< its clue above the column (else left of the row) */
    uint8_t lk_of[81][2];               /**< each cell's diagonals (the down-right one, then the other), LK_NONE padded */
    uint8_t edge_kind;                  /**< Kropki, XV, Greater-Than: EDGE_* */
    uint8_t edge[81];                   /**< per cell, its marks (EDGE_RIGHT, EDGE_DOWN) */
    /* Quadruple: circles, each at the corner where the 2x2 cells from quad_at meet, with
     * 1-4 digits (sorted) that must be among them */
    uint8_t nquads;                     /**< Quadruple: circles */
    uint8_t quad_at[QUADS_MAX];         /**< each one's top-left cell (the circle at its bottom-right corner) */
    uint8_t quad_n[QUADS_MAX];          /**< its digits: how many, 1-4 */
    uint8_t quad_dig[QUADS_MAX][4];     /**< its digits, sorted */
    uint8_t quad_of[81][4];             /**< the circles at a cell's corners, QUAD_NONE padded */
} sudoku_geom_t;

/** @brief Even-Odd's marks (sudoku_geom_t.parity). */
enum {
    PARITY_NONE = 0,                    /**< no mark */
    PARITY_EVEN,                        /**< even */
    PARITY_ODD                          /**< odd */
};
#define CONSEC_RIGHT 1                  /**< Consecutive: a bar to the right */
#define CONSEC_DOWN  2                  /**< Consecutive: a bar below */

/** @brief Builds the tables (once); call early, before tasks use them. */
void sudoku_geom_init(void);
/**
 * @brief The fixed variants; unknown ones, and VAR_JIGSAW (which needs its regions), give classic.
 * @param variant  sudoku_variant_t
 * @return its board
 */
const sudoku_geom_t *sudoku_geom(int variant);
/**
 * @brief A Jigsaw board from its region map (81 cells, region 0..8 each, 9 cells
 * per region) into out; false if the map isn't one.
 * @param regions  the region of each cell
 * @param out  gets the board
 * @return false if the map isn't one
 */
bool sudoku_geom_jigsaw(const uint8_t regions[81], sudoku_geom_t *out);
/**
 * @brief A Killer board from its cages as text: "sum:cell,cell,...;sum:cell,..."
 * (cells 0..80 row by row, each in one cage at most), into out; false if
 * the text isn't one. sudoku_cages_text() writes it (false: buf too small).
 * @param text  the cages
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_killer_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Killer board's cages as text (sudoku_geom_killer_text()'s form).
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_cages_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief Even-Odd / Consecutive / Kropki / XV / Greater-Than boards from their marks
 * as text, 81 characters row by row: Even-Odd '.', 'e' (even) or 'o' (odd);
 * Consecutive '0'-'3' (bit 1 = a bar to the right, bit 2 = a bar below);
 * Kropki, XV, Greater-Than '0'-'8': the mark to the right + 3 x the mark below
 * (MARK_*). sudoku_marks_text() writes it (buf: at least 82).
 * @param variant  VAR_EVENODD, VAR_CONSEC, VAR_KROPKI, VAR_XV or VAR_GT
 * @param text  the marks
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_marks(int variant, const char *text, sudoku_geom_t *out);
/**
 * @brief A board's marks as text (sudoku_geom_marks()'s form).
 * @param g  the board
 * @param buf  gets it (82 at least)
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_marks_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief A Thermo board from its thermometers as text: "cell,cell,...;cell,..."
 * (each from its bulb; cells 0..80, side by side or diagonal neighbours, on
 * one thermometer at most); sudoku_thermos_text() writes it.
 * @param text  the thermometers
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_thermo_text(const char *text, sudoku_geom_t *out);
/**
 * @brief The same for any line variant (Thermo, Arrow: its circle first, Renban, German Whispers,
 * Palindrome, Region Sum, Zipper, Between Lines: its circles at both ends, Entropic, Modular);
 * sudoku_thermos_text() writes them all. A Palindrome line's cells that mirror each other mustn't see
 * each other (they hold the same digit); a Between, Entropic or Modular line has 3 cells at least.
 * @param variant  VAR_THERMO, VAR_ARROW, VAR_RENBAN, VAR_WHISPER, VAR_PALINDROME, VAR_REGIONSUM,
 *                 VAR_ZIPPER, VAR_BETWEEN, VAR_ENTROPIC or VAR_MODULAR
 * @param text  the lines
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_lines_text(int variant, const char *text, sudoku_geom_t *out);
/**
 * @brief A board's lines as text (sudoku_geom_lines_text()'s form).
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_thermos_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief A Sandwich board from its clues as text: 18 numbers 0..35, the rows top
 * to bottom then the columns left to right, comma-separated;
 * sudoku_sandwich_text() writes it.
 * @param text  the clues
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_sandwich_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Sandwich board's clues as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_sandwich_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief A Skyscraper board from its clues as text: 18 numbers 0..9 (0: no clue), the rows from the
 * left top to bottom, then the columns from the top left to right, comma-separated (kept in
 * marks); sudoku_sky_text() writes it.
 * @param text  the clues
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_sky_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Skyscraper board's clues as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_sky_text(const sudoku_geom_t *g, char *buf, int len);
/** @brief Rossini: a row's or column's first three digits, seen from its clue (rossini_dir). */
enum {
    ROSSINI_NONE = 0,                   /**< no arrow: they neither rise nor fall */
    ROSSINI_UP,                         /**< they rise away from the clue (the arrow points into the grid) */
    ROSSINI_DOWN                        /**< they fall away from the clue (the arrow points out) */
};
/**
 * @brief An Outside Sudoku board from its clues as text: 18 entries, comma-separated: 0 (no clue) or
 * 1-3 different digits 1-9 (e.g. "37"), the rows from the left top to bottom, then the columns from
 * the top left to right (kept in marks); sudoku_outside_text() writes it.
 * @param text  the clues
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_outside_text(const char *text, sudoku_geom_t *out);
/**
 * @brief An Outside Sudoku board's clues as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_outside_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief A Rossini board from its arrows as text: 18 numbers 0..2 (ROSSINI_*), the rows from the left,
 * then the columns from the top, comma-separated (kept in marks); sudoku_rossini_text() writes it.
 * @param text  the arrows
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_rossini_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Rossini board's arrows as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_rossini_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief An X-Sums board from its clues as text: 18 numbers 0..45 (0: no clue), the rows from the
 * left top to bottom, then the columns from the top left to right, comma-separated (kept in
 * marks, like Skyscraper's); sudoku_xsum_text() writes it.
 * @param text  the clues
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_xsum_text(const char *text, sudoku_geom_t *out);
/**
 * @brief An X-Sums board's clues as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_xsum_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief A Frame board from its clues as text: 18 numbers, 0 (no clue) or 6..24, the rows from the
 * left top to bottom, then the columns from the top left to right, comma-separated (kept in marks,
 * like Skyscraper's); sudoku_frame_text() writes it.
 * @param text  the clues
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_frame_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Frame board's clues as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_frame_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief X-Sums: the first digits (X) a clue allows: X itself and X - 1 other digits must be able to
 * add up to it (1: X = 1; 2: none; 45: X = 9).
 * @param clue  the clue, 1..45
 * @return the digits (bit d = d)
 */
uint16_t sudoku_xsum_firsts(int clue);
/**
 * @brief X-Sums: the clue of a row or column with these digits in it (the sum of the first X
 * from the clue's side, X being the first).
 * @param vals  the cells
 * @param lu  the row (0..8, from the left) or column (9..17, from the top)
 * @return the sum, 0 if the first X aren't all filled
 */
int sudoku_xsum_of(const uint8_t vals[81], int lu);
/**
 * @brief A Little Killer board from its clues as text: "top|left index,direction,sum;..." e.g.
 * "t3,r,21;l5,u,14" (t: above column 0..8, l: left of row 0..8; r down-right, l down-left
 * (from the top only), u up-right (from the left only)), kept where Killer keeps its cages;
 * sudoku_lk_text() writes it.
 * @param text  the clues
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_lk_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Little Killer board's clues as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_lk_text(const sudoku_geom_t *g, char *buf, int len);
/**
 * @brief A Quadruple board from its circles as text: "cell:digits;cell:digits;..." (cell
 * 0..79 the top-left of the four, not in the last column; 1-4 digits 1-9 each);
 * sudoku_quads_text() writes it. Kept where Killer keeps its cages.
 * @param text  the circles
 * @param out  gets the board
 * @return false if the text isn't one
 */
bool sudoku_geom_quad_text(const char *text, sudoku_geom_t *out);
/**
 * @brief A Quadruple board's circles as text.
 * @param g  the board
 * @param buf  gets it
 * @param len  its size
 * @return false: buf too small
 */
bool sudoku_quads_text(const sudoku_geom_t *g, char *buf, int len);

/**
 * @brief Kropki, XV, Greater-Than: may a (the left / top cell) and b (its neighbour) stand
 * with that mark between them?
 * @param kind  EDGE_*
 * @param mark  MARK_* (0: none)
 * @param a  the left / top cell's digit
 * @param b  its neighbour's
 * @return true if they may
 */
bool is_sudoku_edge_ok(int kind, int mark, int a, int b);
/**
 * @brief Region Sum Lines: the line's digits added up box by box (a box it enters twice counts
 * once, both parts together).
 * @param g  the board
 * @param t  the line
 * @param vals  the cells, 0 = empty
 * @param sum  gets each part's sum, in the order the line enters the boxes (9 at most)
 * @param full  gets whether each part is filled
 * @return the parts
 */
int sudoku_regionsum_parts(const sudoku_geom_t *g, int t, const uint8_t vals[81], int sum[9], bool full[9]);
/**
 * @brief Quadruple: how many of circle q's digits the filled cells of vals still miss
 * (*empty: its empty cells)
 * @param g  the board
 * @param q  the circle
 * @param vals  the cells, 0 = empty
 * @param empty  gets its empty cells
 * @return the digits missed
 */
int sudoku_quad_missing(const sudoku_geom_t *g, int q, const uint8_t vals[81], int *empty);

/**
 * @brief Two cells share a unit (or a cage).
 * @param g  the board
 * @param a  a cell
 * @param b  another
 * @return true if so
 */
static inline bool is_sudoku_geom_peer(const sudoku_geom_t *g, int a, int b)
{
    return (g->peer_bits[a][b >> 5] >> (b & 31)) & 1u;
}

/**
 * @brief A cell is on a diagonal that is a unit (X-Sudoku).
 * @param g  the board
 * @param cell  the cell
 * @return true if so
 */
static inline bool is_sudoku_geom_on_diag(const sudoku_geom_t *g, int cell)
{
    int r = cell / 9, c = cell % 9;
    return g->diag && (r == c || r + c == g->n - 1);
}

/**
 * @brief in a Hyper window: -1, or the window 0..3
 * @param g  the board
 * @param cell  the cell
 * @return -1, or the window 0..3
 */
static inline int sudoku_geom_window(const sudoku_geom_t *g, int cell)
{
    int r = cell / 9, c = cell % 9;
    if (!g->windows || r % 4 == 0 || c % 4 == 0) {
        return -1;
    }
    return (r > 4) * 2 + (c > 4);
}

/**
 * @brief on the Asterisk (9x9: rows and columns 1, 2, 4, 6, 7 around the centre)
 * @param g  the board
 * @param cell  the cell
 * @return true if so
 */
static inline bool is_sudoku_geom_on_asterisk(const sudoku_geom_t *g, int cell)
{
    if (g->variant != VAR_ASTERISK) {
        return false;
    }
    switch (cell) {                     /* (a switch: this header is C++ too, no designated array init) */
    case 13:
    case 20:
    case 24:
    case 37:
    case 40:
    case 43:
    case 56:
    case 60:
    case 67:
        return true;
    default:
        return false;
    }
}

/**
 * @brief a box's centre on a Center Dot board (rows and columns 2, 5 and 8, counted from 1)
 * @param g  the board
 * @param cell  the cell
 * @return true if so
 */
static inline bool is_sudoku_geom_on_centerdot(const sudoku_geom_t *g, int cell)
{
    return g->variant == VAR_CENTERDOT && cell / 9 % 3 == 1 && cell % 9 % 3 == 1;
}

/**
 * @brief Entropic, Modular Lines: the group a digit belongs to (Entropic: 1-3, 4-6, 7-9; Modular:
 * its remainder mod 3)
 * @param kind  LINE_ENTROPIC or LINE_MODULAR
 * @param d  the digit, 1..9
 * @return 0, 1 or 2
 */
static inline int sudoku_line_group(int kind, int d)
{
    return kind == LINE_ENTROPIC ? (d - 1) / 3 : d % 3;
}

/**
 * @brief the cells drawn tinted: X diagonals, Hyper windows, the Asterisk, the Center Dot
 * @param g  the board
 * @param cell  the cell
 * @return true if so
 */
static inline bool is_sudoku_geom_tinted(const sudoku_geom_t *g, int cell)
{
    return is_sudoku_geom_on_diag(g, cell) || sudoku_geom_window(g, cell) >= 0 || is_sudoku_geom_on_asterisk(g, cell) ||
           is_sudoku_geom_on_centerdot(g, cell);
}

/* ---------------------------------------------------------------- generating */

/** @brief A hook the generator and solver call now and then in their long loops: weak, a no-op unless
 * the program defines its own (the firmware: a tick for the idle task, against the task watchdog). */
void sudoku_gen_yield(void);

/**
 * @brief Fills givens (81, 0 = empty) with a new puzzle of that variant and level.
 * seed: any random number. board (may be NULL) gets the puzzle's geometry:
 * for Jigsaw its regions (board->region), for Killer its cages
 * (sudoku_cages_text()). Gives up after a number of tries and returns the
 * closest one found (*got_level says what it is). False only if out of
 * tries for anything at all (shouldn't happen). Not reentrant: one
 * generator at a time.
 * @param variant  sudoku_variant_t
 * @param level  the level
 * @param seed  any random number
 * @param givens  gets the puzzle
 * @param board  gets its board, or NULL
 * @param got_level  gets the level it is
 * @return false only if out of tries for anything at all
 */
bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], sudoku_geom_t *board,
                     gen_level_t *got_level);

/**
 * @brief Level of a puzzle by the techniques it needs; -1 if it doesn't have
 * exactly one solution. (Generator task.)
 * @param g  the board
 * @param givens  the puzzle
 * @return gen_level_t, or -1
 */
int sudoku_grade(const sudoku_geom_t *g, const uint8_t givens[81]);

/**
 * @brief Number of solutions, counting up to limit. (Generator task.)
 * @param g  the board
 * @param cells  the puzzle
 * @param limit  stop counting there
 * @return the solutions, at most limit
 */
int sudoku_count_solutions(const sudoku_geom_t *g, const uint8_t cells[81], int limit);
/**
 * @brief A classic puzzle has exactly one solution (its own scratch: for the
 * database task, which checks imported puzzles while the generator may run).
 * @param cells  the puzzle
 * @return true if so
 */
bool is_sudoku_classic_unique(const uint8_t cells[81]);
/**
 * @brief The same on any board (its own work space, like is_sudoku_classic_unique: for the
 * database task's import, alongside the UI's own solving); gives up (false) on a
 * puzzle that takes too long.
 * @param g  the board
 * @param cells  the puzzle
 * @return true if so
 */
bool is_sudoku_unique(const sudoku_geom_t *g, const uint8_t cells[81]);

/**
 * @brief Digit d may go in the empty cell by every rule of the board, as far as the digits in vals
 * (filled, 0 = empty) tell: no solving ahead, just the rules against what's there (the
 * variant pages' worked examples, tools/variant_examples.c).
 * @param g  the board
 * @param vals  the cells, 0 = empty
 * @param cell  the cell
 * @param d  the digit
 * @return true if it fits
 */
bool is_sudoku_fits(const sudoku_geom_t *g, const uint8_t vals[81], int cell, int d);
/** @brief The same, and which rule says no: FIT_OK (it fits), or the first rule that rules d out */
enum {
    FIT_OK = 0,                         /**< it fits */
    FIT_FILLED,                         /**< the cell isn't empty */
    FIT_BROKEN,                         /**< not a cell of the board, or not a digit */
    FIT_UNIT,                           /**< a row, column, box or other unit has d */
    FIT_XPEER,                          /**< Anti-Knight, Anti-King: a cell that sees it has d */
    FIT_ALLOW,                          /**< Even-Odd: the cell's mark */
    FIT_CONSEC,                         /**< Consecutive, Non-Consecutive: a neighbour */
    FIT_EDGE,                           /**< Kropki, XV, Greater-Than: a mark */
    FIT_QUAD,                           /**< Quadruple: a circle's digits */
    FIT_SANDWICH_ROW,                   /**< Sandwich: the row's sum */
    FIT_SANDWICH_COL,                   /**< Sandwich: the column's sum */
    FIT_THERMO,                         /**< a line (Thermo, Arrow, Renban, Whispers, Palindrome, Region Sum, ...) */
    FIT_CAGE,                           /**< Killer: the cage's sum */
    FIT_SKY_ROW,                        /**< Skyscraper: the row's clue */
    FIT_SKY_COL,                        /**< Skyscraper: the column's clue */
    FIT_LK,                             /**< Little Killer: a diagonal's sum */
    FIT_XSUM_ROW,                       /**< X-Sums: the row's clue */
    FIT_XSUM_COL,                       /**< X-Sums: the column's clue */
    FIT_FRAME_ROW,                      /**< Frame: the row's clue */
    FIT_FRAME_COL,                      /**< Frame: the column's clue */
    FIT_OUTSIDE_ROW,                    /**< Outside Sudoku: the row's clue */
    FIT_OUTSIDE_COL,                    /**< Outside Sudoku: the column's clue */
    FIT_ROSSINI_ROW,                    /**< Rossini: the row's arrow (or none) */
    FIT_ROSSINI_COL,                    /**< Rossini: the column's arrow (or none) */
    FIT_DIAGNC                          /**< Diagonal Non-Consecutive: a cell touching it at a corner */
};
/**
 * @brief Which rule says no to digit d in the cell (is_sudoku_fits()'s answer, with why).
 * @param g  the board
 * @param vals  the cells, 0 = empty
 * @param cell  the cell
 * @param d  the digit
 * @return FIT_OK, or the first rule that rules d out
 */
int sudoku_fits_why(const sudoku_geom_t *g, const uint8_t vals[81], int cell, int d);

/**
 * @brief Solves `cells` (81, row-major, 0 = empty) into `out`. False if the givens
 * break a rule or there is no solution. Milliseconds on the ESP32-P4. (UI task.)
 * @param g  the board
 * @param cells  the puzzle
 * @param out  gets the solution
 * @return false if the givens break a rule or there is no solution
 */
bool sudoku_solve(const sudoku_geom_t *g, const uint8_t cells[81], uint8_t out[81]);

/**
 * @brief A unit's cell.
 * @param g  the board
 * @param unit  the unit
 * @param k  0..n-1
 * @return its k-th cell, or -1
 */
int sudoku_unit_cell(const sudoku_geom_t *g, int unit, int k);

/** @brief The techniques hints explain. Naked triple, X-Wing, Swordfish and XY-Wing are for hints and Learn
 * only: grading (sudoku_grade) doesn't use them, so Hard still means "pairs or locked candidates" and
 * Expert "more than that". */
typedef enum {
    TECH_NONE = 0,           /**< none */
    TECH_POINTING,           /**< pointing: a box's digit in one line */
    TECH_CLAIMING,           /**< claiming: a line's digit in one box */
    TECH_NAKED_PAIR,         /**< naked pair */
    TECH_HIDDEN_PAIR,        /**< hidden pair */
    TECH_CAGE,               /**< Killer: a cage's sum */
    TECH_CONSEC,             /**< Consecutive: the bars */
    TECH_THERMO,             /**< Thermo: a thermometer */
    TECH_SANDWICH,           /**< Sandwich: a sum */
    TECH_NAKED_TRIPLE,       /**< naked triple (hints and Learn only) */
    TECH_X_WING,             /**< X-Wing (hints and Learn only) */
    TECH_SWORDFISH,          /**< Swordfish (hints and Learn only) */
    TECH_XY_WING,            /**< XY-Wing (hints and Learn only) */
    TECH_EDGE,               /**< Kropki, XV, Greater-Than: the marks */
    TECH_QUAD,               /**< Quadruple: a circle */
    TECH_LINE,               /**< Arrow, Renban, Whispers, Palindrome, Region Sum, Zipper, Between: a line */
    TECH_SKY,                /**< Skyscraper: a clue */
    TECH_LK,                 /**< Little Killer: a diagonal */
    TECH_XSUM,               /**< X-Sums: a clue */
    TECH_FRAME,              /**< Frame: a clue */
    TECH_OUTSIDE,            /**< Outside Sudoku: a clue */
    TECH_ROSSINI,            /**< Rossini: an arrow (or none) */
    TECH_DIAGNC              /**< Diagonal Non-Consecutive: the cells touching at a corner */
} tech_kind_t;
/** @brief A technique a hint explains: which, where, with which digits and cells. */
typedef struct {
    int kind;                /**< tech_kind_t */
    /** pointing: box, line; claiming: line, box; pairs, triple: the unit; cage: the
     *  cage; consecutive: the cell whose candidates its neighbours narrowed;
     *  thermo: the thermometer; sandwich: the row or column; edge (Kropki, XV,
     *  Greater-Than): the cell its neighbours narrowed; quad: that cell, unit2
     *  the circle; line (Arrow, Renban, Whispers): the line, cell1 the cell;
     *  sky, xsum, frame, outside, rossini: the row or column (0..17), cell1 the cell; lk: the
     *  diagonal, cell1; diagnc: the cell its corners narrowed;
     *  X-Wing, Swordfish: the 2 or 3 base lines (rows 0..8 or columns 9..17; unit3 -1 for X-Wing) */
    int unit;
    int unit2;               /**< the second unit (see unit) */
    int unit3;               /**< the third (see unit) */
    /** pointing/claiming, X-Wing, Swordfish: digit; pairs: the two digits; triple:
     *  bit mask of its three digits; cage: bit mask of the digits its sum still
     *  allows in its empty cells; X-Wing, Swordfish: digit2 = bit mask of the
     *  cover lines' indexes (columns, or rows, 0..8); XY-Wing: digit = the one
     *  ruled out (z), digit2 = the pivot's two digits (bit mask) */
    int digit;
    int digit2;              /**< the second digit (see digit) */
    int cell1;               /**< pairs: the first cell; XY-Wing: a pincer's digits (bit mask) */
    int cell2;               /**< pairs: the second cell; XY-Wing: the other pincer's digits */
    /** triple, X-Wing, Swordfish, XY-Wing: the cells that make it (XY-Wing: the
     *  pivot first, then the two pincers) */
    int ncells;
    uint8_t cells[9];        /**< those cells */
} sudoku_tech_t;

/** @brief What a step is. */
typedef enum {
    STEP_NONE = 0,           /**< no simple step: cell/digit is just the answer for the easiest cell */
    STEP_WRONG,              /**< cell holds a wrong digit */
    STEP_HIDDEN_SINGLE,      /**< digit fits only cell within unit */
    STEP_NAKED_SINGLE,       /**< cell can only take digit */
} step_kind_t;
/** @brief The next step a person would take: a cell, its digit, and the technique before it. */
typedef struct {
    int kind;                /**< step_kind_t */
    int cell;                /**< the cell */
    int digit;               /**< its digit */
    int unit;                /**< hidden single: the unit */
    sudoku_tech_t tech;      /**< before the single, if a technique was needed (kind != TECH_NONE) */
} sudoku_step_t;

/**
 * @brief The next step a person would take on the board vals (81, 0 = empty;
 * givens included), knowing the solution. False if the board is full.
 * @param g  the board
 * @param vals  the cells, 0 = empty
 * @param solution  the solution
 * @param out  gets the step
 * @return false if the board is full
 */
bool sudoku_next_step(const sudoku_geom_t *g, const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out);

/**
 * @brief For Learn: true if, on the board vals, the technique `tech` (TECH_POINTING ...
 * TECH_XY_WING) opens up a single that wasn't there before: no single is left
 * now, and after that technique (and nothing else) one appears. *out is then that
 * single with the technique in out->tech, like sudoku_next_step() gives it. The
 * technique may not be the only way forward; it is one that works there.
 * @param g  the board
 * @param vals  the cells, 0 = empty
 * @param solution  the solution
 * @param tech  tech_kind_t
 * @param out  gets the single
 * @return true if it opens one up
 */
bool sudoku_step_with(const sudoku_geom_t *g, const uint8_t vals[81], const uint8_t solution[81], int tech,
                      sudoku_step_t *out);

#ifdef __cplusplus
}
#endif
