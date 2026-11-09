#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include <Arduino.h>
#include "config.h"
#include "sensors.h"

/* ========================================================================
   MODE STATE MACHINE
   Purge and manual dryer watch To-Engine only.
   Auto dryer watches both sensors.
   Recharge watches the chamber thermistor and To-Engine humidity, and ignores 40%.
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
    STOP
};

enum class StopReason {
    NONE,
    USER,
    SATURATED,
    SENSOR_LOST,
    RECHARGE_COMPLETE,
    RECHARGE_MAX_TIME,
    THERMAL
};

struct ModeConfig {
    uint8_t  dryerOnMinutes;    // 1-5 min, manual pump-on time
    uint16_t dryerOffHours;     // 1-3 hr, manual rest time
    uint8_t  dryerTargetRH;     // 15-30%, auto From-Engine target
};

class StateMachine {
public:
    StateMachine();

    void begin();
    void update();

    /* STOP is always queued. A start is rejected when a required sensor is
       offline or another cycle is already running. */
    bool command(Command cmd);

    /* nullptr when this start can be accepted. */
    const char* startBlockReason(Command cmd) const;

    /* Warning text for a non-user stop, or nullptr. Clears the reason. */
    const char* takeStopMessage();

    SystemMode mode() const { return _mode; }
    bool isRunning() const { return _mode != SystemMode::IDLE; }
    bool pumpOn() const { return _pumpOn; }
    bool heaterOn() const { return _heaterOn; }
    bool fanOn() const { return _fanOn; }
    float chipTempC() const { return _chipTempC; }

    /* Phase line for the main screen. Valid until the next call. */
    const char* statusText() const;
    bool purgeRecent() const;
    uint32_t purgeAgeMs() const;
    bool hasPurgeRecord() const { return _purgeSeen; }

    ModeConfig config;
    uint32_t timeRemainingMs() const;

private:
    enum class ManualPhase { ON, OFF };
    enum class AutoPhase { DRYING, WAITING, SAMPLE_RUN, SAMPLE_SETTLE };
    enum class RechargePhase { HEATING, PULSE_ON, PULSE_WAIT };

    SystemMode _mode;
    Command _pendingCmd;
    StopReason _stopReason;
    const char* _stopMessage;
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
    bool _purgeSeen;
    bool _thermalLatched;
    uint32_t _lastPurgeMs;
    uint8_t _rechargeGoodCycles;

    bool handleCommand();
    void enterMode(SystemMode newMode);
    void stopWith(StopReason reason);
    void updatePurge();
    void updateManual();
    void updateAuto();
    void updateRecharge();
    void checkSaturation();
    void resetSaturation();
    void checkThermal();
    bool thermalExceeded() const;
    void applyOutputs();
    void updateFan();
    void regulateHeater();
    void maybeLog();
    bool sensorsOkForMode() const;
    void setManualPhase(ManualPhase phase);
    void setAutoPhase(AutoPhase phase);
    void setRechargePhase(RechargePhase phase);
    uint32_t phaseElapsed() const;
    uint32_t phaseRemaining(uint32_t totalMs) const;
};

extern StateMachine stateMachine;

const char* systemModeName(SystemMode mode);

#endif // STATEMACHINE_H