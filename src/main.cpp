/* ========================================================================
   AEROSENTRY CONTROLLER — Main entry point
   Waveshare ESP32-S3 4.3-inch Type B (800x480) + LVGL 8.3
   ======================================================================== */

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "relay.h"
#include "sensors.h"
#include "statemachine.h"
#include "logging.h"
#include "system_clock.h"
#include "ui.h"
#include "lvgl_v8_port.h"

static bool lvglReady = false;

static void initializeDisplay() {
    Serial.println(F("Initializing Waveshare display and touch..."));
    lvglReady = lvgl_port_init();
    if (lvglReady) {
        Serial.printf("Display ready: %d x %d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else {
        Serial.println(F("ERROR: Display/LVGL initialization failed"));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("\n=== AeroSentry Controller Starting ==="));

    // Shared I2C: GT911 touch + Waveshare CH422G (+ relay board later).
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println(F("I2C bus initialized (GPIO 8/9)"));

    // Disabled in config.h until the external relay board address is known.
    if (relay.begin()) {
        Serial.println(F("Relay driver OK - all relays OFF"));
    } else {
        Serial.println(F("Relay driver disabled/unavailable; no relay writes made"));
    }

    initializeDisplay();

    if (sensorsBegin()) {
        Serial.println(F("Sensor receiver ready"));
    } else {
        Serial.println(F("Sensor receiver not available yet (ESP-NOW stub)"));
    }

    if (logger.begin()) {
        Serial.println(F("SD logger ready"));
    } else {
        Serial.println(F("SD logger not available yet (stub)"));
    }

    stateMachine.begin();
    systemClockBegin();
    Serial.println(F("State machine ready - Idle mode"));
    Serial.println(F("Rules: purge/manual watch To-Engine; auto watches both; recharge ignores 40%"));

    if (lvglReady) {
        uiBegin();
        Serial.println(F("UI started - Main screen shown"));
    } else {
        Serial.println(F("UI skipped because display initialization failed"));
    }

    Serial.println(F("=== Setup complete ==="));
}

void loop() {
    sensorsUpdate();
    stateMachine.update();

    if (lvglReady) {
        static uint32_t lastUiTick = 0;
        if (millis() - lastUiTick >= 1000UL) {
            char dateTime[24];
            systemClockFormat(dateTime, sizeof(dateTime));
            uiUpdateDateTime(dateTime);
            uiUpdateStatus(stateMachine.statusText());
            uiUpdateSensors(sensors.toEngineRH, sensors.fromEngineRH,
                            toEngineReady(), fromEngineReady());
            lastUiTick = millis();
        }

        static SystemMode lastMode = SystemMode::IDLE;
        const SystemMode currentMode = stateMachine.mode();
        if (currentMode != lastMode) {
            uiUpdateMode(currentMode);
            uiSetRunning(stateMachine.isRunning());
            uiUpdateStatus(stateMachine.statusText());
            lastMode = currentMode;
        }

        const char* stopMessage = stateMachine.takeStopMessage();
        if (stopMessage != nullptr) {
            uiShowWarning(stopMessage, [](lv_event_t*) { uiShowMain(); }, "OK");
        }
    }

    uiLoop();
    delay(5);
}