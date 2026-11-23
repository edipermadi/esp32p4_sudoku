/**
 * @file stats_db_schema.c
 * @brief The tables: creating them, and bringing an older database up to date.
 */
#include "stats_db_internal.h"

/* table layouts (user_version 11), shared by SCHEMA and the upgrade below */
/** players' layout */
#define T_PLAYERS \
    "players("                                                                  \
    "  id INTEGER PRIMARY KEY,"                                                 \
    "  name TEXT NOT NULL UNIQUE COLLATE NOCASE,"                               \
    "  created_at INTEGER,"                 /* Unix time UTC; NULL = clock not set */ \
    "  color INTEGER,"                      /* 0..7; NULL = by id (PLAYER_COLOR) */      \
    "  avatar INTEGER)"                     /* 1..AVATAR_COUNT (avatars.h); NULL = none */

/** solves' layout */
#define T_SOLVES \
    "solves("                                                                   \
    "  id INTEGER PRIMARY KEY,"                                                 \
    "  player_id INTEGER NOT NULL REFERENCES players(id),"                      \
    "  puzzle INTEGER NOT NULL,"            /* 0..1599, as in the firmware */  \
    "  level TEXT NOT NULL,"                /* collection, e.g. 'Gnome 2 Hard' */ \
    "  number INTEGER NOT NULL,"            /* 1..100 within the level */       \
    "  seconds INTEGER NOT NULL,"                                               \
    "  mistakes INTEGER NOT NULL,"                                              \
    "  finished_at INTEGER,"                /* Unix time UTC; NULL = clock not set */ \
    "  started_at INTEGER,"                 /* when it was opened (or restarted) */ \
    "  hints INTEGER NOT NULL DEFAULT 0,"   /* cells filled with Fill (the solution) */ \
    "  moves BLOB,"                         /* move log (moves.h); NULL = not recorded */ \
    "  flags INTEGER NOT NULL DEFAULT 0)"   /* GAME_FLAG_* (store.h): no-notes challenge, techniques hinted */

/** puzzle_stats' layout */
#define T_PUZZLE_STATS \
    "puzzle_stats("                                                             \
    "  player_id INTEGER NOT NULL REFERENCES players(id),"                      \
    "  puzzle INTEGER NOT NULL,"                                                \
    "  level TEXT NOT NULL,"                                                    \
    "  number INTEGER NOT NULL,"                                                \
    "  best_seconds INTEGER NOT NULL,"                                          \
    "  fewest_mistakes INTEGER,"            /* NULL = solved before mistakes were counted */ \
    "  solves INTEGER NOT NULL,"                                                \
    "  PRIMARY KEY (player_id, puzzle))"

/** saved_game's layout */
#define T_SAVED_GAME \
    "saved_game("                           /* unfinished games, STATS_MAX_GAMES per player */ \
    "  player_id INTEGER NOT NULL REFERENCES players(id),"                      \
    "  puzzle INTEGER NOT NULL,"                                                \
    "  level TEXT NOT NULL,"                                                    \
    "  number INTEGER NOT NULL,"                                                \
    "  cells TEXT NOT NULL,"                /* 81 digits, row by row, 0 = empty, givens included */ \
    "  notes BLOB NOT NULL,"                /* 81 x uint16 LE, bit d = pencil mark d */ \
    "  elapsed_ms INTEGER NOT NULL,"                                            \
    "  mistakes INTEGER NOT NULL,"                                              \
    "  saved_at INTEGER,"                   /* Unix time UTC; NULL = clock not set */ \
    "  started_at INTEGER,"                                                     \
    "  hints INTEGER NOT NULL DEFAULT 0,"                                       \
    "  moves BLOB,"                         /* move log so far (moves.h) */    \
    "  seq INTEGER NOT NULL DEFAULT 0,"     /* higher = played more recently */ \
    "  colors BLOB,"                        /* 81 cell colours, 0 = none */    \
    "  flags INTEGER NOT NULL DEFAULT 0,"   /* GAME_FLAG_* (store.h), as solves.flags */ \
    "  PRIMARY KEY (player_id, puzzle))"

