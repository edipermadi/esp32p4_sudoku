/**
 * @file replay_video.c
 * @ingroup replay_video
 * @brief A replay exported as an MP4 on the card: the file, the frames, the encoder.
 */
#include "replay_video.h"
#include "replay_timeline.h"
#include "video_enc.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"

static bool rp_want_export;             /**< the replay asked for is to be exported, not shown */
static bool rx_busy;                    /**< an export is running */
static bool rx_cancel;                  /**< Cancel was tapped */
static uint8_t *rx_fb;                  /**< the frame buffer (RGB888: see video_enc.c, RGB565 gave the video a green cast) */
static video_enc_t *rx_enc;             /**< the encoder */
static int rx_frame;                    /**< the frames encoded */
static int rx_frames;                   /**< the frames the video has */
static char rx_path[128];               /**< the file */

/* (documented in replay_video.h) */
void replay_set_want_export(bool v)
{
    rp_want_export = v;
}

/* (documented in replay_video.h) */
bool is_replay_want_export(void)
{
    return rp_want_export;
}

/* (documented in replay_video.h) */
bool is_replay_busy(void)
{
    return rx_busy;
}

/* (documented in replay_video.h) */
bool is_replay_exporting(void)
{
    return rx_busy || rp_want_export;
}

/* (documented in replay_video.h) */
void replay_video_name(const stats_replay_t *r, const char *puzzle, char *out, size_t n)
{
    char raw[96], when[20] = "";
    if (r->finished_at) {
        time_t tt = (time_t)r->finished_at;
        struct tm tm;
        localtime_r(&tt, &tm);
        strftime(when, sizeof(when), "%Y%m%d-%H%M", &tm);
    } else {
        snprintf(when, sizeof(when), "solve%lld", (long long)r->solve_id);
    }
    snprintf(raw, sizeof(raw), "%s_%s_%s", r->player, puzzle, when);
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < n; i++) {   /* letters, digits, - and _ only */
        char c = raw[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) {
            c = (c == ' ' || c == '#') ? '-' : 0;
        }
        if (!c || (c == '-' && j && out[j - 1] == '-')) {
            continue;
        }
        out[j++] = c;
    }
    out[j] = 0;
}

/* (documented in replay_video.h) */
bool replay_video_open(const char *file, int w, int h)
{
    int n = replay_n();
    uint32_t play_ms = n ? replay_vt_at(n) / RX_SPEED : 0;
    rx_frames = (int)((RX_INTRO_MS + play_ms + RX_OUTRO_MS) * RX_FPS / 1000) + 1;

    mkdir(RX_DIR, 0775);                            /* fine if it exists */
    snprintf(rx_path, sizeof(rx_path), RX_DIR "/%s.mp4", file);

    rx_fb = heap_caps_aligned_alloc(64, (size_t)w * h * 3, MALLOC_CAP_SPIRAM);
    rx_enc = rx_fb ? video_enc_open(rx_path, w, h, RX_FPS, rx_frames + 8) : NULL;
    if (!rx_enc) {
        printf("export: %s didn't open (frame buffer %s)\n", rx_path, rx_fb ? "ok" : "no memory");
        heap_caps_free(rx_fb);
        rx_fb = NULL;
        return false;
    }
    rx_busy = true;
    rx_cancel = false;
    rx_frame = 0;
    printf("export: %s, %d frames\n", rx_path, rx_frames);
    return true;
}

/* (documented in replay_video.h) */
uint8_t *replay_video_fb(void)
{
    return rx_fb;
}

/* (documented in replay_video.h) */
int replay_video_k_at(int f, bool *playing)
{
    int64_t t = (int64_t)f * 1000 / RX_FPS - RX_INTRO_MS;
    *playing = false;
    int n = replay_n();
    if (t < 0 || n == 0) {
        return 0;
    }
    uint32_t clock = (uint32_t)(t * RX_SPEED);
    int k = 0;
    while (k < n && replay_vt_at(k + 1) <= clock) {
        k++;
    }
    *playing = k < n;
    return k;
}

/* (documented in replay_video.h) */
bool replay_video_frame(bool changed)
{
    if (!video_enc_frame(rx_enc, rx_fb, changed)) {
        return false;
    }
    rx_frame++;
    return true;
}

/* (documented in replay_video.h) */
int replay_video_done(void)
{
    return rx_frame;
}

/* (documented in replay_video.h) */
int replay_video_frames(void)
{
    return rx_frames;
}

/* (documented in replay_video.h) */
uint32_t replay_video_bytes(void)
{
    return video_enc_bytes(rx_enc);
}

/* (documented in replay_video.h) */
void replay_video_cancel(void)
{
    rx_cancel = true;
}

/* (documented in replay_video.h) */
bool is_replay_video_cancelled(void)
{
    return rx_cancel;
}

/* (documented in replay_video.h) */
bool replay_video_end(bool ok)
{
    if (ok) {
        ok = video_enc_close(rx_enc);
    } else {
        video_enc_abort(rx_enc);
    }
    rx_enc = NULL;
    return ok;
}

/* (documented in replay_video.h) */
void replay_video_free(void)
{
    heap_caps_free(rx_fb);
    rx_fb = NULL;
    rx_busy = false;
}

/* (documented in replay_video.h) */
void replay_video_result(bool ok, uint32_t bytes, char *msg, size_t n)
{
    if (ok) {
        const char *name = strrchr(rx_path, '/');
        snprintf(msg, n, "Saved replays%s (%.1f MB). Copy it to a computer with Settings > USB drive.",
                 name ? name : "", bytes / 1048576.0);
        printf("export: %s, %u bytes, %d frames\n", rx_path, (unsigned)bytes, rx_frames);
    } else {
        snprintf(msg, n, rx_cancel ? "Export cancelled." : "Export failed: couldn't write the video.");
    }
}
