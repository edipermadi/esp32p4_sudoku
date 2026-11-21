/**
 * @file ui_remote.c
 * @ingroup ui_sudoku
 * @brief What serial control (devctl) asks of the UI: digits typed into the game, the game as text, which
 *        screen is shown, what is using the card, the Benchmarking panel.
 */
#include "ui_internal.h"

static lv_obj_t *busy_ovl;                /**< the Benchmarking panel (NULL: none) */
static lv_obj_t *busy_text;               /**< its text */

/* (documented in ui_sudoku.h) */
void sudoku_ui_busy(const char *text)
{
    if (!text) {
        if (busy_ovl) {
            lv_obj_delete(busy_ovl);
        }
        busy_ovl = busy_text = NULL;
        return;
    }
    if (!busy_ovl) {
        busy_ovl = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(busy_ovl);
        lv_obj_set_size(busy_ovl, SCR_W, SCR_W);
        lv_obj_set_style_bg_color(busy_ovl, C_BG, 0);
        lv_obj_set_style_bg_opa(busy_ovl, LV_OPA_COVER, 0);
        lv_obj_set_clickable(busy_ovl, true);       /* (the taps stop here) */
        lv_obj_t *t = make_label(busy_ovl, &lv_font_montserrat_28, C_TEXT);
        lv_label_set_text(t, LV_SYMBOL_SETTINGS "  Benchmarking");
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -60);
        busy_text = make_label(busy_ovl, &lv_font_montserrat_20, C_MUTED);
        lv_obj_set_width(busy_text, SCR_W - 80);
        lv_obj_set_style_text_align(busy_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(busy_text, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(busy_text, LV_ALIGN_CENTER, 0, 20);
    }
    lv_label_set_text(busy_text, text);
}

/* (documented in ui_sudoku.h) */
const char *sudoku_ui_io_busy(void)
{
    const char *why = settings_card_busy();
    if (why) {
        return why;
    }
    if (is_replay_exporting()) {
        return "an MP4 video is being exported";
    }
    if (is_screenshot_busy()) {
        return "a screenshot is being saved";
    }
    if (is_files_busy()) {
        return "Files is working on the card";
    }
    if (is_stats_db_busy()) {
        return "sudoku.db is busy (saving, an import, an export or a backup)";
    }
    return NULL;
}

/* (documented in ui_sudoku.h) */
int sudoku_ui_fill(const uint8_t *cells, const uint8_t *digits, int n, char *why, size_t why_n)
{
    if (lv_screen_active() != screen_game() || !game_geo()) {
        snprintf(why, why_n, "no game on screen");
        return -1;
    }
    if (is_game_solved() || is_game_paused()) {
        snprintf(why, why_n, is_game_solved() ? "already solved" : "paused");
        return -1;
    }
    for (int k = 0; k < n; k++) {                   /* all of them first */
        int i = cells[k], d = digits[k];
        if (i > 80 || !game_geo()->used[i]) {
            snprintf(why, why_n, "cell %d isn't on the board", i);
            return -1;
        }
        if (game_given()[i]) {
            snprintf(why, why_n, "cell %d is a given", i);
            return -1;
        }
        if (d < 1 || d > game_geo()->n) {
            snprintf(why, why_n, "digit %d for cell %d: 1..%d", d, i, game_geo()->n);
            return -1;
        }
    }
    bool notes = is_game_notes_mode();
    game_set_notes_mode(false);                           /* digits, not notes */
    int placed = 0;
    for (int k = 0; k < n && !is_game_solved(); k++) {
        if (game_b()->val[cells[k]] == digits[k]) {
            continue;    /* (a second tap would clear it) */
        }
        game_set_sel(cells[k]);
        enter_digit(digits[k]);                     /* as from the pad */
        placed++;
    }
    game_set_notes_mode(notes);
    if (!is_game_solved()) {
        game_set_sel(-1);
        refresh_board();
    }
    why[0] = 0;
    return placed;
}

/* (documented in ui_sudoku.h) */
void sudoku_ui_screen_name(char *buf, size_t n)
{
    static const struct { lv_obj_t *(*scr)(void); const char *name; } S[] = {
        {screen_home, "home"}, {screen_list, "list"}, {screen_game, "game"}, {screen_stats, "stats"},
        {screen_settings, "settings"}, {screen_wifi, "wifi"}, {screen_profile, "players"}, {screen_history, "history"},
        {screen_sysmon, "sysmon"}, {screen_files, "files"}, {screen_plays, "gameplays"}, {screen_trophy, "trophies"},
        {screen_replay, "replay"}, {screen_edit, "edit"},
    };
    lv_obj_t *act = lv_screen_active();
    const char *name = "other";
    for (size_t i = 0; i < sizeof(S) / sizeof(S[0]); i++) {
        if (S[i].scr() && S[i].scr() == act) {
            name = S[i].name;
        }
    }
    int len = snprintf(buf, n, "%s", name);
    if (act == screen_home() && home_menu()) {                /* which page of the menu: home1, home2 */
        int page = (int)((lv_obj_get_scroll_x(home_menu()) + HOME_GRID_W / 2) / HOME_GRID_W) + 1;
        len += snprintf(buf + len, n - (size_t)len, "%d", page);
    }
    /* a dialog or overlay: anything screen-sized on the top layer (not the Gibberlink icon, a notice) */
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        if (!lv_obj_is_hidden(c) && lv_obj_get_width(c) >= SCR_W - 20 && lv_obj_get_height(c) >= SCR_W - 20) {
            snprintf(buf + len, n - (size_t)len, "+dialog");
            break;
        }
    }
}

/* (documented in ui_sudoku.h) */
bool sudoku_ui_state(sudoku_ui_state_t *st, char board[82])
{
    if (lv_screen_active() != screen_game() || !game_geo()) {
        return false;
    }
    memset(st, 0, sizeof(*st));
    if (game_practice() >= 0) {
        snprintf(st->name, sizeof(st->name), "Learn practice");
    } else if (game_id() != STORE_NO_GAME) {
        puzzle_name(game_id(), st->name, sizeof(st->name));
    }
    st->variant = game_var() >= 0 && game_var() < VAR_COUNT ? variant_name(game_var()) : "?";
    st->size = game_geo()->n;
    st->cells = game_geo()->ncells;
    for (int i = 0; i < 81; i++) {
        int v = game_b()->val[i];
        if (!game_geo()->used[i]) {
            board[i] = '#';
        } else if (!v) {
            board[i] = '.';
        } else {
            board[i] = game_given()[i] ? (char)('0' + v) : (char)('a' + v - 1);
        }
        if (game_geo()->used[i] && v) {
            st->filled++;
        }
    }
    board[81] = 0;
    st->solved = is_game_solved();
    st->paused = is_game_paused();
    st->practice = game_practice() >= 0;
    st->attack = is_game_attack();
    st->seconds = (game_elapsed_ms() + 500) / 1000;
    st->mistakes = game_mistakes();
    st->hints = game_hints();
    return true;
}
