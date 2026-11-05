#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2c.h"
#include "config.h"

// --- Relay Control State ---
static uint8_t relayState = 0x00;

esp_err_t xlWrite(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_master_write_to_device(I2C_PORT, XL_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

void applyRelayState() {
    xlWrite(REG_OUTPUT, relayState);
}

void setRelay(uint8_t bit, bool on) {
    if (on) relayState |=  (1 << bit);
    else    relayState &= ~(1 << bit);
    applyRelayState();
}

void allRelaysOff() {
    relayState = 0x00;
    applyRelayState();
}

// --- Sensor Data Struct (Single Wired Remote Node) ---
typedef struct {
    float tempTo;
    float humidityTo;
    float tempFrom;
    float humidityFrom;
    uint8_t batteryPct;
} sensor_data_t;

static float toRH = 0, fromRH = 0;
static bool sensorValid = false;

// ESP-NOW Receive Callback
void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (len == sizeof(sensor_data_t)) {
        sensor_data_t s;
        memcpy(&s, data, sizeof(s));
        toRH = s.humidityTo;
        fromRH = s.humidityFrom;
        sensorValid = true;
        Serial.printf("Sensor Update -> To: %.1f%%RH | From: %.1f%%RH\n", toRH, fromRH);
    }
}

void setupI2C() {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA_PIN;
    conf.scl_io_num = I2C_SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);

    xlWrite(REG_CONFIG, 0x00); // Set all expander pins to output
    allRelaysOff();
}

void setup() {
    Serial.begin(115200);
    Serial.println("AeroSentry Controller Booting...");

    setupI2C();

    // Initialize Wi-Fi & ESP-NOW for wireless sensor telemetry
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Init Failed!");
        return;
    }
    esp_now_register_recv_cb(onDataRecv);

    Serial.println("System Ready.");
}

void loop() {
    // Main loop task management
    delay(100);
}