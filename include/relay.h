#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>
#include "config.h"

/* ========================================================================
   RELAY DRIVER — XL9535 8-channel I2C expander
   K2 pump, K3 heater, K4 case fan. K5 AUX is reserved and stays off.
   K1 and K6-K8 are masked off in the driver.
   ======================================================================== */

class RelayDriver {
public:
    RelayDriver();

    /* Initialize I2C and set all relays OFF. Returns true on success. */
    bool begin();
    bool isReady() const { return _initialized; }

    /* channel is a port-0 bit. Bits outside RELAY_ALLOWED_MASK are ignored. */
    void setChannel(uint8_t channel, bool on);

    /* Convenience helpers */
    void pumpOn();
    void pumpOff();
    void heaterOn();
    void heaterOff();
    void fanOn();
    void fanOff();
    void auxOn();
    void auxOff();

    /* Emergency stop: turn everything OFF */
    void allOff();

    /* Diagnostic: write both output ports while identifying relay mapping. */
    void setTestOutputs(uint8_t port0, uint8_t port1);
    void printDiagnostics();
    bool readPair(uint8_t reg, uint8_t &port0, uint8_t &port1);

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