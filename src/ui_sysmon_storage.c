/**
 * @file ui_sysmon_storage.c
 * @ingroup ui_sysmon
 * @brief System monitor > Storage: the firmware, the flash, NVS, the microSD card.
 */
#include "ui_sysmon_tab.h"
#include "ui_palette.h"
#include <stdio.h>
#include "bsp/bsp_board.h"

#define SD_EVERY     10                             /**< ticks between microSD free-space counts */

static lv_obj_t *s_app_bar;                         /**< the firmware: its bar */
static lv_obj_t *s_app_val;                         /**< ... its value */
static lv_obj_t *s_app_sub;                         /**< ... the slot it runs from */
static lv_obj_t *s_part_val;                        /**< the flash partitioned */
static lv_obj_t *s_nvs_bar;                         /**< NVS: its bar */
static lv_obj_t *s_nvs_val;                         /**< ... its entries */
static lv_obj_t *s_sd_bar;                          /**< the microSD card: its bar */
static lv_obj_t *s_sd_val;                          /**< ... its free space */
static lv_obj_t *s_sd_sub;                          /**< ... the volume and the card's size */

/**
 * @brief The firmware's size in its slot, the flash partitioned, NVS entries and the microSD card (as last counted).
 */
static void show_storage(void)
{
    char a[16], b[16], buf[96];
    const sysmon_flash_t *fl = sysmon_flash();
    if (fl->app_part) {
        fmt_bytes(a, sizeof(a), fl->app_len);
        fmt_bytes(b, sizeof(b), fl->app_part);
        snprintf(buf, sizeof(buf), "%s of %s", a, b);
        set_text(s_app_val, buf);
        set_bar(s_app_bar, fl->app_len, fl->app_part);
        snprintf(buf, sizeof(buf), "Running from %s at 0x%06x", fl->run_label ? fl->run_label : "?",
                 (unsigned)fl->run_addr);
        set_text(s_app_sub, buf);
    }
    fmt_bytes(a, sizeof(a), fl->part_end);
    fmt_bytes(b, sizeof(b), fl->flash_size);
    snprintf(buf, sizeof(buf), "%s of %s", a, b);
    set_text(s_part_val, buf);

    uint32_t used, total;
    if (sysmon_nvs(&used, &total)) {
        snprintf(buf, sizeof(buf), "%u of %u entries", (unsigned)used, (unsigned)total);
        set_text(s_nvs_val, buf);
        set_bar(s_nvs_bar, used, total);
    }

    uint64_t sd_total, sd_free;
    esp_err_t sd_err;
    if (sysmon_sd(&sd_total, &sd_free, &sd_err)) {
        if (sd_err == ESP_OK && sd_total) {
            fmt_bytes(a, sizeof(a), sd_free);
            fmt_bytes(b, sizeof(b), sd_total);
            snprintf(buf, sizeof(buf), "%s free of %s", a, b);
            set_text(s_sd_val, buf);
            set_bar(s_sd_bar, sd_total - sd_free, sd_total);
            fmt_bytes(a, sizeof(a), sysmon_sd_card_bytes());
            snprintf(buf, sizeof(buf), "FAT volume at " BSP_SD_MOUNT_POINT "  |  card %s", a);
            set_text(s_sd_sub, buf);
        } else {
            set_text(s_sd_val, sd_err == ESP_ERR_INVALID_STATE ? "Not mounted" : "No card");
            set_bar(s_sd_bar, 0, 1);
            set_text(s_sd_sub, sd_err == ESP_ERR_INVALID_STATE ? "No card, not FAT, or lent over USB" : "");
        }
    } else {
        set_text(s_sd_val, "Counting...");
    }
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_storage_build(lv_obj_t *pane)
{
    lv_obj_t *c;
    c = card(pane);
    bar_row(c, "Firmware", &s_app_bar, &s_app_val, &s_app_sub);
    s_part_val = kv(c, "Flash partitioned");
    bar_row(c, "Settings (NVS)", &s_nvs_bar, &s_nvs_val, NULL);
    bar_row(c, "microSD", &s_sd_bar, &s_sd_val, &s_sd_sub);
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_storage_tick(int n)
{
    if ((n % SD_EVERY) == 0) {
        sysmon_sd_query();
    }
    show_storage();
}
