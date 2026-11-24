/**
 * @file learn_core.c
 * @ingroup learn_core
 * @brief Learn's lessons, and the search for a position to practise each one.
 */
#include "learn_core.h"
#include "puzzles.h"
#include <string.h>

/** @brief A lesson. */
typedef struct {
    const char *name;                   /**< its name */
    const char *desc;                   /**< what it does, in a sentence */
    int8_t variant;                     /**< VAR_*, or -1: a technique on the built-in Classic puzzles */
    uint8_t tech;                       /**< the step it shows: TECH_*, or TECH_NONE (a single; for a variant:
                                             one its rule makes) */
    bool or_single;                     /**< a single its rule makes will do too (a rule that mostly works
                                             through the starting candidates: the technique is seldom next) */
} learn_lesson_t;

/** @brief The lessons: the techniques (LEARN_*), then a variant rule each, in New puzzle's tabs' order. */
static const learn_lesson_t LESSONS[] = {
    {"Hidden single", "A digit has only one place left in a row, column or box.", -1, TECH_NONE, false},
    {"Naked single", "A cell has only one digit left that fits.", -1, TECH_NONE, false},
    {"Pointing", "In a box, a digit's places all lie on one line: the rest of that line can't have it.", -1, TECH_POINTING, false},
    {"Claiming", "On a line, a digit's places all lie in one box: the rest of that box can't have it.", -1, TECH_CLAIMING, false},
    {"Naked pair", "Two cells of a unit can only be the same two digits: no other cell there can be either.", -1, TECH_NAKED_PAIR, false},
    {"Hidden pair", "Two digits fit only the same two cells of a unit: those cells can't be anything else.", -1, TECH_HIDDEN_PAIR, false},
    {"Naked triple", "Three cells of a unit can only be the same three digits between them: no other cell there can.", -1, TECH_NAKED_TRIPLE, false},
    {"X-Wing", "In two rows a digit fits only the same two columns: it can't be elsewhere in those columns.", -1, TECH_X_WING, false},
    {"Swordfish", "The same with three rows and three columns (or three columns and three rows).", -1, TECH_SWORDFISH, false},
    {"XY-Wing", "A cell that is x or y sees an x-or-z and a y-or-z cell: one of those is z, so what sees both isn't.", -1, TECH_XY_WING, false},
    /* Grids: extra regions */
    {"X-Sudoku diagonals", "The two long diagonals hold 1-9 too: a digit can have one place left on one.", VAR_X, TECH_NONE, false},
    {"Hyper windows", "The four tinted windows hold 1-9 too: a digit can have one place left in one.", VAR_HYPER, TECH_NONE, false},
    {"Jigsaw regions", "Irregular regions instead of boxes: count a digit's places region by region.", VAR_JIGSAW, TECH_NONE, false},
    {"Disjoint groups", "The cells in the same place in each box hold 1-9 too: a digit can have one place left in a group.", VAR_DISJOINT, TECH_NONE, false},
    {"Asterisk", "The nine asterisk cells hold 1-9 too: a digit can have one place left among them.", VAR_ASTERISK, TECH_NONE, false},
    {"Center Dot", "The nine box centres hold 1-9 too: a digit can have one place left among them.", VAR_CENTERDOT, TECH_NONE, false},
    {"Girandola", "The nine pinwheel cells hold 1-9 too: a digit can have one place left among them.", VAR_GIRANDOLA, TECH_NONE, false},
    {"Magic Square", "The centre box's lines make 15: 5 in the middle, even corners, and each line's third digit follows.", VAR_MAGIC, TECH_MAGIC, false},
    {"Anti-Diagonal", "A long diagonal holds three different digits at most: once it has three, the rest are those.", VAR_ANTIDIAG, TECH_ANTIDIAG, false},
    {"Point Symmetry", "Cells mirrored through the centre add up to 10: a digit gives its mirror away.", VAR_PSYM, TECH_PSYM, true},
    {"Clone areas", "Shaded areas of the same shape hold the same digits: what one cell can't be, its clone can't.", VAR_CLONE, TECH_CLONE, true},
    {"Extra region", "No digit repeats in a shaded region: its cells see each other across rows, columns and boxes.", VAR_REGIONS, TECH_NONE, false},
    {"159 Sudoku", "Columns 1, 5 and 9 say where the row's 1, 5 and 9 are: the two must agree.", VAR_159, TECH_159, false},
    {"Argyle lines", "No digit repeats on an argyle line: the cells along one see each other, like a row's.", VAR_ARGYLE, TECH_NONE, false},
    /* Marks: rules between cells */
    {"Chess Sudoku", "Neither a knight's move nor a king's touch: each digit rules out up to twelve more cells.", VAR_CHESS, TECH_NONE, false},
    {"Anti-Knight", "Cells a knight's move apart can't hold the same digit: they see each other too.", VAR_KNIGHT, TECH_NONE, false},
    {"Anti-King", "Cells that touch, also at a corner, can't hold the same digit: they see each other too.", VAR_KING, TECH_NONE, false},
    {"Non-Consecutive", "Side-by-side cells are never 1 apart: a digit's neighbours rule out the digits next to it.", VAR_NONCONSEC, TECH_CONSEC, false},
    {"Diagonal Non-Consecutive", "Cells touching at a corner are never 1 apart: the corners rule out the digits next to theirs.", VAR_DIAGNC, TECH_DIAGNC, false},
    {"Diagonal Consecutive", "Corner neighbours with a mark are 1 apart, without one they're not: a mark leaves two digits.", VAR_DIAGCONSEC, TECH_DIAGNC, false},
    {"Dutch Flatmates", "Every 5 has a 1 right above it or a 9 right below: a 5 can't go where neither can.", VAR_FLATMATES, TECH_FLATMATES, true},
    {"Quadro", "Every 2x2 block holds an odd and an even digit: three of one parity leave the fourth the other.", VAR_QUADRO, TECH_QUADRO, true},
    {"Battenburg marks", "A mark: the four cells are a checkerboard of odd and even. No mark: they aren't.", VAR_BATTEN, TECH_BATTEN, true},
    {"Anti-Queen 9s", "No two 9s share a diagonal: each 9 rules 9 out along both its diagonals.", VAR_AQ9, TECH_AQ9, true},
    {"Search Nine arrows", "An arrow's digit is how far its 9 is that way: the 9 is never behind an arrow.", VAR_S9, TECH_S9, true},
    {"Even-Odd cells", "A shaded cell is even, a ringed one odd: that alone can leave a digit one place.", VAR_EVENODD, TECH_NONE, false},
    {"Consecutive bars", "A bar: the two cells differ by 1. No bar: they don't. Both narrow down a cell's digits.", VAR_CONSEC, TECH_CONSEC, false},
    {"Kropki dots", "White dot: 1 apart; black dot: one is double the other; no dot: neither.", VAR_KROPKI, TECH_EDGE, false},
    {"XV marks", "V: the two add up to 5; X: to 10; no mark: to neither.", VAR_XV, TECH_EDGE, false},
    {"Greater-Than signs", "The signs inside a box chain up: a cell lies between the cells it's bigger and smaller than.", VAR_GT, TECH_EDGE, false},
    {"Quadruple circles", "A circle's digits must be among its four cells: where else they can go narrows them down.", VAR_QUAD, TECH_QUAD, false},
    {"Mathrax circles", "Both diagonal pairs make the circle's sum, difference, product or quotient: few pairs can.", VAR_MATHRAX, TECH_MATHRAX, false},
    {"Fortress walls", "A grey cell is bigger than each white neighbour: grey cells go high, their neighbours low.", VAR_FORTRESS, TECH_EDGE, false},
    /* Lines */
    {"Thermometer", "Digits rise from the bulb: each cell lies between what the cells before and after it can be.", VAR_THERMO, TECH_THERMO, false},
    {"Arrow", "The shaft adds up to the circle: the circle bounds the shaft's digits, the shaft the circle.", VAR_ARROW, TECH_LINE, false},
    {"Renban line", "A run of consecutive digits in any order: the line's length bounds how far apart they are.", VAR_RENBAN, TECH_LINE, false},
    {"German Whispers", "Neighbours on a green line are 5 or more apart: no 5 on it, and low and high take turns.", VAR_WHISPER, TECH_LINE, false},
    {"Palindrome line", "The line reads the same from both ends: mirrored cells hold the same digit.", VAR_PALINDROME, TECH_LINE, false},
    {"Region Sum line", "Each box's part of the line adds up the same: a short part bounds a long one.", VAR_REGIONSUM, TECH_LINE, false},
    {"Zipper line", "Cells the same distance from the middle add up to the same total: the middle's digit.", VAR_ZIPPER, TECH_LINE, false},
    {"Between line", "The cells between the circles lie between the circles' digits.", VAR_BETWEEN, TECH_LINE, false},
    {"Entropic line", "Any three in a row hold a low, a middle and a high digit: cells three apart match.", VAR_ENTROPIC, TECH_LINE, false},
    {"Modular line", "Any three in a row hold one each of 147, 258 and 369: cells three apart match.", VAR_MODULAR, TECH_LINE, false},
    {"Dutch Whispers", "Neighbours on an amber line are 4 or more apart: a 5 only between a 1 and a 9.", VAR_DUTCH, TECH_LINE, false},
    {"Parity line", "Neighbours on a red line are one odd, one even: one digit sets the whole line's parity.", VAR_PARITY, TECH_LINE, false},
    {"Slow Thermometer", "Digits rise or stay the same: they only rise where two neighbours share a row, column or box.", VAR_SLOW, TECH_THERMO, false},
    {"Nabner line", "No two digits on a brown line are the same or 1 apart: each digit rules out its neighbours.", VAR_NABNER, TECH_LINE, false},
    {"Lockout line", "The diamonds are 4+ apart, the digits between outside their range: never a 5 between.", VAR_LOCKOUT, TECH_LINE, false},
    {"Average line", "The circle is the average of the line's other digits: they add up to it times their count.", VAR_AVG, TECH_LINE, false},
    {"Sequence line", "A line's digits go up or down by one step: two digits on it fix the step, and every other cell.", VAR_SEQ, TECH_LINE, false},
    {"Pill Arrow", "The pill's two cells read as a number are the shaft's sum: its first digit is small.", VAR_PILL, TECH_LINE, false},
    {"Double Arrow", "The two circles add up to the digits between them: each end bounds the other.", VAR_DARROW, TECH_LINE, false},
    {"10 Line", "The line splits into runs that each make 10: a big digit needs small ones beside it.", VAR_TEN, TECH_LINE, false},
    /* Clues */
    {"Killer cage", "A cage's sum allows only some sets of digits: with what's around it, its cells narrow down.", VAR_KILLER, TECH_CAGE, false},
    {"Sumless cage", "No digit repeats in a cage: its cells see each other, like a box's, across box borders.", VAR_SUMLESS, TECH_NONE, false},
    {"Product cage", "A cage's digits multiply to its number: only some sets of digits do, and each must divide it.", VAR_PRODUCT, TECH_CAGE, false},
    {"Sandwich clue", "The sum between a row's 1 and 9 leaves the 1 and the 9 only a few places in that row or column.", VAR_SANDWICH, TECH_SANDWICH, false},
    {"Skyscraper clue", "How many buildings are seen: a 1 means the 9 is first, a 9 that they rise 1 to 9.", VAR_SKY, TECH_SKY, false},
    {"Little Killer clue", "A diagonal's sum, repeats allowed: what's left of it bounds its empty cells.", VAR_LK, TECH_LK, false},
    {"X-Sums clue", "The first X digits add up to the clue, X being the first: few first digits fit.", VAR_XSUM, TECH_XSUM, false},
    {"Frame clue", "The first three digits add up to the clue: only some sets of three fit.", VAR_FRAME, TECH_FRAME, false},
    {"Outside clue", "The clue's digits are among the first three cells: so they're nowhere else on that line.", VAR_OUTSIDE, TECH_OUTSIDE, true},
    {"Rossini arrow", "The first three rise toward the arrow; no arrow: they neither rise nor fall.", VAR_ROSSINI, TECH_ROSSINI, false},
    {"Numbered Rooms clue", "The first digit X says which cell holds the clue: only some X point at a cell that can.", VAR_NROOMS, TECH_NROOM, true},
    {"Odd/Even Skyscraper clue", "Only the seen buildings of the clue's parity count: the 9 is always one of the odd.", VAR_PSKY, TECH_PSKY, false},
    {"Sum Skyscraper clue", "The buildings seen add up to the clue: the 9 is always seen, the rest make up the difference.", VAR_SKYSUM, TECH_SKYSUM, false},
    {"Before Nine clue", "The digits before the 9 add up to the clue: a small clue puts the 9 near the edge.", VAR_B9, TECH_B9, false},
    {"Next to Nine clue", "The clue's digits sit right beside the 9: one digit puts the 9 at an end of its line.", VAR_N9, TECH_N9, true},
    {"Battlefield clue", "X, Y: a line's end digits. Its first X and last Y cells overlap on the clue's sum, or leave it between them.", VAR_BF, TECH_BF, true},
};
#define LESSONS_N ((int)(sizeof(LESSONS) / sizeof(LESSONS[0])))   /**< learn_count() */

