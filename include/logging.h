#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include "config.h"

// Forward declare SystemMode (defined in statemachine.h)
enum class SystemMode;

/* ========================================================================
   LOGGING — SD card /hangar.csv
   Stubbed for now; hardware will be added when SD is wired up.
   Stop events are kept in RAM (the last few) so the Admin page can show
   them, and every stop goes through logStop() so the SD log gets the same
   codes as the screen and the serial log. Codes: docs/ERROR_CODES.md.
   ======================================================================== */

struct StopEvent {
    uint32_t epoch;   // systemClockEpoch() when the cycle ended
    uint8_t code;     // StopReason number, shown as "S09"
    uint8_t mode;     // SystemMode number of the cycle that ended
};

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

    /* Log why a cycle ended. code is the StopReason number, name its stable
       name, message the on-screen text (may be nullptr for a user stop). */
    void logStop(uint8_t code, const char* name, SystemMode mode, const char* message);

    /* Copy up to max recent stop events, newest first. Returns the count. */
    uint8_t recentStops(StopEvent* out, uint8_t max) const;

    /* Is the logger ready? */
    bool ready() const { return _ready; }

private:
    static const uint8_t RECENT_STOPS = 6;
    bool _ready;
    StopEvent _recent[RECENT_STOPS];
    uint8_t _recentCount;
    uint8_t _recentNext;
};

extern Logger logger;

#endif // LOGGING_H
