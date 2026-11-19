#pragma once
/*
 * Wi-Fi station manager. The ESP32-P4 has no radio: Wi-Fi goes through the
 * on-board ESP32-C6 over SDIO (ESP-Hosted, SDMMC slot 1, GPIO14..19, reset
 * GPIO54 - the Arduino core's defaults, which match this board).
 *
 * All WiFi calls run in one worker task; the UI sends commands and reads a
 * status snapshot, so nothing here blocks the LVGL task. The network that
 * last connected successfully (SSID + password) and the on/off switch are
 * kept in NVS (namespace "wifi") and used again at boot.
 * The first successful connection also starts SNTP (pool.ntp.org) for the clock.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_ST_OFF,          /* switched off */
    WIFI_ST_STARTING,     /* bringing up the C6 link */
    WIFI_ST_IDLE,         /* on, not connected, not trying */
    WIFI_ST_CONNECTING,   /* joining (or re-joining) a network */
    WIFI_ST_CONNECTED,
    WIFI_ST_FAILED,       /* last attempt failed, see fail_reason */
    WIFI_ST_NO_RADIO,     /* the C6 link could not be started */
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    bool enabled;
    bool scanning;
    uint32_t scan_serial;     /* increments when new scan results are in */
    char ssid[33];            /* network being joined / joined */
    char ip[16];
    int rssi;                 /* dBm while connected */
    char saved_ssid[33];      /* remembered network, "" if none */
    char fail_reason[48];
    char mac[18];             /* the station's MAC (the C6's), "aa:bb:..": "" until the radio first started */
} wifi_status_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
} wifi_ap_t;

/* Starts the worker; connects to the saved network if Wi-Fi was left on. */
void wifi_mgr_start(void);

void wifi_mgr_set_enabled(bool on);
void wifi_mgr_scan(void);
/* Joins ssid; saved once it connects. pass "" means an open network, or
 * the stored password when ssid is the remembered network. */
void wifi_mgr_connect(const char *ssid, const char *pass);
/* Disconnects and forgets the saved network. */
void wifi_mgr_forget(void);

void wifi_mgr_get_status(wifi_status_t *out);
/* True once the C6 link has been brought up (it stays up while the board runs).
 * The microSD slot shares the SDMMC host: card (re)initialisation must not
 * happen after this (see bsp_sdcard.c). */
bool wifi_mgr_radio_started(void);
/* Copies up to max scan results (strongest first, one per SSID); returns the count. */
int wifi_mgr_get_aps(wifi_ap_t *out, int max);

#ifdef __cplusplus
}
#endif
