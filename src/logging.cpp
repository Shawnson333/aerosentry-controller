/* ========================================================================
   LOGGING — SD card /hangar.csv

   DISABLED 2026-11-20 on user request. The Waveshare Type B has no TF
   slot on the default SD pins; SD.begin() fails with f_mount (3), and
   the open/print/close in the old draft stalled the LVGL flush thread.
   All SD calls are no-ops until the correct pins / card are wired.

   Formats (CSV, when re-enabled):
     LOG_FILENAME, one row per LOG_INTERVAL_MS while a cycle runs:
       sec,mode,toRH,fromRH,pump,heater
     Stop events, one row per stop (same code as the screen, "S09"):
       epoch,code,name,mode,message
     The message has its line breaks replaced by " | ".

   Stop events are also kept in RAM (the last RECENT_STOPS) for the Admin
   page. Do not write them to flash here: NVS writes block the PSRAM bus
   and glitch the RGB display.
   ======================================================================== */

#include "logging.h"
#include "system_clock.h"

Logger::Logger() : _ready(false), _recent{}, _recentCount(0), _recentNext(0) {}

bool Logger::begin() {
    _ready = false;  // Disabled — no SD.begin() call.
    return false;
}

void Logger::logRunning(uint32_t seconds, SystemMode mode,
                        float toRH, float toTemp, bool toValid,
                        float fromRH, float fromTemp, bool fromValid,
                        bool pumpOn, bool heaterOn) {
    (void)seconds; (void)mode; (void)toRH; (void)toTemp; (void)toValid;
    (void)fromRH; (void)fromTemp; (void)fromValid;
    (void)pumpOn; (void)heaterOn;
    if (!_ready) return;
}

void Logger::logModeChange(SystemMode oldMode, SystemMode newMode) {
    (void)oldMode; (void)newMode;
    if (!_ready) return;
}

void Logger::logStop(uint8_t code, const char* name, SystemMode mode, const char* message) {
    StopEvent& event = _recent[_recentNext];
    event.epoch = (uint32_t)systemClockEpoch();
    event.code = code;
    event.mode = (uint8_t)mode;
    _recentNext = (uint8_t)((_recentNext + 1) % RECENT_STOPS);
    if (_recentCount < RECENT_STOPS) _recentCount++;
    (void)name; (void)message;
    if (!_ready) return;
    // SD row goes here: epoch,code,name,mode,message
}

uint8_t Logger::recentStops(StopEvent* out, uint8_t max) const {
    uint8_t copied = 0;
    for (uint8_t i = 0; i < _recentCount && copied < max; ++i) {
        const uint8_t index = (uint8_t)((_recentNext + RECENT_STOPS - 1 - i) % RECENT_STOPS);
        out[copied++] = _recent[index];
    }
    return copied;
}

// Global instance
Logger logger;
