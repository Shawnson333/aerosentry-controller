/* ========================================================================
   MODE STATE MACHINE
   Purge / manual: To-Engine 40% hard-stop only.
   Auto: both sensors. Recharge: chamber, To-Engine and vent pressure, 40% ignored.
   Every stop has a code (S01-S18); see docs/ERROR_CODES.md.
   ======================================================================== */

#include "statemachine.h"
#include "relay.h"
#include "logging.h"
#include "system_clock.h"
#include "run_state.h"
#include "probe_care.h"

static float celsiusToFahrenheit(float celsius) {
    return celsius * 9.0f / 5.0f + 32.0f;
}

static void formatDuration(char* buf, size_t size, uint32_t ms) {
    uint32_t totalSeconds = ms / 1000UL;
    uint32_t hours = totalSeconds / 3600UL;
    uint32_t minutes = (totalSeconds % 3600UL) / 60UL;
    uint32_t seconds = totalSeconds % 60UL;
    if (hours > 0) {
        snprintf(buf, size, "%luh %02lum", (unsigned long)hours, (unsigned long)minutes);
    } else if (minutes > 0) {
        snprintf(buf, size, "%lum %02lus", (unsigned long)minutes, (unsigned long)seconds);
    } else {
        snprintf(buf, size, "%lus", (unsigned long)seconds);
    }
}

const char* systemModeName(SystemMode mode) {
    switch (mode) {
        case SystemMode::IDLE:         return "Idle";
        case SystemMode::PURGE:        return "Purge";
        case SystemMode::DRYER_MANUAL: return "Dryer Manual";
        case SystemMode::DRYER_AUTO:   return "Dryer Auto";
        case SystemMode::RECHARGE:     return "Recharge";
        default:                       return "Unknown";
    }
}

/* Stop texts, indexed by the StopReason number. The name is the stable log
   name. The title is the first line on screen and the action says what to
   do next. The second line comes from defaultStopDetail() unless the caller
   passes a more specific one (a temperature, a pressure step). */
struct StopText {
    const char* name;
    const char* title;
    const char* action;
};

static const StopText STOP_TEXTS[] = {
    /*  0 */ {"NONE",               "Cycle stopped.",                          nullptr},
    /*  1 */ {"USER_STOP",          "Stopped.",                                nullptr},
    /*  2 */ {"REMOTE_STOP",        "Stopped from the remote page.",           nullptr},
    /*  3 */ {"SATURATED",          "Desiccant saturated.",                    "Run a Recharge before the next dryer cycle."},
    /*  4 */ {"SENSOR_NODE_SILENT", "Sensor node not responding.",            "Check the sensor node power and its antenna."},
    /*  5 */ {"TO_ENGINE_LOST",     "To-Engine sensor lost.",                  "Check the To-Engine probe and its cable."},
    /*  6 */ {"FROM_ENGINE_LOST",   "From-Engine sensor lost.",                "Check the From-Engine probe and its cable."},
    /*  7 */ {"CHAMBER_LOST",       "Chamber thermistor lost.",                "Check the thermistor and its cable."},
    /*  8 */ {"VENT_SENSOR_LOST",   "Vent pressure sensor lost.",              "Check the pressure module, its cable and its tube."},
    /*  9 */ {"VALVE_SUSPECT",      "Valve position suspect.",                 "Moisture may reach the engine. Check both valves and the vent line."},
    /* 10 */ {"VALVE_NO_CURRENT",   "Valve coil fault.",                       "Check the valve wiring and the 12 V supply."},
    /* 11 */ {"THERMAL_CHAMBER",    "Chamber over temperature.",               "Let it cool. Check the heater control and the thermistor."},
    /* 12 */ {"THERMAL_TO_PROBE",   "To-Engine probe over temperature.",       "Let it cool. Check where the probe is mounted."},
    /* 13 */ {"THERMAL_FROM_PROBE", "From-Engine probe over temperature.",     "Let it cool. Check where the probe is mounted."},
    /* 14 */ {"HEATUP_TIMEOUT",     "Chamber did not reach target temperature.", "Check the heater mat, its fuse and the thermistor."},
    /* 15 */ {"SAFETY_FAILED",      "Safety check failed.",                    "The desiccant is still wet. Run another Recharge."},
    /* 16 */ {"RECHARGE_COMPLETE",  "Recharge complete.",                      nullptr},
    /* 17 */ {"RECHARGE_MAX_TIME",  "Recharge reached its time limit.",        "The desiccant may not be fully dry. Consider another Recharge."},
    /* 18 */ {"CONTROLLER_RESTART", "Controller restarted unexpectedly.",      "All outputs were off at restart. Check the unit before starting again."},
    /* 19 */ {"HEATER_NO_RISE",     "Heater not heating.",                     "Check the heater mat, its fuse, its relay (K3) and the thermistor."},
    /* 20 */ {"VENT_BLOCKED",       "Vent line blocked.",                      "Check the vent line and outlet for a blockage or a closed valve."},
    /* 21 */ {"VENT_UNSTABLE",      "Vent pressure not steady.",               "Check the pressure module, its tube and cable, and the vent line."},
};

static const StopText& stopText(StopReason reason) {
    const size_t index = (size_t)reason;
    if (index >= sizeof(STOP_TEXTS) / sizeof(STOP_TEXTS[0])) return STOP_TEXTS[0];
    return STOP_TEXTS[index];
}

const char* stopReasonName(StopReason reason) {
    return stopText(reason).name;
}

static void defaultStopDetail(StopReason reason, char* buf, size_t size) {
    const unsigned long graceSeconds = SENSOR_LOSS_GRACE_MS / 1000UL;
    switch (reason) {
        case StopReason::USER_REMOTE:
            snprintf(buf, size, "All outputs are off.");
            break;
        case StopReason::SATURATED:
            snprintf(buf, size, "To-Engine stayed above %.0f%% for %lu s with the pump on.",
                     SAT_LIMIT_RH, (unsigned long)(SAT_HOLD_MS / 1000UL));
            break;
        case StopReason::SENSOR_NODE_SILENT:
            snprintf(buf, size, "No radio data from the sensor node for %lu s.", graceSeconds);
            break;
        case StopReason::TO_ENGINE_LOST:
            snprintf(buf, size, "No valid To-Engine reading for %lu s.", graceSeconds);
            break;
        case StopReason::FROM_ENGINE_LOST:
            snprintf(buf, size, "No valid From-Engine reading for %lu s.", graceSeconds);
            break;
        case StopReason::CHAMBER_LOST:
            snprintf(buf, size, "No valid chamber temperature for %lu s.", graceSeconds);
            break;
        case StopReason::VENT_SENSOR_LOST:
            snprintf(buf, size, "No vent pressure reading, so the valve position could not be confirmed.");
            break;
        case StopReason::VALVE_SUSPECT:
            snprintf(buf, size, "A pump pulse did not raise the vent pressure %ld counts.",
                     (long)VENT_PRESSURE_MIN_STEP);
            break;
        case StopReason::VALVE_NO_CURRENT:
            snprintf(buf, size, "No coil current while the valves were energized.");
            break;
        case StopReason::THERMAL_CHAMBER:
            snprintf(buf, size, "Chamber passed %d F. All outputs off.", THERMAL_CHAMBER_F);
            break;
        case StopReason::THERMAL_TO_PROBE:
            snprintf(buf, size, "To-Engine probe passed %d F. All outputs off.", THERMAL_SENSOR_F);
            break;
        case StopReason::THERMAL_FROM_PROBE:
            snprintf(buf, size, "From-Engine probe passed %d F. All outputs off.", THERMAL_SENSOR_F);
            break;
        case StopReason::HEATUP_TIMEOUT:
            snprintf(buf, size, "Still below %d F after %lu min of heating.",
                     RECHARGE_TARGET_TEMP_F, (unsigned long)(RECHARGE_HEATUP_MAX_MS / 60000UL));
            break;
        case StopReason::HEATER_NO_RISE:
            snprintf(buf, size, "Chamber rose less than %.0f F in %lu min of heater-on time.",
                     HEATER_MIN_RISE_F, (unsigned long)(HEATER_RESPONSE_MS / 60000UL));
            break;
        case StopReason::VENT_BLOCKED:
            snprintf(buf, size, "A pump pulse raised the vent pressure more than %ld counts.",
                     (long)VENT_PRESSURE_BLOCKED_STEP);
            break;
        case StopReason::VENT_UNSTABLE:
            snprintf(buf, size, "The vent pressure was not steady for %lu s, so no pump pulse could be checked.",
                     (unsigned long)(VENT_REF_WAIT_MAX_MS / 1000UL));
            break;
        case StopReason::SAFETY_FAILED:
            snprintf(buf, size, "To-Engine was above %.0f%% after the cool down.", RECHARGE_TARGET_RH);
            break;
        case StopReason::RECHARGE_COMPLETE:
            snprintf(buf, size, "%u dry samples, then %u safety samples at or below %.0f%%.",
                     (unsigned)RECHARGE_GOOD_CYCLES, (unsigned)SAFETY_SAMPLE_COUNT, RECHARGE_TARGET_RH);
            break;
        case StopReason::RECHARGE_MAX_TIME:
            snprintf(buf, size, "%lu h at temperature. Cool down and safety check passed.",
                     (unsigned long)(RECHARGE_MAX_MS / 3600000UL));
            break;
        default:
            if (size > 0) buf[0] = '\0';
            break;
    }
}

static void appendLine(char* buf, size_t size, size_t &used, const char* line) {
    if (line == nullptr || line[0] == '\0' || used >= size) return;
    const int written = snprintf(buf + used, size - used, "%s%s", used > 0 ? "\n" : "", line);
    if (written > 0) used += (size_t)written;
    if (used >= size) used = size - 1;
}

