/**
 * @file sysmon_cpu.c
 * @ingroup sysmon_cpu
 * @brief CPU load per core from the idle tasks' run time, and the chip's temperature sensor.
 */
#include "sysmon_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"

/* (documented in sysmon_cpu.h) */
void sysmon_load_reset(sysmon_load_t *s)
{
    s->primed = false;
}

/* (documented in sysmon_cpu.h) */
bool sysmon_load_permille(sysmon_load_t *s, int pm[2])
{
    int64_t now = esp_timer_get_time();
    bool ok = s->t && s->primed;
    for (int c = 0; c < 2; c++) {
        uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);   /* microseconds (esp_timer) */
        uint32_t dt = (uint32_t)(now - s->t), di = idle - s->idle[c];
        if (s->t && dt && s->primed) {
            int p = 1000 - (int)((uint64_t)di * 1000 / dt);
            pm[c] = p < 0 ? 0 : (p > 1000 ? 1000 : p);
        } else {
            ok = false;
        }
        s->idle[c] = idle;
    }
    s->t = now;
    s->primed = true;
    return ok;
}

static temperature_sensor_handle_t s_tsens;     /**< the chip's temperature sensor (installed on first use) */
static bool s_temp_held;                        /**< kept on (the page is shown) */

/** @brief Installs the temperature sensor if it isn't yet. @return true if there is one */
static bool temp_installed(void)
{
    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) {
            s_tsens = NULL;
        }
    }
    return s_tsens != NULL;
}

/* (documented in sysmon_cpu.h) */
void sysmon_temp_hold(bool on)
{
    if (on == s_temp_held || !temp_installed()) {
        s_temp_held = on && s_tsens;
        return;
    }
    s_temp_held = on;
    if (on) {
        temperature_sensor_enable(s_tsens);
    } else {
        temperature_sensor_disable(s_tsens);
    }
}

/* (documented in sysmon_cpu.h) */
bool sysmon_temp_read(float *celsius)
{
    if (!temp_installed()) {
        return false;
    }
    if (!s_temp_held) {                             /* on only for the reading, unless held on */
        temperature_sensor_enable(s_tsens);
    }
    bool ok = temperature_sensor_get_celsius(s_tsens, celsius) == ESP_OK;
    if (!s_temp_held) {
        temperature_sensor_disable(s_tsens);
    }
    return ok;
}
