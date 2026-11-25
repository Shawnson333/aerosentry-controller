/* SHT40 probe drying policy and trusted readings. See probe_care.h. */

#include "probe_care.h"
#include "config.h"
#include "sensors.h"
#include "statemachine.h"
#include <esp_now.h>
#include <string.h>

namespace {

struct ProbeCare {
    Probe probe;
    const char* name;
    uint8_t pendingSeq;        // request sent, not yet acknowledged (0 = none)
    uint8_t pendingPulses;
    uint8_t sends;
    uint32_t lastSendMs;
    uint32_t lastRequestMs;    // last time a dry was asked for (any rule)
    uint32_t wetUntilMs;       // R3 wet history ends (0 = none)
    uint32_t postRechargeUntilMs;
    float displayRH;
    bool displayValid;
    uint32_t displayStamp;     // sample timestamp last folded into displayRH
    uint32_t displayUpdatedMs;
};

ProbeCare care[2] = {
    {Probe::TO_ENGINE, "To-Engine", 0, 0, 0, 0, 0, 0, 0, NAN, false, 0, 0},
    {Probe::FROM_ENGINE, "From-Engine", 0, 0, 0, 0, 0, 0, 0, NAN, false, 0, 0},
};
uint8_t nextSeq = 1;
bool peerAdded = false;
const uint8_t nodeMac[6] = SENSOR_NODE_MAC;

ProbeCare& careFor(Probe probe) { return probe == Probe::FROM_ENGINE ? care[1] : care[0]; }

uint8_t statusOf(Probe probe) {
    return probe == Probe::FROM_ENGINE ? sensors.fromStatus : sensors.toStatus;
}

bool readyOf(Probe probe) {
    return probe == Probe::FROM_ENGINE ? fromEngineReady() : toEngineReady();
}

float rhOf(Probe probe) { return probe == Probe::FROM_ENGINE ? sensors.fromEngineRH : sensors.toEngineRH; }
float tempOf(Probe probe) { return probe == Probe::FROM_ENGINE ? sensors.fromEngineTemp : sensors.toEngineTemp; }
uint32_t stampOf(Probe probe) {
    return probe == Probe::FROM_ENGINE ? sensors.fromTimestamp : sensors.toTimestamp;
}

/* Age of the last completed burn-off right now, in ms. UINT32_MAX when never. */
uint32_t driedAgeMs(Probe probe) {
    const uint16_t ageS = probe == Probe::FROM_ENGINE ? sensors.fromDriedAgeS : sensors.toDriedAgeS;
    if (!(statusOf(probe) & PROBE_STATUS_DRIED) || ageS == 0xFFFF || sensors.timestamp == 0) {
        return UINT32_MAX;
    }
    return (uint32_t)ageS * 1000UL + (millis() - sensors.timestamp);
}

bool sendRequest(ProbeCare& c) {
    if (!peerAdded) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, nodeMac, 6);
        peer.channel = AEROSENTRY_ESPNOW_CHANNEL;
        peer.encrypt = false;
        const esp_err_t result = esp_now_add_peer(&peer);
        peerAdded = result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
        if (!peerAdded) return false;
    }
    ProbeHeatRequest request = {};
    request.magic = AEROSENTRY_HEAT_MAGIC;
    request.probe = (uint8_t)c.probe;
    request.pulses = c.pendingPulses;
    request.seq = c.pendingSeq;
    c.sends++;
    c.lastSendMs = millis();
    return esp_now_send(nodeMac, (const uint8_t*)&request, sizeof(request)) == ESP_OK;
}

/* R2 / R3: how often this probe should be dried right now (0 = not needed). */
uint32_t maintenanceInterval(ProbeCare& c, uint32_t now, uint8_t& pulses) {
    pulses = PROBE_DRY_PULSES;
    if (c.postRechargeUntilMs != 0 && (int32_t)(c.postRechargeUntilMs - now) > 0) {
        pulses = PROBE_DRY_PULSES_LONG;
        return PROBE_POST_RECHARGE_INTERVAL_MS;
    }
    if (c.wetUntilMs != 0 && (int32_t)(c.wetUntilMs - now) > 0) return PROBE_WET_INTERVAL_MS;
    if (!readyOf(c.probe) || probeBusy(c.probe)) return 0;
    const float spread = tempOf(c.probe) - dewPointC(tempOf(c.probe), rhOf(c.probe));
    return spread < PROBE_CONDENSE_SPREAD_C ? PROBE_ZONE_INTERVAL_MS : 0;
}

void updateDisplay(ProbeCare& c, uint32_t now) {
    if (!readyOf(c.probe)) {
        // Hold through a short dropout; blank only when the reading is really gone.
        if (c.displayValid && now - c.displayUpdatedMs > SENSOR_STALE_MS) c.displayValid = false;
        return;
    }
    if (probeBusy(c.probe)) return;  // hold the last clean value while heating / recovering
    const uint32_t stamp = stampOf(c.probe);
    if (stamp == c.displayStamp) return;
    const float rh = rhOf(c.probe);
    if (!c.displayValid) {
        c.displayRH = rh;
    } else {
        const float dt = (float)(stamp - c.displayStamp);
        const float alpha = dt / ((float)PROBE_DISPLAY_TAU_MS + dt);
        c.displayRH += alpha * (rh - c.displayRH);
    }
    c.displayStamp = stamp;
    c.displayValid = true;
    c.displayUpdatedMs = now;
}

}  // namespace

