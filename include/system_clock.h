#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

#include <Arduino.h>
#include <time.h>

enum class ClockField {
    HOUR,
    MINUTE,
    YEAR,
    MONTH,
    DAY
};

// Software clock. Starts from build time, then advances with millis().
// A saved time is restored after reboot, but time spent powered off is not counted yet.
void systemClockBegin();
void systemClockSave();
time_t systemClockEpoch();
void systemClockSavePurge();
time_t systemClockSavedPurge();
void systemClockAdjust(ClockField field, int delta);
int systemClockGet(ClockField field);
void systemClockFormat(char *buffer, size_t size);

#endif // SYSTEM_CLOCK_H