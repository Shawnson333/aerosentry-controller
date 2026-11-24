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

    /* Emergency stop: turn everything OFF (also ends a relay test pulse) */
    void allOff();

    /* Bench diagnostic for the K1 investigation (2026-11-24). Drives ONE pin
       of either port (port 0 or 1, bit 0-7) high for ms, then restores the
       cycle outputs. Bypasses RELAY_ALLOWED_MASK on purpose so P0.0 (K1) and
       every P1 pin can be checked. The caller must only use it while idle;
       starts are refused while a pulse is active. */
    bool testPulseStart(uint8_t port, uint8_t bit, uint32_t ms);
    bool testActive() const { return _testActive; }
    /* Ends a test pulse when its time is up. Call every loop. */
    void update();

    /* Read one register pair: 0x00 input (actual pin levels), 0x02 output,
       0x04 polarity, 0x06 config (0 = output). */
    bool readPair(uint8_t reg, uint8_t &port0, uint8_t &port1);

    /* List the 7-bit addresses that ACK on the shared I2C bus, e.g.
       "0x21 0x24 0x38 0x5D 0x68". Takes about 30 ms. */
    static void scanBus(char *out, size_t size);

private:
    uint8_t _addr;
    uint8_t _state;   // Bit mask of which relays are ON
    bool _initialized;
    bool _testActive;
    uint32_t _testStartMs;
    uint32_t _testMs;

    bool writePairChecked(uint8_t reg, uint8_t port0, uint8_t port1);
    void writePair(uint8_t reg, uint8_t port0, uint8_t port1);
    void applyState();
};

extern RelayDriver relay;

#endif // RELAY_H