/* Heltec V4.3 link. USB serial stands in for the future home-station radio.
   Commands are forwarded on ESP-NOW. This board does not drive relays. */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "link_beacon.h"
#include "link_packet.h"
#include "sensor_packet.h"

static bool radioReady = false;
static uint32_t beaconNumber = 0;
static char line[96];
static size_t lineLen = 0;

static bool radioBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_set_channel(AEROSENTRY_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    if (esp_now_init() != ESP_OK) return false;
    esp_now_peer_info_t peer = {};
    for (int i = 0; i < 6; ++i) peer.peer_addr[i] = 0xFF;
    peer.channel = AEROSENTRY_ESPNOW_CHANNEL;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

static const char *modeName(uint8_t mode) {
    switch (mode) {
        case 0: return "Idle";
        case 1: return "Purge";
        case 2: return "Dryer Manual";
        case 3: return "Dryer Auto";
        case 4: return "Recharge";
        default: return "Unknown";
    }
}

static const char *commandName(uint8_t command) {
    switch (command) {
        case REMOTE_PURGE: return "PURGE";
        case REMOTE_DRYER_MANUAL: return "MANUAL";
        case REMOTE_DRYER_AUTO: return "AUTO";
        case REMOTE_RECHARGE: return "RECHARGE";
        case REMOTE_STOP: return "STOP";
        default: return "UNKNOWN";
    }
}

static void printTenths(int16_t value) {
    if (value < 0) {
        Serial.print('-');
        return;
    }
    Serial.printf("%d.%d", value / 10, value % 10);
}

static void printCenti(int16_t value) {
    if (value < 0) {
        Serial.print('-');
        return;
    }
    Serial.printf("%d.%02d", value / 100, value % 100);
}

static void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    (void)mac;
    if (data == nullptr || len < 2 || data[0] != AEROSENTRY_LINK_MAGIC) return;
    if (data[1] == LINK_KIND_ACK && len == (int)sizeof(LinkAck)) {
        LinkAck ack;
        memcpy(&ack, data, sizeof(ack));
        ack.text[sizeof(ack.text) - 1] = '\0';
        Serial.printf("ACK seq=%u cmd=%s accepted=%u text=%s\n",
                      ack.sequence, commandName(ack.command), ack.accepted, ack.text);
        return;
    }
    if (data[1] == LINK_KIND_STATUS && len == (int)sizeof(LinkStatus)) {
        LinkStatus status;
        memcpy(&status, data, sizeof(status));
        Serial.printf("STATUS mode=%s running=%u relays=%u toValid=%u fromValid=%u chamberValid=%u toRh=",
                      modeName(status.mode),
                      (status.flags & LINK_FLAG_RUNNING) ? 1 : 0,
                      (status.flags & LINK_FLAG_RELAYS) ? 1 : 0,
                      (status.flags & LINK_FLAG_TO) ? 1 : 0,
                      (status.flags & LINK_FLAG_FROM) ? 1 : 0,
                      (status.flags & LINK_FLAG_CHAMBER) ? 1 : 0);
        printCenti(status.toRhCenti);
        Serial.print(F(" fromRh="));
        printCenti(status.fromRhCenti);
        Serial.print(F(" chamberF="));
        printTenths(status.chamberFTimes10);
        Serial.print(F(" chipC="));
        printTenths(status.chipCTimes10);
        Serial.printf(" pump=%u heat=%u fan=%u\n",
                      (status.flags & LINK_FLAG_PUMP) ? 1 : 0,
                      (status.flags & LINK_FLAG_HEATER) ? 1 : 0,
                      (status.flags & LINK_FLAG_FAN) ? 1 : 0);
    }
}

static uint8_t commandCode(const char *name) {
    if (strcmp(name, "PURGE") == 0) return REMOTE_PURGE;
    if (strcmp(name, "MANUAL") == 0) return REMOTE_DRYER_MANUAL;
    if (strcmp(name, "AUTO") == 0) return REMOTE_DRYER_AUTO;
    if (strcmp(name, "RECHARGE") == 0) return REMOTE_RECHARGE;
    if (strcmp(name, "STOP") == 0) return REMOTE_STOP;
    return 0;
}

static void handleLine() {
    unsigned seq = 0;
    char name[16] = {};
    unsigned onMin = 3;
    unsigned offHr = 1;
    unsigned target = 20;
    if (sscanf(line, "CMD %u %15s %u %u %u", &seq, name, &onMin, &offHr, &target) < 2) return;
    const uint8_t code = commandCode(name);
    if (code == 0 || !radioReady) {
        Serial.printf("ACK seq=%u cmd=%s accepted=0 text=Command not sent.\n", seq, name);
        return;
    }
    LinkCommand command = {};
    command.magic = AEROSENTRY_LINK_MAGIC;
    command.kind = LINK_KIND_COMMAND;
    command.command = code;
    command.dryerOnMinutes = (uint8_t)onMin;
    command.dryerOffHours = (uint8_t)offHr;
    command.dryerTargetRH = (uint8_t)target;
    command.sequence = (uint16_t)seq;
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_send(broadcast, (const uint8_t *)&command, sizeof(command)) != ESP_OK) {
        Serial.printf("ACK seq=%u cmd=%s accepted=0 text=Radio send failed.\n", seq, name);
    }
}

static void readSerial() {
    while (Serial.available() > 0) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            line[lineLen] = '\0';
            if (lineLen > 0) handleLine();
            lineLen = 0;
            continue;
        }
        if (lineLen + 1 < sizeof(line)) line[lineLen++] = c;
    }
}

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println(F("\n=== AeroSentry Heltec V4.3 link ==="));
    Serial.println(F("USB stands in for the home station. No relays on this board."));
    radioReady = radioBegin();
    if (radioReady) esp_now_register_recv_cb(onReceive);
    Serial.printf("Channel %d  MAC %s  %s\n",
                  AEROSENTRY_ESPNOW_CHANNEL,
                  WiFi.macAddress().c_str(),
                  radioReady ? "ready" : "FAILED");
}

void loop() {
    readSerial();
    if (!radioReady) {
        delay(200);
        return;
    }
    static uint32_t lastBeacon = 0;
    if (millis() - lastBeacon >= 1000UL) {
        lastBeacon = millis();
        LinkBeacon beacon = {};
        beacon.magic = AEROSENTRY_BEACON_MAGIC;
        beacon.kind = AEROSENTRY_BEACON_KIND;
        beacon.sequence = beaconNumber++;
        const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        const esp_err_t result = esp_now_send(broadcast, (const uint8_t *)&beacon, sizeof(beacon));
        Serial.printf("sent beacon %lu %s\n",
                      (unsigned long)beacon.sequence,
                      result == ESP_OK ? "ok" : "failed");
    }
}