/** @brief A random number from the state (xorshift32). */
static uint32_t learn_rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 1;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* (documented in learn_core.h) */
int learn_count(void)
{
    return LESSONS_N;
}

/* (documented in learn_core.h) */
const char *learn_name(int k)
{
    return k >= 0 && k < LESSONS_N ? LESSONS[k].name : "";
}

/* (documented in learn_core.h) */
const char *learn_desc(int k)
{
    return k >= 0 && k < LESSONS_N ? LESSONS[k].desc : "";
}

/* (documented in learn_core.h) */
int learn_group(int k)
{
    switch (learn_variant(k)) {
    case -1:
        return LEARN_TECHNIQUES;
    case VAR_X:
    case VAR_HYPER:
    case VAR_JIGSAW:
    case VAR_DISJOINT:
    case VAR_ASTERISK:
    case VAR_CENTERDOT:
    case VAR_GIRANDOLA:
    case VAR_REGIONS:
    case VAR_CLONE:
    case VAR_159:
    case VAR_ANTIDIAG:
    case VAR_PSYM:
    case VAR_MAGIC:
    case VAR_ARGYLE:
        return LEARN_GRIDS;
    case VAR_KNIGHT:
    case VAR_KING:
    case VAR_CHESS:
    case VAR_NONCONSEC:
    case VAR_DIAGNC:
    case VAR_DIAGCONSEC:
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
    case VAR_QUAD:
    case VAR_FORTRESS:
    case VAR_FLATMATES:
    case VAR_MATHRAX:
    case VAR_QUADRO:
    case VAR_AQ9:
    case VAR_S9:
    case VAR_BATTEN:
        return LEARN_MARKS;
    case VAR_KILLER:
    case VAR_SUMLESS:
    case VAR_PRODUCT:
    case VAR_SANDWICH:
    case VAR_SKY:
    case VAR_LK:
    case VAR_XSUM:
    case VAR_FRAME:
    case VAR_OUTSIDE:
    case VAR_ROSSINI:
    case VAR_NROOMS:
    case VAR_SKYSUM:
    case VAR_PSKY:
    case VAR_B9:
    case VAR_N9:
    case VAR_BF:
        return LEARN_CLUES;
    default:
        return LEARN_LINES;                         /* (Thermo, Arrow, ... Slow Thermo) */
    }
}

