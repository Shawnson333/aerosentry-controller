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

private:
    uint8_t _addr;
    uint8_t _state;   // Bit mask of which relays are ON
    bool _initialized;

    bool writePairChecked(uint8_t reg, uint8_t port0, uint8_t port1);
    void writePair(uint8_t reg, uint8_t port0, uint8_t port1);
    void applyState();
};

extern RelayDriver relay;

#endif // RELAY_H