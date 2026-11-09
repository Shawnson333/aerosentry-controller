#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include "config.h"

// Forward declare SystemMode (defined in statemachine.h)
enum class SystemMode;

/* ========================================================================
   LOGGING — SD card /hangar.csv
   Stubbed for now; hardware will be added when SD is wired up.
   ======================================================================== */

class Logger {
public:
    Logger();

    /* Initialize SD card. Returns true if ready. */
    bool begin();

    /* Log one sensor snapshot while a cycle is running. */
    void logRunning(uint32_t seconds, SystemMode mode,
                    float toRH, float toTemp, bool toValid,
                    float fromRH, float fromTemp, bool fromValid,
                    bool pumpOn, bool heaterOn);

    /* Log a mode change event. */
    void logModeChange(SystemMode oldMode, SystemMode newMode);

    /* Is the logger ready? */
    bool ready() const { return _ready; }

private:
    bool _ready;
};

extern Logger logger;

#endif // LOGGING_H