/*
 * microSD (TF) slot: SDMMC slot 0, 4-bit, IO_MUX pins CLK43 CMD44 D0..D3 39..42.
 * Based on the esp32p4_86box project's driver. Only what Settings -> Format
 * needs: detect the card and lay down a fresh FAT32 volume. Nothing mounts it.
 *
 * Not using the Arduino SD_MMC library: its generic P4 variant toggles GPIO45
 * as a card power switch, which this board does not have.
 */
#include "bsp_board.h"
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "diskio_impl.h"

static const char *TAG = "bsp_sdcard";

static esp_ldo_channel_handle_t s_ldo;
static bool s_host_ok, s_slot_ok, s_ready;
static sdmmc_card_t s_card;
static BYTE s_pdrv = 0xFF;

esp_err_t bsp_sdcard_host_init(void)
{
    if (s_host_ok) return ESP_OK;
    if (!s_ldo) {
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = BSP_SD_LDO_CHAN,
            .voltage_mv = BSP_SD_LDO_MV,
        };
        ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_ldo), TAG, "LDO VO4 acquire failed");
    }
    esp_err_t err = sdmmc_host_init();
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;     /* already up (ESP-Hosted got there first) */
    ESP_RETURN_ON_ERROR(err, TAG, "SDMMC host init failed");
    s_host_ok = true;
    return ESP_OK;
}

esp_err_t bsp_sdcard_probe(uint64_t *bytes, char *name)
{
    s_ready = false;
    ESP_RETURN_ON_ERROR(bsp_sdcard_host_init(), TAG, "host");
    if (!s_slot_ok) {
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 4;                                  /* slot 0: fixed IO_MUX pins */
        ESP_RETURN_ON_ERROR(sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot), TAG, "slot init failed");
        s_slot_ok = true;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;            /* 40 MHz */
    esp_err_t err = sdmmc_card_init(&host, &s_card);     /* again each time: the card may have changed */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no usable card: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    if (bytes) *bytes = (uint64_t)s_card.csd.capacity * s_card.csd.sector_size;
    if (name) {
        memcpy(name, s_card.cid.name, 7);
        name[7] = 0;
    }
    ESP_LOGI(TAG, "card %s, %llu MB", s_card.cid.name,
             (unsigned long long)s_card.csd.capacity * s_card.csd.sector_size / (1024 * 1024));
    return ESP_OK;
}

esp_err_t bsp_sdcard_check(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    return sdmmc_get_status(&s_card);
}

/* ------------------------------------------------------- FatFs disk driver */
static DSTATUS dio_init(BYTE pdrv)   { return s_ready ? 0 : STA_NOINIT; }
static DSTATUS dio_status(BYTE pdrv) { return s_ready ? 0 : STA_NOINIT; }

static DRESULT dio_read(BYTE pdrv, BYTE *buf, DWORD sector, UINT count)
{
    return sdmmc_read_sectors(&s_card, buf, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT dio_write(BYTE pdrv, const BYTE *buf, DWORD sector, UINT count)
{
    return sdmmc_write_sectors(&s_card, buf, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT dio_ioctl(BYTE pdrv, BYTE cmd, void *buf)
{
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buf = s_card.csd.capacity;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buf = s_card.csd.sector_size;
        return RES_OK;
    case GET_BLOCK_SIZE:
        /* erase-block alignment for the data area, in sectors: 4 MiB as SD
         * cards like it (FatFs pads the reserved area to reach it) */
        *(DWORD *)buf = s_card.csd.capacity >= 2u * 1024 * 1024 ? 8192 : 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t s_diskio = {
    .init = dio_init,
    .status = dio_status,
    .read = dio_read,
    .write = dio_write,
    .ioctl = dio_ioctl,
};

esp_err_t bsp_sdcard_format_fat32(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "probe the card first");
    if (s_pdrv == 0xFF) {
        ESP_RETURN_ON_ERROR(ff_diskio_get_drive(&s_pdrv), TAG, "no free FatFs drive");
        ff_diskio_register(s_pdrv, &s_diskio);
    }
    char drv[3] = {(char)('0' + s_pdrv), ':', 0};

    /* Logical drive N maps to physical drive N, partition 0 (ESP-IDF's VolToPart),
     * so without FM_SFD f_mkfs writes an MBR holding a single primary partition
     * that spans the whole card, then formats it FAT32 (cluster size automatic). */
    const UINT work_len = 64 * 1024;
    void *work = heap_caps_malloc(work_len, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(work, ESP_ERR_NO_MEM, TAG, "no memory for the format buffer");
    const MKFS_PARM opt = {.fmt = FM_FAT32, .n_fat = 2, .align = 0, .n_root = 0, .au_size = 0};
    FRESULT fr = f_mkfs(drv, &opt, work, work_len);
    heap_caps_free(work);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "f_mkfs failed (%d)", fr);
        return fr == FR_MKFS_ABORTED ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
    }
    ESP_LOGI(TAG, "formatted FAT32");
    return ESP_OK;
}
