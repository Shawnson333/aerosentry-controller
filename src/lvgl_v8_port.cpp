/* Minimal single-threaded LVGL 8.3 port for the AeroSentry Type B display. */
#include "lvgl_v8_port.h"
#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

static lv_disp_draw_buf_t drawBuffer;
static lv_disp_drv_t displayDriver;
static lv_indev_drv_t touchDriver;
static lv_color_t *buffer1 = nullptr;
static lv_color_t *buffer2 = nullptr;

static void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    displayWritePixels(area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
    lv_disp_flush_ready(driver);
}

static void readTouch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
    (void)driver;
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;
    touchHardwareRead(x, y, pressed);
    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool lvgl_port_init() {
    if (!displayHardwareBegin()) return false;

    constexpr uint32_t bufferPixels = DISPLAY_WIDTH * 20;
    buffer1 = (lv_color_t *)heap_caps_malloc(bufferPixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buffer2 = (lv_color_t *)heap_caps_malloc(bufferPixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (buffer1 == nullptr || buffer2 == nullptr) {
        Serial.println(F("ERROR: Unable to allocate LVGL DMA buffers"));
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        buffer1 = nullptr;
        buffer2 = nullptr;
        return false;
    }

    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, buffer1, buffer2, bufferPixels);
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = DISPLAY_WIDTH;
    displayDriver.ver_res = DISPLAY_HEIGHT;
    displayDriver.flush_cb = flushDisplay;
    displayDriver.draw_buf = &drawBuffer;
    if (lv_disp_drv_register(&displayDriver) == nullptr) {
        Serial.println(F("ERROR: LVGL display registration failed"));
        return false;
    }

    lv_indev_drv_init(&touchDriver);
    touchDriver.type = LV_INDEV_TYPE_POINTER;
    touchDriver.read_cb = readTouch;
    if (lv_indev_drv_register(&touchDriver) == nullptr) {
        Serial.println(F("ERROR: LVGL touch registration failed"));
        return false;
    }

    Serial.println(F("LVGL initialized"));
    return true;
}

void lvgl_port_loop() {
    lv_timer_handler();
}