void formatStopMessage(char* buf, size_t size, StopReason reason, SystemMode mode,
                       const char* detail, bool afterCooldown) {
    if (buf == nullptr || size == 0) return;
    buf[0] = '\0';
    size_t used = 0;
    const StopText& text = stopText(reason);
    char fallback[128];
    if (detail == nullptr) {
        defaultStopDetail(reason, fallback, sizeof(fallback));
        detail = fallback;
    }
    appendLine(buf, size, used, text.title);
    appendLine(buf, size, used, detail);
    appendLine(buf, size, used, text.action);
    if (afterCooldown) {
        appendLine(buf, size, used, "The chamber was cooled first, with the valves venting outside.");
    }
    char codeLine[48];
    if (mode == SystemMode::IDLE) {
        snprintf(codeLine, sizeof(codeLine), "Code S%02u", (unsigned)reason);
    } else {
        snprintf(codeLine, sizeof(codeLine), "%s ended. Code S%02u",
                 systemModeName(mode), (unsigned)reason);
    }
    appendLine(buf, size, used, codeLine);
}

/* One line per stop: "Stop: S09 VALVE_SUSPECT mode=Recharge | <message>".
   Analysis scripts look for lines starting "Stop:". */
static void printStopLine(StopReason reason, SystemMode mode, const char* message) {
    Serial.printf("Stop: S%02u %s mode=%s", (unsigned)reason, stopReasonName(reason),
                  systemModeName(mode));
    if (message != nullptr) {
        Serial.print(F(" | "));
        for (const char* c = message; *c != '\0'; ++c) {
            if (*c == '\n') Serial.print(F(" | "));
            else Serial.print(*c);
        }
    }
    Serial.println();
}

StateMachine::StateMachine()
    : _mode(SystemMode::IDLE)
    , _pendingCmd(Command::NONE)
    , _pendingSource(CommandSource::LOCAL)
    , _autoRechargeArmed(false)
    , _stopReason(StopReason::NONE)
    , _stopMessage(nullptr)
    , _manualPhase(ManualPhase::ON)
    , _autoPhase(AutoPhase::DRYING)
    , _rechargePhase(RechargePhase::HEATING)
    , _modeStartTime(0)
    , _phaseStartTime(0)
    , _saturationMark(0)
    , _lastLogTime(0)
    , _inGracePeriod(false)
    , _saturationTiming(false)
    , _pumpOn(false)
    , _heaterOn(false)
    , _fanOn(false)
    , _appliedPump(false)
    , _appliedHeater(false)
    , _appliedFan(false)
    , _chipHot(false)
    , _chipTempC(NAN)
    , _valveEnergized(false)
    , _appliedValve(false)
    , _rechargeTriggeredByCycle(false)
    , _rechargeResumeMode(SystemMode::IDLE)
    , _rechargeFailure(false)
    , _rechargeFailureReason(StopReason::NONE)
    , _chamberReachedTarget(false)
    , _chamberReachedMs(0)
    , _rechargeTimedOut(false)
    , _sensorLost(false)
    , _sensorLostSinceMs(0)
    , _ventRef(0)
    , _ventRefFrozen(false)
    , _ventRefWaitStartMs(0)
    , _ventGoodPulses(0)
    , _ventBadPulses(0)
    , _ventRefCount(0)
    , _ventRefHead(0)
    , _ventRefLastStamp(0)
    , _ventPulsePeak(0)
    , _ventPulseSamples(0)
    , _ventPulseLastStamp(0)
    , _ventLastStep(0)
    , _ventLastStepValid(false)
    , _ventLastRef(0)
    , _cooldownWindow(UINT32_MAX)
    , _cooldownPulseRun(false)
    , _valveNoCurrentCount(0)
    , _valveCommandMs(0)
    , _valveCheckMs(0)
    , _autoQuickCycles(0)
    , _safetyCheckCount(0)
    , _rechargeStartEpoch(0)
    , _rechargeEndEpoch(0)
    , _purgeSeen(false)
    , _purgeQualified(false)
    , _thermalLatched(false)
    , _purgeEpoch(0)
    , _rechargeGoodCycles(0)
    , _heaterCheckArmed(false)
    , _heaterCheckLowF(0.0f)
    , _heaterOnMs(0)
    , _heaterCheckLastMs(0)
    , _probeDryAsked(false)
    , _saturationConfirming(false)
    , _saturationConfirmMs(0)
{
    _stopText[0] = '\0';
    config.dryerOnMinutes = 3;
    config.dryerOffHours = 1;
    config.dryerTargetRH = 20;
    config.allowAutoRecharge = false;
}

void StateMachine::begin() {
    _mode = SystemMode::IDLE;
    _pendingCmd = Command::NONE;
    _stopReason = StopReason::NONE;
    _stopMessage = nullptr;
    _pumpOn = false;
    _heaterOn = false;
    _fanOn = false;
    _valveEnergized = false;
    _appliedValve = false;
    _rechargeTriggeredByCycle = false;
    _rechargeResumeMode = SystemMode::IDLE;
    _rechargeFailure = false;
    _rechargeFailureReason = StopReason::NONE;
    _chamberReachedTarget = false;
    _rechargeTimedOut = false;
    _ventRef = 0;
    _ventRefFrozen = false;
    _ventRefWaitStartMs = 0;
    _ventRefCount = 0;
    _ventRefHead = 0;
    _ventRefLastStamp = 0;
    _ventLastRef = 0;
    _ventGoodPulses = 0;
    _ventBadPulses = 0;
    _pulseCount = 0;
    _valveNoCurrentCount = 0;
    _valveCommandMs = 0;
    _valveCheckMs = 0;
    _autoQuickCycles = 0;
    _safetyCheckCount = 0;
    _rechargeStartEpoch = 0;
    _rechargeEndEpoch = 0;
    _appliedPump = false;
    _appliedHeater = false;
    _appliedFan = false;
    _rechargeGoodCycles = 0;
    resetSaturation();
    _purgeEpoch = systemClockSavedPurge();
    _purgeSeen = _purgeEpoch != 0;
    relay.allOff();
}

bool StateMachine::handleCommand() {
    if (_pendingCmd == Command::NONE) return false;
    Command cmd = _pendingCmd;
    const CommandSource source = _pendingSource;
    _pendingCmd = Command::NONE;
    _pendingSource = CommandSource::LOCAL;
    if (cmd == Command::STOP) {
        stopWith(StopReason::USER);
        return true;
    }
    if (cmd == Command::STOP_REMOTE) {
        stopWith(StopReason::USER_REMOTE);
        return true;
    }
    if (_mode != SystemMode::IDLE || startBlockReason(cmd) != nullptr) return true;
    // Auto Recharge is armed per cycle, only for a dryer started at the screen.
    _autoRechargeArmed = source == CommandSource::LOCAL && config.allowAutoRecharge &&
                         (cmd == Command::START_DRYER_MANUAL || cmd == Command::START_DRYER_AUTO);
    if (_autoRechargeArmed) Serial.println(F("Auto Recharge armed for this cycle"));
    if (cmd == Command::START_PURGE) enterMode(SystemMode::PURGE);
    else if (cmd == Command::START_DRYER_MANUAL) {
        _autoQuickCycles = 0;
        enterMode(SystemMode::DRYER_MANUAL);
    }
    else if (cmd == Command::START_DRYER_AUTO) {
        _autoQuickCycles = 0;
        enterMode(SystemMode::DRYER_AUTO);
    }
    else if (cmd == Command::START_RECHARGE) {
        _rechargeTriggeredByCycle = false;
        _rechargeResumeMode = SystemMode::IDLE;
        enterMode(SystemMode::RECHARGE);
    }
    return true;
}

void StateMachine::update() {
    checkThermal();
    trackVentReference();
    if (handleCommand()) {
        updateFan();
        applyOutputs();
        return;
    }
    if (_mode != SystemMode::IDLE) {
        const bool sensorsOk = sensorsOkForMode();
        // Sensor dropout grace. The sensor node sends on ESP-NOW broadcast, which has no
        // retry, so a lost frame or two is normal. A 3.2 s gap (two lost packets) used to
        // stop a cycle at once. Required readings may be missing for SENSOR_LOSS_GRACE_MS.
        if (!sensorsOk) {
            if (!_sensorLost) {
                _sensorLost = true;
                _sensorLostSinceMs = millis();
                Serial.println(F("Sensor dropout - holding"));
            }
        } else if (_sensorLost) {
            _sensorLost = false;
            Serial.printf("Sensors back after %lu ms\n",
                          (unsigned long)(millis() - _sensorLostSinceMs));
        }
        if (sensorsOk && _inGracePeriod && (millis() - _modeStartTime >= GRACE_MS)) {
            _inGracePeriod = false;
        }
        if (_mode == SystemMode::RECHARGE) {
            // Recharge handles a dropout itself: no heat without a fresh chamber
            // reading, no pump pulse the vent check cannot confirm, and a failure
            // cool down that still advances if the sensors never come back.
            updateRecharge(sensorsOk);
        } else if (!sensorsOk) {
            // Purge and the dryers hold their outputs (no new decisions), then stop.
            if (millis() - _sensorLostSinceMs >= SENSOR_LOSS_GRACE_MS) {
                stopWith(sensorLossReason());
            }
        } else {
            switch (_mode) {
                case SystemMode::PURGE:        updatePurge(); break;
                case SystemMode::DRYER_MANUAL: updateManual(); break;
                case SystemMode::DRYER_AUTO:   updateAuto(); break;
                default: break;
            }
        }
        if (_mode != SystemMode::IDLE && sensorsOk) checkSaturation();
        if (_mode != SystemMode::IDLE && sensorsOk) maybeLog();
    }
    checkThermal();
    updateFan();
    applyOutputs();
}

