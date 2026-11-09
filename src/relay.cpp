/* ========================================================================
   RELAY DRIVER — XL9535 8-channel I2C GPIO expander
   
   Controls physical relay channels:
     Channel 1 (bit 0) = Pump
     Channel 2 (bit 1) = Heater mat
     Channel 3 (bit 2) = Case fan (reserved)
     Channels 4–8 (bits 3–7) = Future expansion
   
   The XL9535 has two 8-bit ports (Port 0 and Port 1).
   We use Port 0 for all 8 relays on this board.
   
   Registers (from XL9535 datasheet):
     0x00 – Port 0 Input
     0x01 – Port 1 Input
     0x02 – Port 0 Output  (controls relay states)
     0x03 – Port 1 Output
     0x06 – Port 0 Configuration (0 = output, 1 = input)
     0x07 – Port 1 Configuration
   ======================================================================== */

#include "relay.h"
#include <Wire.h>

RelayDriver::RelayDriver()
    : _addr(XL9535_ADDR), _state(0x00), _initialized(false) {}

bool RelayDriver::begin() {
#if !RELAY_DRIVER_ENABLED
    _initialized = false;
    return false;
#else
    Wire.beginTransmission(_addr);
    if (Wire.endTransmission() != 0) {
        // Device not responding — wrong address or not connected
        _initialized = false;
        return false;
    }

    /* Start with all relays OFF. Logical bits use 1 = ON; the board itself
       is active-low, so the pin mask is inverted in applyState(). */
    _state = 0x00;
    writeReg(0x06, 0x00);  // Port 0: all outputs. Port 1 stays inputs.
    applyState();

    _initialized = true;
    return true;
#endif
}

void RelayDriver::setChannel(uint8_t channel, bool on) {
    if (!_initialized) return;
    if (channel > 7) return;  // Only 8 channels on Port 0

    if (on) {
        _state |= (1 << channel);   // Set bit = relay ON
    } else {
        _state &= ~(1 << channel);  // Clear bit = relay OFF
    }
    applyState();
}

void RelayDriver::pumpOn()    { setChannel(RELAY_BIT_PUMP,   true); }
void RelayDriver::pumpOff()   { setChannel(RELAY_BIT_PUMP,   false); }
void RelayDriver::heaterOn()  { setChannel(RELAY_BIT_HEATER, true); }
void RelayDriver::heaterOff() { setChannel(RELAY_BIT_HEATER, false); }
void RelayDriver::fanOn()     { setChannel(RELAY_BIT_FAN,    true); }
void RelayDriver::fanOff()    { setChannel(RELAY_BIT_FAN,    false); }

void RelayDriver::allOff() {
    if (!_initialized) return;
    _state = 0x00;
    applyState();
}

uint8_t RelayDriver::getState() const {
    return _state;
}

void RelayDriver::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

void RelayDriver::applyState() {
    uint8_t pins = _state;
#if RELAY_ACTIVE_LOW
    pins = (uint8_t)~pins;
#endif
    writeReg(0x02, pins);  // Port 0 output. K1 is bit 0.
}

// Global instance
RelayDriver relay;