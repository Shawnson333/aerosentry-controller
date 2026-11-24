/* Heltec command path. A radio command is only a request.
   Starts still go through StateMachine::command().

   Remote rules (user decision 2026-11-24):
   - Recharge cannot be started remotely. Nobody at a phone can see whether
     the unit is somewhere safe to get hot and expel water.
   - A remote dryer start never arms Auto Recharge, for the same reason. The
     Auto Recharge setting is changed only on the touch screen.
   - Starts are accepted only from the radio bridge (REMOTE_BRIDGE_MAC).
     ESP-NOW is unencrypted, so a MAC check stops accidents and stray
     devices, not a determined attacker.
   - STOP is accepted from any sender: stopping is always the safe direction. */

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
static bool pendingFromBridge = false;
static uint8_t pendingMac[6] = {};
static volatile uint32_t ignoredCommands = 0;  // starts from unknown senders
static uint32_t ignoredReported = 0;
static uint8_t ignoredMac[6] = {};
static uint8_t peerMac[6] = {};
static bool peerKnown = false;
static bool peerAdded = false;
static uint16_t statusSequence = 0;
static uint32_t lastStatusMs = 0;

static const uint8_t BRIDGE_MAC[6] = REMOTE_BRIDGE_MAC;

static bool isBridge(const uint8_t *mac) {
#if REMOTE_REQUIRE_BRIDGE_MAC
    return mac != nullptr && memcmp(mac, BRIDGE_MAC, 6) == 0;
#else
    return mac != nullptr;
#endif
}

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
    if (stateMachine.valveOpen()) status.flags2 |= LINK_FLAG_VALVE >> 8;
    // While a cycle runs this says whether THIS cycle may start a recharge.
    const bool autoRecharge = stateMachine.isRunning() ? stateMachine.autoRechargeArmed()
                                                       : stateMachine.config.allowAutoRecharge;
    if (autoRecharge) status.flags2 |= LINK_FLAG_AUTO_RECHARGE >> 8;
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

static void applyCommand(const LinkCommand &command, bool fromBridge, const uint8_t *mac) {
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
        case REMOTE_STOP: mapped = Command::STOP_REMOTE; break;
        default: break;
    }

    if (mapped == Command::STOP_REMOTE) {
        const bool wasRunning = stateMachine.isRunning();
        stateMachine.command(Command::STOP_REMOTE);
        ack.accepted = 1;
        copyText(ack.text, sizeof(ack.text), wasRunning ? "Stop accepted." : "Already idle. Outputs off.");
        if (!fromBridge) {
            Serial.printf("Remote STOP from %02X:%02X:%02X:%02X:%02X:%02X (not the bridge) accepted\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;  // no reply to an unknown sender
        }
    } else if (mapped == Command::NONE) {
        copyText(ack.text, sizeof(ack.text), "Unknown command.");
    } else if (mapped == Command::START_RECHARGE && !REMOTE_RECHARGE_ALLOWED) {
        copyText(ack.text, sizeof(ack.text), "Recharge must be started at the unit, after its safety check.");
    } else if (!relay.isReady()) {
        copyText(ack.text, sizeof(ack.text), "Relay board is not connected. Start refused.");
    } else {
        const char *block = stateMachine.startBlockReason(mapped);
        if (block != nullptr) {
            copyText(ack.text, sizeof(ack.text), block);
        } else {
            // Settings change only for an accepted start. Auto Recharge is never
            // set from here; remote starts run with it off (CommandSource::REMOTE).
            stateMachine.config.dryerOnMinutes = clampByte(command.dryerOnMinutes, 1, 5);
            stateMachine.config.dryerOffHours = clampByte(command.dryerOffHours, 1, 3);
            stateMachine.config.dryerTargetRH = clampByte(command.dryerTargetRH, 15, 30);
            if (stateMachine.command(mapped, CommandSource::REMOTE)) {
                ack.accepted = 1;
                const bool dryer = mapped == Command::START_DRYER_MANUAL ||
                                   mapped == Command::START_DRYER_AUTO;
                copyText(ack.text, sizeof(ack.text),
                         dryer && stateMachine.config.allowAutoRecharge
                             ? "Start accepted. Auto Recharge stays off for remote starts."
                             : "Start accepted.");
            } else {
                copyText(ack.text, sizeof(ack.text), "Start refused.");
            }
        }
    }

    sendPacket((const uint8_t *)&ack, sizeof(ack));
    Serial.printf("Remote ack seq %u accepted %u: %s\n", ack.sequence, ack.accepted, ack.text);
    sendStatus();
}

void radioLinkNotePeer(const uint8_t *mac) {
    if (mac == nullptr || !isBridge(mac)) return;  // only the bridge gets status replies
    if (peerKnown && memcmp(peerMac, mac, 6) == 0) return;
    memcpy(peerMac, mac, 6);
    peerKnown = true;
    peerAdded = false;
}

void radioLinkAccept(const uint8_t *mac, const uint8_t *data, int len) {
    if (data == nullptr || mac == nullptr || len != (int)sizeof(LinkCommand)) return;
    LinkCommand incoming;
    memcpy(&incoming, data, sizeof(incoming));
    if (incoming.magic != AEROSENTRY_LINK_MAGIC || incoming.kind != LINK_KIND_COMMAND) return;
    const bool fromBridge = isBridge(mac);
    if (!fromBridge && incoming.command != REMOTE_STOP) {
        portENTER_CRITICAL(&linkMux);
        ignoredCommands++;
        memcpy(ignoredMac, mac, 6);
        portEXIT_CRITICAL(&linkMux);
        return;
    }
    radioLinkNotePeer(mac);
    portENTER_CRITICAL(&linkMux);
    pendingCommand = incoming;
    pendingFromBridge = fromBridge;
    memcpy(pendingMac, mac, 6);
    commandPending = true;
    portEXIT_CRITICAL(&linkMux);
}

void radioLinkUpdate() {
    LinkCommand command;
    bool haveCommand = false;
    bool fromBridge = false;
    uint8_t mac[6] = {};
    uint32_t ignored = 0;
    uint8_t strangerMac[6] = {};
    portENTER_CRITICAL(&linkMux);
    if (commandPending) {
        command = pendingCommand;
        fromBridge = pendingFromBridge;
        memcpy(mac, pendingMac, 6);
        commandPending = false;
        haveCommand = true;
    }
    ignored = ignoredCommands;
    memcpy(strangerMac, ignoredMac, 6);
    portEXIT_CRITICAL(&linkMux);

    if (ignored != ignoredReported) {
        ignoredReported = ignored;
        Serial.printf("Remote start from %02X:%02X:%02X:%02X:%02X:%02X ignored (not the bridge), %lu so far\n",
                      strangerMac[0], strangerMac[1], strangerMac[2], strangerMac[3], strangerMac[4],
                      strangerMac[5], (unsigned long)ignored);
    }
    if (haveCommand) applyCommand(command, fromBridge, mac);
    else if (peerKnown && millis() - lastStatusMs >= 2000UL) sendStatus();
}
