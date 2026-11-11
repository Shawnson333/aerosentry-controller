#include "system_clock.h"
#include "config.h"
#include <Preferences.h>
#include <Wire.h>
#include <time.h>

static const uint8_t DS3231_TIME_REG = 0x00;
static const uint8_t DS3231_CONTROL_REG = 0x0E;
static const uint8_t DS3231_STATUS_REG = 0x0F;
static const uint8_t DS3231_OSF = 0x80;
static const uint8_t DS3231_EOSC = 0x80;
static const time_t MIN_BOOT_RTC_EPOCH = 1700000000; // Reject an unset 2000-era clock.

static time_t baseEpoch = 0;
static uint32_t baseMillis = 0;
static bool initialized = false;
static bool rtcPresent = false;
static bool rtcReady = false;
static Preferences clockStore;

static time_t currentEpoch() {
    return baseEpoch + (time_t)((millis() - baseMillis) / 1000UL);
}

static void setBaseEpoch(time_t epoch) {
    baseEpoch = epoch;
    baseMillis = millis();
}

static bool validBcd(uint8_t value) {
    return (value & 0x0F) <= 9 && (value >> 4) <= 9;
}

static uint8_t bcdToBinary(uint8_t value) {
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

static uint8_t binaryToBcd(uint8_t value) {
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool readRtcRegister(uint8_t reg, uint8_t &value) {
    Wire.beginTransmission((uint8_t)DS3231_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)1) != 1) return false;
    value = (uint8_t)Wire.read();
    return true;
}

static bool writeRtcRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission((uint8_t)DS3231_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static int monthNumber(const char *name) {
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *position = strstr(months, name);
    return position == nullptr ? 0 : (int)((position - months) / 3);
}

static bool readRtc(time_t &epoch) {
    uint8_t data[7] = {};
    Wire.beginTransmission((uint8_t)DS3231_ADDR);
    Wire.write(DS3231_TIME_REG);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)sizeof(data)) != sizeof(data)) {
        while (Wire.available()) (void)Wire.read();
        return false;
    }
    for (uint8_t &value : data) value = (uint8_t)Wire.read();

    const uint8_t secondRaw = data[0] & 0x7F;
    const uint8_t minuteRaw = data[1] & 0x7F;
    const bool hour12 = (data[2] & 0x40) != 0;
    const uint8_t hourRaw = data[2] & (hour12 ? 0x1F : 0x3F);
    const uint8_t dayRaw = data[4] & 0x3F;
    const uint8_t monthRaw = data[5] & 0x1F;
    if (!validBcd(secondRaw) || !validBcd(minuteRaw) || !validBcd(hourRaw) ||
        !validBcd(dayRaw) || !validBcd(monthRaw) || !validBcd(data[6])) {
        return false;
    }

    const uint8_t second = bcdToBinary(secondRaw);
    const uint8_t minute = bcdToBinary(minuteRaw);
    int hour = bcdToBinary(hourRaw);
    if (hour12) {
        if (hour < 1 || hour > 12) return false;
        hour %= 12;
        if ((data[2] & 0x20) != 0) hour += 12; // PM; 12 AM becomes 0.
    }

    const uint8_t day = bcdToBinary(dayRaw);
    const uint8_t month = bcdToBinary(monthRaw);
    const int year = 2000 + ((data[5] & 0x80) != 0 ? 100 : 0) + bcdToBinary(data[6]);
    if (second > 59 || minute > 59 || hour > 23 ||
        day < 1 || day > 31 || month < 1 || month > 12) {
        return false;
    }

    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    value.tm_isdst = -1;
    epoch = mktime(&value);
    if (epoch == (time_t)-1) return false;

    // mktime normalizes impossible dates such as February 31; reject those.
    struct tm checked = {};
    if (localtime_r(&epoch, &checked) == nullptr) return false;
    return checked.tm_year == year - 1900 && checked.tm_mon == month - 1 &&
           checked.tm_mday == day && checked.tm_hour == hour &&
           checked.tm_min == minute && checked.tm_sec == second;
}

