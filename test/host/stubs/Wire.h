/* Host-test I2C bus. Emulates the XL9535 at XL9535_ADDR register by register
   (input, output, polarity, config; two-byte auto-increment), so the real
   relay driver can be tested at the register level. */
#pragma once

#include <Arduino.h>

class TwoWire {
public:
    void begin(int sda = -1, int scl = -1) { (void)sda; (void)scl; }
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t address);
    size_t write(uint8_t value);
    uint8_t endTransmission(bool sendStop = true);
    uint8_t requestFrom(uint8_t address, uint8_t count);
    int available();
    int read();
};
extern TwoWire Wire;

/* XL9535 register file: 0/1 input, 2/3 output, 4/5 polarity, 6/7 config.
   Input reads back the output level of pins configured as outputs. */
extern uint8_t hostXl9535[8];
extern bool hostXl9535Present;
uint8_t hostRelayPort0();