/** The tables, created if missing (an older database keeps its own: upgrade_schema()). */
static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS " T_PLAYERS ";"
    "CREATE TABLE IF NOT EXISTS " T_SOLVES ";"
    "CREATE TABLE IF NOT EXISTS " T_PUZZLE_STATS ";"
    "CREATE TABLE IF NOT EXISTS " T_SAVED_GAME ";"
    "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value);"
    "CREATE TABLE IF NOT EXISTS achievements("           /* unlocked achievements (ACH below) */
    "  player_id INTEGER NOT NULL REFERENCES players(id),"
    "  key TEXT NOT NULL,"
    "  unlocked_at INTEGER,"                             /* Unix time; NULL = found done at first fill / no clock */
    "  PRIMARY KEY (player_id, key));"
    "CREATE TABLE IF NOT EXISTS achievement_progress("   /* each achievement's progress: a cache, */
    "  player_id INTEGER NOT NULL REFERENCES players(id),"   /* refreshed by sdb_ach_sync() */
    "  key TEXT NOT NULL,"
    "  value INTEGER NOT NULL,"
    "  PRIMARY KEY (player_id, key));"
    "CREATE TABLE IF NOT EXISTS generated("              /* puzzles made on the board */
    "  id INTEGER PRIMARY KEY,"                          /* puzzle id = 2000 + id */
    "  level INTEGER NOT NULL,"                          /* 0 Easy, 1 Medium, 2 Hard, 3 Expert */
    "  givens TEXT NOT NULL,"                            /* 81 digits row by row, 0 = empty */
    "  created_at INTEGER,"
    "  day TEXT,"                                        /* daily puzzle: its date 'YYYY-MM-DD', else NULL */
    "  variant INTEGER NOT NULL DEFAULT 0,"              /* 0 classic, 1 X-Sudoku, 2 mini 6x6, 3 Hyper, 4 Jigsaw */
    "  regions TEXT,"                                    /* Jigsaw: 81 digits 0-8, the region of each cell */
    "  cages TEXT,"                                      /* Killer: "sum:cell,cell,...;sum:..." */
    "  marks TEXT,"                                      /* Even-Odd / Consecutive: sudoku_marks_text() */
    "  thermos TEXT,"                                    /* Thermo: "cell,cell,...;cell,..." from the bulbs */
    "  source TEXT,"                                     /* imported: its collection */
    "  number INTEGER);"                                 /* imported: its place in the collection, from 1 */
    "CREATE TABLE IF NOT EXISTS imported("               /* collections imported from puzzles/ */
    "  id INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL UNIQUE,"
    "  file TEXT NOT NULL,"
    "  first_id INTEGER NOT NULL,"                       /* generated.id of its first puzzle */
    "  count INTEGER NOT NULL,"
    "  created_at INTEGER);"
    "CREATE TABLE IF NOT EXISTS learned("                /* Learn practice finished, per player */
    "  player_id INTEGER NOT NULL REFERENCES players(id),"
    "  technique INTEGER NOT NULL,"                      /* Learn's list, 0 Hidden single .. 9 XY-Wing */
    "  at INTEGER,"
    "  PRIMARY KEY (player_id, technique));"
    "CREATE TABLE IF NOT EXISTS bookmarks("              /* starred puzzles, per player */
    "  player_id INTEGER NOT NULL REFERENCES players(id),"
    "  puzzle INTEGER NOT NULL,"
    "  at INTEGER,"
    "  PRIMARY KEY (player_id, puzzle));"
    "CREATE TABLE IF NOT EXISTS seen("                   /* shown once per player: 'tour' */
    "  player_id INTEGER NOT NULL REFERENCES players(id),"
    "  what TEXT NOT NULL,"
    "  at INTEGER,"
    "  PRIMARY KEY (player_id, what));"
    "CREATE TABLE IF NOT EXISTS saved_collections("      /* collections made on the board (Save) */
    "  id INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL UNIQUE,"
    "  created_at INTEGER);"
    "CREATE TABLE IF NOT EXISTS saved_puzzles("          /* their puzzles */
    "  collection_id INTEGER NOT NULL REFERENCES saved_collections(id),"
    "  puzzle INTEGER NOT NULL,"                         /* puzzle id (generated: 2000 + generated.id) */
    "  number INTEGER NOT NULL,"                         /* its place in the collection, from 1 */
    "  added_at INTEGER,"
    "  PRIMARY KEY (collection_id, puzzle));"
    "CREATE TABLE IF NOT EXISTS attack("                 /* time attack runs: Easy puzzles solved in 10 minutes */
    "  id INTEGER PRIMARY KEY,"
    "  player_id INTEGER NOT NULL REFERENCES players(id),"
    "  finished_at INTEGER,"
    "  solved INTEGER NOT NULL,"
    "  mistakes INTEGER);";

