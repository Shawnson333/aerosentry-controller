/* ========================================================================
   RELAY DRIVER — XL9535 8-channel I2C GPIO expander

   Port 0, active-high:
     bit 0 / K1 = AUX, switched 12 V output
     bit 1 / K2 = Pump
     bit 2 / K3 = Heater mat
     bit 3 / K4 = Case fan
     bit 4 / K5 = To-Engine valve
     bit 5 / K6 = From-Engine valve
     bit 6 / K7 = AUX, 120 V AC
     bit 7 / K8 = AUX, 120 V AC
   All eight bits are allowed by RELAY_ALLOWED_MASK. The aux channels are
   not driven by any cycle. The register sequence matches the reference
   driver github.com/mcauser/micropython-xl9535-kxv5-relay.

   NEVER read-modify-write the output register (2026-11-24, bench proof).
   The Waveshare's CH422G answers every address from 0x20 to 0x27, which is
   the whole XL9535 range, so a read at 0x21 returns the XL9535's bits ANDed
   with the CH422G's (0x26 0xFD on the bench): bits 0, 3, 4, 6 and 7 of
   port 0 always read 0. Writes are clean. This driver keeps the state in
   _state and only writes. A driver that read the port before each write
   would keep clearing K1 (bit 0), which matches the "K1 does not latch"
   symptom seen on three relay boards.
   Port 1 is unused and always written off.
   ======================================================================== */

#include "relay.h"
#include <Wire.h>

RelayDriver::RelayDriver()
    : _addr(XL9535_ADDR), _state(0x00), _initialized(false),
      _testActive(false), _testStartMs(0), _testMs(0), _testPort0(0), _testPort1(0) {}

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
    Serial.println(ok ? F("XL9535 outputs OFF (8 channels enabled)") : F("XL9535 init write failed"));
    return ok;
#endif
}

void RelayDriver::setChannel(uint8_t channel, bool on) {
    if (!_initialized) return;
    if (channel > 7) return;
    const uint8_t bit = (uint8_t)(1u << channel);
    if ((bit & RELAY_ALLOWED_MASK) == 0) return;

    if (on) {
        _state |= bit;
    } else {
        _state &= (uint8_t)~bit;
    }
    applyState();
}

void RelayDriver::pumpOn()    { setChannel(RELAY_BIT_PUMP,   true); }
void RelayDriver::pumpOff()   { setChannel(RELAY_BIT_PUMP,   false); }
void RelayDriver::heaterOn()  { setChannel(RELAY_BIT_HEATER, true); }
void RelayDriver::heaterOff() { setChannel(RELAY_BIT_HEATER, false); }
void RelayDriver::fanOn()     { setChannel(RELAY_BIT_FAN,    true); }
void RelayDriver::fanOff()    { setChannel(RELAY_BIT_FAN,    false); }
void RelayDriver::valveToOn()   { setChannel(RELAY_BIT_VALVE_TO, true); }
void RelayDriver::valveToOff()  { setChannel(RELAY_BIT_VALVE_TO, false); }
void RelayDriver::valveFromOn() { setChannel(RELAY_BIT_VALVE_FROM, true); }
void RelayDriver::valveFromOff(){ setChannel(RELAY_BIT_VALVE_FROM, false); }

void RelayDriver::aux12vOn()  { setChannel(RELAY_BIT_AUX_12V, true); }
void RelayDriver::aux12vOff() { setChannel(RELAY_BIT_AUX_12V, false); }
void RelayDriver::auxAc1On()  { setChannel(RELAY_BIT_AUX_AC1, true); }
void RelayDriver::auxAc1Off() { setChannel(RELAY_BIT_AUX_AC1, false); }
void RelayDriver::auxAc2On()  { setChannel(RELAY_BIT_AUX_AC2, true); }
void RelayDriver::auxAc2Off() { setChannel(RELAY_BIT_AUX_AC2, false); }

void RelayDriver::allOff() {
    if (!_initialized) return;
    _testActive = false;
    _state = 0x00;
    applyState();
}