/* (documented in learn_core.h) */
const char *learn_group_name(int g)
{
    static const char *const NAMES[LEARN_GROUPS] = {"Techniques", "Grids", "Marks", "Lines", "Clues"};
    return g >= 0 && g < LEARN_GROUPS ? NAMES[g] : "";
}

/* (documented in learn_core.h) */
int learn_variant(int k)
{
    return k >= 0 && k < LESSONS_N ? LESSONS[k].variant : -1;
}

/**
 * @brief Does this step show the technique lesson's technique (a single: one found as such, nothing before it).
 * @param[in] k  LEARN_HIDDEN .. LEARN_HIDDEN_PAIR
 * @param[in] st  the step
 * @return true if so
 */
static bool is_technique_step(int k, const sudoku_step_t *st)
{
    if (k == LEARN_HIDDEN) {
        return st->kind == STEP_HIDDEN_SINGLE && !st->tech.kind;
    }
    if (k == LEARN_NAKED) {
        return st->kind == STEP_NAKED_SINGLE && !st->tech.kind;
    }
    return LESSONS[k].tech && st->tech.kind == LESSONS[k].tech;
}

/**
 * @brief A naked single on a Classic board: a cell only one digit fits (singles are found as hidden ones first,
 *        so the hint engine seldom names this one itself).
 * @param[in] vals  the board
 * @param[in] sol  the solution
 * @param[out] st  gets the step
 * @return true if there is one
 */
