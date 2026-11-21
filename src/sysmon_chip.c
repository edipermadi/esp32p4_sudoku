/**
 * @file sysmon_chip.c
 * @ingroup sysmon_chip
 * @brief The chip: why it started, uptime, its model, the Ethernet MAC, the task count.
 */
#include "sysmon_chip.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_private/esp_clk.h"
#include "esp_mac.h"

/* (documented in sysmon_chip.h) */
const char *sysmon_reset_text(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "Power-on";
    case ESP_RST_EXT:
        return "Reset pin";
    case ESP_RST_SW:
        return "Software restart";
    case ESP_RST_PANIC:
        return "Crash (panic)";
    case ESP_RST_INT_WDT:
        return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "Task watchdog";
    case ESP_RST_WDT:
        return "Watchdog";
    case ESP_RST_DEEPSLEEP:
        return "Wake from sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    default:
        return "Unknown";
    }
}

/* (documented in sysmon_chip.h) */
uint64_t sysmon_uptime_s(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000000);
}

/* (documented in sysmon_chip.h) */
void sysmon_chip_text(char *buf, size_t n)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    snprintf(buf, n, "ESP32-P4 v%u.%u, %u cores, %u MHz", (unsigned)(ci.revision / 100),
             (unsigned)(ci.revision % 100), (unsigned)ci.cores, (unsigned)(esp_clk_cpu_freq() / 1000000));
}

/* (documented in sysmon_chip.h) */
bool sysmon_eth_mac(uint8_t mac[6])
{
    return esp_read_mac(mac, ESP_MAC_ETH) == ESP_OK;
}

/* (documented in sysmon_chip.h) */
unsigned sysmon_task_count(void)
{
    return (unsigned)uxTaskGetNumberOfTasks();
}
