/**
 * @file stats_db_import.c
 * @brief Puzzle collections imported from the card (.sdm, .xml, .sdx files).
 */
#include "stats_db_internal.h"

static int query_int_text(const char *sql, const char *text);

/* ------------------------------------------------------------- imported puzzles */

#define IMPORT_MAX_RUN 2000                          /**< puzzles per import run */

static volatile bool s_imports_changed;              /**< an import changed the collections */

/**
 * @brief 81 cells from text.
 * @param[in] p    the text (digits, '.' or '0' = empty)
 * @param[out] out  gets the cells
 * @return false if it isn't a puzzle
 */
static bool parse_cells(const char *p, uint8_t out[81])
{
    for (int i = 0; i < 81; i++) {
        char c = p[i];
        if (c >= '1' && c <= '9') {
            out[i] = (uint8_t)(c - '0');
        } else if (c == '0' || c == '.') {
            out[i] = 0;
        } else {
            return false;
        }
    }
    char after = p[81];
    return !(after >= '0' && after <= '9') && after != '.';
}

/**
 * @brief The puzzles of one file.
 * @param[in] path   the file
 * @param[in] xml    OpenSudoku's XML: the puzzles are data="..." (or '...') attributes; else a puzzle
 *               per line
 * @param[out] cells  gets them
 * @param[in] max    puzzles at most
 * @return how many
 */
static int parse_file(const char *path, bool xml, uint8_t (*cells)[81], int max)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    static char line[512];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        if (xml) {
            for (const char *q = line; n < max && (q = strstr(q, "data=")) != NULL; q += 5) {
                if ((q[5] == '"' || q[5] == '\'') && strlen(q + 6) >= 82 && parse_cells(q + 6, cells[n])) {
                    n++;
                }
            }
        } else {
            const char *q = line;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (strlen(q) >= 81 && parse_cells(q, cells[n])) {
                n++;
            }
        }
    }
    fclose(f);
    return n;
}

/**
 * @brief Whether a name ends with a suffix (any case).
 * @param[in] s       the name
 * @param[in] suffix  the suffix, e.g. ".sdm"
 * @return true: it does
 */
static bool is_ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

/**
 * @brief Adds a collection's row to imported.
 * @param[in] name   the collection's name
 * @param[in] file   the file it came from
 * @param[in] first  its first generated.id
 * @param[in] count  its puzzles: first .. first + count - 1
 * @return false: a name taken already (the file is skipped), or an error
 */
static bool import_collection(const char *name, const char *file, int first, int count)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "INSERT INTO imported(name, file, first_id, count, created_at)"
                           " VALUES(?1, ?2, ?3, ?4, ?5)", -1, &st, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, file, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, first);
    sqlite3_bind_int(st, 4, count);
    sqlite3_bind_int64(st, 5, time(NULL));         /* (the clock is set: sdb_do_import*() check) */
    bool ok = sdb_step_done(st, "import collection");     /* (a name taken already: the file is skipped) */
    sqlite3_finalize(st);
    return ok;
}

/**
 * @brief A collection's name with a number after it ("base 2"), the name shortened so the number
 *        always fits.
 * @param[out] name  gets it
 * @param[in] n     its size
 * @param[in] base  the name
 * @param[in] k     the number
 */
static void numbered_name(char *name, size_t n, const char *base, int k)
{
    char num[16];
    size_t sl = (size_t)snprintf(num, sizeof(num), " %d", k);
    size_t keep = strlen(base);
    if (keep > n - 1 - sl) {
        keep = n - 1 - sl;
    }
    memcpy(name, base, keep);
    memcpy(name + keep, num, sl + 1);
}

/**
 * @brief Reads an .sdx file into generated and imported (in the caller's transaction).
 * @param[in] path  the file
 * @param[in] file  its name, kept to skip it next time
 * @param[in] base  the collection's name (more than 100 puzzles: "base 1", "base 2", ...)
 * @param[in] max   puzzles at most
 * @param[in,out] bad   counts the lines left out
 * @return how many went in, or -1 if writing failed
 */
