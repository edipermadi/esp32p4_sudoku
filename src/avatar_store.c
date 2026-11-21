/* The avatars' pixels, from the flash's "avatars" partition (partitions.csv): 7 MB that both
 * app slots share, so each slot only holds the code and a firmware update from the card fits
 * (only the flash's first 16 MiB can be mapped: the core's bootloader addresses it with 24 bits).
 *
 * The partition holds extras/avatars.bin (tools/gen_avatars.py): a 16-byte header ("AVTR",
 * version, count, size, CRC-32 of the pixels), then the pixels. It's used when its header
 * matches this firmware's set (AVATAR_COUNT, AVATAR_SIZE, AVATAR_CRC). Otherwise the avatars
 * are blank, and if the card has avatars.bin in its root, avatars_install() writes it into the
 * partition (header last, so a cut-off install isn't taken for a good one) and checks it. */
#include "avatar_store.h"
#include "avatars.h"
#include "bsp/bsp_board.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AV_FILE    BSP_SD_MOUNT_POINT "/avatars.bin"
#define AV_HEADER  16
#define AV_ONE     (AVATAR_SIZE * AVATAR_SIZE * 3)  /* bytes per avatar: RGB565, then alpha */
#define AV_BYTES   ((uint32_t)AVATAR_COUNT * AV_ONE)
#define AV_CHUNK   (64 * 1024)

typedef struct {
    char magic[4];
    uint16_t version, count, size, zero;
    uint32_t crc;
} av_header_t;

static lv_image_dsc_t *s_dsc;                       /* AVATAR_COUNT descriptors (PSRAM) */
static uint8_t *s_blank;                            /* one transparent avatar */
static const uint8_t *s_pixels;                     /* the mapped partition's pixels, NULL = none */
static esp_partition_mmap_handle_t s_map;
static volatile int s_install;                      /* 0 idle, 1 running, 2 done, 3 failed */
static volatile uint32_t s_done;

static bool is_header_ok(const av_header_t *h)
{
    return !memcmp(h->magic, "AVTR", 4) && h->version == 1 && h->count == AVATAR_COUNT &&
           h->size == AVATAR_SIZE && h->crc == AVATAR_CRC;
}

static const esp_partition_t *partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "avatars");
}

/* each descriptor at its pixels, or all at the blank one */
static void point_images(void)
{
    for (int i = 0; i < AVATAR_COUNT; i++) {
        s_dsc[i].data = s_pixels ? s_pixels + (size_t)i * AV_ONE : s_blank;
    }
}

/* maps the partition if it holds this firmware's avatars */
static bool map_avatars(void)
{
    const esp_partition_t *p = partition();
    av_header_t h;
    if (!p || p->size < AV_HEADER + AV_BYTES || esp_partition_read(p, 0, &h, sizeof(h)) != ESP_OK || !is_header_ok(&h)) {
        return false;
    }
    const void *ptr;
    if (esp_partition_mmap(p, 0, AV_HEADER + AV_BYTES, ESP_PARTITION_MMAP_DATA, &ptr, &s_map) != ESP_OK) {
        return false;
    }
    s_pixels = (const uint8_t *)ptr + AV_HEADER;
    return true;
}

bool avatars_init(void)
{
    if (s_dsc) {
        return s_pixels != NULL;
    }
    s_dsc = heap_caps_calloc(AVATAR_COUNT, sizeof(*s_dsc), MALLOC_CAP_SPIRAM);
    s_blank = heap_caps_calloc(1, AV_ONE, MALLOC_CAP_SPIRAM);
    if (!s_dsc || !s_blank) {
        abort();    /* at start-up, with megabytes free */
    }
    for (int i = 0; i < AVATAR_COUNT; i++) {
        s_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565A8;
        s_dsc[i].header.w = s_dsc[i].header.h = AVATAR_SIZE;
        s_dsc[i].header.stride = AVATAR_SIZE * 2;
        s_dsc[i].data_size = AV_ONE;
    }
    bool ok = map_avatars();
    point_images();
    if (ok) {
        printf("avatars: %d from the flash's avatars partition\n", AVATAR_COUNT);
    } else {
        printf("avatars: not in flash (partition missing or another set): blank; avatars.bin on the card installs them\n");
    }
    return ok;
}

