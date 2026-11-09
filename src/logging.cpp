/* ========================================================================
   LOGGING — SD card /hangar.csv
   
   STUB for now. When the SD card is wired up, replace this with real
   SDFat/Arduino SD code.
   
   Format (CSV):
     sec,mode,toRH,fromRH,pump,heater
   ======================================================================== */

#include "logging.h"
#include <SD.h>

Logger::Logger() : _ready(false) {}

bool Logger::begin() {
    // TODO: Initialize SD card here when wired
    // Example:
    //   if (!SD.begin(SD_CS_PIN)) return false;
    _ready = false;  // Not available yet — stub
    return false;
}

void Logger::logRunning(uint32_t seconds, SystemMode mode,
                        float toRH, float toTemp, bool toValid,
                        float fromRH, float fromTemp, bool fromValid,
                        bool pumpOn, bool heaterOn) {
    (void)seconds;
    (void)mode;
    (void)toRH;
    (void)toTemp;
    (void)toValid;
    (void)fromRH;
    (void)fromTemp;
    (void)fromValid;
    (void)pumpOn;
    (void)heaterOn;
    if (!_ready) return;
    // TODO: Write CSV line to /hangar.csv
}

void Logger::logModeChange(SystemMode oldMode, SystemMode newMode) {
    if (!_ready) return;
    // TODO: Log mode change event
}

// Global instance
Logger logger;