const char* StateMachine::startBlockReason(Command cmd) const {
    if (cmd == Command::STOP || cmd == Command::STOP_REMOTE || cmd == Command::NONE) {
        return "Invalid start.";
    }
    if (_mode != SystemMode::IDLE) return "A cycle is already running.";
    if (relay.testActive()) return "A relay test pulse is running.\nWait two seconds and try again.";
    float fahrenheit = NAN;
    if (thermalFault(fahrenheit) != StopReason::NONE) {
        return "Temperature is above the safety limit.\nLet the system cool and try again.";
    }
    switch (cmd) {
        case Command::START_PURGE:
        case Command::START_DRYER_MANUAL:
            if (!toEngineReady()) {
                return "To-Engine sensor unavailable.\nCheck the sensor node and try again.";
            }
            return nullptr;
        case Command::START_DRYER_AUTO:
            if (!toEngineReady() || !fromEngineReady()) {
                return "Auto Cycle needs both sensors.\nCheck the sensor node and try again.";
            }
            return nullptr;
        case Command::START_RECHARGE:
            return rechargeSensorBlockReason();
        default:
            return "Unknown command.";
    }
}

/* Readings a recharge needs before it may start, whether from the screen or
   from a saturated dryer cycle. nullptr when they are all present. The vent
   pressure sensor is required (2026-11-24): without it the valve position
   cannot be confirmed, so no recharge pump pulse may run. */
const char* StateMachine::rechargeSensorBlockReason() const {
    if (!toEngineReady() || !chamberReady()) {
        return "Recharge needs To-Engine and\nthe chamber thermistor.";
    }
    if (!ventSensorOk()) {
        return "Recharge needs the vent pressure sensor.\nCheck the pressure module and its cable.";
    }
    return nullptr;
}

/* Vent pressure reading present, or the check is compiled out. */
bool StateMachine::ventSensorOk() const {
#if VENT_PRESSURE_CHECK_ENABLED
    return pressureReady();
#else
    return true;
#endif
}

bool StateMachine::command(Command cmd, CommandSource source) {
    if (cmd == Command::NONE) return false;
    const bool stop = cmd == Command::STOP || cmd == Command::STOP_REMOTE;
    if (!stop && startBlockReason(cmd) != nullptr) {
        Serial.println(startBlockReason(cmd));
        return false;
    }
    _pendingCmd = cmd;
    _pendingSource = source;
    return true;
}

void StateMachine::reportRestart(SystemMode interrupted, const char* cause) {
    _stopReason = StopReason::CONTROLLER_RESTART;
    formatStopMessage(_stopText, sizeof(_stopText), StopReason::CONTROLLER_RESTART,
                      interrupted, cause, false);
    _stopMessage = _stopText;
    printStopLine(StopReason::CONTROLLER_RESTART, interrupted, _stopMessage);
    logger.logStop((uint8_t)StopReason::CONTROLLER_RESTART,
                   stopReasonName(StopReason::CONTROLLER_RESTART), interrupted, _stopMessage);
}

const char* StateMachine::takeStopMessage() {
    const char* message = _stopMessage;
    _stopReason = StopReason::NONE;
    _stopMessage = nullptr;
    return message;
}

void StateMachine::enterMode(SystemMode newMode) {
    SystemMode oldMode = _mode;
    _mode = newMode;
    _modeStartTime = millis();
    _phaseStartTime = _modeStartTime;
    _inGracePeriod = true;
    _rechargeGoodCycles = 0;
    _stopReason = StopReason::NONE;
    _stopMessage = nullptr;
    _lastLogTime = millis();
    resetSaturation();
    _heaterOn = false;
    _pumpOn = false;
    _fanOn = false;
    _safetyCheckCount = 0;
    _sensorLost = false;
    switch (newMode) {
        case SystemMode::PURGE:
            _pumpOn = true;
            _purgeQualified = false;
            break;
        case SystemMode::DRYER_MANUAL:
            _autoQuickCycles = 0;
            _manualPhase = ManualPhase::ON;
            _pumpOn = true;
            break;
        case SystemMode::DRYER_AUTO:
            _autoQuickCycles = 0;
            // Start with a real sample: run the pump so engine air reaches the From-Engine
            // sensor, settle, then read. Starting in DRYING read the first packet right
            // after the pump started, which was only dry air sitting in the chamber.
            // setAutoPhase() prints "Auto phase 4" so the first sample shows in the log.
            setAutoPhase(AutoPhase::SAMPLE_RUN);
            _pumpOn = true;
            break;
        case SystemMode::RECHARGE:
            _rechargePhase = RechargePhase::HEATING;
            _heaterOn = true;
            _valveEnergized = true;
            setValve(true);
            _rechargeStartEpoch = systemClockEpoch();
            _rechargeFailure = false;
            _rechargeFailureReason = StopReason::NONE;
            _chamberReachedTarget = false;
            _rechargeTimedOut = false;
            _ventRef = 0;
            _ventRefFrozen = false;
            _ventRefWaitStartMs = 0;
            _ventRefCount = 0;
            _ventRefHead = 0;
            _ventRefLastStamp = 0;
            _ventLastRef = 0;
            _ventGoodPulses = 0;
            _ventBadPulses = 0;
            _ventPulsePeak = 0;
            _ventPulseSamples = 0;
            _ventPulseLastStamp = 0;
            _ventLastStep = 0;
            _ventLastStepValid = false;
            _cooldownWindow = UINT32_MAX;
            _cooldownPulseRun = false;
            _pulseCount = 0;
            _valveNoCurrentCount = 0;
            _valveCommandMs = millis();
            _valveCheckMs = 0;
            _heaterCheckArmed = false;
            _heaterOnMs = 0;
            _heaterCheckLastMs = millis();
            Serial.println(F("Recharge started - valve open loop"));
            break;
        default:
            break;
    }
    logger.logModeChange(oldMode, newMode);
    runStateSave(newMode);
    Serial.print(F("Mode -> "));
    Serial.println(systemModeName(newMode));
}

void StateMachine::stopWith(StopReason reason, const char* detail, bool afterCooldown) {
    SystemMode oldMode = _mode;
    _mode = SystemMode::IDLE;
    _pumpOn = false;
    _heaterOn = false;
    _fanOn = false;
    _valveEnergized = false;
    _appliedValve = false;
    setValve(false);
    _inGracePeriod = false;
    _pendingCmd = Command::NONE;
    _sensorLost = false;
    _autoRechargeArmed = false;
    resetSaturation();
    relay.allOff();
    _appliedPump = false;
    _appliedHeater = false;
    _appliedFan = false;
    if (oldMode == SystemMode::IDLE) {
        // Nothing was running (for example Stop while idle). Outputs are forced
        // off above. No new message, and a message still waiting is kept.
        return;
    }
    _stopReason = reason;
    if (oldMode == SystemMode::RECHARGE) {
        _rechargeEndEpoch = systemClockEpoch();
        _rechargeFailure = false;
        _chamberReachedTarget = false;
        _rechargeTimedOut = false;
        _rechargeTriggeredByCycle = false;
        _rechargeResumeMode = SystemMode::IDLE;
        _valveNoCurrentCount = 0;
        Serial.println(F("Recharge ended - valve closed loop"));
        probeCareRechargeEnded();
    }
    if (reason == StopReason::USER) {
        _stopMessage = nullptr;  // The operator pressed Stop; nothing to explain.
    } else {
        formatStopMessage(_stopText, sizeof(_stopText), reason, oldMode, detail, afterCooldown);
        _stopMessage = _stopText;
    }
    logger.logModeChange(oldMode, SystemMode::IDLE);
    runStateSave(SystemMode::IDLE);
    printStopLine(reason, oldMode, _stopMessage);
    logger.logStop((uint8_t)reason, stopReasonName(reason), oldMode, _stopMessage);
}

void StateMachine::updatePurge() {
    _pumpOn = true;
    _heaterOn = false;
    if (!_purgeQualified && (millis() - _modeStartTime) >= PURGE_QUALIFY_MS) {
        _purgeQualified = true;
        _purgeSeen = true;
        _purgeEpoch = systemClockEpoch();
        systemClockSavePurge();
        Serial.println(F("Purge qualified after 60 seconds"));
    }
}

void StateMachine::updateManual() {
    _heaterOn = false;
    uint32_t onMs = (uint32_t)config.dryerOnMinutes * 60000UL;
    uint32_t offMs = (uint32_t)config.dryerOffHours * 3600000UL;
    if (_manualPhase == ManualPhase::ON) {
        _pumpOn = true;
        if (phaseElapsed() >= onMs) setManualPhase(ManualPhase::OFF);
    } else {
        _pumpOn = false;
        if (phaseElapsed() >= offMs) setManualPhase(ManualPhase::ON);
    }
}

