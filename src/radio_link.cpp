/* Heltec command path. A radio command is only a request.
   Starts still go through StateMachine::command(). */

#include "radio_link.h"
#include "config.h"
#include "link_packet.h"
#include "relay.h"
#include "sensors.h"
#include "statemachine.h"
#include <esp_now.h>
#include <string.h>

static portMUX_TYPE linkMux = portMUX_INITIALIZER_UNLOCKED;
static LinkCommand pendingCommand = {};
static volatile bool commandPending = false;
static uint8_t peerMac[6] = {};
static bool peerKnown = false;
static bool peerAdded = false;
static uint16_t statusSequence = 0;
static uint32_t lastStatusMs = 0;

static void copyText(char *dest, size_t size, const char *text) {
    size_t out = 0;
    if (text == nullptr) text = "";
    while (text[0] != '\0' && out + 1 < size) {
        char c = text[0];
        if (c == '\n' || c == '\r') c = ' ';
        dest[out++] = c;
        ++text;
    }
    dest[out] = '\0';
}

static bool ensurePeer() {
    if (!peerKnown) return false;
    if (peerAdded) return true;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, peerMac, 6);
    peer.channel = AEROSENTRY_ESPNOW_CHANNEL;
    peer.encrypt = false;
    const esp_err_t result = esp_now_add_peer(&peer);
    peerAdded = result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
    return peerAdded;
}

static void sendPacket(const uint8_t *data, size_t length) {
    if (!ensurePeer()) return;
    esp_now_send(peerMac, data, length);
}

static void sendStatus() {
    LinkStatus status = {};
    status.magic = AEROSENTRY_LINK_MAGIC;
    status.kind = LINK_KIND_STATUS;
    status.mode = (uint8_t)stateMachine.mode();
    if (stateMachine.pumpOn()) status.flags |= LINK_FLAG_PUMP;
    if (stateMachine.heaterOn()) status.flags |= LINK_FLAG_HEATER;
    if (stateMachine.fanOn()) status.flags |= LINK_FLAG_FAN;
    if (toEngineReady()) status.flags |= LINK_FLAG_TO;
    if (fromEngineReady()) status.flags |= LINK_FLAG_FROM;
    if (chamberReady()) status.flags |= LINK_FLAG_CHAMBER;
    if (stateMachine.isRunning()) status.flags |= LINK_FLAG_RUNNING;
    if (relay.isReady()) status.flags |= LINK_FLAG_RELAYS;
    status.toRhCenti = (status.flags & LINK_FLAG_TO) ? (int16_t)(sensors.toEngineRH * 100.0f) : -1;
    status.fromRhCenti = (status.flags & LINK_FLAG_FROM) ? (int16_t)(sensors.fromEngineRH * 100.0f) : -1;
    if (status.flags & LINK_FLAG_CHAMBER) {
        const float f = sensors.chamberTemp * 9.0f / 5.0f + 32.0f;
        status.chamberFTimes10 = (int16_t)(f * 10.0f);
    } else {
        status.chamberFTimes10 = -1;
    }
    const float chip = stateMachine.chipTempC();
    status.chipCTimes10 = (chip == chip) ? (int16_t)(chip * 10.0f) : -1;
    status.sequence = statusSequence++;
    sendPacket((const uint8_t *)&status, sizeof(status));
    lastStatusMs = millis();
}

static uint8_t clampByte(uint8_t value, uint8_t low, uint8_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void applyCommand(const LinkCommand &command) {
    LinkAck ack = {};
    ack.magic = AEROSENTRY_LINK_MAGIC;
    ack.kind = LINK_KIND_ACK;
    ack.command = command.command;
    ack.sequence = command.sequence;
    ack.accepted = 0;

    Command mapped = Command::NONE;
    switch (command.command) {
        case REMOTE_PURGE: mapped = Command::START_PURGE; break;
        case REMOTE_DRYER_MANUAL: mapped = Command::START_DRYER_MANUAL; break;
        case REMOTE_DRYER_AUTO: mapped = Command::START_DRYER_AUTO; break;
        case REMOTE_RECHARGE: mapped = Command::START_RECHARGE; break;
        case REMOTE_STOP: mapped = Command::STOP; break;
        default: break;
    }

    if (mapped == Command::NONE) {
        copyText(ack.text, sizeof(ack.text), "Unknown command.");
    } else if (mapped == Command::STOP) {
        const bool wasRunning = stateMachine.isRunning();
        stateMachine.command(Command::STOP);
        ack.accepted = 1;
        copyText(ack.text, sizeof(ack.text), wasRunning ? "Stop accepted." : "Already idle. Outputs off.");
    } else if (!relay.isReady()) {
        copyText(ack.text, sizeof(ack.text), "Relay board is not connected. Start refused.");
    } else {
        stateMachine.config.dryerOnMinutes = clampByte(command.dryerOnMinutes, 1, 5);
        stateMachine.config.dryerOffHours = clampByte(command.dryerOffHours, 1, 3);
        stateMachine.config.dryerTargetRH = clampByte(command.dryerTargetRH, 15, 30);
        const char *block = stateMachine.startBlockReason(mapped);
        if (block != nullptr || !stateMachine.command(mapped)) {
            copyText(ack.text, sizeof(ack.text), block != nullptr ? block : "Start refused.");
        } else {
            ack.accepted = 1;
            copyText(ack.text, sizeof(ack.text), "Start accepted.");
        }
    }

    sendPacket((const uint8_t *)&ack, sizeof(ack));
    Serial.printf("Remote ack seq %u accepted %u: %s\n", ack.sequence, ack.accepted, ack.text);
    sendStatus();
}

void radioLinkNotePeer(const uint8_t *mac) {
    if (mac == nullptr) return;
    if (peerKnown && memcmp(peerMac, mac, 6) == 0) return;
    memcpy(peerMac, mac, 6);
    peerKnown = true;
    peerAdded = false;
}

void radioLinkAccept(const uint8_t *mac, const uint8_t *data, int len) {
    if (data == nullptr || len != (int)sizeof(LinkCommand)) return;
    LinkCommand incoming;
    memcpy(&incoming, data, sizeof(incoming));
    if (incoming.magic != AEROSENTRY_LINK_MAGIC || incoming.kind != LINK_KIND_COMMAND) return;
    radioLinkNotePeer(mac);
    portENTER_CRITICAL(&linkMux);
    pendingCommand = incoming;
    commandPending = true;
    portEXIT_CRITICAL(&linkMux);
}

void radioLinkUpdate() {
    LinkCommand command;
    bool haveCommand = false;
    portENTER_CRITICAL(&linkMux);
    if (commandPending) {
        command = pendingCommand;
        commandPending = false;
        haveCommand = true;
    }
    portEXIT_CRITICAL(&linkMux);

    if (haveCommand) applyCommand(command);
    else if (peerKnown && millis() - lastStatusMs >= 2000UL) sendStatus();
}

