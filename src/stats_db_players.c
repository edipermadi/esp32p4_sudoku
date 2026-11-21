/**
 * @file stats_db_players.c
 * @brief Players: listing, adding, renaming, deleting, and switching who plays.
 */
#include "stats_db_internal.h"

static char s_player_name[STORE_NAME_LEN];           /**< who is playing */
static stats_player_t s_players[STATS_MAX_PLAYERS];  /**< CMD_PLAYERS result */
static int s_players_n;                              /**< its rows */
static bool s_players_new;                           /**< s_players is new */
static stats_player_change_t s_change;               /**< who plays now, after a change */
static bool s_change_new;                            /**< s_change is new */

/**
 * @brief A player's name.
 * @param id   players.id
 * @param out  gets it
 * @param n    its size
 * @return false if there's no such player
 */
static bool player_name_of(int32_t id, char *out, size_t n)
{
    bool found = false;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT name FROM players WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            snprintf(out, n, "%s", (const char *)sqlite3_column_text(st, 0));
            found = true;
        }
        sqlite3_finalize(st);
    }
    return found;
}

/**
 * @brief Adds a player.
 * @param name  the name
 * @return its id, 0 if the name is taken, -1 on error
 */
static int32_t insert_player(const char *name)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "INSERT INTO players(name, created_at) VALUES(?1, ?2)", -1, &st, NULL) != SQLITE_OK) {
        return -1;
    }
    time_t now = time(NULL);
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (now > 1700000000) {
        sqlite3_bind_int64(st, 2, now);
    } else {
        sqlite3_bind_null(st, 2);
    }
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE) {
        return (int32_t)sqlite3_last_insert_rowid(sdb_db());
    }
    return (rc & 0xFF) == SQLITE_CONSTRAINT ? 0 : -1;
}

/* (documented in stats_db_internal.h) */
void sdb_select_current_player(const char *name)
{
    int32_t id = sdb_query_int("SELECT p.id FROM players p JOIN settings s ON s.key = 'current_player'"
                               " AND p.id = s.value");
    if (id <= 0) {
        id = sdb_query_int("SELECT MIN(id) FROM players");
    }
    if (id <= 0) {
        id = insert_player(name && name[0] ? name : "Player 1");
    }
    if (id <= 0) {
        id = 1;
    }
    sdb_set_player(id);
    if (!player_name_of(id, s_player_name, sizeof(s_player_name))) {
        snprintf(s_player_name, sizeof(s_player_name), "Player 1");
    }
    sdb_set_setting("current_player", false, id, NULL);
}

