/**
 * @file ui_rules_card.c
 * @ingroup ui_game
 * @brief The rules card: a variant's rules, with a small picture, shown the first time and from the game's menu.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void rules_pic_draw_cb(lv_event_t *e);
static void rules_show(int variant);

/* ------------------------------------------------------------- the rules card */
/** @brief cell size: a whole 9x9 board (Asterisk) smaller, so the dialog fits */
#define RULES_PIC_CS(p) ((p)->h > 6 ? 28 : 40)

static const uint8_t PIC_KROPKI[] = {0, MARK_WHITE, 1, MARK_BLACK, 0xFF};   /**< 3 4 8 2 */
static const uint8_t PIC_XV[] = {0, MARK_V, 1, MARK_X, 0xFF};               /**< 2 3 7 9 */
static const uint8_t PIC_GT[] = {0, MARK_LESS, 1, MARK_MORE, 2, MARK_LESS, 0xFF};   /**< 2 < 5 > 4 < 9 */
static const uint8_t PIC_BARS[] = {0, 1, 0xFF};    /**< 4|5: a bar; 5 3 and 3 7: none */
static const uint8_t PIC_THERMO[] = {0, 1, 2, 3, 0xFF};     /**< 2 4 5 8 */
static const uint8_t PIC_ARROW[] = {0, 1, 2, 0xFF};         /**< 7: 4 + 3 */
static const uint8_t PIC_RENBAN[] = {0, 1, 2, 3, 0xFF};     /**< 5 3 6 4 */
static const uint8_t PIC_WHISPER[] = {0, 1, 2, 3, 0xFF};    /**< 2 8 1 6 */
static const uint8_t PIC_PALINDROME[] = {1, 4, 9, 14, 11, 0xFF};   /**< 3 7 5 7 3, down and back up */
static const uint8_t PIC_REGIONSUM[] = {1, 2, 3, 4, 0xFF};  /**< 2 6 | 5 3: 8 in each box */
static const uint8_t PIC_ZIPPER[] = {0, 1, 2, 3, 4, 0xFF};  /**< 1 3 7 4 6: 1 + 6 = 3 + 4 = 7 */
static const uint8_t PIC_BETWEEN[] = {0, 1, 2, 3, 0xFF};    /**< (3) 6 5 (8) */
static const uint8_t PIC_GROUPS[] = {0, 1, 2, 3, 4, 0xFF};  /**< Entropic 2 9 4 1 8; Modular 1 5 3 7 2; Parity 3 8 1 6 7 */
static const uint8_t PIC_SLOW[] = {0, 1, 6, 7, 0xFF};       /**< 2 4, then 4 again across the boxes' corner, 7 */
static const uint8_t PIC_CAGES[] = {11, 0, 1, 0xFE, 8, 2, 3, 6, 0xFE, 0xFF};   /**< 9 + 2 = 11, and an empty 8 */
static const uint8_t PIC_SUMLESS[] = {0, 1, 2, 5, 0xFE, 0xFF};   /**< a cage over the box border, no sum */
static const uint8_t PIC_PILL[] = {0, 1, 2, 3, 4, 0xFF};    /**< (1 2) 3 4 5: 12 = 3 + 4 + 5 */
static const uint8_t PIC_AVG[] = {0, 1, 2, 3, 0xFF};       /**< (5) 3 8 4: 3 + 8 + 4 = 3 x 5 */
static const uint8_t PIC_PROD[] = {12, 0, 1, 0xFE, 70, 2, 3, 6, 0xFE, 0xFF};   /**< 3 x 4 = 12, and an empty 70 */
static const uint8_t PIC_DMARKS[] = {0, DMARK_DR, 0xFF};   /**< 3 and 4 corner to corner: a mark; 8 and 4: none */
static const uint8_t PIC_TEN[] = {0, 1, 2, 5, 4, 0xFF};    /**< 3 7 | 1 5 4: two runs of 10 */

