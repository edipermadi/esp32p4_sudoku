/*
 * Gibberlink (see gibberlink.h).
 *
 * ggwave at 24 kHz (the speaker's rate, shared by the microphones on the same
 * I2S port) with 512-sample frames: 46.875 Hz a frequency bin, the same as
 * ggwave's usual 48 kHz / 1024, so it understands phones, browsers and
 * Gibberlink's agents. The worker (core 0) reads the microphones a frame at a
 * time (MIC1 and MIC2 averaged) and decodes; a message goes to the LVGL task by
 * a queue (devctl runs there), and the answer comes back to the worker, which
 * encodes it and has the sound-effects task play it (sfx_play_pcm). While that
 * plays, and a little after, nothing is decoded: the microphones hear it too.
 */
#include "gibberlink.h"
#include "devctl.h"
#include "sfx.h"
#include "ui_palette.h"
#include "bsp/bsp_board.h"
#include "ggwave/ggwave/ggwave.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#define SPF        512                  /* samples per ggwave frame (21.3 ms at 24 kHz) */
#define MSG_MAX    (GGWave::kMaxLengthVariable + 1)
#define REPLY_ID   "P4"                 /* our id in Gibberlink's "ID$message" */
#define TX_VOLUME  80                   /* ggwave's 0..100 (30 was too quiet: a laptop a metre away missed 1 reply in 4) */
#define ECHO_MS    400                  /* deaf for this long after a reply */
#define LIT_MS     1500                 /* the icon stays lit after a command */

static GGWave *s_gg;
static volatile bool s_on, s_task_up;
/* s_on and s_task_up change together under it: a worker that is ending (off) and
 * gibberlink_set_enabled(true) at that moment never both think the other one runs */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_cmdq;            /* messages heard: char[MSG_MAX] */
static QueueHandle_t s_txq;             /* messages to send (gibberlink_send): char[MSG_MAX] */
static volatile int s_tx_count;         /* of them, not yet played out (under s_mux) */
#define TX_GAP_MS  700                  /* between two messages sent: the listener's decoder settles */
static char s_reply[MSG_MAX];
static volatile bool s_reply_ready;
static int16_t *s_tx;                   /* the reply's samples, while it plays */
static size_t s_tx_cap;
static lv_obj_t *s_icon;
static uint32_t s_lit_until;

/* ------------------------------------------------------------- worker */
static bool make_ggwave(void)
{
    GGWave::Protocols::rx().disableAll();
    GGWave::Protocols::rx().toggle(GGWAVE_PROTOCOL_AUDIBLE_NORMAL, true);
    GGWave::Protocols::rx().toggle(GGWAVE_PROTOCOL_AUDIBLE_FAST, true);      /* Gibberlink's */
    GGWave::Protocols::rx().toggle(GGWAVE_PROTOCOL_AUDIBLE_FASTEST, true);   /* its "fast" mode */
    GGWave::Protocols::tx().disableAll();
    GGWave::Protocols::tx().toggle(GGWAVE_PROTOCOL_AUDIBLE_FAST, true);
    GGWave::Parameters p = GGWave::getDefaultParameters();
    p.payloadLength = -1;                               /* variable length, with sound markers */
    p.sampleRateInp = (float)bsp_audio_sample_rate();
    p.sampleRateOut = (float)sfx_rate();
    p.sampleRate = 24000.0f;
    p.samplesPerFrame = SPF;
    p.sampleFormatInp = GGWAVE_SAMPLE_FORMAT_I16;
    p.sampleFormatOut = GGWAVE_SAMPLE_FORMAT_I16;
    p.operatingMode = GGWAVE_OPERATING_MODE_RX_AND_TX;
    s_gg = new GGWave(p);                               /* (its buffers: one calloc, in PSRAM) */
    if (!s_gg || s_gg->heapSize() <= 0) {
        printf("gibberlink: ggwave didn't start\n");
        delete s_gg;
        s_gg = nullptr;
        return false;
    }
    printf("gibberlink: ggwave ready, %d bytes\n", s_gg->heapSize());
    return true;
}

