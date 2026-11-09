#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

#include <Arduino.h>

enum class ClockField {
    HOUR,
    MINUTE,
    YEAR,
    MONTH,
    DAY
};

// Software clock: build time at boot, then millis() elapsed time.
void systemClockBegin();
void systemClockAdjust(ClockField field, int delta);
int systemClockGet(ClockField field);
void systemClockFormat(char *buffer, size_t size);

#endif // SYSTEM_CLOCK_H