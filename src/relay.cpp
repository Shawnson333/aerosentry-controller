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

    /* XL9535/PCA9535 protocol. Relays are active-high on this board.
       Set both output ports off before making the pins outputs. */
    bool ok = true;
    ok = writePairChecked(0x04, 0x00, 0x00) && ok;  // No polarity inversion.
    ok = writePairChecked(0x02, 0x00, 0x00) && ok;  // Both output ports OFF.
    ok = writePairChecked(0x06, 0x00, 0x00) && ok;  // Both ports outputs.
    _state = 0x00;
    _initialized = ok;
    Serial.println(ok ? F("XL9535 both output ports OFF") : F("XL9535 init write failed"));
    return ok;
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

bool RelayDriver::writePairChecked(uint8_t reg, uint8_t port0, uint8_t port1) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(port0);
    Wire.write(port1);
    const uint8_t result = Wire.endTransmission();
    Serial.printf("XL9535 reg 0x%02X <= 0x%02X 0x%02X result %u\n",
                  reg, port0, port1, result);
    return result == 0;
}

void RelayDriver::writePair(uint8_t reg, uint8_t port0, uint8_t port1) {
    writePairChecked(reg, port0, port1);
}

void RelayDriver::applyState() {
    writePair(0x02, _state, 0x00);
}

// Global instance
RelayDriver relay;