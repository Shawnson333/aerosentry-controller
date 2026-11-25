/* Fakes behind the host-test stubs: simulated clock, captured serial log,
   XL9535 register emulation, ESP-NOW capture, clock and run-state stand-ins. */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include "config.h"
#include "system_clock.h"
#include "run_state.h"
#include "statemachine.h"
#include "probe_heat.h"
#include <vector>

uint32_t hostNowMs = 0;
HostSerial Serial;
TwoWire Wire;
HostWiFi WiFi;

float temperatureRead() { return 40.0f; }

size_t HostSerial::write(const char* text) {
    if (text == nullptr) return 0;
    log += text;
    if (echo) fputs(text, stdout);
    return strlen(text);
}

size_t HostSerial::printf(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return write(buf);
}

/* ---------- XL9535 ---------- */
uint8_t hostXl9535[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};
bool hostXl9535Present = true;
bool hostCh422Overlap = true;
uint8_t hostCh422Answer[2] = {0x26, 0xFD};
static uint8_t wireAddress = 0;
static uint8_t wireTx[16];
static size_t wireTxLen = 0;
static uint8_t wireRegPointer = 0;
static uint8_t wireRx[16];
static size_t wireRxLen = 0;
static size_t wireRxPos = 0;

uint8_t hostRelayPort0() {
    // Output pins drive the register value; input pins are not driven.
    return (uint8_t)(hostXl9535[2] & (uint8_t)~hostXl9535[6]);
}

void TwoWire::beginTransmission(uint8_t address) {
    wireAddress = address;
    wireTxLen = 0;
}

size_t TwoWire::write(uint8_t value) {
    if (wireTxLen < sizeof(wireTx)) wireTx[wireTxLen++] = value;
    return 1;
}

uint8_t TwoWire::endTransmission(bool) {
    if (wireAddress != XL9535_ADDR || !hostXl9535Present) return 2;  // NACK
    if (wireTxLen == 0) return 0;
    wireRegPointer = wireTx[0] & 0x07;
    uint8_t reg = wireRegPointer;
    for (size_t i = 1; i < wireTxLen; ++i) {
        if (reg >= 2) hostXl9535[reg] = wireTx[i];  // input registers are read-only
        reg = (uint8_t)((reg & 0x06) | ((reg + 1) & 0x01));  // pairs auto-increment
    }
    // Input registers follow the pins configured as outputs.
    hostXl9535[0] = (uint8_t)((hostXl9535[2] & ~hostXl9535[6]) | hostXl9535[6]);
    hostXl9535[1] = (uint8_t)((hostXl9535[3] & ~hostXl9535[7]) | hostXl9535[7]);
    return 0;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t count) {
    wireRxLen = 0;
    wireRxPos = 0;
    if (address != XL9535_ADDR || !hostXl9535Present) return 0;
    uint8_t reg = wireRegPointer;
    for (uint8_t i = 0; i < count && i < sizeof(wireRx); ++i) {
        const uint8_t overlap = hostCh422Overlap ? hostCh422Answer[i % 2] : 0xFF;
        wireRx[wireRxLen++] = (uint8_t)(hostXl9535[reg] & overlap);
        reg = (uint8_t)((reg & 0x06) | ((reg + 1) & 0x01));
    }
    return (uint8_t)wireRxLen;
}

int TwoWire::available() { return (int)(wireRxLen - wireRxPos); }
int TwoWire::read() { return wireRxPos < wireRxLen ? wireRx[wireRxPos++] : -1; }

/* ---------- ESP-NOW ---------- */
esp_now_recv_cb_t hostEspNowReceive = nullptr;
int hostEspNowSends = 0;
uint8_t hostLastSendMac[6] = {};
uint8_t hostLastSend[128] = {};
size_t hostLastSendLen = 0;

esp_err_t esp_now_init() { return ESP_OK; }
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) { hostEspNowReceive = cb; return ESP_OK; }
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *) { return ESP_OK; }
std::vector<ProbeHeatRequest> hostHeatRequests;

esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len) {
    if (len == sizeof(ProbeHeatRequest) && data[0] == AEROSENTRY_HEAT_MAGIC) {
        ProbeHeatRequest request;
        memcpy(&request, data, sizeof(request));
        hostHeatRequests.push_back(request);
    }
    hostEspNowSends++;
    memcpy(hostLastSendMac, mac, 6);
    hostLastSendLen = len < sizeof(hostLastSend) ? len : sizeof(hostLastSend);
    memcpy(hostLastSend, data, hostLastSendLen);
    return ESP_OK;
}

/* ---------- Clock and run state ---------- */
static const time_t HOST_EPOCH_START = 1795000000;
void systemClockBegin() {}
void systemClockSave() {}
time_t systemClockEpoch() { return HOST_EPOCH_START + (time_t)(hostNowMs / 1000UL); }
void systemClockSavePurge() {}
time_t systemClockSavedPurge() { return 0; }
void systemClockCommit() {}
void systemClockAdjust(ClockField, int) {}
int systemClockGet(ClockField) { return 0; }
void systemClockFormat(char *buffer, size_t size) { snprintf(buffer, size, "--"); }

SystemMode hostSavedRunMode = SystemMode::IDLE;
int hostRunStateWrites = 0;
SystemMode runStateBegin() { return SystemMode::IDLE; }
const char* runStateResetCause() { return "Host test restart."; }
void runStateSave(SystemMode mode) {
    if (mode == hostSavedRunMode) return;
    hostSavedRunMode = mode;
    hostRunStateWrites++;
}
