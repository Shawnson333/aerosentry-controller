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
#include "radio_link.h"
#include "run_state.h"

static bool lvglReady = false;
static uint32_t loopMaxMs = 0;  // Slowest loop() pass since boot, for the watchdog margin

uint32_t mainLoopMaxMs() { return loopMaxMs; }

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

    // Relays first. The XL9535 keeps its outputs through an ESP32 reset, so after
    // a watchdog or crash restart the heater could still be latched on. Turn every
    // relay off before anything else, including the 1 s wait for the USB console.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    const bool relaysOk = relay.begin();

    delay(1000);
    Serial.println(F("\n=== AeroSentry Controller Starting ==="));
    Serial.printf("Firmware %s, built %s %s\n", FIRMWARE_VERSION, __DATE__, __TIME__);
    Serial.println(F("I2C bus initialized (GPIO 8/9)"));

    // XL9535 at 0x21. All eight channels are enabled. Active outputs start off.
    if (relaysOk) {
        Serial.println(F("Relay driver OK - all relays OFF"));
    } else {
        Serial.println(F("Relay driver disabled/unavailable; no relay writes made"));
    }

    initializeDisplay();
    if (!relay.isReady()) {
        Serial.println(F("Retrying XL9535 after touch reset"));
        relay.begin();
    }

    char devices[96];
    RelayDriver::scanBus(devices, sizeof(devices));
    Serial.printf("I2C devices: %s\n", devices);

    if (sensorsBegin()) {
        Serial.println(F("Sensor receiver ready"));
    } else {
        Serial.println(F("Sensor receiver not available yet (ESP-NOW stub)"));
    }

    // SD logger disabled for now (no card / wrong pins). Do not call SD.begin().
    if (logger.begin()) {
        Serial.println(F("SD logger ready"));
    } else {
        Serial.println(F("SD logger disabled"));
    }

    systemClockBegin();
    const SystemMode interrupted = runStateBegin();
    stateMachine.begin();
    if (interrupted != SystemMode::IDLE || runStateUnexpectedReset()) {
        // Shown by the first loop() pass like any other stop message. A watchdog,
        // crash or brownout reset is reported even when nothing was running.
        stateMachine.reportRestart(interrupted, runStateResetCause());
    }
    Serial.println(F("State machine ready - Idle mode"));
    Serial.println(F("Rules: purge/manual watch To-Engine; auto watches both; recharge ignores 40%"));

    if (lvglReady) {
        uiBegin();
        Serial.println(F("UI started - Main screen shown"));
    } else {
        Serial.println(F("UI skipped because display initialization failed"));
    }

#if LOOP_WATCHDOG_ENABLED
    enableLoopWDT();
    Serial.println(F("Loop watchdog on (5 s)"));
#endif
    Serial.println(F("=== Setup complete ==="));
}

void loop() {
    const uint32_t loopStart = millis();
    sensorsUpdate();
    relay.update();  // ends an Admin relay test pulse on time
    radioLinkUpdate();
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
            uiUpdateOutputs(stateMachine.pumpOn(), stateMachine.heaterOn(),
                            stateMachine.fanOn(), stateMachine.valveOpen(),
                            stateMachine.chipTempC());
            if (uiAdminVisible()) uiUpdateAdmin();
            if (uiRelayTestVisible()) uiUpdateRelayTest();
            systemClockSave();
            lastUiTick = millis();
        }

        static SystemMode lastMode = SystemMode::IDLE;
        const SystemMode currentMode = stateMachine.mode();
        if (currentMode != lastMode) {
            uiUpdateMode(currentMode);
            uiSetRunning(stateMachine.isRunning());
            uiUpdateStatus(stateMachine.statusText());
            uiUpdateOutputs(stateMachine.pumpOn(), stateMachine.heaterOn(),
                            stateMachine.fanOn(), stateMachine.valveOpen(),
                            stateMachine.chipTempC());
            lastMode = currentMode;
        }

        const char* stopMessage = stateMachine.takeStopMessage();
        if (stopMessage != nullptr) {
            uiShowWarning(stopMessage, [](lv_event_t*) { uiShowMain(); }, "OK");
        }
    }

    uiLoop();
    const uint32_t loopMs = millis() - loopStart;
    if (loopMs > loopMaxMs) {
        loopMaxMs = loopMs;
        if (loopMs >= 250UL) Serial.printf("Slow loop pass: %lu ms\n", (unsigned long)loopMs);
    }
    delay(5);
}