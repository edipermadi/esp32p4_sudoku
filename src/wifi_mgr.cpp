/*
 * Wi-Fi station manager (see wifi_mgr.h). From the esp32p4_86box project;
 * here "forget" also clears the C6's own stored config (radio_forget()).
 */
#include "wifi_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "nvs.h"
#include "bsp/bsp_board.h"

#define NS              "wifi"
#define MAX_APS         32
#define CONNECT_TIMEOUT 20000     /* ms */
#define WORKER_STACK    6144

enum cmd_type { CMD_ENABLE, CMD_DISABLE, CMD_SCAN, CMD_CONNECT, CMD_FORGET };
struct cmd_t {
    cmd_type type;
    char ssid[33];
    char pass[65];
};

static QueueHandle_t s_q;
static SemaphoreHandle_t s_lock;            /* guards s_st and s_aps */
static wifi_status_t s_st;
static wifi_ap_t s_aps[MAX_APS];
static int s_aps_n;

/* worker-only state */
static bool s_radio_up;
static char s_saved_pass[65];
static char s_try_ssid[33], s_try_pass[65];
static bool s_trying;                       /* a connect command is in progress */
static uint32_t s_try_start;
static volatile uint8_t s_last_reason;      /* from the disconnect event */
static bool s_sntp_started;
static volatile bool s_radio_started;       /* ESP-Hosted transport has been started */

/* ------------------------------------------------------------------ NVS */
static void nvs_load(bool *on, char *ssid, char *pass)
{
    *on = false;
    ssid[0] = pass[0] = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v;
    if (nvs_get_u8(h, "on", &v) == ESP_OK) {
        *on = v;
    }
    size_t n = 33;
    if (nvs_get_str(h, "ssid", ssid, &n) != ESP_OK) {
        ssid[0] = 0;
    }
    n = 65;
    if (nvs_get_str(h, "pass", pass, &n) != ESP_OK) {
        pass[0] = 0;
    }
    nvs_close(h);
}

static void nvs_store(const bool *on, const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (on) {
        nvs_set_u8(h, "on", *on);
    }
    if (ssid) {
        if (ssid[0]) {
            nvs_set_str(h, "ssid", ssid);
            nvs_set_str(h, "pass", pass);
        } else {
            nvs_erase_key(h, "ssid");
            nvs_erase_key(h, "pass");
        }
    }
    nvs_commit(h);
    nvs_close(h);
}

/* --------------------------------------------------------------- helpers */
static void set_state(wifi_state_t st, const char *reason = nullptr)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.state = st;
    if (reason) {
        strlcpy(s_st.fail_reason, reason, sizeof(s_st.fail_reason));
    }
    xSemaphoreGive(s_lock);
}

static const char *reason_text(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "Wrong password";
    case WIFI_REASON_NO_AP_FOUND:
        return "Network not found";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE:
        return "Network refused the connection";
    default:
        return "Could not connect";
    }
}

static void on_disconnect(arduino_event_id_t ev, arduino_event_info_t info)
{
    s_last_reason = info.wifi_sta_disconnected.reason;
}

/* The station's MAC, from the C6 (a round trip over SDIO), kept once known. Right after
 * the radio starts it can still read all zeros: tried again when a network connects. */
static void read_mac(void)
{
    if (s_st.mac[0]) return;
    String mac = WiFi.macAddress();
    if (mac.length() != 17 || mac == "00:00:00:00:00:00") return;
    mac.toLowerCase();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_st.mac, mac.c_str(), sizeof(s_st.mac));
    xSemaphoreGive(s_lock);
}