/** @brief Each variant's rules card picture. */
static const rules_pic_t RULES_PIC[VAR_COUNT] = {
    /* w, h, box w, box h, digits, tint, regions, parity, cross, bars, thermo, cages, sandwich clue */
    [VAR_CLASSIC] = {3, 3, 3, 3, "591763842", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_X] = {
        5, 5, 0, 0, "1...." ".2..." "..3.." "...4." "....5", "1000101010001000101010001", NULL, NULL, NULL, NULL,
        NULL, NULL, -1
    },
    [VAR_MINI] = {6, 4, 3, 2, "123456" "456123" "......" "......", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_HYPER] = {
        5, 5, 0, 0, "....." ".123." ".456." ".789." ".....", "0000001110011100111000000", NULL, NULL, NULL, NULL,
        NULL, NULL, -1
    },
    [VAR_JIGSAW] = {
        5, 5, 0, 0, "....." "....." "....." "....." ".....", NULL, "aabbb" "aabbc" "adccc" "ddcee" "dddee",
        NULL, NULL, NULL, NULL, NULL, -1
    },
    [VAR_KILLER] = {4, 2, 0, 0, "92.." "....", NULL, NULL, NULL, NULL, NULL, NULL, PIC_CAGES, -1},
    [VAR_KNIGHT] = {
        5, 5, 0, 0, "....." "....." "..5.." "....." ".....", NULL, NULL, NULL,
        ".x.x." "x...x" "....." "x...x" ".x.x.", NULL, NULL, NULL, -1
    },
    [VAR_EVENODD] = {4, 1, 0, 0, "4.7.", NULL, NULL, "e.o.", NULL, NULL, NULL, NULL, -1},
    [VAR_CONSEC] = {4, 1, 0, 0, "4537", NULL, NULL, NULL, NULL, PIC_BARS, NULL, NULL, -1},
    [VAR_THERMO] = {4, 1, 0, 0, "2458", NULL, NULL, NULL, NULL, NULL, PIC_THERMO, NULL, -1},
    [VAR_SANDWICH] = {6, 1, 0, 0, "614329", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 9},
    [VAR_KIDS] = {4, 4, 2, 2, "1234" "3412" "2143" "4321", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_KING] = {3, 3, 0, 0, "..." ".5." "...", NULL, NULL, NULL, "xxx" "x.x" "xxx", NULL, NULL, NULL, -1},
    [VAR_NONCONSEC] = {4, 1, 0, 0, "4627", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_DISJOINT] = {
        6, 3, 3, 3, "......" ".7..2." "......", "000000" "010010" "000000", NULL, NULL, NULL, NULL,
        NULL, NULL, -1
    },
    [VAR_ASTERISK] = {
        9, 9, 3, 3, "........." "....1...." "..2...3.." "........." ".4..5..6." "........."
        "..7...8.." "....9...." ".........",
        "000000000" "000010000" "001000100" "000000000" "010010010" "000000000" "001000100"
        "000010000" "000000000", NULL, NULL, NULL, NULL, NULL, NULL, -1
    },
    [VAR_KROPKI] = {4, 1, 0, 0, "3482", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, EDGE_KROPKI, PIC_KROPKI},
    [VAR_XV] = {4, 1, 0, 0, "2379", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, EDGE_XV, PIC_XV},
    [VAR_GT] = {4, 1, 0, 0, "2549", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, EDGE_GT, PIC_GT},
    [VAR_QUAD] = {2, 2, 0, 0, "16" "93", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, NULL, "139"},
    [VAR_ARROW] = {3, 1, 0, 0, "743", NULL, NULL, NULL, NULL, NULL, PIC_ARROW, NULL, -1, 0, NULL, NULL, LINE_ARROW},
    [VAR_RENBAN] = {4, 1, 0, 0, "5364", NULL, NULL, NULL, NULL, NULL, PIC_RENBAN, NULL, -1, 0, NULL, NULL, LINE_RENBAN},
    [VAR_WHISPER] = {
        4, 1, 0, 0, "2816", NULL, NULL, NULL, NULL, NULL, PIC_WHISPER, NULL, -1, 0, NULL, NULL,
        LINE_WHISPER
    },
    [VAR_SKY] = {5, 1, 0, 0, "25198", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 3, 0, NULL, NULL, 0, 0},
    [VAR_LK] = {
        4, 4, 0, 0, "3..." ".1.." "..4." "...2", "1000010000100001", NULL, NULL, NULL, NULL, NULL, NULL, -1, 0,
        NULL, NULL, 0, 10
    },
    [VAR_PALINDROME] = {
        4, 4, 0, 0, ".3.." "7..." ".5.3" "..7.", NULL, NULL, NULL, NULL, NULL, PIC_PALINDROME, NULL, -1, 0, NULL,
        NULL, LINE_PALINDROME
    },
    [VAR_REGIONSUM] = {
        6, 1, 3, 1, "426538", NULL, NULL, NULL, NULL, NULL, PIC_REGIONSUM, NULL, -1, 0, NULL, NULL,
        LINE_REGIONSUM
    },
    [VAR_XSUM] = {5, 1, 0, 0, "34298", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 9, 0, NULL, NULL, 0, 0},
    [VAR_ZIPPER] = {5, 1, 0, 0, "13746", NULL, NULL, NULL, NULL, NULL, PIC_ZIPPER, NULL, -1, 0, NULL, NULL, LINE_ZIPPER},
    [VAR_FRAME] = {6, 1, 3, 1, "625198", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 13, 0, NULL, NULL, 0, 0},
    [VAR_BETWEEN] = {4, 1, 0, 0, "3658", NULL, NULL, NULL, NULL, NULL, PIC_BETWEEN, NULL, -1, 0, NULL, NULL, LINE_BETWEEN},
    [VAR_ENTROPIC] = {5, 1, 0, 0, "29418", NULL, NULL, NULL, NULL, NULL, PIC_GROUPS, NULL, -1, 0, NULL, NULL, LINE_ENTROPIC},
    [VAR_MODULAR] = {5, 1, 0, 0, "15372", NULL, NULL, NULL, NULL, NULL, PIC_GROUPS, NULL, -1, 0, NULL, NULL, LINE_MODULAR},
    [VAR_OUTSIDE] = {
        6, 1, 3, 1, "582934", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, NULL, NULL, 0, 0, "25"
    },
    [VAR_ROSSINI] = {
        6, 1, 3, 1, "247953", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, NULL, NULL, 0, 0, LV_SYMBOL_RIGHT
    },
    [VAR_DIAGNC] = {3, 3, 0, 0, "248" "651" "937", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_DUTCH] = {4, 1, 0, 0, "2738", NULL, NULL, NULL, NULL, NULL, PIC_WHISPER, NULL, -1, 0, NULL, NULL, LINE_DUTCH},
    [VAR_PARITY] = {5, 1, 0, 0, "38167", NULL, NULL, NULL, NULL, NULL, PIC_GROUPS, NULL, -1, 0, NULL, NULL, LINE_PARITY},
    [VAR_NABNER] = {4, 1, 0, 0, "1486", NULL, NULL, NULL, NULL, NULL, PIC_RENBAN, NULL, -1, 0, NULL, NULL, LINE_NABNER},
    [VAR_NROOMS] = {5, 1, 0, 0, "37594", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 5, 0, NULL, NULL, 0, 0},
    [VAR_SKYSUM] = {5, 1, 0, 0, "25198", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 16, 0, NULL, NULL, 0, 0},
    [VAR_LOCKOUT] = {5, 1, 0, 0, "28917", NULL, NULL, NULL, NULL, NULL, PIC_GROUPS, NULL, -1, 0, NULL, NULL, LINE_LOCKOUT},
    [VAR_SUMLESS] = {4, 2, 2, 2, "..8." "....", NULL, NULL, NULL, NULL, NULL, NULL, PIC_SUMLESS, -1},
    [VAR_DARROW] = {4, 1, 0, 0, "3546", NULL, NULL, NULL, NULL, NULL, PIC_RENBAN, NULL, -1, 0, NULL, NULL, LINE_DARROW},
    [VAR_DIAGCONSEC] = {
        3, 2, 0, 0, "3.8" ".42", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, NULL, NULL, 0, 0, NULL, PIC_DMARKS
    },
    [VAR_TEN] = {3, 2, 0, 0, "371" "945", NULL, NULL, NULL, NULL, NULL, PIC_TEN, NULL, -1, 0, NULL, NULL, LINE_TEN},
    [VAR_159] = {9, 1, 0, 0, "321465798", "100010001", NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_PRODUCT] = {4, 2, 0, 0, "34.." "....", NULL, NULL, NULL, NULL, NULL, NULL, PIC_PROD, -1},
    [VAR_ANTIDIAG] = {
        5, 5, 0, 0, "3...7" ".7.3." "..3.." ".1.7." "7...3", "10001" "01010" "00100" "01010" "10001",
        NULL, NULL, NULL, NULL, NULL, NULL, -1
    },
    [VAR_CHESS] = {
        5, 5, 0, 0, "....." "....." "..5.." "....." ".....", NULL, NULL, NULL,
        ".x.x." "xxxxx" ".x.x." "xxxxx" ".x.x.", NULL, NULL, NULL, -1
    },
    [VAR_MAGIC] = {3, 3, 3, 3, "816" "357" "492", "111" "111" "111", NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_AVG] = {4, 1, 0, 0, "5384", NULL, NULL, NULL, NULL, NULL, PIC_AVG, NULL, -1, 0, NULL, NULL, LINE_AVG},
    [VAR_FLATMATES] = {.w = 2, .h = 3, .digits = "1." "55" ".9", .left_clue = -1},
    [VAR_MATHRAX] = {.w = 2, .h = 2, .digits = "24" "57", .left_clue = -1, .mathrax = "9+"},
    [VAR_N9] = {.w = 6, .h = 1, .digits = "512984", .left_clue = -1, .left_text = "28"},
    [VAR_BF] = {.w = 9, .h = 1, .digits = "374951862", .left_clue = 23},
    [VAR_S9] = {.w = 6, .h = 1, .digits = "3.5962", .left_clue = -1, .s9 = "r....l"},
    [VAR_CLONE] = {.w = 4, .h = 2, .digits = "35.." "..35", .tint = "1100" "0011", .left_clue = -1},
    [VAR_QUADRO] = {.w = 3, .h = 2, .digits = "472" "915", .left_clue = -1},
    [VAR_AQ9] = {.w = 5, .h = 5, .digits = "....." "....." "..9.." "....." ".....", .cross = "x...x" ".x.x." "....." ".x.x." "x...x", .left_clue = -1},
    [VAR_PSYM] = {.w = 3, .h = 3, .digits = "2.7" ".5." "3.8", .tint = "000" "010" "000", .left_clue = -1},
    [VAR_B9] = {.w = 6, .h = 1, .digits = "352914", .left_clue = 10},
    [VAR_PILL] = {5, 1, 0, 0, "12345", NULL, NULL, NULL, NULL, NULL, PIC_PILL, NULL, -1, 0, NULL, NULL, LINE_PILL},
    [VAR_PSKY] = {5, 1, 0, 0, "25198", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 2, 0, NULL, NULL, 0, 0},
    [VAR_REGIONS] = {4, 2, 2, 2, "..8." "....", "0110" "0100", NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_GIRANDOLA] = {
        9, 9, 3, 3, "4.......7" "....2...." "........." "........." ".9..5..1." "........." "........."
        "....8...." "3.......6",
        "100000001" "000010000" "000000000" "000000000" "010010010" "000000000" "000000000"
        "000010000" "100000001", NULL, NULL, NULL, NULL, NULL, NULL, -1
    },
    [VAR_SLOW] = {4, 2, 2, 2, "24.." "..47", NULL, NULL, NULL, NULL, NULL, PIC_SLOW, NULL, -1, 0, NULL, NULL, LINE_SLOW},
    [VAR_FORTRESS] = {3, 3, 0, 0, ".4." "685" ".2.", NULL, NULL, "..." ".f." "...", NULL, NULL, NULL, NULL, -1},
    [VAR_ARGYLE] = {9, 9, 3, 3, "....3...." ".....7..." "......1.." ".......9." "........4" "........." "........." "........." ".........", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_CENTERDOT] = {
        9, 9, 3, 3, "........." ".3..7..1." "........." "........." ".6..2..9." "........."
        "........." ".8..4..5." ".........",
        "000000000" "010010010" "000000000" "000000000" "010010010" "000000000" "000000000"
        "010010010" "000000000", NULL, NULL, NULL, NULL, NULL, NULL, -1
    },
};

