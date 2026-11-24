/* Cycle-running flag in NVS, for restart reporting (stop code S18). */

#include "run_state.h"
#include "statemachine.h"
#include <Preferences.h>
#include <esp_system.h>

static Preferences runStore;
static bool storeOpen = false;
static uint8_t savedMode = 0;
static esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;

const char* runStateResetCause() {
    switch (resetReason) {
        case ESP_RST_POWERON:  return "Power was lost or switched off.";
        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:      return "The controller froze and the watchdog restarted it.";
        case ESP_RST_PANIC:    return "The controller crashed and restarted.";
        case ESP_RST_BROWNOUT: return "The supply voltage dropped too low.";
        case ESP_RST_SW:       return "The controller restarted itself.";
        case ESP_RST_EXT:      return "The reset line was pulled (reset button or USB).";
        default:               return "Reset over USB (flashing or a serial tool), or the cause is unknown.";
    }
}

bool runStateUnexpectedReset() {
    switch (resetReason) {
        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
        case ESP_RST_PANIC:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

SystemMode runStateBegin() {
    resetReason = esp_reset_reason();
    storeOpen = runStore.begin("aerorun", false);
    savedMode = storeOpen ? runStore.getUChar("mode", 0) : 0;
    Serial.printf("Reset cause %d: %s\n", (int)resetReason, runStateResetCause());
    if (savedMode > (uint8_t)SystemMode::RECHARGE) savedMode = 0;
    const SystemMode interrupted = (SystemMode)savedMode;
    if (interrupted != SystemMode::IDLE) {
        Serial.printf("A %s cycle was running when the controller restarted\n",
                      systemModeName(interrupted));
        runStateSave(SystemMode::IDLE);
    }
    return interrupted;
}

void runStateSave(SystemMode mode) {
    const uint8_t value = (uint8_t)mode;
    if (!storeOpen || value == savedMode) return;
    runStore.putUChar("mode", value);
    savedMode = value;
}