static bool radio_up(void)
{
    if (s_radio_up) {
        return true;
    }
    set_state(WIFI_ST_STARTING);
#ifdef BSP_HOSTED_SDIO_CLK
    /* the C6's SDIO pins differ from the core's defaults on this board */
    WiFi.setPins(BSP_HOSTED_SDIO_CLK, BSP_HOSTED_SDIO_CMD, BSP_HOSTED_SDIO_D0, BSP_HOSTED_SDIO_D1,
                 BSP_HOSTED_SDIO_D2, BSP_HOSTED_SDIO_D3, BSP_HOSTED_RESET);
#endif
    s_radio_started = true;
    WiFi.persistent(false);                  /* credentials live in our own NVS keys */
    WiFi.onEvent(on_disconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (!WiFi.mode(WIFI_STA)) {              /* first call brings up ESP-Hosted / the C6 */
        set_state(WIFI_ST_NO_RADIO, "Wi-Fi chip not responding");
        return false;
    }
    WiFi.setAutoReconnect(true);
    s_radio_up = true;
    read_mac();
    set_state(WIFI_ST_IDLE);
    return true;
}

/* The ESP32-C6 runs its own Wi-Fi stack and keeps the last station config in
 * its own flash (the P4 side stores nothing: ESP-Hosted sets nvs_enable =
 * false). Erasing only our NVS keys left it there, and it reconnected to the
 * old network when Wi-Fi was switched on. Needs the radio up. */
static void radio_forget(void)
{
    WiFi.disconnect(false, true);            /* disconnect + empty station config */
    WiFi.eraseAP();                          /* esp_wifi_restore: stored config back to defaults */
}

static void begin_connect(const char *ssid, const char *pass)
{
    strlcpy(s_try_ssid, ssid, sizeof(s_try_ssid));
    strlcpy(s_try_pass, pass, sizeof(s_try_pass));
    s_trying = true;
    s_try_start = millis();
    s_last_reason = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_st.ssid, ssid, sizeof(s_st.ssid));
    s_st.ip[0] = 0;
    s_st.fail_reason[0] = 0;
    s_st.state = WIFI_ST_CONNECTING;
    xSemaphoreGive(s_lock);
    WiFi.disconnect(false, false);
    WiFi.begin(ssid, pass[0] ? pass : nullptr);
}

static void collect_scan(int n)
{
    static wifi_ap_t tmp[MAX_APS];
    int m = 0;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;                                    /* hidden network */
        }
        int8_t rssi = (int8_t)WiFi.RSSI(i);
        int j = 0;
        while (j < m && strcmp(tmp[j].ssid, ssid.c_str()) != 0) {
            j++;
        }
        if (j < m) {                                     /* same SSID: keep the strongest */
            if (rssi > tmp[j].rssi) {
                tmp[j].rssi = rssi;
                tmp[j].channel = WiFi.channel(i);
            }
            continue;
        }
        if (m == MAX_APS) {
            continue;
        }
        strlcpy(tmp[m].ssid, ssid.c_str(), sizeof(tmp[m].ssid));
        tmp[m].rssi = rssi;
        tmp[m].channel = WiFi.channel(i);
        tmp[m].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        m++;
    }
    for (int i = 1; i < m; i++) {                        /* strongest first */
        wifi_ap_t t = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].rssi < t.rssi) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = t;
    }
    WiFi.scanDelete();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_aps, tmp, sizeof(wifi_ap_t) * m);
    s_aps_n = m;
    s_st.scanning = false;
    s_st.scan_serial++;
    xSemaphoreGive(s_lock);
}

/* ---------------------------------------------------------------- worker */
static void handle(const cmd_t &c)
{
    switch (c.type) {
    case CMD_ENABLE: {
        bool on = true;
        nvs_store(&on, nullptr, nullptr);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.enabled = true;
        xSemaphoreGive(s_lock);
        if (radio_up()) {
            if (s_st.saved_ssid[0]) {
                begin_connect(s_st.saved_ssid, s_saved_pass);
            } else {
                radio_forget();              /* nothing saved: nor may the C6 have anything */
            }
            WiFi.scanNetworks(true);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.scanning = true;
            xSemaphoreGive(s_lock);
        }
        break;
    }
    case CMD_DISABLE: {
        bool on = false;
        nvs_store(&on, nullptr, nullptr);
        s_trying = false;
        if (s_radio_up) {
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);
            s_radio_up = false;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.enabled = false;
        s_st.scanning = false;
        s_st.state = WIFI_ST_OFF;
        s_st.ip[0] = 0;
        s_aps_n = 0;
        s_st.scan_serial++;
        xSemaphoreGive(s_lock);
        break;
    }
    case CMD_SCAN:
        if (s_st.enabled && radio_up() && !s_st.scanning) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.scanning = true;
            xSemaphoreGive(s_lock);
            if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_st.scanning = false;
                xSemaphoreGive(s_lock);
            }
        }
        break;
    case CMD_CONNECT:
        if (s_st.enabled && radio_up()) {
            /* no password given for the remembered network: use the stored one */
            bool saved = !c.pass[0] && strcmp(c.ssid, s_st.saved_ssid) == 0;
            begin_connect(c.ssid, saved ? s_saved_pass : c.pass);
        }
        break;
    case CMD_FORGET: {
        nvs_store(nullptr, "", "");
        s_saved_pass[0] = 0;
        s_trying = false;
        /* the C6's copy too, bringing the radio up just for that if it's off */
        bool was_up = s_radio_up;
        if (s_radio_up || radio_up()) {
            radio_forget();
            if (!was_up && !s_st.enabled) {
                WiFi.mode(WIFI_OFF);
                s_radio_up = false;
                set_state(WIFI_ST_OFF);
            }
        }
        printf("wifi_mgr: network forgotten (board and Wi-Fi chip)\n");
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.saved_ssid[0] = 0;
        s_st.ssid[0] = 0;
        s_st.ip[0] = 0;
        if (s_st.enabled) {
            s_st.state = s_radio_up ? WIFI_ST_IDLE : s_st.state;
        }
        xSemaphoreGive(s_lock);
        break;
    }
    }
}