/** each variant's rules, for its rules card */
static const char *const RULES_TEXT[VAR_COUNT] = {
    [VAR_CLASSIC] = "Fill the grid so that every row, every column and every 3x3 box holds the digits 1 to 9, each "
    "once. The given digits can't be changed.",
    [VAR_X] = "Classic rules, and the two long diagonals (tinted) also hold 1 to 9, each once.",
    [VAR_MINI] = "A 6x6 grid: every row, column and 2x3 box holds 1 to 6, each once.",
    [VAR_HYPER] = "Classic rules, and the four tinted 3x3 windows also hold 1 to 9, each once.",
    [VAR_JIGSAW] = "Classic rules, but the boxes are irregular regions of nine cells, outlined with thick lines. "
    "Every region holds 1 to 9, each once.",
    [VAR_KILLER] = "Classic rules, plus cages (dashed outlines): the digits in a cage add up to the small number "
    "in its corner, and no digit repeats in a cage. Hold a cell to see which digits can make up "
    "its cage.",
    [VAR_KNIGHT] = "Classic rules, and two cells a chess knight's move apart never hold the same digit: the 5 "
    "here means none of the crossed cells can be 5.",
    [VAR_EVENODD] = "Classic rules, and some cells are marked: a shaded cell holds an even digit (2, 4, 6, 8), a "
    "cell with a ring an odd one (1, 3, 5, 7, 9).",
    [VAR_CONSEC] = "Classic rules, and a bar joins two side-by-side cells whose digits are 1 apart (4 and 5). "
    "Where there is no bar, they are not 1 apart (5 and 3, 3 and 7).",
    [VAR_THERMO] = "Classic rules, and along each thermometer the digits rise from the round bulb: every digit "
    "is bigger than the one before it.",
    [VAR_SANDWICH] = "Classic rules, and the number by each row (left) and column (top) is the sum of the digits "
    "between that line's 1 and 9, wherever they are: here 4 + 3 + 2 = 9.",
    [VAR_KIDS] = "A small 4x4 grid to start with: every row, every column and every 2x2 box holds 1, 2, 3 and 4, "
    "each once.",
    [VAR_KING] = "Classic rules, and two cells that touch, side by side or corner to corner (a chess king's move), "
    "never hold the same digit: the 5 here means none of the eight cells around it can be 5.",
    [VAR_NONCONSEC] = "Classic rules, and two side-by-side cells never hold digits 1 apart: a 4 can't sit next to "
    "a 3 or a 5. Corner to corner doesn't count.",
    [VAR_DISJOINT] = "Classic rules, and the cells in the same place in each 3x3 box (all the centres, all the "
    "top-left corners, ...) also hold 1 to 9, each once: the tinted 7 and 2 here.",
    [VAR_ASTERISK] = "Classic rules, and the nine tinted cells that make an asterisk around the centre also hold "
    "1 to 9, each once.",
    [VAR_KROPKI] = "Classic rules, and dots between side-by-side cells: a white dot means the digits are 1 apart "
    "(3 and 4), a black dot that one is double the other (4 and 8). No dot: neither (8 and 2). "
    "1 and 2 can have either dot.",
    [VAR_XV] = "Classic rules, and marks between side-by-side cells: V means they add up to 5 (2 and 3), X to 10 "
    "(3 and 7). No mark: they add up to neither (7 and 9).",
    [VAR_GT] = "Classic rules, and inside each 3x3 box a sign between every two side-by-side cells points to "
    "the smaller digit: 2 < 5 > 4 < 9.",
    [VAR_QUAD] = "Classic rules, and a circle where four cells meet lists digits that must be among those four "
    "cells, in any order: here 1, 3 and 9. The fourth cell can be anything else.",
    [VAR_ARROW] = "Classic rules, and the digits along each arrow's shaft add up to the digit in its circle: "
    "4 + 3 = 7. Digits on a shaft can repeat, where the rows, columns and boxes allow it.",
    [VAR_RENBAN] = "Classic rules, and the digits on each purple line are a run of consecutive digits, in any "
    "order: 5, 3, 6, 4 is 3 to 6. No digit repeats on a line.",
    [VAR_WHISPER] = "Classic rules, and along each green line, two neighbours differ by at least 5: 2, 8, 1, 6. "
    "So there's never a 5 on a line.",
    [VAR_SKY] = "Classic rules, and a clue left of a row or above a column says how many buildings are seen from "
    "there: each digit is a building that tall, and a taller one hides the shorter ones behind it. Here "
    "2, 5 and 9 are seen: 3.",
    [VAR_LK] = "Classic rules, and a clue outside the grid with an arrow gives the sum of the digits along that "
    "diagonal: 3 + 1 + 4 + 2 = 10. Digits can repeat on a diagonal where the boxes allow it.",
    [VAR_PALINDROME] = "Classic rules, and each grey line reads the same from both ends: 3, 7, 5, 7, 3. Its first "
    "and last cells hold the same digit, so do the second and the second to last, and so on.",
    [VAR_REGIONSUM] = "Classic rules, and on each blue line the digits in each box it passes through add up to the "
    "same total: 2 + 6 = 5 + 3 = 8. A box the line enters twice counts both parts together.",
    [VAR_XSUM] = "Classic rules, and a clue left of a row or above a column is the sum of the first X digits "
    "from there, where X is the first digit itself: here X is 3, and 3 + 4 + 2 = 9.",
    [VAR_ZIPPER] = "Classic rules, and on each pink line, two cells the same distance from its middle add up to the "
    "same total: 1 + 6 = 3 + 4. On a line with a middle cell, that total is its digit: 7.",
    [VAR_FRAME] = "Classic rules, and a clue left of a row or above a column is the sum of the first three digits "
    "from there (the ones in the first box): 6 + 2 + 5 = 13.",
    [VAR_BETWEEN] = "Classic rules, and the digits on a line between two circles lie strictly between the circles' "
    "digits: 6 and 5 are between 3 and 8. Either circle can be the smaller one.",
    [VAR_ENTROPIC] = "Classic rules, and any three cells in a row on an orange line hold a low (1-3), a middle "
    "(4-6) and a high (7-9) digit: 2 9 4, 9 4 1 and 4 1 8. So cells three apart on a line are both low, both middle "
    "or both high.",
    [VAR_MODULAR] = "Classic rules, and any three cells in a row on a teal line hold one digit of each of 1 4 7, "
    "2 5 8 and 3 6 9: 1 5 3, 5 3 7 and 3 7 2. So cells three apart on a line are in the same one of those groups.",
    [VAR_CENTERDOT] = "Classic rules, and the nine tinted cells in the centres of the boxes also hold 1 to 9, "
    "each once.",
    [VAR_OUTSIDE] = "Classic rules, and a clue left of a row or above a column lists digits that are among the first "
    "three cells from there, in any order: here 2 and 5 are among 5, 8, 2. The third can be anything else.",
    [VAR_ROSSINI] = "Classic rules, and an arrow left of a row or above a column says its first three digits rise "
    "toward where it points: 2, 4, 7. A row or column with no arrow: its first three neither rise nor fall.",
    [VAR_DIAGNC] = "Classic rules, and two cells that touch at a corner never hold digits 1 apart: no 4 or 6 in a "
    "corner of the 5. Side by side doesn't count: the 4 and 6 next to the 5 are fine.",
    [VAR_DUTCH] = "Classic rules, and along each amber line, two neighbours differ by at least 4: 2, 7, 3, 8 (7 and "
    "3 are just far enough apart). A 5 can be on a line, between a 1 and a 9.",
    [VAR_PARITY] = "Classic rules, and along each red line, neighbours are one odd and one even digit: 3, 8, 1, 6, 7. "
    "So the digits on a line alternate odd, even, odd, ...",
    [VAR_FORTRESS] = "Classic rules, and every grey cell holds a bigger digit than each white cell side by side with it: "
    "the grey 8 beats 4, 6, 5 and 2. Two grey cells side by side can be either way round, and corner to corner "
    "doesn't count.",
    [VAR_ARGYLE] = "Classic rules, and no digit repeats along any of the eight diagonal lines of the argyle pattern: "
    "3 7 1 9 4 on a short one here. The lines have 5 or 8 cells, so they don't hold every digit.",
    [VAR_NABNER] = "Classic rules, and no two digits on a brown line are the same or 1 apart, anywhere on the line, "
    "not just side by side: 1, 4, 8, 6 is fine (8 and 6 are 2 apart). A line has 5 cells at most.",
    [VAR_NROOMS] = "Classic rules, and a clue left of a row or above a column is the digit in the X-th cell from "
    "there, where X is the first digit: here X is 3, and the third cell holds the clue, 5.",
    [VAR_SKYSUM] = "Classic rules, and a clue left of a row or above a column is the sum of the buildings seen from "
    "there: each digit is a building that tall, and a taller one hides the shorter ones behind it. Here 2, 5 "
    "and 9 are seen: 2 + 5 + 9 = 16.",
    [VAR_LOCKOUT] = "Classic rules, and on a line the two diamonds at its ends are 4 or more apart, and every digit "
    "between them on the line lies outside their range: with 2 and 7 at the ends, 8, 9 and 1 fit, 3 to 6 don't. "
    "So a 5 is never between.",
    [VAR_SUMLESS] = "Classic rules, plus cages (dashed outlines) without sums: no digit repeats in a cage. A cage "
    "reaches over a box border, so the 8 here rules out 8 in the cage's other cells, even in the next box.",
    [VAR_DARROW] = "Classic rules, and on a line the two circles at its ends add up to the digits between them: "
    "3 + 6 = 5 + 4. Digits may repeat along it where the rules allow.",
    [VAR_DIAGCONSEC] = "Classic rules, and where two cells touching at a corner are joined by a mark, their "
    "digits are 1 apart (3 and 4); where no mark joins them, they aren't (8 and 4).",
    [VAR_TEN] = "Classic rules, and each green line splits into runs of cells next to each other along it that "
    "each add up to 10: 3 + 7, then 1 + 5 + 4. Where it splits isn't shown.",
    [VAR_159] = "Classic rules, and in each row the digit in column 1 says which column holds the row's 1, the "
    "digit in column 5 where its 5 is, and the digit in column 9 where its 9 is (those columns are tinted). Here "
    "the 3 in column 1 puts the 1 in column 3, the 6 in column 5 puts the 5 in column 6, and the 8 in column 9 "
    "puts the 9 in column 8.",
    [VAR_PRODUCT] = "Classic rules, plus cages (dashed outlines): the digits in a cage multiply to the number in its "
    "corner, and no digit repeats in a cage. Here 3 x 4 = 12, and the three empty cells make 70: 2, 5 and 7.",
    [VAR_CHESS] = "Classic rules, and no digit repeats a chess knight's move apart, nor in cells that touch, side by "
    "side or corner to corner: the 5 here rules out every crossed cell.",
    [VAR_MAGIC] = "Classic rules, and the tinted centre box is a magic square: each of its rows, columns and both "
    "diagonals add up to 15, as here (8 + 1 + 6, 8 + 3 + 4, 8 + 5 + 2 ...). So its middle is always 5, its corners "
    "even and the rest odd.",
    [VAR_AVG] = "Classic rules, and on each teal line, the digit in the circle is the average of the line's other "
    "digits: they add up to it times how many they are, 3 + 8 + 4 = 3 x 5. Digits may repeat along a line where the "
    "rules allow.",
    [VAR_BF] = "Classic rules, and a clue left of a row or above a column. Take X, the line's first digit, and Y, "
    "its last: the first X cells and the last Y cells. Where they overlap, the clue is the sum of the overlap; where "
    "they don't, the sum of the cells between them. Here X = 3 and Y = 2: 3 7 4 and 6 2 leave 9 + 5 + 1 + 8 = 23 "
    "between them. With X = 7 and Y = 5 instead, the two would overlap, and the clue would add up the overlap.",
    [VAR_S9] = "Classic rules, and an arrow in a cell points along its row or column to the 9: the cell's digit is how "
    "many cells away that 9 is. Here the 3 points right, three cells on to the 9, and the 2 points left, two cells "
    "back to it. An arrow's cell is never 9, and the 9 is never behind it.",
    [VAR_N9] = "Classic rules, and a clue left of a row or above a column lists the digits right beside its 9, in any "
    "order: here 2 and 8. A clue of one digit puts the 9 at an end of its row or column.",
    [VAR_CLONE] = "Classic rules, and pairs of shaded areas of the same shape (one the other moved, marked with the same "
    "letter) hold the same digits in the same places: here 3 5 and 3 5. A digit in one area is its clone's too.",
    [VAR_QUADRO] = "Classic rules, and every 2x2 block of cells holds at least one odd and one even digit: never four "
    "odd or four even together. Here 4 7 / 9 1 has the even 4, and 7 2 / 1 5 the even 2.",
    [VAR_AQ9] = "Classic rules, and no two 9s share a diagonal, of any length: a 9 rules out every crossed cell along "
    "both its diagonals, as a chess queen would. The other digits may repeat on diagonals.",
    [VAR_PSYM] = "Classic rules, and two cells mirrored through the centre (turned half a turn round it) add up to "
    "10: here 2 and 8, 7 and 3. So the tinted centre is always 5, and every digit gives its mirror away.",
    [VAR_B9] = "Classic rules, and a clue left of a row or above a column is the sum of the digits between it and "
    "the 9: here 3 + 5 + 2 = 10. A line without a clue says nothing (its 9 may even be first).",
    [VAR_FLATMATES] = "Classic rules, and every 5 has a 1 right above it or a 9 right below it, or both: here the left "
    "5 has its 1, the right one its 9. So a 5 in the top row needs a 9 under it, and one in the bottom row a 1 over it.",
    [VAR_MATHRAX] = "Classic rules, and a circle where four cells meet holds a clue that both diagonal pairs of those "
    "cells make: here 9+, 2 + 7 = 4 + 5 = 9. With a minus sign it is how far apart each pair is, with a times sign "
    "what each pair multiplies to, with a divide sign how many times the smaller digit goes into the larger. E: all "
    "four cells are even; O: all four are odd.",
    [VAR_ANTIDIAG] = "Classic rules, and each of the two long diagonals (tinted) holds at most three different "
    "digits, which repeat: here the top-left one holds 3 and 7, the other 7, 3 and 1. A digit can be on a diagonal up "
    "to three times, once per box it crosses.",
    [VAR_PILL] = "Classic rules, and on each arrow the pill, two cells in a capsule, read as a two-digit number, is "
    "the sum of the digits along the shaft: 12 = 3 + 4 + 5. Digits may repeat along a shaft where the rules allow.",
    [VAR_PSKY] = "Classic rules, and the digits are buildings that tall: from a clue, a taller one hides the "
    "shorter ones behind it. A ringed clue counts the odd buildings seen, a shaded one the even ones. Here 2, 5 "
    "and 9 are seen: the ringed 2 counts the odd ones, 5 and 9 (a shaded clue here would be 1: the 2).",
    [VAR_REGIONS] = "Classic rules, plus a few shaded regions: no digit repeats in a region. A region reaches over "
    "box borders, so the 8 here rules out 8 in the region's other cells, even in the next box.",
    [VAR_GIRANDOLA] = "Classic rules, and the nine tinted cells in a pinwheel pattern, one in each box, also hold "
    "1 to 9, each once.",
    [VAR_SLOW] = "Classic rules, and along each thermometer the digits rise from the round bulb or stay the same: "
    "2, 4, 4, 7. A digit can only stay the same where the two cells share no row, column or box, as across the "
    "corner between two boxes here.",
};

