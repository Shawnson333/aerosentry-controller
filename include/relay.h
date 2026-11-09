#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>
#include "config.h"

/* ========================================================================
   RELAY DRIVER — XL9535 8-channel I2C expander
   Controls pump, heater, fan via physical relay channels 1-3.
   Channels 4-8 reserved for future expansion.
   ======================================================================== */

class RelayDriver {
public:
    RelayDriver();

    /* Initialize I2C and set all relays OFF. Returns true on success. */
    bool begin();

    /* Turn a specific relay channel ON or OFF.
       channel: 0=pump, 1=heater, 2=fan (matches RELAY_BIT_* in config.h) */
    void setChannel(uint8_t channel, bool on);

    /* Convenience helpers */
    void pumpOn();
    void pumpOff();
    void heaterOn();
    void heaterOff();
    void fanOn();
    void fanOff();

    /* Emergency stop: turn everything OFF */
    void allOff();

    /* One-shot reproduction of the two-byte writes that energized K2-K8. */
    void sendOriginalSequence();
    void probe(const char* label, uint8_t pins);

private:
    uint8_t _addr;
    uint8_t _state;   // Bit mask of which relays are ON
    bool _initialized;

    void writePins(uint8_t pins);
    void applyState();
};

extern RelayDriver relay;

#endif // RELAY_H