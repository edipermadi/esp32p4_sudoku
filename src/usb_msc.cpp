/**
 * @file usb_msc.cpp
 * @brief The microSD card as a USB drive (TinyUSB mass storage on the OTG port).
 *
 * microSD card -> USB mass storage (see usb_msc.h), using the Arduino core's
 * TinyUSB USBMSC class. TinyUSB on the P4 runs on the OTG high-speed port.
 *
 */
#include "usb_msc.h"
#include "bsp/bsp_board.h"
#include "USB.h"
#include "USBMSC.h"
#include "esp_log.h"

#if ARDUINO_USB_MODE
#error "Set Tools > USB Mode to \"USB-OTG (TinyUSB)\" for the USB drive"
#endif

[[maybe_unused]] static const char *TAG = "usb_msc";   /**< (ESP_LOGE compiles out at a low log level) */

static USBMSC s_msc;                    /**< the drive */
static uint32_t s_sector_size = 512;    /**< the card's sector size, bytes */
static bool s_started;                  /**< usb_msc_start() has run */
static volatile bool s_enabled;          /**< checked by the TinyUSB callbacks */
static volatile bool s_ejected;         /**< the computer ejected the drive */
static volatile bool s_suspended;         /**< bus suspended: unplugged, or the host sleeps */
static volatile uint32_t s_suspended_ms;  /**< millis() when it was */

/**
 * @brief
 * @param lba  the first sector
 * @param offset  into it (0)
 * @param buffer  gets the data
 * @param bufsize  bytes
 * @return bufsize, or -1
 */
/** @brief TinyUSB passes whole sectors in practice (bufsize = n * 512, offset = 0);
 * reject anything else rather than silently corrupting the card. */
static int32_t on_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (!s_enabled || offset != 0 || bufsize % s_sector_size) {
        return -1;
    }
    esp_err_t err = bsp_sdcard_read(buffer, lba, bufsize / s_sector_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read lba %lu failed: %s", (unsigned long)lba, esp_err_to_name(err));
        return -1;
    }
    return (int32_t)bufsize;
}

/**
 * @brief Writes whole sectors to the card for the computer (anything else is refused).
 * @param lba  the first sector
 * @param offset  into it (0)
 * @param buffer  the data
 * @param bufsize  bytes
 * @return bufsize, or -1
 */
static int32_t on_write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    if (!s_enabled || offset != 0 || bufsize % s_sector_size) {
        return -1;
    }
    esp_err_t err = bsp_sdcard_write(buffer, lba, bufsize / s_sector_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write lba %lu failed: %s", (unsigned long)lba, esp_err_to_name(err));
        return -1;
    }
    return (int32_t)bufsize;
}

/**
 * @brief The computer starts or stops the drive; an eject reports "no media" from then on.
 * @param power_condition  unused
 * @param start  start (else stop)
 * @param load_eject  load or eject
 * @return true
 */
static bool on_start_stop(uint8_t power_condition, bool start, bool load_eject)
{
    if (load_eject && !start) {                /* "Eject" / "Safely remove" on the computer */
        s_msc.mediaPresent(false);
        s_enabled = false;
        s_ejected = true;
    }
    return true;
}

/**
 * @brief USB events: the bus suspended or resumed, the port plugged or unplugged.
 * @param arg  unused
 * @param base  the event base
 * @param id  the event
 * @param data  unused
 */
static void on_usb_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ARDUINO_USB_SUSPEND_EVENT:
        s_suspended = true;
        s_suspended_ms = millis();
        break;
    case ARDUINO_USB_RESUME_EVENT:
    case ARDUINO_USB_STARTED_EVENT:
        s_suspended = false;
        break;   /* STARTED = enumerated */
    default:
        break;
    }
    static const char *const names[] = {"enumerated", "disconnected", "suspended", "resumed"};
    if (id >= 0 && id < 4) {
        printf("usb_msc: OTG port %s\n", names[id]);
    }
}

/* (documented in usb_msc.h) */
extern "C" esp_err_t usb_msc_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    uint32_t sectors = bsp_sdcard_sector_count();
    if (!sectors) {
        return ESP_ERR_INVALID_STATE;    /* no card */
    }
    s_sector_size = bsp_sdcard_sector_size();
    s_msc.vendorID("ESP32P4");
    s_msc.productID("Sudoku SD Card");
    s_msc.productRevision("1.0");
    s_msc.onRead(on_read);
    s_msc.onWrite(on_write);
    s_msc.onStartStop(on_start_stop);
    s_msc.isWritable(true);
    s_msc.mediaPresent(false);                               /* the board's until Connect */
    USB.onEvent(on_usb_event);
    if (!s_msc.begin(sectors, (uint16_t)s_sector_size) || !USB.begin()) {
        return ESP_FAIL;
    }
    s_started = true;
    return ESP_OK;
}

/* (documented in usb_msc.h) */
extern "C" bool is_usb_msc_host_connected(void)
{
    return s_started && (bool)USB && !s_suspended;
}

/* (documented in usb_msc.h) */
extern "C" esp_err_t usb_msc_set_enabled(bool on)
{
    if (on) {
        if (!s_started) {
            return ESP_ERR_INVALID_STATE;
        }
        s_ejected = false;
        s_enabled = true;
        s_msc.mediaPresent(true);
        printf("usb_msc: card on USB (%lu sectors)\n", (unsigned long)bsp_sdcard_sector_count());
        return ESP_OK;
    }
    s_enabled = false;
    if (s_started) {
        s_msc.mediaPresent(false);
    }
    printf("usb_msc: card back to the board%s\n", s_ejected ? " (ejected by the computer)"
           : (s_started && (!(bool)USB || s_suspended)) ? " (USB unplugged)" : "");
    return ESP_OK;
}

/* (documented in usb_msc.h) */
extern "C" bool is_usb_msc_ejected(void)
{
    return s_ejected;
}

/* (documented in usb_msc.h) */
extern "C" bool is_usb_msc_unplugged(void)
{
    if (!s_started || !s_enabled) {
        return false;
    }
    if (!(bool)USB) {
        return true;    /* not enumerated any more */
    }
    return s_suspended && millis() - s_suspended_ms > 1500;   /* bus quiet: cable out */
}