static int import_sdx(const char *path, const char *file, const char *base, int max, int *bad)
{
    static char *line;
    static sudoku_geom_t *geom;
    if (!line) {
        line = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    }
    if (!geom) {
        geom = heap_caps_malloc(sizeof(*geom), MALLOC_CAP_SPIRAM);
    }
    FILE *f = line && geom ? fopen(path, "r") : NULL;
    if (!f) {
        return 0;
    }
    /* the puzzles first: are there more than 100 (then the names get numbers)? */
    int lines = 0;
    while (fgets(line, 2048, f)) {
        lines += line[0] != '#' && line[0] != '\n' && line[0] != '\r';
    }
    rewind(f);
    bool numbered = lines > 100;
    int added = 0, in_chunk = 0, first = 0, chunk = 0;
    char name[STATS_IMPORT_NAME];
    while (added < max && fgets(line, 2048, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) {
            line[--l] = 0;
        }
        if (!l || line[0] == '#') {
            continue;
        }
        sdx_puzzle_t p;                             /* (sdx.c: the same reading as the preview's and the sound's) */
        if (!sdx_parse(line, &p) || !sdx_geom(&p, geom)) {
            (*bad)++;
            continue;
        }
        const sudoku_geom_t *G = geom;
        uint8_t g[81];
        memcpy(g, p.givens, 81);
        int v = p.variant;
        int lv = p.level >= 0 ? (is_sudoku_unique(G, g) ? p.level : -1)
                 : sudoku_grade(G, g);   /* no level: rated (-1: not one solution) */
        if (lv < 0) {                               /* not exactly one solution */
            (*bad)++;
            continue;
        }
        const char *fld[7] = {"", "", "", p.regions, p.cages, p.marks, p.thermos};
        if (in_chunk == 100) {                      /* the next hundred: another collection */
            if (!import_collection(name, file, first, in_chunk)) {
                fclose(f);
                return -1;
            }
            in_chunk = 0;
        }
        if (in_chunk == 0) {
            chunk++;
            if (numbered) {
                numbered_name(name, sizeof(name), base, chunk);
            } else {
                snprintf(name, sizeof(name), "%s", base);
            }
        }
        char gv[82];
        for (int i = 0; i < 81; i++) {
            gv[i] = (char)('0' + g[i]);
        }
        gv[81] = 0;
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(sdb_db(), "INSERT INTO generated(level, givens, created_at, variant, regions, cages, marks,"
                               " thermos, source, number) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)", -1,
                               &st, NULL) != SQLITE_OK) {
            fclose(f);
            return -1;
        }
        sqlite3_bind_int(st, 1, lv);
        sqlite3_bind_text(st, 2, gv, 81, SQLITE_STATIC);
        time_t now = time(NULL);
        if (now > 1700000000) {
            sqlite3_bind_int64(st, 3, now);
        } else {
            sqlite3_bind_null(st, 3);
        }
        sqlite3_bind_int(st, 4, v);
        for (int k = 3; k < 7; k++) {               /* regions, cages, marks, thermos: NULL when unused */
            if (fld[k][0]) {
                sqlite3_bind_text(st, 5 + k - 3, fld[k], -1, SQLITE_STATIC);
            } else {
                sqlite3_bind_null(st, 5 + k - 3);
            }
        }
        sqlite3_bind_text(st, 9, name, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 10, in_chunk + 1);
        bool ok = sdb_step_done(st, "import puzzle");
        sqlite3_finalize(st);
        if (!ok) {
            fclose(f);
            return -1;
        }
        if (in_chunk == 0) {
            first = (int)sqlite3_last_insert_rowid(sdb_db());
        }
        in_chunk++;
        added++;
    }
    fclose(f);
    if (in_chunk && !import_collection(name, file, first, in_chunk)) {
        return -1;
    }
    return added;
}

/**
 * @brief One file into generated and imported, as a collection named after it (more than 100
 *        puzzles: "name 1", "name 2" ...), in its own transaction.
 * @param[in] path   the file
 * @param[in] fn     its name, kept to skip it next time
 * @param[in] cells  room for max puzzles
 * @param[in] max    puzzles at most
 * @param[in,out] bad    counts the puzzles left out (broken, or not exactly one solution)
 * @return how many went in, 0 if none did, -1 if writing failed
 */
