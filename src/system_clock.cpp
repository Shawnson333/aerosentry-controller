#include "system_clock.h"
#include <time.h>

static time_t baseEpoch = 0;
static uint32_t baseMillis = 0;
static bool initialized = false;

static time_t currentEpoch() {
    return baseEpoch + (time_t)((millis() - baseMillis) / 1000UL);
}

static int monthNumber(const char *name) {
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *position = strstr(months, name);
    return position == nullptr ? 0 : (int)((position - months) / 3);
}

void systemClockBegin() {
    if (initialized) return;

    char monthName[4] = {};
    int day = 1, year = 2026, hour = 0, minute = 0, second = 0;
    sscanf(__DATE__, "%3s %d %d", monthName, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    struct tm buildTime = {};
    buildTime.tm_year = year - 1900;
    buildTime.tm_mon = monthNumber(monthName);
    buildTime.tm_mday = day;
    buildTime.tm_hour = hour;
    buildTime.tm_min = minute;
    buildTime.tm_sec = second;
    buildTime.tm_isdst = -1;

    baseEpoch = mktime(&buildTime);
    baseMillis = millis();
    initialized = true;
}

void systemClockAdjust(ClockField field, int delta) {
    time_t now = currentEpoch();
    struct tm adjusted = {};
    localtime_r(&now, &adjusted);

    switch (field) {
        case ClockField::HOUR:   adjusted.tm_hour += delta; break;
        case ClockField::MINUTE: adjusted.tm_min += delta; break;
        case ClockField::YEAR:   adjusted.tm_year += delta; break;
        case ClockField::MONTH:  adjusted.tm_mon += delta; break;
        case ClockField::DAY:    adjusted.tm_mday += delta; break;
    }

    adjusted.tm_isdst = -1;
    baseEpoch = mktime(&adjusted);
    baseMillis = millis();
}

int systemClockGet(ClockField field) {
    time_t now = currentEpoch();
    struct tm value = {};
    localtime_r(&now, &value);
    switch (field) {
        case ClockField::HOUR:   return value.tm_hour;
        case ClockField::MINUTE: return value.tm_min;
        case ClockField::YEAR:   return value.tm_year + 1900;
        case ClockField::MONTH:  return value.tm_mon + 1;
        case ClockField::DAY:    return value.tm_mday;
    }
    return 0;
}

void systemClockFormat(char *buffer, size_t size) {
    time_t now = currentEpoch();
    struct tm value = {};
    localtime_r(&now, &value);
    snprintf(buffer, size, "%02d:%02d  %02d/%02d/%04d",
             value.tm_hour, value.tm_min,
             value.tm_mon + 1, value.tm_mday, value.tm_year + 1900);
}