/**
 * @brief Draws a rules card's picture.
 * @param[in] e  the draw event (the object's user data: the rules_pic_t)
 */
static void rules_pic_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    const rules_pic_t *p = lv_obj_get_user_data(obj);
    if (!p) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const int cs = RULES_PIC_CS(p), w = p->w * cs, h = p->h * cs;
    const int x0 = (co.x1 + co.x2) / 2 - w / 2, y0 = (co.y1 + co.y2) / 2 - h / 2;
    lv_draw_rect_dsc_t r;
    lv_draw_line_dsc_t l;
    for (int i = 0; i < p->w * p->h; i++) {        /* cells */
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_draw_rect_dsc_init(&r);
        lv_color_t bg = C_CELL;
        if (p->tint && p->tint[i] == '1') {
            bg = lv_color_mix(C_ORANGE, bg, 40);
        }
        if (p->parity && p->parity[i] == 'e') {
            bg = lv_color_mix(C_GRID_THIN, bg, 150);
        } else if (p->parity && p->parity[i] == 'f') {   /* (Fortress: a grey cell, as on the board) */
            bg = lv_color_mix(C_GRID_THIN, bg, 190);
        }
        r.bg_color = bg;
        r.border_color = C_GRID_THIN;
        r.border_width = 1;
        lv_area_t a = {.x1 = x, .y1 = y, .x2 = x + cs, .y2 = y + cs};
        lv_draw_rect(layer, &r, &a);
    }
    lv_draw_line_dsc_init(&l);                      /* thick lines: boxes or regions, and the frame */
    l.color = C_GRID_THICK;
    l.width = 3;
    for (int i = 0; i < p->w * p->h; i++) {
        int c = i % p->w, rr = i / p->w, x = x0 + c * cs, y = y0 + rr * cs;
        bool right = c + 1 < p->w && (p->regions ? p->regions[i] != p->regions[i + 1] : p->bw && (c + 1) % p->bw == 0);
        bool down = rr + 1 < p->h && (p->regions ? p->regions[i] != p->regions[i + p->w] : p->bh && (rr + 1) % p->bh == 0);
        if (right) {
            l.p1.x = l.p2.x = x + cs;
            l.p1.y = y;
            l.p2.y = y + cs;
            lv_draw_line(layer, &l);
        }
        if (down) {
            l.p1.y = l.p2.y = y + cs;
            l.p1.x = x;
            l.p2.x = x + cs;
            lv_draw_line(layer, &l);
        }
    }
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_TRANSP;
    r.border_color = C_GRID_THICK;
    r.border_width = 3;
    lv_area_t fr = {.x1 = x0 - 1, .y1 = y0 - 1, .x2 = x0 + w + 1, .y2 = y0 + h + 1};
    lv_draw_rect(layer, &r, &fr);
    for (int k = 0; p == &RULES_PIC[VAR_ARGYLE] && k < ARGYLE_LINES; k++) {   /* Argyle: its lines */
        uint8_t line[9];
        const int n = sudoku_argyle_line(k, line);
        const lv_area_t a = {.x1 = x0 + line[0] % 9 * cs, .y1 = y0 + line[0] / 9 * cs, .x2 = x0 + (line[0] % 9 + 1) * cs,
                             .y2 = y0 + (line[0] / 9 + 1) * cs
                            };
        const lv_area_t b = {.x1 = x0 + line[n - 1] % 9 * cs, .y1 = y0 + line[n - 1] / 9 * cs,
                             .x2 = x0 + (line[n - 1] % 9 + 1) * cs, .y2 = y0 + (line[n - 1] / 9 + 1) * cs
                            };
        draw_argyle_line(layer, &a, &b, k < ARGYLE_LINES / 2, 2);
    }
    for (int i = 0; p->s9 && i < p->w * p->h; i++) {   /* Search Nine's arrows */
        const char *at = p->s9[i] != '.' ? strchr(".urdl", p->s9[i]) : NULL;
        if (at) {
            const int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
            const lv_area_t a = {.x1 = x, .y1 = y, .x2 = x + cs, .y2 = y + cs};
            draw_s9_arrow(layer, &a, (int)(at - ".urdl"));
        }
    }
    for (int k = 0; p->dmarks && p->dmarks[k] != 0xFF; k += 2) {   /* Diagonal Consecutive's corner marks */
        int c = p->dmarks[k], dl = p->dmarks[k + 1] == DMARK_DL;
        draw_dmark(layer, x0 + (c % p->w + (dl ? 0 : 1)) * cs, y0 + (c / p->w + 1) * cs, dl, cs / 7);
    }
    if (p->thermo) {                                /* a thermometer (or an arrow, a Renban, Whispers, ... line) */
        lv_point_t pts[9];
        int n = 0;
        for (; n < 9 && p->thermo[n] != 0xFF; n++) {
            pts[n].x = x0 + (p->thermo[n] % p->w) * cs + cs / 2;
            pts[n].y = y0 + (p->thermo[n] / p->w) * cs + cs / 2;
        }
        draw_line_path(layer, p->line_kind, pts, n, cs);
    }
    for (int i = 0; p->parity && i < p->w * p->h; i++) {   /* odd rings */
        if (p->parity[i] != 'o') {
            continue;
        }
        lv_draw_rect_dsc_init(&r);
        r.bg_opa = LV_OPA_TRANSP;
        r.border_color = C_MUTED;
        r.border_width = 2;
        r.radius = LV_RADIUS_CIRCLE;
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_area_t a = {.x1 = x + 4, .y1 = y + 4, .x2 = x + cs - 4, .y2 = y + cs - 4};
        lv_draw_rect(layer, &r, &a);
    }
    for (int k = 0; p->edges && p->edges[k] != 0xFF; k += 2) {   /* Kropki, XV, Greater-Than marks */
        int a = p->edges[k];
        draw_edge_mark(layer, p->edge_kind, p->edges[k + 1], false, x0 + (a % p->w + 1) * cs,
                       y0 + (a / p->w) * cs + cs / 2);
    }
    if (p->mathrax) {                               /* Mathrax: the circle in the middle */
        draw_mathrax_circle(layer, x0 + w / 2, y0 + h / 2, p->mathrax);
    }
    if (p->quad) {                                  /* Quadruple: the circle in the middle */
        uint8_t dig[4];
        int n = 0;
        for (const char *q = p->quad; *q && n < 4; q++) {
            dig[n++] = (uint8_t)(*q - '0');
        }
        draw_quad_circle(layer, x0 + w / 2, y0 + h / 2, dig, n);
    }
    for (int k = 0; p->bars && p->bars[k] != 0xFF; k += 2) {   /* consecutive bars */
        int a = p->bars[k], b = p->bars[k + 1];
        lv_draw_rect_dsc_init(&r);
        r.bg_color = C_ORANGE;
        r.radius = 3;
        int x = x0 + (a % p->w + 1) * cs, y = y0 + (a / p->w) * cs + cs / 2;
        (void)b;
        lv_area_t ar = {.x1 = x - 4, .y1 = y - 11, .x2 = x + 4, .y2 = y + 11};
        lv_draw_rect(layer, &r, &ar);
    }
    if (p->cages) {                                 /* dashed cages with their sums */
        const uint8_t *q = p->cages;
        while (*q != 0xFF) {
            int sum = *q++, first = *q;
            uint8_t in[16];
            int n = 0;
            while (*q != 0xFE) {
                in[n++] = *q++;
            }
            q++;
            lv_draw_line_dsc_init(&l);
            l.color = C_MUTED;
            l.width = 1;
            l.dash_width = 4;
            l.dash_gap = 3;
            for (int j = 0; j < n; j++) {
                int c = in[j], cx = c % p->w, cy = c / p->w, x = x0 + cx * cs, y = y0 + cy * cs;
                bool up = false, dn = false, lf = false, rt = false;
                for (int k = 0; k < n; k++) {
                    up |= in[k] == c - p->w;
                    dn |= in[k] == c + p->w;
                    lf |= cx > 0 && in[k] == c - 1;
                    rt |= cx < p->w - 1 && in[k] == c + 1;
                }
                if (!up) {
                    l.p1.x = x + 4;
                    l.p2.x = x + cs - 4;
                    l.p1.y = l.p2.y = y + 4;
                    lv_draw_line(layer, &l);
                }
                if (!dn) {
                    l.p1.x = x + 4;
                    l.p2.x = x + cs - 4;
                    l.p1.y = l.p2.y = y + cs - 4;
                    lv_draw_line(layer, &l);
                }
                if (!lf) {
                    l.p1.y = y + 4;
                    l.p2.y = y + cs - 4;
                    l.p1.x = l.p2.x = x + 4;
                    lv_draw_line(layer, &l);
                }
                if (!rt) {
                    l.p1.y = y + 4;
                    l.p2.y = y + cs - 4;
                    l.p1.x = l.p2.x = x + cs - 4;
                    lv_draw_line(layer, &l);
                }
            }
            if (!sum) {
                continue;                           /* (Sumless Killer: no sum) */
            }
            char t[4];
            snprintf(t, sizeof(t), "%d", sum);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = t;
            d.text_local = 1;
            lv_area_t la = {.x1 = x0 + (first % p->w) * cs + 3, .y1 = y0 + (first / p->w) * cs + 2,
                            .x2 = x0 + (first % p->w) * cs + 30, .y2 = y0 + (first / p->w) * cs + 16
                           };
            lv_draw_label(layer, &d, &la);
        }
    }
    if (p->lk_clue > 0) {                           /* Little Killer: a sum above the corner, its arrow down-right */
        char t[4];
        snprintf(t, sizeof(t), "%d", p->lk_clue);
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_20;
        d.color = C_ACCENT;
        d.align = LV_TEXT_ALIGN_RIGHT;
        d.text = t;
        d.text_local = 1;
        lv_area_t a = {.x1 = x0 - 50, .y1 = y0 - 30, .x2 = x0 - 14, .y2 = y0 - 6};
        lv_draw_label(layer, &d, &a);
        draw_lk_arrow(layer, x0 - 12, y0 - 12, LK_DOWN_RIGHT, 9);
    }
    if (p->left_clue >= 0 || p->left_text) {        /* Sandwich, Skyscraper, X-Sums, Frame, ...: the clue left of the row */
        char t[8];
        snprintf(t, sizeof(t), "%s", p->left_text ? p->left_text : "");
        if (!p->left_text) {
            snprintf(t, sizeof(t), "%d", p->left_clue);
        }
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_22;
        d.color = C_ACCENT;
        d.align = LV_TEXT_ALIGN_RIGHT;
        d.text = t;
        d.text_local = 1;
        lv_area_t a = {.x1 = x0 - 50, .y1 = y0 + (cs - 24) / 2, .x2 = x0 - 10, .y2 = y0 + cs};
        lv_draw_label(layer, &d, &a);
        if (p == &RULES_PIC[VAR_PSKY]) {            /* Odd/Even Skyscrapers: its clue counts odd ones: a ring */
            lv_draw_rect_dsc_t rr;
            lv_draw_rect_dsc_init(&rr);
            rr.bg_opa = LV_OPA_TRANSP;
            rr.border_color = C_MUTED;
            rr.border_width = 2;
            rr.radius = LV_RADIUS_CIRCLE;
            lv_area_t ring = {.x1 = x0 - 36, .y1 = y0 + cs / 2 - 15, .x2 = x0 - 6, .y2 = y0 + cs / 2 + 15};
            lv_draw_rect(layer, &rr, &ring);
        }
    }
    for (int i = 0; i < p->w * p->h; i++) {         /* digits and crosses */
        char t[2] = {0, 0};
        lv_color_t col = C_TEXT;
        if (p->digits[i] >= '1' && p->digits[i] <= '9') {
            t[0] = p->digits[i];
        } else if (p->cross && p->cross[i] == 'x') {
            t[0] = 'x';
            col = C_RED;
        }
        if (!t[0]) {
            continue;
        }
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_22;
        d.color = col;
        d.align = LV_TEXT_ALIGN_CENTER;
        d.text = t;
        d.text_local = 1;
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_area_t a = {.x1 = x, .y1 = y + (cs - 24) / 2, .x2 = x + cs, .y2 = y + cs};
        lv_draw_label(layer, &d, &a);
    }
}