static int import_one(const char *path, const char *fn, uint8_t (*cells)[81], int max, int *bad)
{
    char base[STATS_IMPORT_NAME];                   /* the collection's name: the file's, without the extension */
    const char *dot = strrchr(fn, '.');
    snprintf(base, sizeof(base), "%.*s", (int)(dot ? dot - fn : (int)strlen(fn)), fn);
    if (is_ends_with(fn, ".sdx")) {                    /* any variant: line by line (see import_sdx) */
        int got = sdb_exec("BEGIN") ? import_sdx(path, fn, base, max, bad) : -1;
        if (got > 0 && sdb_exec("COMMIT")) {
            return got;
        }
        sdb_exec("ROLLBACK");
        return got < 0 ? -1 : 0;
    }
    int n = parse_file(path, is_ends_with(fn, ".opensudoku"), cells, max);
    static uint8_t *levels;                         /* each one rated by the grader (generator.h) */
    if (!levels && !(levels = heap_caps_malloc(IMPORT_MAX_RUN, MALLOC_CAP_SPIRAM))) {
        return -1;
    }
    int good = 0;
    const sudoku_geom_t *classic = sudoku_geom(VAR_CLASSIC);
    for (int i = 0; i < n; i++) {                   /* keep the ones with exactly one solution, rated */
        int lv = sudoku_grade(classic, cells[i]);   /* (-1: not exactly one solution) */
        if (lv < 0) {
            (*bad)++;
            continue;
        }
        levels[good] = (uint8_t)lv;
        memmove(cells[good++], cells[i], 81);
    }
    if (!good) {
        return 0;
    }
    bool ok = sdb_exec("BEGIN");
    int chunks = (good + 99) / 100;
    for (int k = 0; ok && k < chunks; k++) {
        int from = k * 100, cnt = good - from < 100 ? good - from : 100, first = 0;
        char name[STATS_IMPORT_NAME];
        if (chunks == 1) {
            snprintf(name, sizeof(name), "%s", base);
        } else {
            numbered_name(name, sizeof(name), base, k + 1);
        }
        for (int i = 0; ok && i < cnt; i++) {
            sqlite3_stmt *st;
            char g[82];
            for (int c = 0; c < 81; c++) {
                g[c] = (char)('0' + cells[from + i][c]);
            }
            g[81] = 0;
            ok = sqlite3_prepare_v2(sdb_db(), "INSERT INTO generated(level, givens, created_at, variant, source, number)"
                                    " VALUES(?5, ?1, ?2, 0, ?3, ?4)", -1, &st, NULL) == SQLITE_OK;
            if (!ok) {
                break;
            }
            sqlite3_bind_int(st, 5, levels[from + i]);
            sqlite3_bind_text(st, 1, g, 81, SQLITE_STATIC);
            time_t now = time(NULL);
            if (now > 1700000000) {
                sqlite3_bind_int64(st, 2, now);
            } else {
                sqlite3_bind_null(st, 2);
            }
            sqlite3_bind_text(st, 3, name, -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 4, i + 1);
            ok = sdb_step_done(st, "import puzzle");
            sqlite3_finalize(st);
            if (ok && i == 0) {
                first = (int)sqlite3_last_insert_rowid(sdb_db());
            }
        }
        ok = ok && import_collection(name, fn, first, cnt);   /* (a name taken already: the file is skipped) */
    }
    if (ok && sdb_exec("COMMIT")) {
        return good;
    }
    sdb_exec("ROLLBACK");
    return -1;
}

/* (documented in stats_db_internal.h) */
void sdb_do_import(void)
{
    if (!sdb_db()) {
        return;
    }
    static uint8_t (*cells)[81];
    if (!cells) {
        cells = heap_caps_malloc(81 * IMPORT_MAX_RUN, MALLOC_CAP_SPIRAM);
    }
    if (!cells) {
        return;
    }
    if (!CLOCK_VALID(time(NULL))) {                 /* (every collection gets its date) */
        sdb_post_message(SDB_NO_CLOCK);
        return;
    }
    DIR *dir = opendir(IMPORT_DIR);
    if (!dir) {
        mkdir(IMPORT_DIR, 0775);                    /* there for next time */
        sdb_post_message("No puzzles/ folder on the card: it's made now. Put .opensudoku or .sdm files in it.");
        return;
    }
    int files = 0, added = 0, bad = 0, skipped = 0, total = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL && total < IMPORT_MAX_RUN) {
        const char *fn = e->d_name;
        if (!is_ends_with(fn, ".opensudoku") && !is_ends_with(fn, ".sdx") && !is_ends_with(fn, ".sdm") &&
                !is_ends_with(fn, ".txt")) {
            continue;
        }
        if (query_int_text("SELECT COUNT(*) FROM imported WHERE file = ?1", fn) > 0) {
            skipped++;
            continue;
        }
        char path[300];
        snprintf(path, sizeof(path), IMPORT_DIR "/%s", fn);
        int got = import_one(path, fn, cells, IMPORT_MAX_RUN - total, &bad);
        if (got > 0) {
            files++;
            added += got;
            total += got;
            printf("stats_db: imported %d puzzles from %s\n", got, fn);
        }
    }
    closedir(dir);
    char msg[160];
    if (added) {
        snprintf(msg, sizeof(msg), "Imported %d puzzle%s from %d file%s%s.", added, added == 1 ? "" : "s", files,
                 files == 1 ? "" : "s", bad ? " (some had no single solution and were left out)" : "");
        s_imports_changed = true;
    } else if (skipped && !bad) {
        snprintf(msg, sizeof(msg), "Nothing new in puzzles/: those files are imported already.");
    } else {
        snprintf(msg, sizeof(msg), "No puzzles found in puzzles/ (.opensudoku, .sdm, .sdx or .txt files).");
    }
    sdb_post_message(msg);
}