static bool find_naked(const uint8_t vals[81], const uint8_t sol[81], sudoku_step_t *st)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    for (int c = 0; c < 81; c++) {
        if (vals[c]) {
            continue;
        }
        uint16_t m = 0x3FE;
        for (int p = 0; p < G->npeers[c]; p++) {
            m &= (uint16_t)~(1u << vals[G->peers[c][p]]);
        }
        if (__builtin_popcount(m) == 1 && __builtin_ctz(m) == sol[c]) {
            memset(st, 0, sizeof(*st));
            st->kind = STEP_NAKED_SINGLE;
            st->cell = c;
            st->digit = sol[c];
            st->unit = -1;
            return true;
        }
    }
    return false;
}

/**
 * @brief A technique lesson: built-in puzzles played forward until the next step is the technique (the harder
 *        collections for the harder ones; the four rarest: until the singles run out and it opens one up).
 * @param[in] k  the lesson
 * @param[in] seed  the random state's start
 * @param[out] out  gets the position
 * @return false: none found
 */
static bool find_technique(int k, uint32_t seed, learn_found_t *out)
{
    static uint8_t vals[81], sol[81];
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    const bool rare = k >= LEARN_NAKED_TRIPLE;      /* (sudoku_step_with(): the hint engine seldom reaches them first) */
    uint32_t r = seed;
    for (int tries = 0; tries < 2000; tries++) {    /* (pairs are the rarest: ~200 puzzles) */
        int c = (int)((seed + (uint32_t)tries * 7) % PUZZLE_COLLECTION_COUNT);
        if (k >= LEARN_POINTING && strstr(puzzle_collection(c)->name, "Easy")) {
            continue;
        }
        uint16_t id = (uint16_t)(puzzle_collection(c)->first + learn_rnd(&r) % puzzle_collection(c)->count);
        puzzle_load(id, vals);
        if (!sudoku_solve(G, vals, sol)) {
            continue;
        }
        for (int step = 0; step < 81; step++) {
            sudoku_step_t st, w;
            bool found = false;
            if (k == LEARN_NAKED && step >= 8 && find_naked(vals, sol, &st)) {
                found = true;
            } else if (!sudoku_next_step(G, vals, sol, &st) || st.cell < 0) {
                break;
            } else if (rare && (st.tech.kind || st.kind == STEP_NONE) &&
                       sudoku_step_with(G, vals, sol, LESSONS[k].tech, &w)) {
                st = w;                             /* stuck on singles, and this technique opens one */
                found = true;
            } else if (!rare && is_technique_step(k, &st)) {
                found = true;
            }
            if (found) {
                out->puzzle = id;
                memcpy(out->vals, vals, 81);
                out->step = st;
                return true;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    return false;
}

/**
 * @brief A single the variant's rule made: one that the same digits on a Classic board wouldn't give (a hidden
 *        single in a region of the variant's own, or one whose other places, or a naked single whose other
 *        digits, only the variant's cells that see each other or its even and odd marks rule out).
 * @param[in] G  the variant's board
 * @param[in] vals  the position
 * @param[in] st  the step
 * @return true if so
 */
static bool is_rule_single(const sudoku_geom_t *G, const uint8_t vals[81], const sudoku_step_t *st)
{
    if (st->tech.kind || (st->kind != STEP_HIDDEN_SINGLE && st->kind != STEP_NAKED_SINGLE)) {
        return false;
    }
    if (st->kind == STEP_HIDDEN_SINGLE && (st->unit >= UNIT_DIAG_MAIN || (G->variant == VAR_JIGSAW && st->unit >= 18))) {
        return true;                                /* (the variant's own region) */
    }
    const sudoku_geom_t *C = sudoku_geom(VAR_CLASSIC);
    uint16_t cand[81];                              /* the digits each cell could take on a Classic board */
    for (int c = 0; c < 81; c++) {
        cand[c] = 0x3FE;
        for (int p = 0; !vals[c] && p < C->npeers[c]; p++) {
            cand[c] &= (uint16_t)~(1u << vals[C->peers[c][p]]);
        }
    }
    if (st->kind == STEP_NAKED_SINGLE) {
        return __builtin_popcount(cand[st->cell]) > 1;
    }
    int places = 0;
    for (int k = 0; k < 9; k++) {                   /* the unit's empty cells the digit fits by Classic rules */
        int o = G->unit[st->unit][k];
        places += !vals[o] && (cand[o] >> st->digit & 1);
    }
    return places > 1;
}

/**
 * @brief A variant rule lesson: new puzzles of the variant played forward until the next step needs its rule.
 * @param[in] k  the lesson
 * @param[in] seed  the random state's start
 * @param[out] board  gets the puzzle's board
 * @param[out] out  gets the puzzle and the position
 * @return false: none found
 */
static bool find_rule(int k, uint32_t seed, sudoku_geom_t *board, learn_found_t *out)
{
    static uint8_t vals[81], sol[81];
    const int var = LESSONS[k].variant, tech = LESSONS[k].tech;
    uint32_t r = seed;
    for (int tries = 0; tries < 8; tries++) {       /* most puzzles have one early; a few seconds each at most */
        gen_level_t got;
        if (!sudoku_generate(var, GEN_MEDIUM, learn_rnd(&r), out->givens, board, &got) ||
                !sudoku_solve(board, out->givens, sol)) {
            continue;
        }
        memcpy(vals, out->givens, 81);
        for (int step = 0; step < 81; step++) {
            sudoku_step_t st;
            if (!sudoku_next_step(board, vals, sol, &st) || st.cell < 0) {
                break;
            }
            if ((tech && st.tech.kind == tech) || ((!tech || LESSONS[k].or_single) && is_rule_single(board, vals, &st))) {
                memcpy(out->vals, vals, 81);
                out->step = st;
                return true;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    return false;
}

/* (documented in learn_core.h) */
bool learn_find(int k, uint32_t seed, sudoku_geom_t *board, learn_found_t *out)
{
    if (k < 0 || k >= LESSONS_N) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    return LESSONS[k].variant < 0 ? find_technique(k, seed, out) : find_rule(k, seed, board, out);
}
