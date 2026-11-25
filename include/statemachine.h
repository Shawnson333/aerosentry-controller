#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include <Arduino.h>
#include "config.h"
#include "sensors.h"

/* ========================================================================
   MODE STATE MACHINE
   Purge and manual dryer watch To-Engine only.
   Auto dryer watches both sensors.
   Recharge watches the chamber thermistor, To-Engine humidity and the vent
   pressure, and ignores 40%.
   ======================================================================== */

enum class SystemMode {
    IDLE,
    PURGE,
    DRYER_MANUAL,
    DRYER_AUTO,
    RECHARGE
};

enum class Command {
    NONE,
    START_PURGE,
    START_DRYER_MANUAL,
    START_DRYER_AUTO,
    START_RECHARGE,
    STOP,
    STOP_REMOTE     // STOP from the remote page; same effect, its own stop code
};

/* Where a command came from. A remote start never arms Auto Recharge: only
   someone at the unit can see that it is somewhere safe to get hot. */
enum class CommandSource {
    LOCAL,   // touch screen
    REMOTE   // radio bridge / phone page
};

/* Every way a cycle can end. The number is the stop code shown on screen
   ("Code S09") and written to the serial log and the future SD event log.
   Never renumber or reuse a code: logs and notes refer to them. A new reason
   takes the next free number. The full list is in docs/ERROR_CODES.md. */
enum class StopReason : uint8_t {
    NONE               = 0,
    USER               = 1,   // Stop pressed on the screen
    USER_REMOTE        = 2,   // Stop from the remote page
    SATURATED          = 3,   // To-Engine above 40% for 30 s (not in recharge)
    SENSOR_NODE_SILENT = 4,   // No packets from the sensor node
    TO_ENGINE_LOST     = 5,
    FROM_ENGINE_LOST   = 6,
    CHAMBER_LOST       = 7,
    VENT_SENSOR_LOST   = 8,   // Vent pressure missing, valve position unconfirmed
    VALVE_SUSPECT      = 9,   // Pump pulse without the vent pressure step
    VALVE_NO_CURRENT   = 10,  // Coil current check (disabled until the shunt is wired)
    THERMAL_CHAMBER    = 11,
    THERMAL_TO_PROBE   = 12,
    THERMAL_FROM_PROBE = 13,
    HEATUP_TIMEOUT     = 14,
    SAFETY_FAILED      = 15,
    RECHARGE_COMPLETE  = 16,
    RECHARGE_MAX_TIME  = 17,
    CONTROLLER_RESTART = 18,  // Power loss during a cycle, or any watchdog / crash / brownout (shown at boot)
    HEATER_NO_RISE     = 19,  // Heater on for HEATER_RESPONSE_MS without the chamber rising
};

/* Stable upper-case name for a stop code, e.g. "VALVE_SUSPECT". */
const char* stopReasonName(StopReason reason);

/* Write the on-screen text for a stop into buf: title, detail, what to do,
   and "<mode> ended. Code Sxx". detail may be nullptr for the default line. */
void formatStopMessage(char* buf, size_t size, StopReason reason, SystemMode mode,
                       const char* detail, bool afterCooldown);

struct ModeConfig {
    uint8_t  dryerOnMinutes;    // 1-5 min, manual pump-on time
    uint16_t dryerOffHours;     // 1-3 hr, manual rest time
    uint8_t  dryerTargetRH;     // 15-30%, auto From-Engine target
    bool     allowAutoRecharge; // Allow automatic recharge on saturation stop
};

class StateMachine {
public:
    StateMachine();

    void begin();
    void update();

    /* STOP is always queued. A start is rejected when a required sensor is
       offline or another cycle is already running. */
    bool command(Command cmd, CommandSource source = CommandSource::LOCAL);

    /* nullptr when this start can be accepted. */
    const char* startBlockReason(Command cmd) const;

    /* Warning text for a non-user stop, or nullptr. Clears the reason. */
    const char* takeStopMessage();

