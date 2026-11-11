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

// Battery-backed DS3231M clock with a saved software-clock fallback.
// The clock editor updates the RTC when it is connected.
void systemClockBegin();
void systemClockSave();
time_t systemClockEpoch();
void systemClockSavePurge();
time_t systemClockSavedPurge();
void systemClockCommit();
void systemClockAdjust(ClockField field, int delta);
int systemClockGet(ClockField field);
void systemClockFormat(char *buffer, size_t size);

#endif // SYSTEM_CLOCK_H