/** After the upgrade: indexes and the view, always in their current form. */
static const char SCHEMA_POST[] =
    "CREATE INDEX IF NOT EXISTS solves_puzzle ON solves(puzzle);"
    "CREATE INDEX IF NOT EXISTS solves_player ON solves(player_id, puzzle);"
    "DROP INDEX IF EXISTS puzzle_stats_level;"
    "CREATE INDEX puzzle_stats_level ON puzzle_stats(player_id, level, number);"
    "DROP VIEW IF EXISTS level_stats;"
    "CREATE VIEW level_stats AS"
    "  SELECT p.name AS player, s.level, COUNT(*) AS solved, SUM(s.solves) AS solves,"
    "         MIN(s.best_seconds) AS fastest_seconds,"
    "         CAST(ROUND(AVG(s.best_seconds)) AS INTEGER) AS average_seconds,"
    "         ROUND(AVG(s.fewest_mistakes), 1) AS average_mistakes,"
    "         SUM(s.fewest_mistakes = 0) AS without_mistakes"
    "  FROM puzzle_stats s JOIN players p ON p.id = s.player_id"
    "  GROUP BY s.player_id, s.level ORDER BY s.player_id, MIN(s.puzzle);"
    "PRAGMA user_version = 11;";

/**
 * @brief Whether a table has a column.
 * @param[in] table  the table
 * @param[in] col    the column
 * @return true: it has
 */
static bool has_column(const char *table, const char *col)
{
    bool found = false;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT 1 FROM pragma_table_info(?1) WHERE name = ?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, col, -1, SQLITE_STATIC);
        found = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    return found;
}

/**
 * @brief Databases made by earlier builds: CREATE ... IF NOT EXISTS keeps their old tables, so
 *        bring them up to date (user_version 1-5 -> 6). Their data becomes player 1's.
 * @return false on an error
 */
