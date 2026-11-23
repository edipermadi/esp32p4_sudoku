/**
 * @file stats_db_kinds.c
 * @brief Puzzle kinds (New, Daily, X-Sudoku, ...) and the level names built from them.
 */
#include "stats_db_internal.h"

/** Each kind's name (stats_kind_name()). */
static const char *const STATS_KIND_NAMES[KIND_COUNT] = {"New puzzles", "Daily puzzle", "X-Sudoku", "Hyper", "Jigsaw",
                                                         "Killer", "Anti-Knight", "Even-Odd", "Consecutive", "Thermo",
                                                         "Sandwich", "Mini 6x6", "Kids 4x4", "Anti-King",
                                                         "Non-Consecutive", "Disjoint Groups", "Asterisk", "Kropki", "XV",
                                                         "Greater-Than", "Quadruple", "Arrow", "Renban", "German Whispers",
                                                         "Skyscraper", "Little Killer", "Palindrome", "Region Sum Lines",
                                                         "X-Sums", "Zipper Lines", "Frame", "Between Lines",
                                                         "Entropic Lines", "Modular Lines", "Center Dot", "Outside Sudoku",
                                                         "Rossini", "Diagonal Non-Consecutive", "Dutch Whispers",
                                                         "Parity Lines", "Slow Thermo", "Fortress", "Argyle", "Nabner",
                                                         "Numbered Rooms", "Sum Skyscrapers", "Lockout Lines", "Sumless Killer", "Double Arrow",
                                                         "Diagonal Consecutive", "10 Lines", "Girandola"
                                                        };

/** Each kind's prefix in level names (stats_kind_prefix()). */
static const char *const STATS_KIND_PREFIX[KIND_COUNT] = {"Generated", "Daily", "X-Sudoku", "Hyper", "Jigsaw", "Killer",
                                                          "Anti-Knight", "Even-Odd", "Consecutive", "Thermo", "Sandwich", "Mini",
                                                          "Kids", "Anti-King", "Non-Consecutive", "Disjoint", "Asterisk",
                                                          "Kropki", "XV", "Greater-Than", "Quadruple", "Arrow", "Renban",
                                                          "German Whispers", "Skyscraper", "Little Killer", "Palindrome",
                                                          "Region Sum", "X-Sums", "Zipper", "Frame", "Between",
                                                          "Entropic", "Modular", "Center Dot", "Outside", "Rossini",
                                                          "Diagonal Non-Consecutive", "Dutch Whispers", "Parity",
                                                          "Slow Thermo", "Fortress", "Argyle", "Nabner",
                                                          "Numbered Rooms", "Sum Skyscrapers", "Lockout Lines", "Sumless Killer", "Double Arrow",
                                                          "Diagonal Consecutive", "10 Lines", "Girandola"
                                                         };

/* (documented in stats_db.h) */
const char *stats_kind_name(int kind) { return STATS_KIND_NAMES[kind]; }

/* (documented in stats_db.h) */
const char *stats_kind_prefix(int kind) { return STATS_KIND_PREFIX[kind]; }

