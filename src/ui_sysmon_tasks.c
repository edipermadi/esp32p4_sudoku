/**
 * @file ui_sysmon_tasks.c
 * @ingroup ui_sysmon
 * @brief System monitor > Tasks: the FreeRTOS task table, sortable by its column names.
 */
#include "ui_sysmon_tab.h"
#include "ui_palette.h"
#include <stdio.h>

/* table columns: name, core, priority, state, CPU, free stack */
static const int16_t COL_W[] = {196, 60, 60, 84, 96, 108}; /**< = the card's 604 px */
#define COLS ((int)(sizeof(COL_W) / sizeof(COL_W[0]))) /**< the table's columns */
static lv_obj_t *s_table;                           /**< the task table */
static void show_task_table(void);

/**
 * @brief The task list read last (a second old) into the table, if there's a new one; the next asked for.
 */
static void show_tasks(void)
{
    if (sysmon_tasks_poll()) {
        show_task_table();
    }
}

/**
 * @brief The table from the latest list (sysmon_task()): name, core, priority, state, CPU share, free stack.
 */
static void show_task_table(void)
{
    char buf[16];
    uint32_t n = (uint32_t)sysmon_tasks_count();
    lv_table_set_row_count(s_table, n);
    for (uint32_t r = 0; r < n; r++) {
        uint32_t permille;
        const TaskStatus_t *t = sysmon_task((int)r, &permille);
        lv_table_set_cell_value(s_table, r, 0, t->pcTaskName);
        if (t->xCoreID == tskNO_AFFINITY) {
            lv_table_set_cell_value(s_table, r, 1, "-");
        } else {
            lv_table_set_cell_value_fmt(s_table, r, 1, "%d", (int)t->xCoreID);
        }
        lv_table_set_cell_value_fmt(s_table, r, 2, "%u", (unsigned)t->uxCurrentPriority);
        const char *st = "?";
        switch (t->eCurrentState) {
        case eRunning:
            st = "Run";
            break;
        case eReady:
            st = "Ready";
            break;
        case eBlocked:
            st = "Wait";
            break;
        case eSuspended:
            st = "Susp";
            break;
        case eDeleted:
            st = "Del";
            break;
        default:
            break;
        }
        lv_table_set_cell_value(s_table, r, 3, st);
        if (is_sysmon_tasks_cpu_valid()) {
            lv_table_set_cell_value_fmt(s_table, r, 4, "%u.%u%%", (unsigned)(permille / 10), (unsigned)(permille % 10));
        } else {
            lv_table_set_cell_value(s_table, r, 4, "...");
        }
        fmt_bytes(buf, sizeof(buf), t->usStackHighWaterMark);   /* bytes on ESP-IDF */
        lv_table_set_cell_value(s_table, r, 5, buf);
    }
}

static const char *const HDR[] = {"Name", "Core", "Prio", "State", "CPU", "Stack free"}; /**< the column names */
/** The columns a tap sorts by. */
#define SORTABLE(c) ((c) == SYSMON_COL_NAME || (c) == SYSMON_COL_CORE || (c) == SYSMON_COL_CPU || (c) == SYSMON_COL_STACK)
static lv_obj_t *s_hdr[6];                          /**< the column names' labels */

/**
 * @brief The column names: the sorted one in the accent colour, with an arrow for the direction.
 */
static void show_sort_header(void)
{
    for (int i = 0; i < COLS; i++) {
        if (i == sysmon_tasks_sort_col()) {
            lv_label_set_text_fmt(s_hdr[i], "%s %s", HDR[i], is_sysmon_tasks_sort_desc() ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
            lv_obj_set_style_text_color(s_hdr[i], C_ACCENT, 0);
        } else {
            lv_label_set_text(s_hdr[i], HDR[i]);
            lv_obj_set_style_text_color(s_hdr[i], SORTABLE(i) ? C_TEXT : C_MUTED, 0);
        }
    }
}

/**
 * @brief A column name tapped: sorted by it (again: reversed; CPU busiest first, the others from the top).
 * @param[in] e  the event (its user data is the column)
 */
static void on_sort(lv_event_t *e)
{
    int col = (int)(intptr_t)lv_event_get_user_data(e);
    if (col == sysmon_tasks_sort_col()) {
        sysmon_tasks_sort(col, !is_sysmon_tasks_sort_desc());
    } else {
        sysmon_tasks_sort(col, col == SYSMON_COL_CPU);   /* busiest first; the others from the top */
    }
    show_sort_header();
    if (sysmon_tasks_count() > 0) {
        show_task_table();
    }
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_tasks_build(lv_obj_t *pane)
{
    lv_obj_t *c;
    c = card(pane);
    lv_obj_set_style_pad_row(c, 4, 0);
    /* the column names; Name, Core, CPU and Stack free sort the table (tap again: reversed) */
    lv_obj_t *hr = lv_obj_create(c);
    lv_obj_remove_style_all(hr);
    lv_obj_set_size(hr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hr, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < COLS; i++) {
        lv_obj_t *l = label(hr, &lv_font_montserrat_14, C_MUTED, HDR[i]);
        lv_obj_set_width(l, COL_W[i]);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_pad_left(l, 6, 0);
        lv_obj_set_style_pad_ver(l, 10, 0);            /* a finger-sized target */
        lv_obj_set_style_radius(l, 6, 0);
        if (SORTABLE(i)) {
            lv_obj_set_clickable(l, true);
            lv_obj_set_style_bg_color(l, C_CARD2, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_add_event_cb(l, on_sort, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
        s_hdr[i] = l;
    }
    show_sort_header();
    s_table = lv_table_create(c);
    lv_obj_set_size(s_table, LV_PCT(100), LV_SIZE_CONTENT);
    lv_table_set_column_count(s_table, COLS);
    for (int i = 0; i < COLS; i++) {
        lv_table_set_column_width(s_table, i, COL_W[i]);
    }
    lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_table, 0, 0);
    lv_obj_set_style_pad_all(s_table, 0, 0);
    lv_obj_set_style_bg_color(s_table, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_table, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_table, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_TOP, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_table, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_table, 7, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(s_table, 6, LV_PART_ITEMS);
    lv_obj_set_scrollable(s_table, false);
    lv_obj_set_clickable(s_table, false);           /* drags scroll the page */
    section(pane, "Tap Name, Core, CPU or Stack free to sort, and again to reverse. CPU is a share of "
            "one core. Stack free: the least it has had.");
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_tasks_tick(void)
{
    show_tasks();
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_tasks_opened(void)
{
    sysmon_tasks_restart();                         /* (CPU shares from the next list on) */
    show_tasks();
}