/**
 * @brief Shows a variant's rules card.
 * @param[in] variant  sudoku_variant_t
 */
static void rules_show(int variant)
{
    if (variant < 0 || variant >= VAR_COUNT) {
        variant = VAR_CLASSIC;
    }
    char title[40];
    snprintf(title, sizeof(title), "%s rules", variant_name(variant));
    lv_obj_t *m = make_msgbox(title, NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *pic = lv_obj_create(lv_msgbox_get_content(m));
    lv_obj_remove_style_all(pic);
    const rules_pic_t *p = &RULES_PIC[variant];
    lv_obj_set_size(pic, LV_PCT(100), p->h * RULES_PIC_CS(p) + 16 + (p->lk_clue > 0 ? 32 : 0));
    lv_obj_set_user_data(pic, (void *)p);
    lv_obj_add_event_cb(pic, rules_pic_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_t *t = make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, RULES_TEXT[variant]);
    lv_obj_set_style_margin_top(t, 10, 0);
    msgbox_button(m, "OK", mbox_close_cb, true);
}

/* (documented in ui_internal.h) */
void rules_cb(lv_event_t *e)
{
    rules_show(game_var());
}

_Static_assert(VAR_COUNT <= 128, "store_prefs_t.rules_seen and rules_seen_hi: a bit per variant: widen them");

/* (documented in ui_internal.h) */
void rules_first_time(void)
{
    if (game_var() == VAR_CLASSIC || is_store_rules_seen(game_var())) {
        return;
    }
    store_set_rules_seen(game_var());
    rules_show(game_var());
}