static bool upgrade_schema(void)
{
    bool ok = true;
    sdb_exec("DROP VIEW IF EXISTS level_stats");           /* refers to tables rebuilt below */
    if (!has_column("solves", "finished_at") && has_column("solves", "solved_at")) {
        ok &= sdb_exec("ALTER TABLE solves RENAME COLUMN solved_at TO finished_at");
    }
    if (!has_column("solves", "started_at")) {
        ok &= sdb_exec("ALTER TABLE solves ADD COLUMN started_at INTEGER");
    }
    if (!has_column("saved_game", "started_at")) {
        ok &= sdb_exec("ALTER TABLE saved_game ADD COLUMN started_at INTEGER");
    }
    if (!has_column("solves", "hints")) {
        ok &= sdb_exec("ALTER TABLE solves ADD COLUMN hints INTEGER NOT NULL DEFAULT 0");
    }
    if (!has_column("saved_game", "hints")) {
        ok &= sdb_exec("ALTER TABLE saved_game ADD COLUMN hints INTEGER NOT NULL DEFAULT 0");
    }
    if (!has_column("solves", "moves")) {
        ok &= sdb_exec("ALTER TABLE solves ADD COLUMN moves BLOB");
    }
    if (!has_column("saved_game", "moves")) {
        ok &= sdb_exec("ALTER TABLE saved_game ADD COLUMN moves BLOB");
    }
    if (!has_column("generated", "day")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN day TEXT");
    }
    if (!has_column("generated", "variant")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN variant INTEGER NOT NULL DEFAULT 0");
    }
    if (!has_column("generated", "regions")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN regions TEXT");
    }
    if (!has_column("generated", "cages")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN cages TEXT");
    }
    if (!has_column("generated", "marks")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN marks TEXT");
    }
    if (!has_column("generated", "thermos")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN thermos TEXT");
    }
    if (!has_column("generated", "source")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN source TEXT");
    }
    if (!has_column("generated", "number")) {
        ok &= sdb_exec("ALTER TABLE generated ADD COLUMN number INTEGER");
    }
    if (!has_column("players", "color")) {
        ok &= sdb_exec("ALTER TABLE players ADD COLUMN color INTEGER");
    }
    if (!has_column("players", "avatar")) {
        ok &= sdb_exec("ALTER TABLE players ADD COLUMN avatar INTEGER");
    }
    if (has_column("saved_game", "player_id") && !has_column("saved_game", "seq")) {
        /* one game per player -> several: the key becomes (player, puzzle) */
        ok &= sdb_exec("BEGIN");
        ok &= sdb_exec("ALTER TABLE saved_game RENAME TO saved_game_v8");
        ok &= sdb_exec("CREATE TABLE " T_SAVED_GAME);
        ok &= sdb_exec("INSERT INTO saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms, mistakes,"
                       " saved_at, started_at, hints, moves, seq) SELECT player_id, puzzle, level, number, cells,"
                       " notes, elapsed_ms, mistakes, saved_at, started_at, hints, moves, 1 FROM saved_game_v8");
        ok &= sdb_exec("DROP TABLE saved_game_v8");
        ok &= sdb_exec(ok ? "COMMIT" : "ROLLBACK");
        if (ok) {
            printf("stats_db: saved_game now keeps up to %d games per player\n", STATS_MAX_GAMES);
        }
    }
    if (!has_column("saved_game", "colors")) {
        ok &= sdb_exec("ALTER TABLE saved_game ADD COLUMN colors BLOB");
    }
    if (!has_column("solves", "flags")) {
        ok &= sdb_exec("ALTER TABLE solves ADD COLUMN flags INTEGER NOT NULL DEFAULT 0");
    }
    if (!has_column("saved_game", "flags")) {
        ok &= sdb_exec("ALTER TABLE saved_game ADD COLUMN flags INTEGER NOT NULL DEFAULT 0");
    }

    bool old_solves = !has_column("solves", "player_id");
    bool old_stats = !has_column("puzzle_stats", "player_id");
    bool old_game = !has_column("saved_game", "player_id");
    if (!(old_solves || old_stats || old_game)) {
        return ok;
    }
    ok &= sdb_exec("BEGIN");
    ok &= sdb_exec("INSERT OR IGNORE INTO players(id, name) VALUES(1, 'Player 1')");
    if (old_solves) {
        ok &= sdb_exec("ALTER TABLE solves ADD COLUMN player_id INTEGER NOT NULL DEFAULT 1");
    }
    if (old_stats) {
        ok &= sdb_exec("ALTER TABLE puzzle_stats RENAME TO puzzle_stats_v3");
        ok &= sdb_exec("CREATE TABLE " T_PUZZLE_STATS);
        ok &= sdb_exec("INSERT INTO puzzle_stats(player_id, puzzle, level, number, best_seconds, fewest_mistakes, solves)"
                       " SELECT 1, puzzle, level, number, best_seconds, fewest_mistakes, solves FROM puzzle_stats_v3");
        ok &= sdb_exec("DROP TABLE puzzle_stats_v3");
    }
    if (old_game) {
        ok &= sdb_exec("ALTER TABLE saved_game RENAME TO saved_game_v3");
        ok &= sdb_exec("CREATE TABLE " T_SAVED_GAME);
        ok &= sdb_exec("INSERT INTO saved_game(player_id, puzzle, level, number, cells, notes, elapsed_ms, mistakes,"
                       " saved_at, started_at, hints) SELECT 1, puzzle, level, number, cells, notes, elapsed_ms,"
                       " mistakes, saved_at, started_at, hints FROM saved_game_v3");
        ok &= sdb_exec("DROP TABLE saved_game_v3");
    }
    if (ok) {
        sdb_exec("COMMIT");
        printf("stats_db: upgraded: existing statistics and game now belong to \"Player 1\"\n");
    } else {
        sdb_exec("ROLLBACK");
    }
    return ok;
}

/* (documented in stats_db_internal.h) */
bool sdb_schema_create(void)
{
    return sdb_exec(SCHEMA) && upgrade_schema() && sdb_exec(SCHEMA_POST);
}
