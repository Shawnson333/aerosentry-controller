#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>
#include "config.h"

/* ========================================================================
   RELAY DRIVER — XL9535 8-channel I2C expander
   K2 pump, K3 heater, K4 case fan, K5/K6 the two recharge valves.
   K1, K7, K8 are aux outputs and are enabled in the driver mask, but no
   cycle drives them. The bench relay board has a faulty K1 channel, so
   leave bit 0 alone until the custom carrier board replaces that board.
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
    void valveToOn();
    void valveToOff();
    void valveFromOn();
    void valveFromOff();

    /* Aux outputs. Not driven by any cycle - manual or future use.
       K1 is a switched 12 V output. K7 and K8 are 120 V AC outputs, so
       treat those two as mains switching. */
    void aux12vOn();
    void aux12vOff();
    void auxAc1On();
    void auxAc1Off();
    void auxAc2On();
    void auxAc2Off();

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