void StateMachine::updateAuto() {
    _heaterOn = false;
    float target = (float)config.dryerTargetRH;
    switch (_autoPhase) {
        case AutoPhase::DRYING:
            _pumpOn = true;
            // Do not trust a reading until the pump has run long enough to bring engine
            // air to the sensor. The first packets after the pump starts are old air.
            if (phaseElapsed() >= AUTO_SAMPLE_RUN_MS && fromEngineSampleReady() &&
                !probeBusy(Probe::FROM_ENGINE) && sensors.fromEngineRH <= target) {
                _autoQuickCycles = 0;
                setAutoPhase(AutoPhase::WAITING);
            } else if (phaseElapsed() >= AUTO_MAX_RUN_MS) {
                _pumpOn = false;
                setAutoPhase(AutoPhase::COOLDOWN);
            }
            break;
        case AutoPhase::COOLDOWN:
            _pumpOn = false;
            if (phaseElapsed() >= AUTO_COOLDOWN_MS) {
                setAutoPhase(AutoPhase::QUICK_SAMPLE);
            }
            break;
        case AutoPhase::QUICK_SAMPLE:
            // Pump first so engine air reaches the sensor, then a short settle, then read.
            // This used to wait with the pump off and read stale chamber air.
            _pumpOn = phaseElapsed() < AUTO_SAMPLE_RUN_MS;
            if (phaseElapsed() < AUTO_SAMPLE_RUN_MS + AUTO_SAMPLE_SETTLE_MS) break;
            if (!fromEngineSampleReady()) break;
            // R1: the probe was dried at the start of this sample; wait for it to recover.
            if (probeBusy(Probe::FROM_ENGINE) &&
                phaseElapsed() < AUTO_SAMPLE_RUN_MS + AUTO_SAMPLE_SETTLE_MS + AUTO_PROBE_WAIT_MAX_MS) break;
            if (sensors.fromEngineRH <= target) {
                _autoQuickCycles = 0;
                setAutoPhase(AutoPhase::WAITING);
            } else if (_autoQuickCycles < AUTO_MAX_QUICK_CYCLES) {
                _autoQuickCycles++;
                setAutoPhase(AutoPhase::DRYING);
            } else {
                _autoQuickCycles = 0;
                setAutoPhase(AutoPhase::WAITING);
            }
            break;
        case AutoPhase::WAITING:
            _pumpOn = false;
            if (phaseElapsed() >= AUTO_SAMPLE_INTERVAL_MS) {
                setAutoPhase(AutoPhase::SAMPLE_RUN);
            }
            break;
        case AutoPhase::SAMPLE_RUN:
            _pumpOn = true;
            if (phaseElapsed() >= AUTO_SAMPLE_RUN_MS) {
                setAutoPhase(AutoPhase::SAMPLE_SETTLE);
            }
            break;
        case AutoPhase::SAMPLE_SETTLE:
            _pumpOn = false;
            if (phaseElapsed() < AUTO_SAMPLE_SETTLE_MS) break;
            if (!fromEngineSampleReady()) break;
            // R1: the probe was dried at the start of this sample; wait for it to recover.
            if (probeBusy(Probe::FROM_ENGINE) && phaseElapsed() < AUTO_SAMPLE_SETTLE_MS + AUTO_PROBE_WAIT_MAX_MS) break;
            if (sensors.fromEngineRH > target) setAutoPhase(AutoPhase::DRYING);
            else setAutoPhase(AutoPhase::WAITING);
            break;
    }
}

void StateMachine::updateRecharge(bool sensorsOk) {
    /* Sensor loss (2026-11-24). While required readings are missing there is no
       heat without a fresh chamber reading and no pump pulse that the vent check
       cannot confirm. When the grace runs out, the failure cool down starts once
       and then advances on time even if the sensors never come back. */
    const bool lossExpired = !sensorsOk && _sensorLost &&
                             millis() - _sensorLostSinceMs >= SENSOR_LOSS_GRACE_MS;
    if (lossExpired && !_rechargeFailure) {
        rechargeFailureCooldown(sensorLossReason());
        if (_mode != SystemMode::RECHARGE) return;
    }

    // Heat-up backstop. The max run time below only starts once the chamber reaches
    // target, so a heater or thermistor fault that never gets there needs its own limit.
    // The chamber may be hot, so it cools down with the valves venting before it stops.
    if (!_chamberReachedTarget && _rechargePhase == RechargePhase::HEATING &&
        millis() - _modeStartTime >= RECHARGE_HEATUP_MAX_MS) {
        Serial.println(F("Recharge heat-up timed out - chamber never reached target"));
        rechargeFailureCooldown(StopReason::HEATUP_TIMEOUT);
        if (_mode != SystemMode::RECHARGE) return;
    }
    if (!_rechargeTimedOut && _chamberReachedTarget &&
        millis() - _chamberReachedMs >= RECHARGE_MAX_MS &&
        (_rechargePhase == RechargePhase::PULSE_ON ||
         _rechargePhase == RechargePhase::PULSE_WAIT)) {
        // Max run time, counted from the moment the chamber first reached target
        // (heat-up does not count). Latch it once so it cannot re-fire, never resume
        // an earlier cycle, and run the normal cool down and safety check before
        // stopping. A cycle already in cool down or the safety check finishes
        // normally and is reported as complete.
        _rechargeTimedOut = true;
        _rechargeTriggeredByCycle = false;
        Serial.println(F("Recharge 6h max at temperature - cool down, then safety check, no resume"));
        setRechargePhase(RechargePhase::COOLDOWN);
    }
    checkValveCurrent();
    if (_mode != SystemMode::RECHARGE) return;  // Coil fault stopped the cycle.

    const bool cooling = _rechargePhase == RechargePhase::COOLDOWN ||
                         _rechargePhase == RechargePhase::SAFETY_PULSE ||
                         _rechargePhase == RechargePhase::SAFETY_WAIT ||
                         _rechargePhase == RechargePhase::SAFETY_PREP;
    if (cooling) {
        _heaterOn = false;
        _heaterCheckArmed = false;
    } else {
        if (sensorsOk || chamberReady()) {
            regulateHeater();  // may hold the previous command briefly; never newly heats on a stale reading
        } else {
            _heaterOn = false;
        }
        checkHeaterResponse();
        if (_mode != SystemMode::RECHARGE) return;
    }

    if (!sensorsOk && _rechargePhase != RechargePhase::COOLDOWN) {
        holdRechargeForSensors();
        return;
    }

    switch (_rechargePhase) {
        case RechargePhase::HEATING:
            _pumpOn = false;
            if (chamberReady() &&
                celsiusToFahrenheit(sensors.chamberTemp) >= (float)RECHARGE_TARGET_TEMP_F) {
                _chamberReachedTarget = true;
                _chamberReachedMs = millis();  // the long-pulse hour counts from here, not from start
                setRechargePhase(RechargePhase::PULSE_ON);
            }
            break;
        case RechargePhase::PULSE_ON: {
            // The pulse is judged against the pump-off pressure just before it. No
            // steady reference, no pulse: the pump stays off (fails closed after 2 min).
            if (!_ventRefFrozen && !beginVentPulse()) break;
            _pumpOn = true;
            trackVentPulse();  // highest vent pressure at ANY point in this pulse
            // Pump on time: 10 s for the first hour after the chamber first reaches
            // target (heat-up time does not count). After that hour,
            // RECHARGE_LONG_PULSE_ON_MS (also 10 s since 2026-11-23). Off time stays 1 minute.
            const bool soaked = _chamberReachedTarget &&
                                (millis() - _chamberReachedMs) >= RECHARGE_LONG_PULSE_AFTER_MS;
            const uint32_t pulseMs = (_pulseCount == 0)
                                         ? RECHARGE_FIRST_PULSE_MS
                                         : (soaked ? RECHARGE_LONG_PULSE_ON_MS
                                                   : RECHARGE_PULSE_ON_MS);
            if (phaseElapsed() < pulseMs) break;
            _pumpOn = false;
            const VentResult vent = judgeVentPulse("recharge");
            if (vent == VentResult::FAULT) return;  // Stopped with VALVE_SUSPECT.
            if (vent == VentResult::NOT_JUDGED) {
                setRechargePhase(RechargePhase::PULSE_WAIT);  // does not count as a pulse
                break;
            }
            if (_pulseCount < 255) _pulseCount++;
            // R1: the probe was dried during the wait. A reading taken while it is
            // still heating or recovering is not counted either way.
            const bool probeHeld = probeBusy(Probe::TO_ENGINE);
            if (toEngineSampleReady() && !probeHeld) {
                if (sensors.toEngineRH <= RECHARGE_TARGET_RH) {
                    if (_rechargeGoodCycles < 255) _rechargeGoodCycles++;
                } else {
                    _rechargeGoodCycles = 0;
                }
            }
            Serial.printf("Recharge sample %.1f%% good %u/%u%s\n",
                          toEngineSampleReady() ? sensors.toEngineRH : -1.0f,
                          _rechargeGoodCycles, RECHARGE_GOOD_CYCLES,
                          probeHeld ? " (probe still drying, not counted)"
                                    : (probeDriedWithin(Probe::TO_ENGINE, RECHARGE_PULSE_WAIT_MS * 2)
                                           ? " (dried probe)" : " (probe not dried)"));
            if (_rechargeGoodCycles >= RECHARGE_GOOD_CYCLES) {
                setRechargePhase(RechargePhase::COOLDOWN);
                return;
            }
            setRechargePhase(RechargePhase::PULSE_WAIT);
            break;
        }
        case RechargePhase::PULSE_WAIT:
            _pumpOn = false;
            // R1: dry the To-Engine probe during the wait so the next pulse-end
            // sample reads the exhaust, not water left on the chip. The pump is off
            // here, so the probe has cooled from the hot exhaust (heater limit 60 C).
            if (!_probeDryAsked && phaseElapsed() >= RECHARGE_PROBE_DRY_AT_MS) {
                _probeDryAsked = true;
                probeDry(Probe::TO_ENGINE, PROBE_DRY_PULSES, "before a recharge sample");
            }
            if (phaseElapsed() >= RECHARGE_PULSE_WAIT_MS) {
                setRechargePhase(RechargePhase::PULSE_ON);
            }
            break;
        case RechargePhase::COOLDOWN: {
            _heaterOn = false;
            _fanOn = true;
            // Cool down: heater off, case fan on, valves OPEN, pump 10 s every 10 min
            // when the vent check can confirm it. Ends at 120 F or after 2 hours.
            // sensors.chamberTemp is Celsius, so convert before comparing. It was compared
            // to 120 as is, which is 248 F, so the cool down ended at once on every run.
            // Runs on time alone when the sensors are missing (failure cool down).
            const uint32_t cooldownMs = phaseElapsed();
            const bool coolReached = chamberReady() &&
                                     celsiusToFahrenheit(sensors.chamberTemp) <= RECHARGE_FAILED_FAN_TEMP_F;
            if (coolReached || cooldownMs >= RECHARGE_COOLDOWN_MS) {
                _pumpOn = false;
                resetVentPulse();
                Serial.printf("Cool down done after %lu min: %s\n",
                              (unsigned long)(cooldownMs / 60000UL),
                              coolReached ? "chamber at or below 120 F" : "time limit");
                if (_rechargeFailure) {
                    StopReason reason = _rechargeFailureReason;
                    _valveEnergized = false;
                    _rechargeEndEpoch = systemClockEpoch();
                    stopWith(reason, nullptr, true);
                    return;
                }
                _safetyCheckCount = 0;
                setRechargePhase(RechargePhase::SAFETY_PREP);
                break;
            }
            runCooldownPulse(cooldownMs);
            break;
        }
        case RechargePhase::SAFETY_PREP: {
            // R1: the safety sample decides the cycle, and the probe has just spent
            // hours in saturated exhaust. Dry it first. Water only reads high, so if
            // the node refuses (probe too hot) the sample is still taken.
            _heaterOn = false;
            _pumpOn = false;
            if (!_probeDryAsked) {
                _probeDryAsked = true;
                probeDry(Probe::TO_ENGINE, PROBE_DRY_PULSES_LONG, "before a safety sample");
                break;
            }
            const bool ready = probeDriedWithin(Probe::TO_ENGINE, SAFETY_PREP_DRIED_MS) ||
                               (!probeBusy(Probe::TO_ENGINE) && probeRefused(Probe::TO_ENGINE));
            if (ready || phaseElapsed() >= SAFETY_PREP_MAX_MS) {
                if (!probeDriedWithin(Probe::TO_ENGINE, SAFETY_PREP_DRIED_MS)) {
                    Serial.println(F("Safety prep: To-Engine probe not dried, sample taken anyway"));
                }
                setRechargePhase(RechargePhase::SAFETY_PULSE);
            }
            break;
        }
        case RechargePhase::SAFETY_PULSE: {
            _heaterOn = false;
            if (!_ventRefFrozen && !beginVentPulse()) break;
            _pumpOn = true;
            trackVentPulse();
            if (phaseElapsed() < SAFETY_PULSE_MS) break;
            _pumpOn = false;
            const VentResult vent = judgeVentPulse("safety");
            if (vent == VentResult::FAULT) return;
            if (vent == VentResult::NOT_JUDGED) {
                setRechargePhase(RechargePhase::SAFETY_PULSE);  // run the pulse again
                break;
            }
            setRechargePhase(RechargePhase::SAFETY_WAIT);
            break;
        }
        case RechargePhase::SAFETY_WAIT:
            _heaterOn = false;
            _pumpOn = false;
            if (phaseElapsed() >= SAFETY_SAMPLE_INTERVAL_MS) {
                if (!toEngineSampleReady()) break;
                if (sensors.toEngineRH <= RECHARGE_TARGET_RH) {
                    _safetyCheckCount++;
                    if (_safetyCheckCount >= SAFETY_SAMPLE_COUNT) {
                        _valveEnergized = false;
                        _rechargeEndEpoch = systemClockEpoch();
                        if (_rechargeTimedOut) {
                            Serial.println(F("Recharge timed out - cool down and safety check done"));
                            stopWith(StopReason::RECHARGE_MAX_TIME);
                            return;
                        }
                        if (_rechargeTriggeredByCycle &&
                            (_rechargeResumeMode == SystemMode::DRYER_MANUAL ||
                             _rechargeResumeMode == SystemMode::DRYER_AUTO)) {
                            SystemMode resumeMode = _rechargeResumeMode;
                            _rechargeTriggeredByCycle = false;
                            _rechargeResumeMode = SystemMode::IDLE;
                            Serial.println(F("Recharge complete - resuming previous cycle"));
                            probeCareRechargeEnded();
                            logger.logStop((uint8_t)StopReason::RECHARGE_COMPLETE,
                                           stopReasonName(StopReason::RECHARGE_COMPLETE),
                                           SystemMode::RECHARGE, nullptr);
                            enterMode(resumeMode);
                            return;
                        }
                        stopWith(StopReason::RECHARGE_COMPLETE);
                        return;
                    }
                } else {
                    Serial.printf("Safety sample above 15%% - fail safe to idle (%s)\n",
                                  probeDriedWithin(Probe::TO_ENGINE, SAFETY_PREP_DRIED_MS + SAFETY_SAMPLE_INTERVAL_MS)
                                      ? "dried probe" : "probe not dried");
                    _valveEnergized = false;
                    _rechargeEndEpoch = systemClockEpoch();
                    char detail[96];
                    snprintf(detail, sizeof(detail), "To-Engine read %.1f%% after the cool down. It must be %.0f%% or less.",
                             sensors.toEngineRH, RECHARGE_TARGET_RH);
                    stopWith(StopReason::SAFETY_FAILED, detail);
                    return;
                }
                setRechargePhase(RechargePhase::SAFETY_PREP);
            }
            break;
    }
}