const lv_image_dsc_t *avatar_image(int i)
{
    if (!s_dsc) {
        avatars_init();
    }
    return &s_dsc[i < 0 || i >= AVATAR_COUNT ? 0 : i];
}

bool is_avatars_ready(void) { return s_pixels != NULL; }

static void install_task(void *arg)
{
    const esp_partition_t *p = partition();
    uint8_t *buf = heap_caps_malloc(AV_CHUNK, MALLOC_CAP_SPIRAM);
    FILE *f = fopen(AV_FILE, "rb");
    av_header_t h;
    bool ok = p && buf && f && p->size >= AV_HEADER + AV_BYTES && fread(&h, 1, sizeof(h), f) == sizeof(h) &&
              is_header_ok(&h);
    if (ok) {
        printf("avatars: installing %s into the avatars partition\n", AV_FILE);
        /* all at once, quicker than sector by sector (the screen is dark meanwhile: the caller) */
        ok = esp_partition_erase_range(p, 0, (AV_HEADER + AV_BYTES + 0xFFF) & ~0xFFFu) == ESP_OK;
    }
    uint32_t crc = 0;
    for (uint32_t at = 0; ok && at < AV_BYTES;) {   /* the pixels, then (below) the header */
        size_t want = AV_BYTES - at < AV_CHUNK ? AV_BYTES - at : AV_CHUNK, n = fread(buf, 1, want, f);
        ok = n == want && esp_partition_write(p, AV_HEADER + at, buf, n) == ESP_OK;
        crc = esp_rom_crc32_le(crc, buf, n);
        at += n;
        s_done = at;
        if (!(at % (1024 * 1024))) {
            printf("avatars: %u MB written\n", (unsigned)(at >> 20));
        }
    }
    ok = ok && crc == AVATAR_CRC;                   /* the file's pixels are the ones expected */
    if (ok) {                                       /* and they're in flash as they were read */
        crc = 0;
        for (uint32_t at = 0; ok && at < AV_BYTES; at += AV_CHUNK) {
            size_t n = AV_BYTES - at < AV_CHUNK ? AV_BYTES - at : AV_CHUNK;
            ok = esp_partition_read(p, AV_HEADER + at, buf, n) == ESP_OK;
            crc = esp_rom_crc32_le(crc, buf, n);
        }
        ok = ok && crc == AVATAR_CRC && esp_partition_write(p, 0, &h, sizeof(h)) == ESP_OK;
    }
    if (f) {
        fclose(f);
    }
    heap_caps_free(buf);
    printf("avatars: install %s\n", ok ? "done" : "failed (the file isn't this firmware's avatars.bin?)");
    s_install = ok ? 2 : 3;
    vTaskDelete(NULL);
}

bool is_avatars_installable(void)
{
    if (s_pixels || s_install == 1 || !partition()) {
        return false;
    }
    FILE *f = fopen(AV_FILE, "rb");
    av_header_t h;
    bool ok = f && fread(&h, 1, sizeof(h), f) == sizeof(h) && is_header_ok(&h);
    if (f) {
        fclose(f);
    }
    return ok;
}

bool avatars_install(void)
{
    if (!is_avatars_installable()) {
        return false;
    }
    s_install = 1;
    s_done = 0;
    if (xTaskCreatePinnedToCore(install_task, "avatars", 4096, NULL, 1, NULL, 0) != pdPASS) {
        s_install = 0;
        return false;
    }
    return true;
}

int avatars_install_poll(uint32_t *done, uint32_t *total)
{
    if (done) {
        *done = s_done;
    }
    if (total) {
        *total = AV_BYTES;
    }
    if (s_install == 2) {                           /* written and checked: use it */
        s_install = 0;
        if (!map_avatars()) {
            return 3;
        }
        point_images();
        return 2;
    }
    if (s_install == 3) {
        s_install = 0;
        return 3;
    }
    return s_install;
}