    /* Boot only: a cycle was running when the controller restarted. Queues
       the S18 message for the screen and logs it like any other stop. */
    void reportRestart(SystemMode interrupted, const char* cause);

    SystemMode mode() const { return _mode; }
    bool isRunning() const { return _mode != SystemMode::IDLE; }
    bool pumpOn() const { return _pumpOn; }
    bool heaterOn() const { return _heaterOn; }
    bool fanOn() const { return _fanOn; }
    bool valveOpen() const { return _valveEnergized; }
    bool autoRechargeAllowed() const { return config.allowAutoRecharge; }
    /* True when the running dryer cycle may start a recharge on saturation:
       Auto Recharge was on AND the cycle was started at the screen. */
    bool autoRechargeArmed() const { return _autoRechargeArmed; }
    float chipTempC() const { return _chipTempC; }

    /* Phase line for the main screen. Valid until the next call. */
    const char* statusText() const;
    int32_t ventBaseline() const;
    bool ventBaselineValid() const;
    /* Step of the last judged pump pulse (peak minus baseline). False if none yet. */
    bool ventLastStep(int32_t &step) const;
    uint32_t ventAmbientCount() const;
    uint16_t ventGoodPulses() const;
    uint16_t ventBadPulses() const;
    bool purgeRecent() const;
    uint32_t purgeAgeMs() const;
    bool hasPurgeRecord() const { return _purgeSeen; }

    ModeConfig config;

private:
    enum class ManualPhase { ON, OFF };
    enum class AutoPhase { DRYING, COOLDOWN, QUICK_SAMPLE, WAITING, SAMPLE_RUN, SAMPLE_SETTLE };
    // SAFETY_PREP (6, 2026-11-24) dries the To-Engine probe before each safety pulse.
    enum class RechargePhase { HEATING, PULSE_ON, PULSE_WAIT, COOLDOWN, SAFETY_PULSE, SAFETY_WAIT, SAFETY_PREP };

    SystemMode _mode;
    Command _pendingCmd;
    CommandSource _pendingSource;
    bool _autoRechargeArmed;        // This dryer cycle may start a recharge (local start + setting on)
    StopReason _stopReason;
    const char* _stopMessage;       // Points at _stopText while a message waits for the UI
    char _stopText[360];
    ManualPhase _manualPhase;
    AutoPhase _autoPhase;
    RechargePhase _rechargePhase;

