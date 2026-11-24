/* Host-test stand-in for the Arduino core. Only what the controller logic
   (statemachine, sensors, relay, radio_link, logging) uses. Time is
   simulated: millis() returns hostNowMs, which the test advances. */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <string>

typedef bool boolean;

extern uint32_t hostNowMs;
inline uint32_t millis() { return hostNowMs; }
inline void delay(uint32_t ms) { hostNowMs += ms; }
float temperatureRead();

#define F(x) (x)
#define IRAM_ATTR

/* FreeRTOS critical sections: the host test is single-threaded. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

/* Serial writes into a log the tests can search. Set echo to print it. */
class HostSerial {
public:
    std::string log;
    bool echo = false;
    void begin(unsigned long) {}
    size_t write(const char* text);
    size_t print(const char* text) { return write(text); }
    size_t print(char c) { char s[2] = {c, 0}; return write(s); }
    size_t print(int v) { return printf("%d", v); }
    size_t print(unsigned v) { return printf("%u", v); }
    size_t print(long v) { return printf("%ld", v); }
    size_t print(unsigned long v) { return printf("%lu", v); }
    size_t println() { return write("\n"); }
    size_t println(const char* text) { return write(text) + write("\n"); }
    size_t println(int v) { return print(v) + write("\n"); }
    size_t println(unsigned v) { return print(v) + write("\n"); }
    size_t println(long v) { return print(v) + write("\n"); }
    size_t println(unsigned long v) { return print(v) + write("\n"); }
    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));
};
extern HostSerial Serial;