bool RelayDriver::testPulseStart(uint8_t port, uint8_t bit, uint32_t ms) {
    if (!_initialized || port > 1 || bit > 7 || _testActive) return false;
    const uint8_t mask = (uint8_t)(1u << bit);
    _testActive = true;
    _testStartMs = millis();
    _testMs = ms;
    _testPort0 = port == 0 ? mask : 0x00;
    _testPort1 = port == 1 ? mask : 0x00;
    Serial.printf("Relay test: P%u.%u high for %lu ms\n", port, bit, (unsigned long)ms);
    writePair(0x02, port == 0 ? mask : 0x00, port == 1 ? mask : 0x00);
    return true;
}

void RelayDriver::update() {
    if (!_testActive || millis() - _testStartMs < _testMs) return;
    _testActive = false;
    applyState();  // back to the cycle outputs (all off while idle)
    Serial.println(F("Relay test: pulse ended"));
}

void RelayDriver::scanBus(char *out, size_t size) {
    if (out == nullptr || size == 0) return;
    bool present[0x78] = {};
    for (uint8_t address = 0x08; address < 0x78; ++address) {
        Wire.beginTransmission(address);
        present[address] = Wire.endTransmission() == 0;
    }
    size_t used = 0;
    out[0] = '\0';
    for (uint8_t address = 0x08; address < 0x78; ++address) {
        if (!present[address]) continue;
        uint8_t last = address;
        while (last + 1 < 0x78 && present[last + 1]) last++;
        const int written = last > address
            ? snprintf(out + used, size - used, "%s0x%02X-0x%02X", used ? " " : "", address, last)
            : snprintf(out + used, size - used, "%s0x%02X", used ? " " : "", address);
        if (written < 0 || (size_t)written >= size - used) break;
        used += (size_t)written;
        address = last;
    }
    if (used == 0) snprintf(out, size, "none");
}

bool RelayDriver::readPairTrue(uint8_t reg, uint8_t &port0, uint8_t &port1) {
    uint8_t a0 = 0, a1 = 0, b1 = 0, b0 = 0;
    const uint8_t base = (uint8_t)(reg & 0xFE);
    if (!readPair(base, a0, a1)) return false;             // port 0 first, port 1 second
    if (!readPair((uint8_t)(base | 0x01), b1, b0)) return false;  // port 1 first, port 0 second
    port0 = (uint8_t)(a0 | b0);
    port1 = (uint8_t)(a1 | b1);
    return true;
}

bool RelayDriver::readbackTest(char *report, size_t size) {
    static const uint8_t patterns[3][2] = {{0xA5, 0x5A}, {0xFF, 0xFF}, {0x00, 0x00}};
    bool clean = true;
    size_t used = 0;
    if (report != nullptr && size > 0) report[0] = '\0';
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t got0 = 0, got1 = 0, true0 = 0, true1 = 0;
        const bool wrote = writePairChecked(0x04, patterns[i][0], patterns[i][1]);
        const bool read = readPair(0x04, got0, got1) && readPairTrue(0x04, true0, true1);
        const bool match = wrote && read && true0 == patterns[i][0] && true1 == patterns[i][1];
        if (!match) clean = false;
        if (report != nullptr && used < size) {
            const int written = snprintf(report + used, size - used,
                                         "%swrote %02X %02X read %02X %02X recovered %02X %02X",
                                         i ? ", " : "", patterns[i][0], patterns[i][1], got0, got1,
                                         true0, true1);
            if (written > 0) used += (size_t)written;
        }
    }
    writePairChecked(0x04, 0x00, 0x00);  // no polarity inversion, as begin() leaves it
    return clean;
}

bool RelayDriver::readPair(uint8_t reg, uint8_t &port0, uint8_t &port1) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(_addr, (uint8_t)2) != 2) return false;
    port0 = Wire.read();
    port1 = Wire.read();
    return true;
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
    _state &= RELAY_ALLOWED_MASK;
    writePair(0x02, _state, 0x00);
}

// Global instance
RelayDriver relay;