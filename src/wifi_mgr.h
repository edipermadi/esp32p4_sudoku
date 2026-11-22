/**
 * @file wifi_mgr.h
 * @brief The Wi-Fi station: scanning, connecting, saved networks, the clock from NTP.
 */
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

/** Where Wi-Fi is. */
typedef enum {
    WIFI_ST_OFF,          /**< switched off */
    WIFI_ST_STARTING,     /**< bringing up the C6 link */
    WIFI_ST_IDLE,         /**< on, not connected, not trying */
    WIFI_ST_CONNECTING,   /**< joining (or re-joining) a network */
    WIFI_ST_CONNECTED,    /**< connected */
    WIFI_ST_FAILED,       /**< last attempt failed, see fail_reason */
    WIFI_ST_NO_RADIO,     /**< the C6 link could not be started */
} wifi_state_t;

/** Wi-Fi now (wifi_mgr_get_status()). */
typedef struct {
    wifi_state_t state;                 /**< where it is */
    bool enabled;                       /**< switched on (Settings) */
    bool scanning;                      /**< a scan runs */
    uint32_t scan_serial;     /**< increments when new scan results are in */
    char ssid[33];            /**< network being joined / joined */
    char ip[16];                        /**< the IP address while connected, "" otherwise */
    int rssi;                 /**< dBm while connected */
    char saved_ssid[33];      /**< remembered network, "" if none */
    char fail_reason[48];               /**< why the last attempt failed (WIFI_ST_FAILED) */
    char mac[18];             /**< the station's MAC (the C6's), "aa:bb:..": "" until the radio first started */
} wifi_status_t;

/** A network found by a scan. */
typedef struct {
    char ssid[33];                      /**< its name */
    int8_t rssi;                        /**< its signal, dBm */
    uint8_t channel;                    /**< its channel */
    bool secure;                        /**< it needs a password */
} wifi_ap_t;

/** @brief Starts the worker; connects to the saved network if Wi-Fi was left on. */
void wifi_mgr_start(void);

/**
 * @brief Switches Wi-Fi on or off (remembered).
 * @param[in] on  on
 */
void wifi_mgr_set_enabled(bool on);
/**
 * @brief Scans for networks; wifi_status_t.scan_serial changes when the results are in.
 */
void wifi_mgr_scan(void);
/**
 * @brief Joins ssid; saved once it connects. pass "" means an open network, or
 * the stored password when ssid is the remembered network.
 * @param[in] ssid  the network
 * @param[in] pass  its password, or ""
 */
void wifi_mgr_connect(const char *ssid, const char *pass);
/** @brief Disconnects and forgets the saved network. */
void wifi_mgr_forget(void);

/**
 * @brief Wi-Fi now.
 * @param[out] out  gets it
 */
void wifi_mgr_get_status(wifi_status_t *out);
/**
 * @brief True once the C6 link has been brought up (it stays up while the board runs).
 * The microSD slot shares the SDMMC host: card (re)initialisation must not
 * happen after this (see bsp_sdcard.c).
 * @return true if so
 */
bool is_wifi_mgr_radio_started(void);
/**
 * @brief Copies up to max scan results (strongest first, one per SSID); returns the count.
 * @param[out] out  gets them
 * @param[in] max  out's room
 * @return how many
 */
int wifi_mgr_get_aps(wifi_ap_t *out, int max);

#ifdef __cplusplus
}
#endif