/* (documented in stats_db.h) */
int stats_kind_of(int variant, bool daily)
{
    if (daily) {
        return KIND_DAILY;
    }
    switch (variant) {
    case VAR_X:
        return KIND_X;
    case VAR_HYPER:
        return KIND_HYPER;
    case VAR_JIGSAW:
        return KIND_JIGSAW;
    case VAR_KILLER:
        return KIND_KILLER;
    case VAR_KNIGHT:
        return KIND_KNIGHT;
    case VAR_EVENODD:
        return KIND_EVENODD;
    case VAR_CONSEC:
        return KIND_CONSEC;
    case VAR_THERMO:
        return KIND_THERMO;
    case VAR_SANDWICH:
        return KIND_SANDWICH;
    case VAR_MINI:
        return KIND_MINI;
    case VAR_KIDS:
        return KIND_KIDS;
    case VAR_KING:
        return KIND_KING;
    case VAR_NONCONSEC:
        return KIND_NONCONSEC;
    case VAR_DISJOINT:
        return KIND_DISJOINT;
    case VAR_ASTERISK:
        return KIND_ASTERISK;
    case VAR_KROPKI:
        return KIND_KROPKI;
    case VAR_XV:
        return KIND_XV;
    case VAR_GT:
        return KIND_GT;
    case VAR_QUAD:
        return KIND_QUAD;
    case VAR_ARROW:
        return KIND_ARROW;
    case VAR_RENBAN:
        return KIND_RENBAN;
    case VAR_WHISPER:
        return KIND_WHISPER;
    case VAR_SKY:
        return KIND_SKY;
    case VAR_LK:
        return KIND_LK;
    case VAR_PALINDROME:
        return KIND_PALINDROME;
    case VAR_REGIONSUM:
        return KIND_REGIONSUM;
    case VAR_XSUM:
        return KIND_XSUM;
    case VAR_ZIPPER:
        return KIND_ZIPPER;
    case VAR_FRAME:
        return KIND_FRAME;
    case VAR_BETWEEN:
        return KIND_BETWEEN;
    case VAR_ENTROPIC:
        return KIND_ENTROPIC;
    case VAR_MODULAR:
        return KIND_MODULAR;
    case VAR_CENTERDOT:
        return KIND_CENTERDOT;
    case VAR_OUTSIDE:
        return KIND_OUTSIDE;
    case VAR_ROSSINI:
        return KIND_ROSSINI;
    case VAR_DIAGNC:
        return KIND_DIAGNC;
    case VAR_DUTCH:
        return KIND_DUTCH;
    case VAR_PARITY:
        return KIND_PARITY;
    case VAR_SLOW:
        return KIND_SLOW;
    case VAR_FORTRESS:
        return KIND_FORTRESS;
    case VAR_ARGYLE:
        return KIND_ARGYLE;
    case VAR_NABNER:
        return KIND_NABNER;
    case VAR_NROOMS:
        return KIND_NROOMS;
    case VAR_SKYSUM:
        return KIND_SKYSUM;
    case VAR_LOCKOUT:
        return KIND_LOCKOUT;
    case VAR_SUMLESS:
        return KIND_SUMLESS;
    case VAR_DARROW:
        return KIND_DARROW;
    case VAR_DIAGCONSEC:
        return KIND_DIAGCONSEC;
    case VAR_TEN:
        return KIND_TEN;
    case VAR_GIRANDOLA:
        return KIND_GIRANDOLA;
    default:
        return KIND_NEW;
    }
}

/* (documented in stats_db_internal.h) */
bool sdb_kind_level_of(const char *name, int *kind, int *level)
{
    for (int k = 0; name && k < KIND_COUNT; k++) {
        size_t n = strlen(STATS_KIND_PREFIX[k]);
        if (strncmp(name, STATS_KIND_PREFIX[k], n) != 0 || name[n] != ' ') {
            continue;
        }
        for (int l = 0; l < GEN_LEVELS; l++) {
            if (strcmp(name + n + 1, gen_level_name(l)) == 0) {
                *kind = k;
                *level = l;
                return true;
            }
        }
    }
    return false;
}

/* (documented in stats_db_internal.h) */
void sdb_bind_puzzle(sqlite3_stmt *st, uint16_t id)
{
    sqlite3_bind_int(st, 1, id);
    if (puzzle_is_generated(id)) {               /* level "Generated Hard", "X-Sudoku Hard", "Mini Hard"; number = generated.id */
        char src[STATS_IMPORT_NAME];
        int num;
        if (sdb_gen_source_of(id - PUZZLE_GEN_BASE, src, &num)) {   /* imported: its collection and number */
            sqlite3_bind_text(st, 2, src, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 3, num);
            return;
        }
        char lv[24];
        bool daily = false;
        int variant = VAR_CLASSIC;
        int g = sdb_gen_level_of(id - PUZZLE_GEN_BASE, &daily, &variant);
        snprintf(lv, sizeof(lv), "%s %s", STATS_KIND_PREFIX[stats_kind_of(variant, daily)],
                 g >= 0 && g < GEN_LEVELS ? gen_level_name(g) : "");
        sqlite3_bind_text(st, 2, lv, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, id - PUZZLE_GEN_BASE);
        return;
    }
    int c = puzzle_collection_of(id);
    sqlite3_bind_text(st, 2, puzzle_collection(c)->name, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, id - puzzle_collection(c)->first + 1);
}
