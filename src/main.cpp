#include <Arduino.h>
#include <Wire.h>

#define XL_ADDR     0x20
#define REG_OUTPUT  0x02
#define REG_CONFIG  0x06

// Channel Mappings based on verified hardware response:
// A0 -> Unused / Unrouted
// A1 -> Pump
// A2 -> Heater
// A3 -> Fan

uint8_t currentRelayState = 0x00;

void xlWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(XL_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

void setRelay(uint8_t bit, bool on) {
    if (on) {
        currentRelayState |= (1 << bit);
    } else {
        currentRelayState &= ~(1 << bit);
    }
    xlWrite(REG_OUTPUT, currentRelayState);
    Serial.printf("Relay State Register: 0x%02X\n", currentRelayState);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- AeroSentry Core Controller Initializing ---");

    // Initialize I2C for the expander
    Wire.begin(8, 9);

    // Set Port 0 configuration to outputs (0x00)
    xlWrite(REG_CONFIG, 0x00);
    
    // Start with all relays OFF
    currentRelayState = 0x00;
    xlWrite(REG_OUTPUT, currentRelayState);

    Serial.println("System ready using verified channels A1 (Pump), A2 (Heater), A3 (Fan).");
}

void loop() {
    // Example test sequence for your 3 active channels:
    
    Serial.println("Turning Pump (A1) ON...");
    setRelay(1, true);
    delay(3000);

    Serial.println("Turning Heater (A2) ON...");
    setRelay(2, true);
    delay(3000);

    Serial.println("Turning Fan (A3) ON...");
    setRelay(3, true);
    delay(3000);

    Serial.println("Shutting all channels OFF...");
    currentRelayState = 0x00;
    xlWrite(REG_OUTPUT, currentRelayState);
    delay(5000);
}