    uint32_t _modeStartTime;
    uint32_t _phaseStartTime;
    uint32_t _saturationMark;
    uint32_t _lastLogTime;
    bool _inGracePeriod;
    bool _saturationTiming;
    bool _pumpOn;
    bool _heaterOn;
    bool _fanOn;
    bool _appliedPump;
    bool _appliedHeater;
    bool _appliedFan;
    bool _chipHot;
    float _chipTempC;
    bool _valveEnergized;
    bool _appliedValve;
    bool _rechargeTriggeredByCycle; // true if recharge was triggered by saturation during dryer/auto
    SystemMode _rechargeResumeMode; // Cycle to resume after a successful auto recharge
    bool _rechargeFailure;          // true while running the failure cool down
    StopReason _rechargeFailureReason;
    bool _chamberReachedTarget;     // Arms the valve verification after real heat-up
    uint32_t _chamberReachedMs;     // millis() when the chamber first reached target; starts the long-pulse hour
    bool _rechargeTimedOut;         // Max run time reached: cool down and safety check, then stop
    bool _sensorLost;               // Required sensors are missing right now (grace timer running)
    uint32_t _sensorLostSinceMs;    // millis() when they went missing
    int32_t _ventBaseline;          // Idle vent pressure, learned while the pump is off
    bool _ventBaselineValid;
    uint32_t _ventLearnStartMs;
    uint16_t _ventGoodPulses;       // Pulses that showed the expected pressure step
    uint16_t _ventBadPulses;        // Pulses that showed no step (one stops the cycle)
    // Ambient averaging (2026-11-20): running average of pump-OFF samples.
    // Re-learned every recharge; drift is tracked between pulses.
    double _ventAmbientSum;
    uint32_t _ventAmbientCount;
    uint32_t _ventLastSampleMs;
    int32_t _ventPulsePeak;         // Max raw seen during the current 10 s pulse
    uint16_t _ventPulseSamples;     // Pressure packets seen during the current pulse
    uint32_t _ventPulseLastStamp;   // sensors.pressureTimestamp of the last counted packet
    int32_t _ventLastStep;          // Step of the last judged pulse, for the Admin page
    bool _ventLastStepValid;
    uint32_t _cooldownWindow;       // Which 10 min cool down window the pulse logic is in
    bool _cooldownPulseRun;         // This window's cool down pulse is running
    uint8_t _pulseCount;            // Recharge pulses completed, used for the first-pulse flush
    uint8_t _valveNoCurrentCount;   // Consecutive low shunt readings while energized
    uint32_t _valveCommandMs;       // millis() when the valve was energized
    uint32_t _valveCheckMs;         // Last coil-current check
    uint8_t _autoQuickCycles;       // Count of 10-min quick cycles attempted
    uint8_t _safetyCheckCount;      // Safety check pulses completed (0, 1, 2)
    uint32_t _rechargeStartEpoch;   // Epoch when recharge started, for logging
    uint32_t _rechargeEndEpoch;     // Epoch when recharge ended, for logging
    bool _purgeSeen;
    bool _purgeQualified;
    bool _thermalLatched;
    time_t _purgeEpoch;
    uint8_t _rechargeGoodCycles;
    bool _heaterCheckArmed;         // Heater response check: a low point is being tracked
    float _heaterCheckLowF;         // Lowest chamber F since the heater last proved it works
    uint32_t _heaterOnMs;           // Heater-ON time since then
    uint32_t _heaterCheckLastMs;
    bool _probeDryAsked;            // R1 burn-off already requested in this phase
    bool _saturationConfirming;     // saturation timer ran out; drying the probe to confirm
    uint32_t _saturationConfirmMs;

    bool handleCommand();
    void enterMode(SystemMode newMode);
    /* End the cycle: all outputs off, valves closed, one log line, one message.
       detail replaces the default second line; afterCooldown adds the line
       that says the chamber was cooled with the valves venting first. */
    void stopWith(StopReason reason, const char* detail = nullptr, bool afterCooldown = false);
    StopReason sensorLossReason() const;
    StopReason thermalFault(float &fahrenheit) const;
    void updatePurge();
    void updateManual();
    void updateAuto();
    void updateRecharge(bool sensorsOk);
    void holdRechargeForSensors();
    void runCooldownPulse(uint32_t cooldownMs);
    void checkHeaterResponse();
    void checkSaturation();
    void resetSaturation();
    void checkThermal();
    void applyOutputs();
    void updateFan();
    void regulateHeater();
    void maybeLog();
    bool sensorsOkForMode() const;
    void setManualPhase(ManualPhase phase);
    void setAutoPhase(AutoPhase phase);
    void setRechargePhase(RechargePhase phase);
    void setValve(bool on);
    void rechargeFailureCooldown(StopReason reason);
    void checkValveCurrent();
    enum class VentResult { OK, FAULT, NOT_JUDGED };
    VentResult judgeVentPulse(const char* label);
    void trackVentPulse();
    void resetVentPulse();
    bool ventCheckReady() const;
    bool ventBaselineReady() const;
    bool ventSensorOk() const;
    const char* rechargeSensorBlockReason() const;
    void learnVentBaseline();
    uint32_t phaseElapsed() const;
    uint32_t phaseRemaining(uint32_t totalMs) const;
};

extern StateMachine stateMachine;

const char* systemModeName(SystemMode mode);

#endif // STATEMACHINE_H
