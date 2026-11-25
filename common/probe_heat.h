#pragma once

#include <stdint.h>
#include <math.h>

/* SHT40 probe heater: request packet and the burn-off scheduler.

   Datasheet facts (Sensirion SHT4x datasheet v7.3, section 4.9):
   - Heater commands: 0x39 200 mW 1 s, 0x32 200 mW 0.1 s, 0x2F 110 mW 1 s,
     0x24 110 mW 0.1 s, 0x1E 20 mW 1 s, 0x15 20 mW 0.1 s. Each ends with a
     high-precision measurement taken while still hot (not a valid reading).
   - No off command; each command switches itself off after its time.
   - Designed for a maximum duty cycle of 10%.
   - Only below 65 C ambient; die must stay under 125 C.
   - "Removal of condensed / spray water... it will make the sensor
     non-responsive to RH changes as long as there is liquid water."
   - RH response time tau63 = 4 s, temperature 2 s (1 m/s air).

   The controller decides WHEN a probe is dried (it knows when the pump
   runs); the sensor node runs ProbeHeater, which enforces the hard limits
   and reports the probe state in every SensorPacket. This header has no
   Arduino dependency so the host simulation runs the same code. */

#define AEROSENTRY_HEAT_MAGIC 0xA9
#define HEAT_PROBE_TO   1
#define HEAT_PROBE_FROM 2

/* Controller -> sensor node, ESP-NOW unicast. The node echoes seq in
   SensorPacket.heatAck once it has taken the request. */
struct __attribute__((packed)) ProbeHeatRequest {
    uint8_t magic;   // AEROSENTRY_HEAT_MAGIC
    uint8_t probe;   // HEAT_PROBE_TO or HEAT_PROBE_FROM
    uint8_t pulses;  // 1-3 heater pulses of 200 mW for 1 s
    uint8_t seq;     // 1-255, 0 never used
};

/* Per-probe status bits in SensorPacket.toStatus / fromStatus. */
#define PROBE_STATUS_HEATING      0x01  // burst in progress: reading is distorted
#define PROBE_STATUS_RECOVERING   0x02  // after a burst, die still cooling: not for decisions
#define PROBE_STATUS_REFUSED_HOT  0x04  // last burst refused: probe at or above the heater limit
#define PROBE_STATUS_REFUSED_DUTY 0x08  // last request refused: duty-cycle budget used up
#define PROBE_STATUS_DRIED        0x10  // driedAgeS is valid (a burst has completed since boot)

/* Hard limits, enforced on the node whatever the controller asks. */
#define HEATER_MAX_PROBE_C       60.0f    // datasheet: heater only below 65 C ambient
#define HEATER_PULSE_MS          1000UL   // command 0x39: 200 mW for 1 s
#define HEATER_PULSE_GAP_MS      10000UL  // at least 10 s between pulses (<10% duty in a burst)
#define HEATER_WINDOW_MS         600000UL // duty budget window: 10 min
#define HEATER_WINDOW_MAX_PULSES 60       // 60 x 1 s in 10 min = 10%
#define HEATER_HOT_WAIT_MS       60000UL  // a pulse refused as too hot for this long ends the burst
#define HEATER_RECOVER_MIN_MS    8000UL   // die cooling: at least this long after the last pulse
#define HEATER_RECOVER_MAX_MS    60000UL  // ...and never longer than this
#define HEATER_RECOVER_BAND_C    1.0f     // recovered once within this of the pre-burst temperature
#define HEATER_RECOVER_RATE_CPS  0.15f    // ...or once it is cooling slower than this (air temp changed)

class ProbeHeater {
public:
    ProbeHeater() : _pulsesLeft(0), _nextPulseMs(0), _burstStartMs(0), _hotSinceMs(0),
                    _pulsedThisBurst(false), _recovering(false), _recoverSinceMs(0), _preC(NAN), _lastC(NAN), _lastObsMs(0),
                    _refusedHot(false), _refusedDuty(false), _dried(false), _lastDriedMs(0),
                    _historyCount(0), _historyNext(0) {}

    /* Start a burst of 1-3 pulses. Ignored while a burst or recovery runs
       (it is already being dried). Returns false when refused for duty. */
    bool request(uint8_t pulses, uint32_t now) {
        if (busy()) return true;
        if (pulses < 1) pulses = 1;
        if (pulses > 3) pulses = 3;
        _refusedHot = false;
        _refusedDuty = false;
        if (pulsesInWindow(now) + pulses > HEATER_WINDOW_MAX_PULSES) {
            _refusedDuty = true;
            return false;
        }
        _pulsesLeft = pulses;
        _nextPulseMs = now;
        _burstStartMs = now;
        _hotSinceMs = 0;
        _pulsedThisBurst = false;
        _preC = _lastC;
        return true;
    }