/* Heater response check (user idea 2026-11-24). Tracks the lowest chamber
   reading since the heater last proved it works (a rise of HEATER_MIN_RISE_F,
   or reaching target) and the heater-ON time since then. Only fresh chamber
   readings count, and only time the heater relay was really on. */
void StateMachine::checkHeaterResponse() {
    const uint32_t now = millis();
    const uint32_t elapsed = now - _heaterCheckLastMs;
    _heaterCheckLastMs = now;
    if (_rechargeFailure || !chamberReady()) return;
    const float fahrenheit = celsiusToFahrenheit(sensors.chamberTemp);
    if (!_heaterCheckArmed) {
        _heaterCheckArmed = true;
        _heaterCheckLowF = fahrenheit;
        _heaterOnMs = 0;
        return;
    }
    if (fahrenheit < _heaterCheckLowF) _heaterCheckLowF = fahrenheit;
    if (fahrenheit >= _heaterCheckLowF + HEATER_MIN_RISE_F ||
        fahrenheit >= (float)RECHARGE_TARGET_TEMP_F) {
        _heaterCheckLowF = fahrenheit;  // the heater works; start a new window
        _heaterOnMs = 0;
        return;
    }
    if (_appliedHeater) _heaterOnMs += elapsed;
    if (_heaterOnMs < HEATER_RESPONSE_MS) return;
    Serial.printf("Heater response fault: %.0f F, low %.0f F, %lu s of heater-on time\n",
                  fahrenheit, _heaterCheckLowF, (unsigned long)(_heaterOnMs / 1000UL));
    rechargeFailureCooldown(StopReason::HEATER_NO_RISE);
}

/* Required recharge readings are missing but the grace has not run out. No new
   decisions. The pump stops at once: a pulse the vent check cannot judge must
   not run. A cut pulse is not judged and is run again once the sensors return. */
void StateMachine::holdRechargeForSensors() {
    switch (_rechargePhase) {
        case RechargePhase::PULSE_ON:
            Serial.println(F("Recharge pulse cut short by a sensor dropout - not judged"));
            resetVentPulse();
            setRechargePhase(RechargePhase::PULSE_WAIT);
            break;
        case RechargePhase::SAFETY_PULSE:
            resetVentPulse();
            _phaseStartTime = millis();  // the full safety pulse runs once sensors are back
            break;
        default:
            break;
    }
    _pumpOn = false;
}

/* Cool down pump pulses (10 s every 10 min) push the hot, wet air out of the
   vent. A pulse runs only when the vent pressure check can confirm the air is
   going outside, and it is judged like a recharge pulse. Without vent pressure
   the cool down runs on the case fan alone (user decision 2026-11-24). */
void StateMachine::runCooldownPulse(uint32_t cooldownMs) {
    const uint32_t window = cooldownMs / RECHARGE_COOLDOWN_INTERVAL_MS;
    const bool inPulse = (cooldownMs % RECHARGE_COOLDOWN_INTERVAL_MS) < RECHARGE_COOLDOWN_PULSE_ON_MS;
    if (window != _cooldownWindow) {
        _cooldownWindow = window;
        resetVentPulse();
        _cooldownPulseRun = inPulse && freezeVentReference();
        if (inPulse && !_cooldownPulseRun) {
            Serial.println(F("Cool down pulse skipped - no steady vent pressure reference to confirm the valves"));
        }
    }
    if (!_cooldownPulseRun) {
        _pumpOn = false;
        return;
    }
    if (!ventCheckReady()) {
        Serial.println(F("Cool down pulse cut short - vent pressure lost, not judged"));
        _cooldownPulseRun = false;
        _pumpOn = false;
        resetVentPulse();
        return;
    }
    if (inPulse) {
        _pumpOn = true;
        trackVentPulse();
        return;
    }
    _pumpOn = false;
    _cooldownPulseRun = false;
    judgeVentPulse("cool down");  // a fault stops the cycle
}

bool StateMachine::purgeRecent() const {
    if (!_purgeSeen || _purgeEpoch == 0) return false;
    const time_t now = systemClockEpoch();
    if (now < _purgeEpoch) return false;
    return (uint32_t)(now - _purgeEpoch) * 1000UL <= PURGE_RECENT_MS;
}

uint32_t StateMachine::purgeAgeMs() const {
    if (!_purgeSeen || _purgeEpoch == 0) return 0;
    const time_t now = systemClockEpoch();
    if (now < _purgeEpoch) return 0;
    return (uint32_t)(now - _purgeEpoch) * 1000UL;
}

/* Which software thermal limit is exceeded, if any. fahrenheit returns the
   offending reading. Only fresh readings count; a stale one cannot trip or
   clear the limit. */
