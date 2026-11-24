/* ========================================================================
   LOGGING — SD card /hangar.csv

   DISABLED 2026-11-20 on user request. The Waveshare Type B has no TF
   slot on the default SD pins; SD.begin() fails with f_mount (3), and
   the open/print/close in the old draft stalled the LVGL flush thread.
   All calls are no-ops until the correct pins / card are wired.

   Format (CSV, when re-enabled):
     sec,mode,toRH,fromRH,pump,heater
   ======================================================================== */

#include "logging.h"

Logger::Logger() : _ready(false) {}

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

// Global instance
Logger logger;