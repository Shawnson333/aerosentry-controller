/* Waveshare ESP32-S3 4.3-inch Type B: ST7262 RGB LCD + GT911 touch. */
#include "display.h"
#include "config.h"
#include <Wire.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_heap_caps.h>

static esp_lcd_panel_handle_t lcdPanel = nullptr;
static uint8_t touchAddress = 0;
static bool touchReady = false;

static bool touchReadRegs(uint8_t address, uint16_t reg, uint8_t *data, uint8_t length) {
    Wire.beginTransmission(address);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address, length) != length) return false;
    for (uint8_t i = 0; i < length; ++i) {
        if (!Wire.available()) return false;
        data[i] = (uint8_t)Wire.read();
    }
    return true;
}

static bool touchWriteReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(touchAddress);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static void initializeTouch() {
    const uint8_t addresses[] = {0x5D, 0x14};
    uint8_t productId[4] = {};
    for (uint8_t address : addresses) {
        if (touchReadRegs(address, 0x8140, productId, sizeof(productId))) {
            touchAddress = address;
            touchReady = true;
            Serial.printf("GT911 touch found at 0x%02X, ID %c%c%c\n",
                          address, productId[0], productId[1], productId[2]);
            return;
        }
    }
    Serial.println(F("WARNING: GT911 not found at 0x5D or 0x14; display will still run"));
}

bool touchHardwareRead(uint16_t &x, uint16_t &y, bool &pressed) {
    pressed = false;
    if (!touchReady) return false;

    uint8_t status = 0;
    if (!touchReadRegs(touchAddress, 0x814E, &status, 1)) return false;
    const uint8_t pointCount = status & 0x0F;

    if ((status & 0x80) && pointCount > 0 && pointCount <= 5) {
        uint8_t point[8] = {};
        if (!touchReadRegs(touchAddress, 0x814F, point, sizeof(point))) return false;
        x = (uint16_t)point[1] | ((uint16_t)point[2] << 8);
        y = (uint16_t)point[3] | ((uint16_t)point[4] << 8);
        x = min<uint16_t>(x, DISPLAY_WIDTH - 1);
        y = min<uint16_t>(y, DISPLAY_HEIGHT - 1);
        pressed = true;
    }

    if (status & 0x80) touchWriteReg(0x814E, 0);
    return true;
}

bool displayHardwareBegin() {
    Serial.printf("PSRAM detected: %s; free: %u bytes\n",
                  psramFound() ? "yes" : "no", ESP.getFreePsram());
    if (!psramFound()) {
        Serial.println(F("ERROR: RGB framebuffer requires working PSRAM"));
        return false;
    }

    esp_lcd_rgb_panel_config_t config = {};
    config.clk_src = LCD_CLK_SRC_PLL160M;
    config.timings.pclk_hz = 16000000;
    config.timings.h_res = DISPLAY_WIDTH;
    config.timings.v_res = DISPLAY_HEIGHT;
    config.timings.hsync_pulse_width = 4;
    config.timings.hsync_back_porch = 8;
    config.timings.hsync_front_porch = 8;
    config.timings.vsync_pulse_width = 4;
    config.timings.vsync_back_porch = 8;
    config.timings.vsync_front_porch = 8;
    config.timings.flags.pclk_active_neg = 1;
    config.data_width = 16;
    config.sram_trans_align = 4;
    config.psram_trans_align = 64;
    config.hsync_gpio_num = 46;
    config.vsync_gpio_num = 3;
    config.de_gpio_num = 5;
    config.pclk_gpio_num = 7;
    config.disp_gpio_num = -1;

    const int dataPins[16] = {
        14, 38, 18, 17, 10,       // B0-B4
        39, 0, 45, 48, 47, 21,    // G0-G5
        1, 2, 42, 41, 40          // R0-R4
    };
    memcpy(config.data_gpio_nums, dataPins, sizeof(dataPins));
    config.flags.fb_in_psram = 1;

    esp_err_t error = esp_lcd_new_rgb_panel(&config, &lcdPanel);
    if (error != ESP_OK) {
        Serial.printf("ERROR: esp_lcd_new_rgb_panel: %s\n", esp_err_to_name(error));
        return false;
    }
    error = esp_lcd_panel_reset(lcdPanel);
    if (error != ESP_OK) {
        Serial.printf("ERROR: LCD reset: %s\n", esp_err_to_name(error));
        return false;
    }
    error = esp_lcd_panel_init(lcdPanel);
    if (error != ESP_OK) {
        Serial.printf("ERROR: LCD init: %s\n", esp_err_to_name(error));
        return false;
    }

    initializeTouch();
    Serial.println(F("ST7262 RGB display initialized (800x480)"));
    return true;
}

void displayWritePixels(int x1, int y1, int x2, int y2, const void *colorData) {
    if (lcdPanel != nullptr) {
        esp_lcd_panel_draw_bitmap(lcdPanel, x1, y1, x2, y2, colorData);
    }
}