StopReason StateMachine::thermalFault(float &fahrenheit) const {
    if (chamberReady()) {
        fahrenheit = celsiusToFahrenheit(sensors.chamberTemp);
        if (fahrenheit > (float)THERMAL_CHAMBER_F) return StopReason::THERMAL_CHAMBER;
    }
    // A probe that is heating or recovering reads its own heated die, not the air.
    if (toEngineReady() && !probeBusy(Probe::TO_ENGINE)) {
        fahrenheit = celsiusToFahrenheit(sensors.toEngineTemp);
        if (fahrenheit > (float)THERMAL_SENSOR_F) return StopReason::THERMAL_TO_PROBE;
    }
    if (fromEngineReady() && !probeBusy(Probe::FROM_ENGINE)) {
        fahrenheit = celsiusToFahrenheit(sensors.fromEngineTemp);
        if (fahrenheit > (float)THERMAL_SENSOR_F) return StopReason::THERMAL_FROM_PROBE;
    }
    return StopReason::NONE;
}

void StateMachine::checkThermal() {
    float fahrenheit = NAN;
    const StopReason fault = thermalFault(fahrenheit);
    if (fault == StopReason::NONE) {
        _thermalLatched = false;
        return;
    }
    if (_thermalLatched) return;
    _thermalLatched = true;
    const bool chamber = fault == StopReason::THERMAL_CHAMBER;
    char detail[96];
    snprintf(detail, sizeof(detail), "%s read %.0f F, limit %d F. All outputs off.",
             chamber ? "Chamber" : (fault == StopReason::THERMAL_TO_PROBE ? "To-Engine probe"
                                                                         : "From-Engine probe"),
             fahrenheit, chamber ? THERMAL_CHAMBER_F : THERMAL_SENSOR_F);
    Serial.println(F("Thermal limit reached"));
    if (_mode != SystemMode::IDLE) {
        stopWith(fault, detail);
        return;
    }
    // Idle: nothing is running, but force everything off and tell the operator.
    _pumpOn = false;
    _heaterOn = false;
    _fanOn = false;
    relay.allOff();
    _appliedPump = false;
    _appliedHeater = false;
    _appliedFan = false;
    _stopReason = fault;
    formatStopMessage(_stopText, sizeof(_stopText), fault, SystemMode::IDLE, detail, false);
    _stopMessage = _stopText;
    printStopLine(fault, SystemMode::IDLE, _stopMessage);
    logger.logStop((uint8_t)fault, stopReasonName(fault), SystemMode::IDLE, _stopMessage);
}

/* Which required sensor is missing, for the stop code. A silent sensor node
   beats a single missing probe because it explains all of them. */
StopReason StateMachine::sensorLossReason() const {
    if (!sensorPacketFresh()) return StopReason::SENSOR_NODE_SILENT;
    if (!toEngineReady()) return StopReason::TO_ENGINE_LOST;
    switch (_mode) {
        case SystemMode::DRYER_AUTO:
            if (!fromEngineReady()) return StopReason::FROM_ENGINE_LOST;
            break;
        case SystemMode::RECHARGE:
            if (!chamberReady() && !chamberRetryPending()) return StopReason::CHAMBER_LOST;
            if (!ventSensorOk()) return StopReason::VENT_SENSOR_LOST;
            break;
        default:
            break;
    }
    // Sensors came back in the same pass; name the sensor this mode depends on most.
    return _mode == SystemMode::RECHARGE ? StopReason::CHAMBER_LOST : StopReason::TO_ENGINE_LOST;
}

void StateMachine::regulateHeater() {
    if (chamberRetryPending()) {
        // Hold the previous command briefly; never newly enable heat without a sample.
        // sensorsOkForMode() stops the cycle and all outputs after CHAMBER_RETRY_MS.
        return;
    }
    if (!chamberReady()) {
        _heaterOn = false;
        return;
    }
    const float fahrenheit = celsiusToFahrenheit(sensors.chamberTemp);
    if (_rechargePhase == RechargePhase::HEATING) {
        // Heat-up ramp (2026-11-21): heater 30 s on / 15 s off until the chamber
        // first reaches target, so the mat does not slam up to temperature. The
        // normal hysteresis hold below takes over once the target is reached.
        const uint32_t rampCycleMs = RECHARGE_RAMP_ON_MS + RECHARGE_RAMP_OFF_MS;
        _heaterOn = fahrenheit < (float)RECHARGE_TARGET_TEMP_F &&
                    (phaseElapsed() % rampCycleMs) < RECHARGE_RAMP_ON_MS;
        return;
    }
    if (fahrenheit >= (float)RECHARGE_TARGET_TEMP_F) {
        _heaterOn = false;
    } else if (fahrenheit <= (float)(RECHARGE_TARGET_TEMP_F - RECHARGE_TEMP_HYSTERESIS_F)) {
        _heaterOn = true;
    }
}

void StateMachine::setValve(bool on) {
    if (on) {
        relay.valveToOn();
        relay.valveFromOn();
    } else {
        relay.valveToOff();
        relay.valveFromOff();
    }
}

/* On any recharge failure, keep venting outside and cool the chamber before
   closing the valves and going idle. Entered once: the first failure is the
   one reported, and a cool down already under way keeps the time it has
   spent. If only the safety check was left, the chamber is already cool, so
   it stops at once. */
void StateMachine::rechargeFailureCooldown(StopReason reason) {
    if (_rechargeFailure) return;
    _rechargeFailure = true;
    _rechargeFailureReason = reason;
    _rechargeTriggeredByCycle = false;  // a failed recharge never resumes a dryer cycle
    _heaterOn = false;
    _pumpOn = false;
    resetVentPulse();
    Serial.printf("Recharge failure %s - cool down, valves stay open\n", stopReasonName(reason));
    switch (_rechargePhase) {
        case RechargePhase::SAFETY_PREP:
        case RechargePhase::SAFETY_PULSE:
        case RechargePhase::SAFETY_WAIT:
            _valveEnergized = false;
            _rechargeEndEpoch = systemClockEpoch();
            stopWith(reason, nullptr, true);
            return;
        case RechargePhase::COOLDOWN:
            break;
        default:
            setRechargePhase(RechargePhase::COOLDOWN);
            break;
    }
}

/* Store pump-off vent readings (2026-11-25). Every fresh pressure packet that
   arrives while the pump is off goes into a small ring; the median of the ones
   from the last VENT_REF_WINDOW_MS becomes the reference for the next pulse
   (freezeVentReference). The old running average could sit 15,000 counts below
   the real idle level once the chamber was hot, which made good pulses look
   short. Nothing is stored while the pump runs or while a pulse is prepared. */
void StateMachine::trackVentReference() {
#if VENT_PRESSURE_CHECK_ENABLED
    if (_pumpOn || _ventRefFrozen) return;
    if (!pressureReady()) return;
    if (sensors.pressureTimestamp == _ventRefLastStamp) return;  // same packet as before
    _ventRefLastStamp = sensors.pressureTimestamp;
    _ventRefValue[_ventRefHead] = sensors.ventPressureRaw;
    _ventRefMs[_ventRefHead] = millis();
    _ventRefHead = (uint8_t)((_ventRefHead + 1) % VENT_REF_RING);
    if (_ventRefCount < VENT_REF_RING) _ventRefCount++;
#endif
}

/* Take the reference for the pulse about to start: the median of the recent
   pump-off readings, provided there are enough of them and they agree. False
   means the pulse must not start. Always true with the check compiled out. */
bool StateMachine::freezeVentReference() {
#if VENT_PRESSURE_CHECK_ENABLED
    if (!pressureReady()) return false;
    const uint32_t now = millis();
    int32_t values[VENT_REF_RING];
    uint8_t n = 0;
    for (uint8_t i = 0; i < _ventRefCount; ++i) {
        if (now - _ventRefMs[i] <= VENT_REF_WINDOW_MS) values[n++] = _ventRefValue[i];
    }
    if (n < VENT_REF_MIN_SAMPLES) return false;
    for (uint8_t i = 1; i < n; ++i) {  // insertion sort, n <= 8
        const int32_t v = values[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && values[j] > v) { values[j + 1] = values[j]; j--; }
        values[j + 1] = v;
    }
    const int32_t spread = values[n - 1] - values[0];
    if (spread > VENT_REF_MAX_SPREAD) return false;
    _ventRef = values[n / 2];
    _ventRefFrozen = true;
    Serial.printf("Vent reference %ld raw (median of %u, spread %ld)\n",
                  (long)_ventRef, (unsigned)n, (long)spread);
    return true;
#else
    return true;
#endif
}

/* Start of a recharge or safety pulse: take the reference, or keep the pump off
   and wait. A reference that stays unusable for VENT_REF_WAIT_MAX_MS fails the
   recharge closed (S21): no pulse that cannot be checked is ever run. The pulse
   timer starts when the reference is taken. */
bool StateMachine::beginVentPulse() {
#if VENT_PRESSURE_CHECK_ENABLED
    if (freezeVentReference()) {
        if (_ventRefWaitStartMs != 0) Serial.println(F("Vent pressure steady - pulse starting"));
        _ventRefWaitStartMs = 0;
        _phaseStartTime = millis();
        return true;
    }
    _pumpOn = false;
    const uint32_t now = millis();
    if (_ventRefWaitStartMs == 0) {
        _ventRefWaitStartMs = now;
        Serial.println(F("Vent pressure not steady enough for a reference - pulse waits"));
    }
    if (now - _ventRefWaitStartMs >= VENT_REF_WAIT_MAX_MS) {
        _ventRefWaitStartMs = 0;
        Serial.println(F("Vent pressure never steadied - failing closed"));
        rechargeFailureCooldown(StopReason::VENT_UNSTABLE);
    }
    return false;
#else
    return true;
#endif
}

/* True while a pulse is running with its reference taken and a fresh reading.
   Always true with the check compiled out. */
bool StateMachine::ventCheckReady() const {
#if VENT_PRESSURE_CHECK_ENABLED
    return pressureReady() && _ventRefFrozen;
#else
    return true;
#endif
}