#define IMPORT_RESULTS 4                        /**< file imports' results kept (stats_db_import_result()) */
static stats_import_result_t s_import_res[IMPORT_RESULTS];   /**< the last few, by ticket (under sdb_lock()) */
static uint32_t s_import_ticket;                /**< the last ticket handed out (stats_db_import_file()) */

/**
 * @brief A file import finished: its result kept for stats_db_import_result(), its message for the notice.
 * @param[in] ticket  the import's ticket
 * @param[in] got  puzzles imported (or 0, -1, -2: see stats_import_result_t)
 * @param[in] bad  puzzles left out
 * @param[in] msg  what happened
 */
static void import_finished(uint32_t ticket, int got, int bad, const char *msg)
{
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    stats_import_result_t *r = &s_import_res[ticket % IMPORT_RESULTS];
    r->ticket = ticket;
    r->got = got;
    r->bad = bad;
    snprintf(r->msg, sizeof(r->msg), "%s", msg);
    xSemaphoreGive(sdb_lock());
    sdb_post_message(msg);
}

/* (documented in stats_db.h) */
bool stats_db_import_result(uint32_t ticket, stats_import_result_t *out)
{
    if (!sdb_lock() || !ticket) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    const stats_import_result_t *r = &s_import_res[ticket % IMPORT_RESULTS];
    bool done = r->ticket == ticket;
    if (done) {
        *out = *r;
    }
    xSemaphoreGive(sdb_lock());
    return done;
}

/* (documented in stats_db_internal.h) */
void sdb_do_import_file(const char *path, uint32_t ticket)
{
    if (!sdb_db()) {
        import_finished(ticket, -1, 0, "Importing needs sudoku.db on the microSD card.");
        return;
    }
    if (!CLOCK_VALID(time(NULL))) {                 /* (every collection gets its date) */
        import_finished(ticket, -1, 0, SDB_NO_CLOCK);
        return;
    }
    static uint8_t (*cells)[81];
    if (!cells) {
        cells = heap_caps_malloc(81 * IMPORT_MAX_RUN, MALLOC_CAP_SPIRAM);
    }
    if (!cells) {
        import_finished(ticket, -1, 0, "Couldn't import: no memory.");
        return;
    }
    const char *slash = strrchr(path, '/'), *fn = slash ? slash + 1 : path;
    char msg[128];
    if (query_int_text("SELECT COUNT(*) FROM imported WHERE file = ?1", fn) > 0) {
        snprintf(msg, sizeof(msg), "%.80s is imported already: it's in Puzzles > Imported.", fn);
        import_finished(ticket, -2, 0, msg);
        return;
    }
    int bad = 0, got = import_one(path, fn, cells, IMPORT_MAX_RUN, &bad);
    if (got > 0) {
        printf("stats_db: imported %d puzzles from %s\n", got, path);
        snprintf(msg, sizeof(msg), "Imported %d puzzle%s: Puzzles > Imported%s.", got, got == 1 ? "" : "s",
                 bad ? " (some had no single solution and were left out)" : "");
        s_imports_changed = true;
    } else if (got < 0) {
        snprintf(msg, sizeof(msg), "Couldn't import %.60s (is a collection of that name there already?).", fn);
    } else {
        snprintf(msg, sizeof(msg), "No puzzle with exactly one solution in %.70s.", fn);
    }
    import_finished(ticket, got, bad, msg);
}