/* Polls the driver into the snapshot. */
static void poll(void)
{
    if (!s_radio_up) {
        return;
    }
    if (s_st.scanning) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            collect_scan(n);
        } else if (n == WIFI_SCAN_FAILED) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.scanning = false;
            s_st.scan_serial++;
            xSemaphoreGive(s_lock);
        }
    }

    wl_status_t ws = WiFi.status();
    if (ws == WL_CONNECTED) {
        if (s_trying) {                                  /* new network works: remember it */
            s_trying = false;
            if (strcmp(s_try_ssid, s_st.saved_ssid) != 0 || strcmp(s_try_pass, s_saved_pass) != 0) {
                nvs_store(nullptr, s_try_ssid, s_try_pass);      /* only when it changed */
            }
            strlcpy(s_saved_pass, s_try_pass, sizeof(s_saved_pass));
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(s_st.saved_ssid, s_try_ssid, sizeof(s_st.saved_ssid));
            xSemaphoreGive(s_lock);
        }
        if (!s_sntp_started) {
            /* SNTP keeps itself in sync from here on (re-polls hourly, survives
             * reconnects). Pass the zone app_ui already applied so it's kept. */
            const char *tz = getenv("TZ");
            configTzTime(tz ? tz : "UTC0", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
            s_sntp_started = true;
        }
        read_mac();
        String ip = WiFi.localIP().toString();
        int rssi = WiFi.RSSI();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.state = WIFI_ST_CONNECTED;
        strlcpy(s_st.ip, ip.c_str(), sizeof(s_st.ip));
        s_st.rssi = rssi;
        xSemaphoreGive(s_lock);
    } else if (s_trying) {
        if (millis() - s_try_start > CONNECT_TIMEOUT) {
            s_trying = false;
            WiFi.disconnect(false, false);
            set_state(WIFI_ST_FAILED, reason_text(s_last_reason));
        }
    } else if (s_st.state == WIFI_ST_CONNECTED) {
        /* dropped: auto-reconnect is on */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.state = WIFI_ST_CONNECTING;
        s_st.ip[0] = 0;
        xSemaphoreGive(s_lock);
    }
}

static void worker(void *arg)
{
    if (s_st.enabled && radio_up()) {
        if (s_st.saved_ssid[0]) begin_connect(s_st.saved_ssid, s_saved_pass);
        else radio_forget();                 /* nothing saved: clear the C6's old network */
    }
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s_q, &c, pdMS_TO_TICKS(250)) == pdTRUE) {
            handle(c);
            memset(c.pass, 0, sizeof(c.pass));
        }
        poll();
    }
}

static void send(cmd_type t, const char *ssid = "", const char *pass = "")
{
    if (!s_q) {
        return;
    }
    cmd_t c = {};
    c.type = t;
    strlcpy(c.ssid, ssid, sizeof(c.ssid));
    strlcpy(c.pass, pass, sizeof(c.pass));
    xQueueSend(s_q, &c, 0);
    memset(c.pass, 0, sizeof(c.pass));
}

/* ------------------------------------------------------------------- API */
extern "C" void wifi_mgr_start(void)
{
    if (s_q) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(6, sizeof(cmd_t));
    bool on;
    nvs_load(&on, s_st.saved_ssid, s_saved_pass);
    s_st.enabled = on;
    s_st.state = on ? WIFI_ST_STARTING : WIFI_ST_OFF;
    xTaskCreate(worker, "wifi_mgr", WORKER_STACK, NULL, 3, NULL);
}

extern "C" void wifi_mgr_set_enabled(bool on)                  { send(on ? CMD_ENABLE : CMD_DISABLE); }
extern "C" void wifi_mgr_scan(void)                            { send(CMD_SCAN); }
extern "C" void wifi_mgr_connect(const char *ssid, const char *pass) { send(CMD_CONNECT, ssid, pass); }
extern "C" void wifi_mgr_forget(void)                          { send(CMD_FORGET); }

extern "C" bool wifi_mgr_radio_started(void)
{
    return s_radio_started;
}

extern "C" void wifi_mgr_get_status(wifi_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

extern "C" int wifi_mgr_get_aps(wifi_ap_t *out, int max)
{
    if (!s_lock) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_aps_n < max ? s_aps_n : max;
    memcpy(out, s_aps, sizeof(wifi_ap_t) * n);
    xSemaphoreGive(s_lock);
    return n;
}