void probeCareBegin() {
    for (ProbeCare& c : care) {
        c.pendingSeq = 0;
        c.pendingPulses = 0;
        c.sends = 0;
        c.lastSendMs = 0;
        c.lastRequestMs = 0;
        c.wetUntilMs = 0;
        c.postRechargeUntilMs = 0;
        c.displayRH = NAN;
        c.displayValid = false;
        c.displayStamp = 0;
        c.displayUpdatedMs = 0;
    }
    nextSeq = 1;
}

bool probeDry(Probe probe, uint8_t pulses, const char* reason) {
    ProbeCare& c = careFor(probe);
    if (probeBusy(probe)) return true;  // already drying
    if (pulses < 1) pulses = 1;
    if (pulses > 3) pulses = 3;
    c.pendingSeq = nextSeq;
    nextSeq = (uint8_t)(nextSeq == 255 ? 1 : nextSeq + 1);
    c.pendingPulses = pulses;
    c.sends = 0;
    c.lastRequestMs = millis();
    const bool sent = sendRequest(c);
    Serial.printf("Probe dry %s: %u pulses (%s)%s\n", c.name, pulses, reason,
                  sent ? "" : " - send failed, will retry");
    return true;
}

bool probeBusy(Probe probe) {
    const ProbeCare& c = careFor(probe);
    if (c.pendingSeq != 0) return true;
    if (!sensorPacketFresh()) return false;
    return (statusOf(probe) & (PROBE_STATUS_HEATING | PROBE_STATUS_RECOVERING)) != 0;
}

bool probeDriedWithin(Probe probe, uint32_t maxAgeMs) {
    if (!readyOf(probe) || probeBusy(probe)) return false;
    return driedAgeMs(probe) <= maxAgeMs;
}

bool probeRefused(Probe probe) {
    return (statusOf(probe) & (PROBE_STATUS_REFUSED_HOT | PROBE_STATUS_REFUSED_DUTY)) != 0;
}

void probeCareRechargeEnded() {
    const uint32_t now = millis();
    for (ProbeCare& c : care) {
        c.postRechargeUntilMs = now + PROBE_POST_RECHARGE_MS;
        if (c.postRechargeUntilMs == 0) c.postRechargeUntilMs = 1;
        c.wetUntilMs = now + PROBE_WET_HOLD_MS;
        if (c.wetUntilMs == 0) c.wetUntilMs = 1;
    }
    Serial.println(F("Probe care: post-recharge drying series started"));
}

void probeCareUpdate(SystemMode mode) {
    (void)mode;
    const uint32_t now = millis();
    for (ProbeCare& c : care) {
        // Acknowledged, or give up after a few tries (the node may be off).
        if (c.pendingSeq != 0) {
            if (sensors.heatAck == c.pendingSeq) {
                c.pendingSeq = 0;
            } else if (now - c.lastSendMs >= PROBE_REQUEST_RETRY_MS) {
                if (c.sends >= PROBE_REQUEST_TRIES) {
                    Serial.printf("Probe dry %s: no answer from the sensor node, dropped\n", c.name);
                    c.pendingSeq = 0;
                } else {
                    sendRequest(c);
                }
            }
        }
        // R3: a very wet reading starts a wet history.
        if (readyOf(c.probe) && !probeBusy(c.probe) && rhOf(c.probe) >= PROBE_WET_RH) {
            const bool wasWet = c.wetUntilMs != 0 && (int32_t)(c.wetUntilMs - now) > 0;
            c.wetUntilMs = now + PROBE_WET_HOLD_MS;
            if (c.wetUntilMs == 0) c.wetUntilMs = 1;
            if (!wasWet) Serial.printf("Probe care: %s read %.0f%%, wet history started\n", c.name, rhOf(c.probe));
        }
        // R2 / R3 maintenance.
        uint8_t pulses = 0;
        const uint32_t interval = maintenanceInterval(c, now, pulses);
        if (interval != 0 && !probeBusy(c.probe) && sensorPacketFresh() &&
            driedAgeMs(c.probe) >= interval && now - c.lastRequestMs >= interval) {
            probeDry(c.probe, pulses, c.postRechargeUntilMs != 0 && (int32_t)(c.postRechargeUntilMs - now) > 0
                                          ? "post-recharge series"
                                          : (c.wetUntilMs != 0 && (int32_t)(c.wetUntilMs - now) > 0
                                                 ? "wet history" : "condensation zone"));
        }
        updateDisplay(c, now);
    }
}

bool probeDisplayRH(Probe probe, float &rh) {
    const ProbeCare& c = careFor(probe);
    if (!c.displayValid) return false;
    rh = c.displayRH;
    return true;
}

void probeStateText(Probe probe, char* buf, size_t size) {
    const uint8_t status = statusOf(probe);
    const char* state = careFor(probe).pendingSeq != 0 ? "REQUESTED"
                      : (status & PROBE_STATUS_HEATING) ? "HEATING"
                      : (status & PROBE_STATUS_RECOVERING) ? "RECOVERING"
                      : (status & PROBE_STATUS_REFUSED_HOT) ? "refused: too hot"
                      : (status & PROBE_STATUS_REFUSED_DUTY) ? "refused: duty"
                      : "clean";
    char dried[16] = "never";
    const uint32_t age = driedAgeMs(probe);
    if (age != UINT32_MAX) snprintf(dried, sizeof(dried), "%lu min", (unsigned long)(age / 60000UL));
    if (readyOf(probe)) {
        const float t = tempOf(probe);
        snprintf(buf, size, "%.1f%% %.1fF dew spread %.1fF, %s, dried %s ago",
                 rhOf(probe), t * 9.0f / 5.0f + 32.0f,
                 (t - dewPointC(t, rhOf(probe))) * 9.0f / 5.0f, state, dried);
    } else {
        snprintf(buf, size, "no reading, %s", state);
    }
}