/* encodes text and starts it playing */
static void send_text(const char *text)
{
    while (is_sfx_pcm_busy()) {
        vTaskDelay(pdMS_TO_TICKS(20));    /* (the last one first) */
    }
    if (!s_gg->init(text, GGWAVE_PROTOCOL_AUDIBLE_FAST, TX_VOLUME)) {
        printf("gibberlink: can't encode \"%s\"\n", text);
        return;
    }
    uint32_t bytes = s_gg->encode();
    size_t n = bytes / sizeof(int16_t);
    if (!n) {
        printf("gibberlink: \"%s\" encoded to nothing\n", text);
        return;
    }
    if (n > s_tx_cap) {
        heap_caps_free(s_tx);
        s_tx = (int16_t *)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        s_tx_cap = s_tx ? n : 0;
        if (!s_tx) {
            printf("gibberlink: no memory for %u samples\n", (unsigned)n);
            return;
        }
    }
    memcpy(s_tx, s_gg->txWaveform(), n * sizeof(int16_t));
    bool played = false;                                /* (the sound queue may be full of taps' ticks: retry) */
    for (int k = 0; k < 50 && !(played = sfx_play_pcm(s_tx, n)); k++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (played) {
        printf("gibberlink: sent \"%s\" (%.1f s)\n", text, n / (double)sfx_rate());
    } else {
        printf("gibberlink: \"%s\" not played (the sound task was busy or off)\n", text);
    }
}

/* "AB$tap 1 2" -> "tap 1 2" (a Gibberlink sender id); false: our own, or empty */
static bool take_message(char *msg)
{
    if (strlen(msg) >= 3 && msg[2] == '$' && isalnum((unsigned char)msg[0]) && isalnum((unsigned char)msg[1])) {
        if (!strncmp(msg, REPLY_ID "$", 3)) {
            return false;    /* (an echo of ours) */
        }
        memmove(msg, msg + 3, strlen(msg + 3) + 1);
    }
    size_t l = strlen(msg);
    while (l && (msg[l - 1] == '\n' || msg[l - 1] == '\r' || msg[l - 1] == ' ')) {
        msg[--l] = 0;
    }
    return l > 0;
}

static void worker(void *arg)
{
    static int16_t raw[SPF * 2], mono[SPF];
    bool mic = false;
    int64_t deaf_until = 0;
    for (;;) {
        char out[MSG_MAX];
        bool have_tx = s_txq && xQueuePeek(s_txq, out, 0) == pdTRUE;
        if (have_tx) {                                  /* something to send (listening or not) */
            if (!s_gg && !make_ggwave()) {
                xQueueReset(s_txq);                     /* (can't: dropped) */
                portENTER_CRITICAL(&s_mux);
                s_tx_count = 0;
                portEXIT_CRITICAL(&s_mux);
                continue;
            }
            xQueueReceive(s_txq, out, 0);
            send_text(out);
            while (is_sfx_pcm_busy()) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelay(pdMS_TO_TICKS(TX_GAP_MS));
            deaf_until = esp_timer_get_time() + ECHO_MS * 1000;
            portENTER_CRITICAL(&s_mux);
            if (s_tx_count > 0) {
                s_tx_count--;
            }
            portEXIT_CRITICAL(&s_mux);
            continue;
        }
        if (!s_on) {
            if (mic) {
                bsp_mic_stop();
                mic = false;
                printf("gibberlink: off\n");
            }
            if (is_sfx_pcm_busy()) {                       /* a reply still playing from s_tx: let it end */
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            delete s_gg;                                /* its ~9 MB of buffers back */
            s_gg = nullptr;
            heap_caps_free(s_tx);
            s_tx = nullptr;
            s_tx_cap = 0;
            s_reply_ready = false;
            /* not used: the task ends (gibberlink_set_enabled starts another), unless it was
             * just turned on again */
            portENTER_CRITICAL(&s_mux);
            bool end = !s_on && !s_tx_count;            /* (nothing more to send either) */
            if (end) {
                s_task_up = false;
            }
            portEXIT_CRITICAL(&s_mux);
            if (end) {
                printf("gibberlink: worker ended\n");
                vTaskDelete(NULL);
            }
            continue;
        }
        if (!s_gg && !make_ggwave()) {
            s_on = false;
            continue;
        }
        if (!mic) {
            esp_err_t err = bsp_mic_start();
            if (err != ESP_OK) {
                printf("gibberlink: microphones didn't start (%s)\n", esp_err_to_name(err));
                s_on = false;
                continue;
            }
            mic = true;
            printf("gibberlink: listening\n");
        }
        if (s_reply_ready) {
            send_text(s_reply);
            s_reply_ready = false;
        }
        size_t got = 0;
        if (bsp_mic_read(raw, SPF, &got, 200) != ESP_OK || got < SPF) {
            continue;
        }
        int64_t now = esp_timer_get_time();
        if (is_sfx_pcm_busy()) {
            deaf_until = now + ECHO_MS * 1000;
        }
        if (now < deaf_until) {
            continue;    /* our own reply in the room */
        }
        for (int i = 0; i < SPF; i++) {
            mono[i] = (int16_t)((raw[2 * i] + raw[2 * i + 1]) / 2);
        }
        s_gg->decode(mono, sizeof(mono));
        GGWave::TxRxData data;
        int n = s_gg->rxTakeData(data);
        if (n > 0) {
            char msg[MSG_MAX];
            if (n > MSG_MAX - 1) {
                n = MSG_MAX - 1;
            }
            memcpy(msg, data.data(), (size_t)n);
            msg[n] = 0;
            printf("gibberlink: heard \"%s\"\n", msg);
            if (take_message(msg)) {
                xQueueSend(s_cmdq, msg, 0);
            }
        } else if (n < 0) {
            printf("gibberlink: heard something it couldn't decode\n");
        }
    }
}

/* ------------------------------------------------------------- UI side */
static void icon_update(void)
{
    if (s_on && !s_icon) {
        s_icon = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_icon);
        lv_obj_set_size(s_icon, 40, 40);
        lv_obj_align(s_icon, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
        lv_obj_set_style_radius(s_icon, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_icon, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(s_icon, lv_color_black(), 0);
        lv_obj_set_clickable(s_icon, false);            /* touches go through to the screen */
        lv_obj_t *l = lv_label_create(s_icon);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text(l, LV_SYMBOL_AUDIO);
        lv_obj_center(l);
    } else if (!s_on && s_icon) {
        lv_obj_delete(s_icon);
        s_icon = NULL;
    }
    if (s_icon) {
        bool lit = (int32_t)(s_lit_until - lv_tick_get()) > 0;
        lv_obj_set_style_bg_color(s_icon, lit ? C_ACCENT : lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_icon, lit ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}

bool gibberlink_send(const char *text)
{
    if (!s_txq) {
        s_txq = xQueueCreate(8, MSG_MAX);
    }
    if (!s_txq || strlen(text) >= MSG_MAX) {
        return false;
    }
    char m[MSG_MAX];
    strlcpy(m, text, sizeof(m));
    portENTER_CRITICAL(&s_mux);
    s_tx_count++;                                       /* (before the worker can see the queue empty and end) */
    bool start = !s_task_up;
    if (start) {
        s_task_up = true;
    }
    portEXIT_CRITICAL(&s_mux);
    bool ok = xQueueSend(s_txq, m, 0) == pdTRUE;
    if (start && ok && xTaskCreatePinnedToCore(worker, "gibberlink", 8192, NULL, 3, NULL, 0) != pdPASS) {
        printf("gibberlink: no memory for the worker\n");
        xQueueReset(s_txq);
        ok = false;
        portENTER_CRITICAL(&s_mux);
        s_task_up = false;
        s_tx_count = 0;
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    if (!ok) {
        portENTER_CRITICAL(&s_mux);
        if (s_tx_count > 0) {
            s_tx_count--;
        }
        portEXIT_CRITICAL(&s_mux);
    }
    return ok;
}

int gibberlink_sending(void)
{
    return s_tx_count;
}

/* on: starts the worker (unless one runs); off: the worker stops listening, frees ggwave and ends */
void gibberlink_set_enabled(bool on)
{
    if (on && !s_cmdq) {
        s_cmdq = xQueueCreate(4, MSG_MAX);    /* (kept: the UI reads it) */
    }
    if (on && !s_cmdq) {
        printf("gibberlink: no memory for the queue\n");
        return;
    }
    portENTER_CRITICAL(&s_mux);
    s_on = on;
    bool start = on && !s_task_up;
    if (start) {
        s_task_up = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (start && xTaskCreatePinnedToCore(worker, "gibberlink", 8192, NULL, 3, NULL, 0) != pdPASS) {
        printf("gibberlink: no memory for the worker\n");
        portENTER_CRITICAL(&s_mux);
        s_task_up = false;
        s_on = false;
        portEXIT_CRITICAL(&s_mux);
    }
}

bool is_gibberlink_enabled(void)
{
    return s_on;
}

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF): the optional "*XXXX" check */
static uint16_t crc16(const char *p, size_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)((uint8_t)*p++ << 8);
        for (int k = 0; k < 8; k++) {
            c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
        }
    }
    return c;
}

/* "tap 1 2*ABCD": true if it ends in a check (removed); *ok: it matched */
static bool take_check(char *msg, bool *ok)
{
    size_t l = strlen(msg);
    if (l < 6 || msg[l - 5] != '*') {
        return false;
    }
    for (size_t i = l - 4; i < l; i++) {
        if (!isxdigit((unsigned char)msg[i])) {
            return false;
        }
    }
    unsigned want = (unsigned)strtoul(msg + l - 4, NULL, 16);
    msg[l - 5] = 0;
    *ok = crc16(msg, l - 5) == want;
    return true;
}

void gibberlink_poll(void)
{
    icon_update();
    if (!s_cmdq || s_reply_ready) {
        return;    /* (one at a time: the last answer goes first) */
    }
    char msg[MSG_MAX];
    if (xQueueReceive(s_cmdq, msg, 0) != pdTRUE) {
        return;
    }
    s_lit_until = lv_tick_get() + LIT_MS;
    icon_update();
    lv_refr_now(NULL);
    char out[MSG_MAX - 9];                              /* (room for "P4$" and a "*XXXX") */
    bool crc_ok = true, checked = take_check(msg, &crc_ok);
    if (!crc_ok) {                                      /* garbled on the way: not run */
        strlcpy(out, "ERR checksum", sizeof(out));
    } else {
        devctl_run_line(msg, out, sizeof(out));
    }
    printf("gibberlink: %s -> %s\n", msg, out);
    if (!strncmp(msg, "pz ", 3)) {
        return;    /* a puzzle's part: no answer (it would talk over the next) */
    }
    if (checked) {                                      /* asked with a check: answered with one */
        snprintf(s_reply, sizeof(s_reply), REPLY_ID "$%s*%04X", out, crc16(out, strlen(out)));
    } else {
        snprintf(s_reply, sizeof(s_reply), REPLY_ID "$%s", out);
    }
    s_reply_ready = true;
}