/* Start a new pulse window: forget the peak and the sample count. */
void StateMachine::resetVentPulse() {
    _ventRefFrozen = false;
    _ventPulsePeak = 0;
    _ventPulseSamples = 0;
    _ventPulseLastStamp = 0;
}

/* Called every loop while the pump runs. Counts each new pressure packet once
   and keeps the highest reading. Judging only the last packet before the
   phase timer was wrong (2026-11-21): the peak can come at any point. */
void StateMachine::trackVentPulse() {
#if VENT_PRESSURE_CHECK_ENABLED
    if (!pressureReady()) return;
    if (sensors.pressureTimestamp == _ventPulseLastStamp) return;  // same packet as before
    _ventPulseLastStamp = sensors.pressureTimestamp;
    if (_ventPulseSamples == 0 || sensors.ventPressureRaw > _ventPulsePeak) {
        _ventPulsePeak = sensors.ventPressureRaw;
    }
    if (_ventPulseSamples < 0xFFFF) _ventPulseSamples++;
#endif
}

/* Vent pressure verification. The pump draws air through the chamber and the
   dump valve sends the regen exhaust out the vent line. With the valve open
   the vent line must show a pressure step against the pump-off reading taken
   just before the pulse. No step means the dump valve did not open, so the
   exhaust is not leaving the system. A step far too big means the line is
   blocked (a closed valve or a pinched hose) and the pump is dead-heading.
   This is a positive check on the mechanical valve position and does not
   depend on humidity at all.

   A pulse is good only if its peak rose by VENT_PRESSURE_MIN_STEP or more and
   less than VENT_PRESSURE_BLOCKED_STEP. One bad pulse stops the cycle
   (VALVE_SUSPECT, or VENT_BLOCKED for a blocked line); each pulse stands
   alone, so a good pulse never hides a later disconnect. A pulse with no
   pressure sample at all is NOT judged and does not count: the callers never
   start a pulse without fresh pressure, so this only happens when the reading
   drops out mid-pulse, and the sensor-loss grace handles that (fails closed). */
StateMachine::VentResult StateMachine::judgeVentPulse(const char* label) {
#if VENT_PRESSURE_CHECK_ENABLED
    if (_ventPulseSamples == 0 || !_ventRefFrozen) {
        Serial.printf("Vent pressure: no samples in the %s pulse - not judged\n", label);
        resetVentPulse();
        return VentResult::NOT_JUDGED;
    }
    const int32_t peakStep = _ventPulsePeak - _ventRef;
    const bool blocked = peakStep >= VENT_PRESSURE_BLOCKED_STEP;
    const bool sawStep = peakStep >= VENT_PRESSURE_MIN_STEP && !blocked;
    if (sawStep) {
        if (_ventGoodPulses < 0xFFFF) _ventGoodPulses++;
    } else if (_ventBadPulses < 0xFFFF) {
        _ventBadPulses++;
    }
    _ventLastStep = peakStep;
    _ventLastRef = _ventRef;
    _ventLastStepValid = true;
    Serial.printf("Vent pressure raw %ld peak %ld step %ld %s good %u bad %u %s pulse\n",
                  (long)sensors.ventPressureRaw, (long)_ventPulsePeak, (long)peakStep,
                  sawStep ? "OK" : (blocked ? "BLOCKED" : "NO FLOW"), _ventGoodPulses, _ventBadPulses, label);
    resetVentPulse();
    if (blocked) {
        Serial.printf("Vent pressure fault - pulse rose %ld, line blocked\n", (long)peakStep);
        char detail[96];
        snprintf(detail, sizeof(detail), "Pump pulse raised the vent pressure %ld counts. More than %ld means the line is blocked.",
                 (long)peakStep, (long)VENT_PRESSURE_BLOCKED_STEP);
        stopWith(StopReason::VENT_BLOCKED, detail);
        return VentResult::FAULT;
    }
    if (!sawStep) {
        Serial.printf("Vent pressure fault - pulse did not rise %ld over the reference\n",
                      (long)VENT_PRESSURE_MIN_STEP);
        char detail[96];
        snprintf(detail, sizeof(detail), "Pump pulse raised the vent pressure %ld counts. It needs %ld.",
                 (long)peakStep, (long)VENT_PRESSURE_MIN_STEP);
        stopWith(StopReason::VALVE_SUSPECT, detail);
        return VentResult::FAULT;
    }
#else
    (void)label;
#endif
    return VentResult::OK;
}

/* Coil current supervision. The sensor node reads the 1-ohm shunt and reports
   millivolts in every packet. While the valve is commanded energized, the
   reading must stay above threshold; a dead coil, missing 12 V, or an open
   return wire shows up within a few seconds. Skipped when the sensor node is
   offline - the SENSOR_LOST path handles that case. */
void StateMachine::checkValveCurrent() {
#if VALVE_CURRENT_CHECK_ENABLED
    if (!_valveEnergized) {
        _valveNoCurrentCount = 0;
        return;
    }
    const uint32_t now = millis();
    if (now - _valveCommandMs < VALVE_CURRENT_GRACE_MS) return;
    if (now - _valveCheckMs < VALVE_CURRENT_CHECK_MS) return;
    _valveCheckMs = now;
    if (!sensorPacketFresh()) return;
    if (sensors.valveAdcMillivolts < VALVE_CURRENT_MIN_MV) {
        if (_valveNoCurrentCount < 255) _valveNoCurrentCount++;
    } else {
        _valveNoCurrentCount = 0;
    }
    if (_valveNoCurrentCount >= VALVE_CURRENT_FAULT_COUNT) {
        Serial.println(F("Valve coil fault - no current while commanded on"));
        stopWith(StopReason::VALVE_NO_CURRENT);
    }
#endif
}

void StateMachine::updateFan() {
    static uint32_t lastRead = 0;
    const uint32_t now = millis();
    if (now - lastRead >= 2000UL) {
        lastRead = now;
        const float chipC = temperatureRead();
        if (chipC == chipC) {
            _chipTempC = chipC;
            if (chipC >= CASE_FAN_CHIP_ON_C) _chipHot = true;
            else if (chipC <= CASE_FAN_CHIP_OFF_C) _chipHot = false;
        }
    }
    _fanOn = _pumpOn || _mode == SystemMode::RECHARGE || _chipHot;
}

void StateMachine::checkSaturation() {
    // Recharge ignores the 40% limit. During regeneration the To-Engine sensor sits in
    // the hot, wet exhaust and reads near 100% by design. The longer pulses used after the
    // first hour can last as long as SAT_HOLD_MS, so without this exclusion they tripped a
    // false "Desiccant saturated" stop.
    if (_mode == SystemMode::IDLE || _mode == SystemMode::RECHARGE || _inGracePeriod) {
        resetSaturation();
        return;
    }
    // A heating or recovering probe reads low (heated die), so neither start,
    // reset nor finish the timer on it: hold.
    if (probeBusy(Probe::TO_ENGINE)) return;
    if (!_pumpOn || !toEngineReady() || sensors.toEngineRH <= SAT_LIMIT_RH) {
        resetSaturation();
        return;
    }
    uint32_t now = millis();
    if (!_saturationTiming) {
        _saturationTiming = true;
        _saturationMark = now;
        Serial.println(F("To-Engine above 40% - 30s timer started"));
        return;
    }
    if (now - _saturationMark >= SAT_HOLD_MS) {
        // R1: a wet probe reads high, so confirm on a freshly dried probe first.
        if (!probeDriedWithin(Probe::TO_ENGINE, SAT_CONFIRM_DRIED_MS)) {
            if (!_saturationConfirming) {
                _saturationConfirming = true;
                _saturationConfirmMs = now;
                Serial.println(F("To-Engine above 40% for 30 s - drying the probe to confirm"));
                probeDry(Probe::TO_ENGINE, PROBE_DRY_PULSES, "confirm saturation");
                return;
            }
            if (now - _saturationConfirmMs < SAT_CONFIRM_MAX_MS && !probeRefused(Probe::TO_ENGINE)) return;
            Serial.println(F("Saturation confirm: probe could not be dried in time, stopping on the reading"));
        }
        const char* detail = nullptr;
        if (_mode == SystemMode::DRYER_MANUAL || _mode == SystemMode::DRYER_AUTO) {
            if (_autoRechargeArmed) {
                // An automatic recharge needs the same readings as one started on
                // the screen. enterMode() does not check them, so check here.
                const char* block = rechargeSensorBlockReason();
                if (block == nullptr) {
                    Serial.println(F("Saturation reached - starting automatic recharge"));
                    _rechargeTriggeredByCycle = true;
                    _rechargeResumeMode = _mode;
                    enterMode(SystemMode::RECHARGE);
                    return;
                }
                Serial.printf("Automatic recharge not started: %s\n", block);
                detail = "Auto Recharge could not start: a recharge sensor is missing.";
            }
        }
        stopWith(StopReason::SATURATED, detail);
    }
}

void StateMachine::resetSaturation() {
    _saturationTiming = false;
    _saturationMark = 0;
    _saturationConfirming = false;
}

void StateMachine::applyOutputs() {
    if (_pumpOn != _appliedPump) {
        if (_pumpOn) relay.pumpOn();
        else relay.pumpOff();
        _appliedPump = _pumpOn;
        Serial.println(_pumpOn ? F("Pump ON") : F("Pump OFF"));
    }
    if (_heaterOn != _appliedHeater) {
        if (_heaterOn) relay.heaterOn();
        else relay.heaterOff();
        _appliedHeater = _heaterOn;
        Serial.println(_heaterOn ? F("Heater ON") : F("Heater OFF"));
    }
    if (_fanOn != _appliedFan) {
        if (_fanOn) relay.fanOn();
        else relay.fanOff();
        _appliedFan = _fanOn;
        Serial.println(_fanOn ? F("Case fan ON") : F("Case fan OFF"));
    }
    if (_valveEnergized != _appliedValve) {
        setValve(_valveEnergized);
        _appliedValve = _valveEnergized;
        Serial.println(_valveEnergized ? F("Valves OPEN loop") : F("Valves CLOSED loop"));
    }
}