/* (documented in stats_db_internal.h) */
void sdb_do_list_imports(void)
{
    s_gen.nimports = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT name, first_id, count, created_at FROM imported ORDER BY id", -1, &st,
                                       NULL) == SQLITE_OK) {
        while (s_gen.nimports < STATS_MAX_IMPORTS && sqlite3_step(st) == SQLITE_ROW) {
            stats_import_t *r = &s_gen.imports[s_gen.nimports++];
            const char *n = (const char *)sqlite3_column_text(st, 0);
            snprintf(r->name, sizeof(r->name), "%s", n ? n : "");
            r->first_gid = sqlite3_column_int(st, 1);
            r->count = sqlite3_column_int(st, 2);
            r->created_at = sqlite3_column_int64(st, 3);   /* (NULL: 0) */
        }
        sqlite3_finalize(st);
    }
    s_gen.ok = sdb_db() != NULL;
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_solved_in(void)
{
    memset(s_gen.solved_bits, 0, sizeof(s_gen.solved_bits));
    s_gen.nsolved = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT puzzle FROM puzzle_stats WHERE player_id = ?1 AND puzzle >= ?2"
                                       " AND puzzle < ?3", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, s_gen.puzzle);
        sqlite3_bind_int(st, 3, s_gen.puzzle + s_gen.count);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int i = sqlite3_column_int(st, 0) - s_gen.puzzle;
            if (i < 0 || i >= s_gen.count || i >= 128) {
                continue;
            }
            s_gen.solved_bits[i >> 3] |= (uint8_t)(1u << (i & 7));
            s_gen.nsolved++;
        }
        sqlite3_finalize(st);
    }
    s_gen.ok = sdb_db() != NULL;
    sdb_gen_reply();
}

/**
 * @brief One number from a query with one text parameter (?1).
 * @param[in] sql   the query
 * @param[in] text  ?1
 * @return the number; 0 for none or an error
 */
static int query_int_text(const char *sql, const char *text)
{
    int v = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, text, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            v = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    return v;
}

/* (documented in stats_db.h) */
uint32_t stats_db_import_file(const char *path)
{
    /* the path goes with the command (the worker frees it): two imports queued at once each import
     * their own file */
    char *copy = strdup(path);
    uint32_t ticket = ++s_import_ticket ? s_import_ticket : ++s_import_ticket;   /* (never 0) */
    cmd_t c = {.type = CMD_IMPORT_FILE, .blob = (uint8_t *)copy, .ival = (int32_t)ticket};
    if (!copy || !sdb_send(&c)) {
        free(copy);
        sdb_post_message("Importing needs sudoku.db on the microSD card.");
        return 0;
    }
    return ticket;
}

/* (documented in stats_db.h) */
void stats_db_import_puzzles(void)
{
    cmd_t c = {.type = CMD_IMPORT};
    if (!sdb_send(&c)) {
        sdb_post_message("Import: no database.");
    }
}

/* (documented in stats_db.h) */
bool stats_db_take_imports_changed(void)
{
    bool v = s_imports_changed;
    s_imports_changed = false;
    return v;
}

/* (documented in stats_db.h) */
int stats_db_list_imports(stats_import_t *out)
{
    if (!sdb_gen() || !sdb_gen_call(CMD_LIST_IMPORTS)) {
        return -1;
    }
    memcpy(out, s_gen.imports, sizeof(stats_import_t) * (size_t)s_gen.nimports);
    return s_gen.nimports;
}

/* (documented in stats_db.h) */
int stats_db_solved_in(uint16_t first, int count, uint8_t *solved)
{
    if (!sdb_gen()) {
        return 0;
    }
    s_gen.puzzle = first;
    s_gen.count = count > 128 ? 128 : count;
    if (!sdb_gen_call(CMD_SOLVED_IN)) {
        return 0;
    }
    if (solved) {
        memcpy(solved, s_gen.solved_bits, (size_t)(s_gen.count + 7) / 8);
    }
    return s_gen.nsolved;
}

/* (documented in stats_db.h) */
bool stats_db_import_delete(const char *name)
{
    if (!sdb_gen()) {
        return false;
    }
    s_gen.id = 0;
    snprintf(s_gen.name, sizeof(s_gen.name), "%s", name);
    return sdb_gen_call(CMD_COLL_DELETE);
}