    /* Feed every normal (non-heater) reading of this probe. */
    void observe(float probeC, uint32_t now) {
        if (_recovering) {
            const uint32_t since = now - _recoverSinceMs;
            const float dtS = _lastObsMs != 0 ? (now - _lastObsMs) / 1000.0f : 0.0f;
            const float rate = (dtS > 0.0f && !isnan(_lastC)) ? fabsf(probeC - _lastC) / dtS : 1e9f;
            const bool back = !isnan(_preC) && probeC <= _preC + HEATER_RECOVER_BAND_C;
            const bool steady = rate <= HEATER_RECOVER_RATE_CPS;
            if ((since >= HEATER_RECOVER_MIN_MS && (back || steady)) || since >= HEATER_RECOVER_MAX_MS) {
                _recovering = false;
                _dried = true;
                _lastDriedMs = now;
            }
        }
        _lastC = probeC;
        _lastObsMs = now;
    }

    /* True when a heater pulse should be fired now. otherProbePulsing keeps
       the two probes from heating in the same second (shared 3.3 V supply). */
    bool pulseDue(uint32_t now, bool otherProbePulsing) {
        if (_pulsesLeft == 0 || otherProbePulsing) return false;
        if ((int32_t)(now - _nextPulseMs) < 0) return false;
        // Judge the AIR temperature: during a burst the die reads hot from the
        // previous pulse, so after the first pulse use the reading taken just
        // before it. Until the first pulse, follow the live reading so a probe
        // that starts too hot can cool into range.
        if (!_pulsedThisBurst) _preC = _lastC;
        if (isnan(_preC) || _preC >= HEATER_MAX_PROBE_C) {
            if (_hotSinceMs == 0) _hotSinceMs = now;
            if (now - _hotSinceMs >= HEATER_HOT_WAIT_MS) {  // give up this burst
                _pulsesLeft = 0;
                _refusedHot = true;
            }
            return false;
        }
        _hotSinceMs = 0;
        if (pulsesInWindow(now) >= HEATER_WINDOW_MAX_PULSES) {
            _pulsesLeft = 0;
            _refusedDuty = true;
            return false;
        }
        return true;
    }

    /* Call right after a heater pulse was sent and completed. */
    void pulseDone(uint32_t now) {
        recordPulse(now);
        _pulsedThisBurst = true;
        if (_pulsesLeft > 0) _pulsesLeft--;
        _nextPulseMs = now + HEATER_PULSE_GAP_MS;
        if (_pulsesLeft == 0) {
            _recovering = true;
            _recoverSinceMs = now;
            _lastObsMs = 0;  // the next rate uses a fresh pair of readings
        }
    }

    bool busy() const { return _pulsesLeft > 0 || _recovering; }
    bool pulsing() const { return _pulsesLeft > 0; }

    uint8_t status() const {
        uint8_t s = 0;
        if (_pulsesLeft > 0) s |= PROBE_STATUS_HEATING;
        if (_recovering) s |= PROBE_STATUS_RECOVERING;
        if (_refusedHot) s |= PROBE_STATUS_REFUSED_HOT;
        if (_refusedDuty) s |= PROBE_STATUS_REFUSED_DUTY;
        if (_dried) s |= PROBE_STATUS_DRIED;
        return s;
    }

    /* Seconds since the last completed burst (saturates at 65535). */
    uint16_t driedAgeS(uint32_t now) const {
        if (!_dried) return 0xFFFF;
        const uint32_t s = (now - _lastDriedMs) / 1000UL;
        return s > 0xFFFE ? 0xFFFE : (uint16_t)s;
    }

    uint8_t pulsesInWindow(uint32_t now) const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < _historyCount; ++i) {
            if (now - _history[i] < HEATER_WINDOW_MS) n++;
        }
        return n;
    }

private:
    static const uint8_t HISTORY = HEATER_WINDOW_MAX_PULSES;
    void recordPulse(uint32_t now) {
        _history[_historyNext] = now;
        _historyNext = (uint8_t)((_historyNext + 1) % HISTORY);
        if (_historyCount < HISTORY) _historyCount++;
    }

    uint8_t _pulsesLeft;
    uint32_t _nextPulseMs;
    uint32_t _burstStartMs;
    uint32_t _hotSinceMs;
    bool _pulsedThisBurst;
    bool _recovering;
    uint32_t _recoverSinceMs;
    float _preC;
    float _lastC;
    uint32_t _lastObsMs;
    bool _refusedHot;
    bool _refusedDuty;
    bool _dried;
    uint32_t _lastDriedMs;
    uint32_t _history[HEATER_WINDOW_MAX_PULSES];
    uint8_t _historyCount;
    uint8_t _historyNext;
};

/* Dew point (Magnus, Sensirion coefficients). Water condenses on a surface
   at or below this temperature. */
inline float dewPointC(float tempC, float rh) {
    if (rh <= 0.0f) rh = 0.01f;
    const float b = 17.62f, c = 243.12f;
    const float gamma = logf(rh / 100.0f) + b * tempC / (c + tempC);
    return c * gamma / (b - gamma);
}