void StateMachine::maybeLog() {
    uint32_t now = millis();
    if (now - _lastLogTime < LOG_INTERVAL_MS) return;
    _lastLogTime = now;
    uint32_t seconds = (now - _modeStartTime) / 1000UL;
    logger.logRunning(seconds, _mode,
                      sensors.toEngineRH, sensors.toEngineTemp, sensors.toValid,
                      sensors.fromEngineRH, sensors.fromEngineTemp, sensors.fromValid,
                      _pumpOn, _heaterOn);
    Serial.printf("LOG %lus %s toRH=%.1f fromRH=%.1f chamberF=%.0f pump=%d heat=%d fan=%d chipC=%.0f\n",
                  (unsigned long)seconds, systemModeName(_mode),
                  sensors.toEngineRH, sensors.fromEngineRH,
                  celsiusToFahrenheit(sensors.chamberTemp),
                  _pumpOn ? 1 : 0, _heaterOn ? 1 : 0, _fanOn ? 1 : 0, _chipTempC);
}

bool StateMachine::sensorsOkForMode() const {
    switch (_mode) {
        case SystemMode::PURGE:
        case SystemMode::DRYER_MANUAL:
            return toEngineReady();
        case SystemMode::RECHARGE:
            // A retry may hold the previous heater command, but cannot newly enable it.
            // Vent pressure is required too (2026-11-24): no pump pulse without it.
            return toEngineReady() && (chamberReady() || chamberRetryPending()) &&
                   ventSensorOk();
        case SystemMode::DRYER_AUTO:
            return toEngineReady() && fromEngineReady();
        default:
            return true;
    }
}

void StateMachine::setManualPhase(ManualPhase phase) {
    _manualPhase = phase;
    _phaseStartTime = millis();
    resetSaturation();
    Serial.println(phase == ManualPhase::ON ? F("Manual: pump on") : F("Manual: resting"));
}

void StateMachine::setAutoPhase(AutoPhase phase) {
    _autoPhase = phase;
    _phaseStartTime = millis();
    resetSaturation();
    if (phase == AutoPhase::SAMPLE_RUN || phase == AutoPhase::QUICK_SAMPLE) {
        // R1: dry the From-Engine probe while the sample pump pulls engine air.
        probeDry(Probe::FROM_ENGINE, PROBE_DRY_PULSES, "before an auto dryer sample");
    }
    Serial.print(F("Auto phase "));
    Serial.println((int)phase);
}

void StateMachine::setRechargePhase(RechargePhase phase) {
    _rechargePhase = phase;
    _phaseStartTime = millis();
    _probeDryAsked = false;
    if (phase == RechargePhase::COOLDOWN) {
        _cooldownWindow = UINT32_MAX;  // the first cool down pulse starts at once
        _cooldownPulseRun = false;
    }
    Serial.print(F("Recharge phase "));
    Serial.println((int)phase);
}

uint32_t StateMachine::phaseElapsed() const {
    return millis() - _phaseStartTime;
}

uint32_t StateMachine::phaseRemaining(uint32_t totalMs) const {
    uint32_t elapsed = phaseElapsed();
    return elapsed >= totalMs ? 0 : totalMs - elapsed;
}

const char* StateMachine::statusText() const {
    static char buf[80];
    char remain[24];

    if (_mode == SystemMode::IDLE) {
        if (!sensorPacketFresh()) return "Sensors offline";
        if (!sensors.toValid && !sensors.fromValid && !sensors.chamberValid) {
            return "Sensor node online, sensors not wired";
        }
        if (!sensors.toValid) return "To-Engine sensor offline";
        if (!sensors.fromValid) return "From-Engine sensor offline";
        if (!sensors.chamberValid) return "Humidity live, chamber offline";
        return "Sensors ready";
    }

    if (_sensorLost && !_rechargeFailure) {
        const uint32_t lostMs = millis() - _sensorLostSinceMs;
        formatDuration(remain, sizeof(remain), lostMs >= SENSOR_LOSS_GRACE_MS ? 0 : SENSOR_LOSS_GRACE_MS - lostMs);
        snprintf(buf, sizeof(buf), "Sensor dropout - holding, %s left", remain);
        return buf;
    }

    switch (_mode) {
        case SystemMode::PURGE:
            return "Watching To-Engine, 40% limit";
        case SystemMode::DRYER_MANUAL:
            if (_manualPhase == ManualPhase::ON) {
                formatDuration(remain, sizeof(remain),
                               phaseRemaining((uint32_t)config.dryerOnMinutes * 60000UL));
                snprintf(buf, sizeof(buf), "Pump on, %s left", remain);
            } else {
                formatDuration(remain, sizeof(remain),
                               phaseRemaining((uint32_t)config.dryerOffHours * 3600000UL));
                snprintf(buf, sizeof(buf), "Resting, %s left", remain);
            }
            return buf;
        case SystemMode::DRYER_AUTO:
            switch (_autoPhase) {
                case AutoPhase::DRYING:
                    snprintf(buf, sizeof(buf), "Drying until From-Engine <= %u%%",
                             config.dryerTargetRH);
                    return buf;
                case AutoPhase::COOLDOWN:
                    formatDuration(remain, sizeof(remain), phaseRemaining(AUTO_COOLDOWN_MS));
                    snprintf(buf, sizeof(buf), "Cooling %s, %u/%u quick", remain,
                             _autoQuickCycles, AUTO_MAX_QUICK_CYCLES);
                    return buf;
                case AutoPhase::QUICK_SAMPLE:
                    formatDuration(remain, sizeof(remain),
                                   phaseRemaining(AUTO_SAMPLE_RUN_MS + AUTO_SAMPLE_SETTLE_MS));
                    snprintf(buf, sizeof(buf), "Quick sample, %s left", remain);
                    return buf;
                case AutoPhase::WAITING:
                    formatDuration(remain, sizeof(remain), phaseRemaining(AUTO_SAMPLE_INTERVAL_MS));
                    snprintf(buf, sizeof(buf), "Next sample in %s", remain);
                    return buf;
                case AutoPhase::SAMPLE_RUN:
                    formatDuration(remain, sizeof(remain), phaseRemaining(AUTO_SAMPLE_RUN_MS));
                    snprintf(buf, sizeof(buf), "Sample pump, %s left", remain);
                    return buf;
                case AutoPhase::SAMPLE_SETTLE:
                    formatDuration(remain, sizeof(remain), phaseRemaining(AUTO_SAMPLE_SETTLE_MS));
                    snprintf(buf, sizeof(buf), "Settling, %s left", remain);
                    return buf;
            }
            break;
        case SystemMode::RECHARGE:
            switch (_rechargePhase) {
                case RechargePhase::HEATING:
                    if (chamberRetryPending()) {
                        snprintf(buf, sizeof(buf), "Chamber reading retry");
                    } else if (chamberReady()) {
                        snprintf(buf, sizeof(buf), "%s %dF, now %.0fF",
                                 _heaterOn ? "Heating to" : "Holding",
                                 RECHARGE_TARGET_TEMP_F,
                                 celsiusToFahrenheit(sensors.chamberTemp));
                    } else {
                        snprintf(buf, sizeof(buf), "Heating to %dF", RECHARGE_TARGET_TEMP_F);
                    }
                    return buf;
                case RechargePhase::PULSE_ON:
                    snprintf(buf, sizeof(buf), "Chamber pulse, %u/%u dry",
                             _rechargeGoodCycles, RECHARGE_GOOD_CYCLES);
                    return buf;
                case RechargePhase::PULSE_WAIT:
                    formatDuration(remain, sizeof(remain), phaseRemaining(RECHARGE_PULSE_WAIT_MS));
                    snprintf(buf, sizeof(buf), "Pulse wait %s, %u/%u dry",
                             remain, _rechargeGoodCycles, RECHARGE_GOOD_CYCLES);
                    return buf;
                case RechargePhase::COOLDOWN:
                    formatDuration(remain, sizeof(remain), phaseRemaining(RECHARGE_COOLDOWN_MS));
                    snprintf(buf, sizeof(buf), "%s %s, vent outside",
                             _rechargeFailure ? "Fault cool down" : "Cool down", remain);
                    return buf;
                case RechargePhase::SAFETY_PULSE:
                    formatDuration(remain, sizeof(remain),
                                   phaseRemaining(SAFETY_PULSE_MS));
                    snprintf(buf, sizeof(buf), "Safety pulse %s", remain);
                    return buf;
                case RechargePhase::SAFETY_PREP:
                    snprintf(buf, sizeof(buf), "Drying To-Engine probe for the safety sample");
                    return buf;
                case RechargePhase::SAFETY_WAIT:
                    formatDuration(remain, sizeof(remain),
                                   phaseRemaining(SAFETY_SAMPLE_INTERVAL_MS));
                    snprintf(buf, sizeof(buf), "Safety sample %s", remain);
                    return buf;
            }
            break;
        default:
            break;
    }
    return systemModeName(_mode);
}

StateMachine stateMachine;

int32_t StateMachine::ventBaseline() const { return _ventLastRef; }
bool StateMachine::ventBaselineValid() const { return _ventLastStepValid; }
bool StateMachine::ventLastStep(int32_t &step) const {
    step = _ventLastStep;
    return _ventLastStepValid;
}
uint32_t StateMachine::ventAmbientCount() const { return _ventRefCount; }
uint16_t StateMachine::ventGoodPulses() const { return _ventGoodPulses; }
uint16_t StateMachine::ventBadPulses() const { return _ventBadPulses; }