static bool writeRtc(time_t epoch) {
    struct tm value = {};
    if (localtime_r(&epoch, &value) == nullptr) return false;
    const int year = value.tm_year + 1900;
    if (year < 2000 || year > 2199) return false;

    // EOSC is active-low. Preserve every other control bit.
    uint8_t control = 0;
    if (!readRtcRegister(DS3231_CONTROL_REG, control)) return false;
    if ((control & DS3231_EOSC) != 0 &&
        !writeRtcRegister(DS3231_CONTROL_REG, control & (uint8_t)~DS3231_EOSC)) {
        return false;
    }

    const bool century = year >= 2100;
    Wire.beginTransmission((uint8_t)DS3231_ADDR);
    Wire.write(DS3231_TIME_REG);
    Wire.write(binaryToBcd((uint8_t)value.tm_sec));
    Wire.write(binaryToBcd((uint8_t)value.tm_min));
    Wire.write(binaryToBcd((uint8_t)value.tm_hour)); // Always use 24-hour mode.
    Wire.write(binaryToBcd((uint8_t)value.tm_wday + 1));
    Wire.write(binaryToBcd((uint8_t)value.tm_mday));
    Wire.write(binaryToBcd((uint8_t)value.tm_mon + 1) | (century ? 0x80 : 0x00));
    Wire.write(binaryToBcd((uint8_t)(year % 100)));
    if (Wire.endTransmission() != 0) return false;

    // Clearing OSF says that the newly written time is now trusted.
    uint8_t status = 0;
    if (!readRtcRegister(DS3231_STATUS_REG, status)) return false;
    return writeRtcRegister(DS3231_STATUS_REG, status & (uint8_t)~DS3231_OSF);
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

    setBaseEpoch(mktime(&buildTime));
    clockStore.begin("aerosentry", false);
    const uint32_t saved = clockStore.getUInt("clock", 0);
    if (saved > 1700000000UL) setBaseEpoch((time_t)saved);

    Wire.beginTransmission((uint8_t)DS3231_ADDR);
    rtcPresent = Wire.endTransmission() == 0;
    if (rtcPresent) {
        time_t rtcEpoch = 0;
        uint8_t status = 0;
        const bool validTime = readRtc(rtcEpoch);
        const bool running = validTime && readRtcRegister(DS3231_STATUS_REG, status) &&
                             (status & DS3231_OSF) == 0;
        if (running && rtcEpoch > MIN_BOOT_RTC_EPOCH) {
            setBaseEpoch(rtcEpoch);
            rtcReady = true;
            Serial.printf("DS3231M RTC ready at 0x%02X\n", DS3231_ADDR);
        } else {
            Serial.println(F("DS3231M found, but its time is invalid or stopped; using fallback until Set Clock is used"));
        }
    } else {
        Serial.println(F("DS3231M not found at 0x68; using saved software clock"));
    }
    initialized = true;
}

time_t systemClockEpoch() {
    return currentEpoch();
}

void systemClockSave() {
    if (!initialized) return;
    static uint32_t lastSave = 0;
    if (millis() - lastSave < 60000UL) return;
    lastSave = millis();

    if (rtcReady) {
        time_t rtcEpoch = 0;
        uint8_t status = 0;
        if (readRtc(rtcEpoch) && readRtcRegister(DS3231_STATUS_REG, status) &&
            (status & DS3231_OSF) == 0) {
            setBaseEpoch(rtcEpoch);
            return;
        }
        Serial.println(F("DS3231M refresh failed; keeping the software fallback current"));
    }
    const time_t fallbackEpoch = currentEpoch();
    clockStore.putUInt("clock", (uint32_t)fallbackEpoch);
    setBaseEpoch(fallbackEpoch); // Re-anchor periodically so millis() rollover cannot accumulate.
}

void systemClockSavePurge() {
    if (!initialized) return;
    const uint32_t now = (uint32_t)currentEpoch();
    clockStore.putUInt("purge", now);
    clockStore.putUInt("clock", now);
}

time_t systemClockSavedPurge() {
    if (!initialized) return 0;
    return (time_t)clockStore.getUInt("purge", 0);
}

void systemClockCommit() {
    if (!initialized) return;
    const time_t epoch = currentEpoch();
    if (rtcPresent) {
        if (writeRtc(epoch)) {
            if (!rtcReady) Serial.println(F("DS3231M time set; battery-backed clock enabled"));
            rtcReady = true;
        } else {
            rtcReady = false;
            Serial.println(F("DS3231M write failed; time saved to the software fallback"));
        }
    }
    if (!rtcReady) clockStore.putUInt("clock", (uint32_t)epoch);
}

void systemClockAdjust(ClockField field, int delta) {
    time_t now = currentEpoch();
    struct tm adjusted = {};
    if (localtime_r(&now, &adjusted) == nullptr) return;

    switch (field) {
        case ClockField::HOUR:   adjusted.tm_hour += delta; break;
        case ClockField::MINUTE: adjusted.tm_min += delta; break;
        case ClockField::YEAR:   adjusted.tm_year += delta; break;
        case ClockField::MONTH:  adjusted.tm_mon += delta; break;
        case ClockField::DAY:    adjusted.tm_mday += delta; break;
    }

    adjusted.tm_isdst = -1;
    const time_t adjustedEpoch = mktime(&adjusted);
    if (adjustedEpoch == (time_t)-1) return;
    setBaseEpoch(adjustedEpoch);
    systemClockCommit();
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