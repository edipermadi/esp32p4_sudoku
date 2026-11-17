/*
 * GT911 capacitive touch, polled over the shared I2C bus (GPIO7/8).
 * INT is not routed on the 4B, and Waveshare's own Arduino helper does not
 * drive the reset line, so the controller can come up at either address.
 */
#include "bsp_board.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bsp_touch";

#define GT911_ADDR_PRIMARY   0x5D
#define GT911_ADDR_BACKUP    0x14
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS     0x814E
#define GT911_REG_POINT1     0x814F

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_tp = NULL;

esp_err_t bsp_i2c_init(i2c_master_bus_handle_t *ret_bus)
{
    if (!s_bus) {
        i2c_master_bus_config_t cfg = {
            .i2c_port = BSP_I2C_PORT,
            .sda_io_num = BSP_I2C_SDA_GPIO,
            .scl_io_num = BSP_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "I2C bus init failed");
    }
    if (ret_bus) {
        *ret_bus = s_bus;
    }
    return ESP_OK;
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t r[2] = {reg >> 8, reg & 0xFF};
    return i2c_master_transmit_receive(s_tp, r, 2, buf, len, 50);
}

static esp_err_t gt911_write_u8(uint16_t reg, uint8_t val)
{
    uint8_t w[3] = {reg >> 8, reg & 0xFF, val};
    return i2c_master_transmit(s_tp, w, 3, 50);
}

esp_err_t bsp_touch_init(void)
{
    if (s_tp) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_i2c_init(NULL), TAG, "I2C");

    uint16_t addr = 0;
    if (i2c_master_probe(s_bus, GT911_ADDR_PRIMARY, 100) == ESP_OK) {
        addr = GT911_ADDR_PRIMARY;
    } else if (i2c_master_probe(s_bus, GT911_ADDR_BACKUP, 100) == ESP_OK) {
        addr = GT911_ADDR_BACKUP;
    } else {
        ESP_LOGE(TAG, "GT911 not found at 0x%02X or 0x%02X", GT911_ADDR_PRIMARY, GT911_ADDR_BACKUP);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BSP_TOUCH_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_tp), TAG, "add device failed");

    uint8_t id[4] = {0};
    gt911_read(GT911_REG_PRODUCT_ID, id, 4);
    ESP_LOGI(TAG, "GT911 at 0x%02X, product id \"%.4s\"", addr, (char *)id);
    return ESP_OK;
}

bool bsp_touch_read(uint16_t *x, uint16_t *y)
{
    static bool last_pressed = false;
    static uint16_t last_x, last_y;

    if (!s_tp) {
        return false;
    }
    uint8_t status = 0;
    if (gt911_read(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        return false;
    }
    if (status & 0x80) {                 /* buffer ready: new data */
        uint8_t n = status & 0x0F;
        if (n > 0 && n <= 5) {
            uint8_t p[8];
            if (gt911_read(GT911_REG_POINT1, p, sizeof(p)) == ESP_OK) {
                uint16_t px = p[1] | (p[2] << 8);
                uint16_t py = p[3] | (p[4] << 8);
                last_x = px < BSP_LCD_H_RES ? px : BSP_LCD_H_RES - 1;
                last_y = py < BSP_LCD_V_RES ? py : BSP_LCD_V_RES - 1;
                last_pressed = true;
            }
        } else {
            last_pressed = false;
        }
        gt911_write_u8(GT911_REG_STATUS, 0);   /* ack */
    }
    if (last_pressed) {
        *x = last_x;
        *y = last_y;
    }
    return last_pressed;
}