/* (documented in stats_db_internal.h) */
void sdb_do_list(void)
{
    stats_player_t list[STATS_MAX_PLAYERS];
    int n = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT p.id, p.name, " PLAYER_COLOR("p") ","
                                       "  (SELECT COUNT(*) FROM puzzle_stats s WHERE s.player_id = p.id),"
                                       "  (SELECT COUNT(*) FROM solves v WHERE v.player_id = p.id),"
                                       "  COALESCE(p.avatar, 0)"
                                       " FROM players p ORDER BY p.id", -1, &st, NULL) == SQLITE_OK) {
        while (n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
            stats_player_t *p = &list[n++];
            p->id = sqlite3_column_int(st, 0);
            snprintf(p->name, sizeof(p->name), "%s", (const char *)sqlite3_column_text(st, 1));
            p->color = (uint8_t)sqlite3_column_int(st, 2);
            p->solved = sqlite3_column_int(st, 3);
            p->solves = sqlite3_column_int(st, 4);
            p->avatar = (uint16_t)sqlite3_column_int(st, 5);
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    memcpy(s_players, list, sizeof(stats_player_t) * n);
    s_players_n = n;
    s_players_new = true;
    xSemaphoreGive(sdb_lock());
}

/**
 * @brief Hands who plays now to the UI (stats_db_take_player_change()).
 * @param switched  another player: their game in progress goes along
 */
static void post_change(bool switched)
{
    static stats_player_change_t ch;
    memset(&ch, 0, sizeof(ch));
    ch.id = sdb_player();
    snprintf(ch.name, sizeof(ch.name), "%s", s_player_name);
    ch.switched = switched;
    if (switched) {
        sdb_load_game(&ch.game);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    s_change = ch;
    s_change_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_do_switch(int32_t id)
{
    if (!sdb_db()) {
        return;
    }
    char name[STORE_NAME_LEN];
    if (!player_name_of(id, name, sizeof(name))) {
        sdb_post_message("That player no longer exists.");
        sdb_do_list();
        return;
    }
    sdb_flush_game();                             /* the old player's last moves */
    sdb_set_player(id);
    snprintf(s_player_name, sizeof(s_player_name), "%s", name);
    sdb_set_setting("current_player", false, id, NULL);
    sdb_load_cache();
    post_change(true);
    sdb_do_list();
}

/**
 * @brief Trims and checks a name typed on the keyboard.
 * @param in   the name as typed
 * @param out  gets it trimmed
 * @param n    its size
 * @return false if unusable
 */
static bool clean_name(const char *in, char *out, size_t n)
{
    while (*in && isspace((unsigned char)*in)) {
        in++;
    }
    snprintf(out, n, "%s", in);
    size_t len = strlen(out);
    while (len && isspace((unsigned char)out[len - 1])) {
        out[--len] = 0;
    }
    return len > 0;
}

/* (documented in stats_db_internal.h) */
void sdb_do_add(const char *raw)
{
    if (!sdb_db()) {
        return;
    }
    char name[STORE_NAME_LEN], msg[96];
    if (!clean_name(raw, name, sizeof(name))) {
        sdb_post_message("Type a name first.");
        return;
    }
    int32_t id = insert_player(name);
    if (id == 0) {
        snprintf(msg, sizeof(msg), "There is already a player called \"%s\".", name);
        sdb_post_message(msg);
        return;
    }
    if (id < 0) {
        sdb_post_message("Couldn't add the player.");
        return;
    }
    sdb_do_switch(id);                            /* a new player starts playing */
}

/* (documented in stats_db_internal.h) */
void sdb_do_rename(int32_t id, const char *raw)
{
    if (!sdb_db()) {
        return;
    }
    char name[STORE_NAME_LEN], msg[96];
    if (!clean_name(raw, name, sizeof(name))) {
        sdb_post_message("Type a name first.");
        return;
    }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "UPDATE players SET name = ?1 WHERE id = ?2", -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
        snprintf(msg, sizeof(msg), "There is already a player called \"%s\".", name);
        sdb_post_message(msg);
    } else if (rc == SQLITE_DONE && id == sdb_player()) {
        snprintf(s_player_name, sizeof(s_player_name), "%s", name);
        post_change(false);
    }
    sdb_do_list();
}

/* (documented in stats_db_internal.h) */
void sdb_do_delete(int32_t id)
{
    if (!sdb_db()) {
        return;
    }
    char name[STORE_NAME_LEN], msg[96];
    if (!player_name_of(id, name, sizeof(name))) {
        sdb_do_list();
        return;
    }
    if (sdb_query_int("SELECT COUNT(*) FROM players") <= 1) {
        sdb_post_message("The only player can't be deleted.");
        return;
    }
    bool current = id == sdb_player();
    if (current) {
        sdb_flush_game();    /* nothing pending may land after the delete */
    }
    static const char *const sql[] = {
        "DELETE FROM solves WHERE player_id = ?1",
        "DELETE FROM puzzle_stats WHERE player_id = ?1",
        "DELETE FROM saved_game WHERE player_id = ?1",
        "DELETE FROM achievements WHERE player_id = ?1",
        "DELETE FROM learned WHERE player_id = ?1",
        "DELETE FROM bookmarks WHERE player_id = ?1",
        "DELETE FROM seen WHERE player_id = ?1",
        "DELETE FROM attack WHERE player_id = ?1",
        "DELETE FROM players WHERE id = ?1",
    };
    bool ok = sdb_exec("BEGIN");
    for (size_t i = 0; ok && i < sizeof(sql) / sizeof(sql[0]); i++) {
        sqlite3_stmt *st;
        ok = sqlite3_prepare_v2(sdb_db(), sql[i], -1, &st, NULL) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int(st, 1, id);
            ok = sdb_step_done(st, "delete player");
            sqlite3_finalize(st);
        }
    }
    if (!ok) {
        sdb_exec("ROLLBACK");
        sdb_post_message("Couldn't delete the player.");
        return;
    }
    sdb_exec("COMMIT");
    sdb_set_solves(sdb_query_int("SELECT COUNT(*) FROM solves"));
    snprintf(msg, sizeof(msg), "Deleted %s.", name);
    sdb_post_message(msg);
    printf("stats_db: deleted player \"%s\"\n", name);
    if (current) {
        sdb_set_player(0);                         /* gone: any player is "another" now */
        sdb_do_switch(sdb_query_int("SELECT MIN(id) FROM players"));
    } else {
        sdb_do_list();
    }
}

/* (documented in stats_db.h) */
void stats_db_set_player_color(int32_t id, int color)
{
    cmd_t c = {.type = CMD_PLAYER_COLOR, .ival = id, .level = color % STATS_PLAYER_COLORS};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_set_player_avatar(int32_t id, int avatar)
{
    cmd_t c = {.type = CMD_PLAYER_AVATAR, .ival = id, .level = avatar};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_list_players(void)
{
    cmd_t c = {.type = CMD_PLAYERS};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_add_player(const char *name)
{
    cmd_t c = {.type = CMD_ADD_PLAYER};
    snprintf(c.text, sizeof(c.text), "%s", name);
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_rename_player(int32_t id, const char *name)
{
    cmd_t c = {.type = CMD_RENAME_PLAYER, .ival = id};
    snprintf(c.text, sizeof(c.text), "%s", name);
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_switch_player(int32_t id)
{
    cmd_t c = {.type = CMD_SWITCH_PLAYER, .ival = id};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_delete_player(int32_t id)
{
    cmd_t c = {.type = CMD_DELETE_PLAYER, .ival = id};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
int stats_db_take_players(stats_player_t *out)
{
    if (!sdb_lock()) {
        return -1;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    int n = s_players_new ? s_players_n : -1;
    if (n > 0) {
        memcpy(out, s_players, sizeof(stats_player_t) * n);
    }
    s_players_new = false;
    xSemaphoreGive(sdb_lock());
    return n;
}

/* (documented in stats_db.h) */
bool stats_db_take_player_change(stats_player_change_t *out)
{
    if (!sdb_lock()) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_change_new;
    if (fresh) {
        *out = s_change;
    }
    s_change_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db_internal.h) */
const char *sdb_player_name(void)
{
    return s_player_name;
}
