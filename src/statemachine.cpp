/* ========================================================================
   MODE STATE MACHINE
   Purge / manual: To-Engine 40% hard-stop only.
   Auto: both sensors. Recharge: To-Engine temp + RH, 40% ignored.
   ======================================================================== */

#include "statemachine.h"
#include "relay.h"
#include "logging.h"
#include "system_clock.h"
#include "run_state.h"

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
    /* 18 */ {"CONTROLLER_RESTART", "Controller restarted during a cycle.",    "All outputs were off at restart. Check the unit before starting again."},
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
    , _ventBaseline(0)
    , _ventBaselineValid(false)
    , _ventLearnStartMs(0)
    , _ventGoodPulses(0)
    , _ventBadPulses(0)
    , _ventAmbientSum(0.0)
    , _ventAmbientCount(0)
    , _ventLastSampleMs(0)
    , _ventPulsePeak(0)
    , _ventPulseHit(false)
    , _valveBaselineRH(-1.0f)
    , _valvePulseCount(0)
    , _valveHighCount(0)
    , _valveNoCurrentCount(0)
    , _valveCommandMs(0)
    , _valveCheckMs(0)
    , _autoQuickCycles(0)
    , _safetyCheckCount(0)
    , _rechargeStartEpoch(0)
    , _rechargeEndEpoch(0)
    , _appliedPump(false)
    , _appliedHeater(false)
    , _appliedFan(false)
    , _chipHot(false)
    , _chipTempC(NAN)
    , _purgeSeen(false)
    , _purgeQualified(false)
    , _thermalLatched(false)
    , _lastPurgeMs(0)
    , _purgeEpoch(0)
    , _rechargeGoodCycles(0)
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
    _ventBaseline = 0;
    _ventBaselineValid = false;
    _ventLearnStartMs = 0;
    _ventGoodPulses = 0;
    _ventBadPulses = 0;
    _pulseCount = 0;
    _valveBaselineRH = -1.0f;
    _valvePulseCount = 0;
    _valveHighCount = 0;
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
    _pendingCmd = Command::NONE;
    if (cmd == Command::STOP) {
        stopWith(StopReason::USER);
        return true;
    }
    if (cmd == Command::STOP_REMOTE) {
        stopWith(StopReason::USER_REMOTE);
        return true;
    }
    if (_mode != SystemMode::IDLE || startBlockReason(cmd) != nullptr) return true;
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
    learnVentBaseline();
    if (handleCommand()) {
        updateFan();
        applyOutputs();
        return;
    }
    if (_mode != SystemMode::IDLE) {
        if (!sensorsOkForMode()) {
            // Sensor dropout grace. The sensor node sends on ESP-NOW broadcast, which has no
            // retry, so a lost frame or two is normal. A 3.2 s gap (two lost packets) used to
            // stop a cycle at once. Hold for SENSOR_LOSS_GRACE_MS before stopping. While
            // holding there are no new decisions, and in recharge there is no heat unless the
            // chamber reading is fresh.
            if (!_sensorLost) {
                _sensorLost = true;
                _sensorLostSinceMs = millis();
                Serial.println(F("Sensor dropout - holding"));
            }
            if (millis() - _sensorLostSinceMs < SENSOR_LOSS_GRACE_MS) {
                if (_mode == SystemMode::RECHARGE) {
                    if (chamberReady()) regulateHeater();
                    else _heaterOn = false;
                }
                updateFan();
                applyOutputs();
                return;
            }
            if (_mode == SystemMode::RECHARGE && _valveEnergized && _chamberReachedTarget) {
                rechargeFailureCooldown(sensorLossReason());
            } else {
                stopWith(sensorLossReason());
            }
        } else {
            if (_sensorLost) {
                _sensorLost = false;
                Serial.printf("Sensors back after %lu ms\n",
                              (unsigned long)(millis() - _sensorLostSinceMs));
            }
            if (_inGracePeriod && (millis() - _modeStartTime >= GRACE_MS)) {
                _inGracePeriod = false;
            }
            switch (_mode) {
                case SystemMode::PURGE:        updatePurge(); break;
                case SystemMode::DRYER_MANUAL: updateManual(); break;
                case SystemMode::DRYER_AUTO:   updateAuto(); break;
                case SystemMode::RECHARGE:     updateRecharge(); break;
                default: break;
            }
            if (_mode != SystemMode::IDLE) checkSaturation();
            if (_mode != SystemMode::IDLE) maybeLog();
        }
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
            if (!toEngineReady() || !chamberReady()) {
                return "Recharge needs To-Engine and\nthe chamber thermistor.";
            }
            return nullptr;
        default:
            return "Unknown command.";
    }
}

bool StateMachine::command(Command cmd) {
    if (cmd == Command::NONE) return false;
    const bool stop = cmd == Command::STOP || cmd == Command::STOP_REMOTE;
    if (!stop && startBlockReason(cmd) != nullptr) {
        Serial.println(startBlockReason(cmd));
        return false;
    }
    _pendingCmd = cmd;
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
            _autoPhase = AutoPhase::SAMPLE_RUN;
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
            _ventBaseline = 0;
            _ventBaselineValid = false;
            _ventLearnStartMs = 0;
            _ventGoodPulses = 0;
            _ventBadPulses = 0;
            _ventAmbientSum = 0.0;
            _ventAmbientCount = 0;
            _ventLastSampleMs = 0;
            _ventPulsePeak = 0;
            _ventPulseHit = false;
            _pulseCount = 0;
            _valveBaselineRH = -1.0f;
            _valvePulseCount = 0;
            _valveHighCount = 0;
            _valveNoCurrentCount = 0;
            _valveCommandMs = millis();
            _valveCheckMs = 0;
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
        _valveBaselineRH = -1.0f;
        _valvePulseCount = 0;
        _valveHighCount = 0;
        _valveNoCurrentCount = 0;
        Serial.println(F("Recharge ended - valve closed loop"));
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
        _lastPurgeMs = millis();
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
                sensors.fromEngineRH <= target) {
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
            if (sensors.fromEngineRH > target) setAutoPhase(AutoPhase::DRYING);
            else setAutoPhase(AutoPhase::WAITING);
            break;
    }
}

void StateMachine::updateRecharge() {
    // Heat-up backstop. The max run time below only starts once the chamber reaches
    // target, so a heater or thermistor fault that never gets there needs its own limit.
    if (!_chamberReachedTarget && _rechargePhase == RechargePhase::HEATING &&
        millis() - _modeStartTime >= RECHARGE_HEATUP_MAX_MS) {
        Serial.println(F("Recharge heat-up timed out - chamber never reached target"));
        stopWith(StopReason::HEATUP_TIMEOUT);
        return;
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
    regulateHeater();

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
            _pumpOn = true;
            // Track the highest vent pressure seen at ANY point in this pulse.
            if (sensors.pressureValid && sensors.ventPressureRaw > _ventPulsePeak) {
                _ventPulsePeak = sensors.ventPressureRaw;
            }
            // Pump on time: 10 s for the first hour after the chamber first reaches
            // target (heat-up time does not count). After that hour,
            // RECHARGE_LONG_PULSE_ON_MS (15 s). Off time stays 1 minute.
            const bool soaked = _chamberReachedTarget &&
                                (millis() - _chamberReachedMs) >= RECHARGE_LONG_PULSE_AFTER_MS;
            const uint32_t pulseMs = (_pulseCount == 0)
                                         ? RECHARGE_FIRST_PULSE_MS
                                         : (soaked ? RECHARGE_LONG_PULSE_ON_MS
                                                   : RECHARGE_PULSE_ON_MS);
            if (phaseElapsed() >= pulseMs) {
                if (!fromEngineSampleReady()) {
                    // Wait for the reading instead of failing on one missed packet.
                    // Pump off while waiting. Fail only after the grace time.
                    if (phaseElapsed() < pulseMs + SENSOR_LOSS_GRACE_MS) {
                        _pumpOn = false;
                        break;
                    }
                    rechargeFailureCooldown(StopReason::FROM_ENGINE_LOST);
                    return;
                }
                if (!evaluateValveVerification()) return;
                if (!evaluateVentPressure()) return;
                if (_pulseCount < 255) _pulseCount++;
                if (toEngineSampleReady()) {
                    if (sensors.toEngineRH <= RECHARGE_TARGET_RH) {
                        if (_rechargeGoodCycles < 255) _rechargeGoodCycles++;
                    } else {
                        _rechargeGoodCycles = 0;
                    }
                }
                Serial.printf("Recharge sample %.1f%% good %u/%u\n",
                              toEngineSampleReady() ? sensors.toEngineRH : -1.0f,
                              _rechargeGoodCycles, RECHARGE_GOOD_CYCLES);
                if (_rechargeGoodCycles >= RECHARGE_GOOD_CYCLES) {
                    setRechargePhase(RechargePhase::COOLDOWN);
                    return;
                }
                setRechargePhase(RechargePhase::PULSE_WAIT);
            }
            break;
        }
        case RechargePhase::PULSE_WAIT:
            _pumpOn = false;
            if (phaseElapsed() >= RECHARGE_PULSE_WAIT_MS) {
                setRechargePhase(RechargePhase::PULSE_ON);
            }
            break;
        case RechargePhase::COOLDOWN: {
            _heaterOn = false;
            _fanOn = true;
            // Loose cooldown (2026-11-20): 10 s ON / 10 min OFF, valves OPEN.
            // Case fan ON the whole hour. Early exit when chamber <= 120 F.
            // sensors.chamberTemp is Celsius, so convert before comparing. It was compared
            // to 120 as is, which is 248 F, so the cool down ended at once on every run.
            const uint32_t cooldownMs = phaseElapsed();
            const bool coolReached = chamberReady() &&
                                     celsiusToFahrenheit(sensors.chamberTemp) <= RECHARGE_FAILED_FAN_TEMP_F;
            const bool phaseDone = coolReached || cooldownMs >= RECHARGE_COOLDOWN_MS;
            if (phaseDone) {
                _pumpOn = false;
                if (_rechargeFailure) {
                    StopReason reason = _rechargeFailureReason;
                    _valveEnergized = false;
                    _rechargeEndEpoch = systemClockEpoch();
                    stopWith(reason, nullptr, true);
                    return;
                }
                _safetyCheckCount = 0;
                setRechargePhase(RechargePhase::SAFETY_PULSE);
                break;
            }
            const bool pulseOn = (cooldownMs % RECHARGE_COOLDOWN_INTERVAL_MS) < RECHARGE_COOLDOWN_PULSE_ON_MS;
            _pumpOn = pulseOn;
            break;
        }
        case RechargePhase::SAFETY_PULSE:
            _heaterOn = false;
            _pumpOn = true;
            if (phaseElapsed() >= SAFETY_PULSE_MS) {
                _pumpOn = false;
                setRechargePhase(RechargePhase::SAFETY_WAIT);
            }
            break;
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
                            enterMode(resumeMode);
                            return;
                        }
                        stopWith(StopReason::RECHARGE_COMPLETE);
                        return;
                    }
                } else {
                    Serial.println(F("Safety sample above 15% - fail safe to idle"));
                    _valveEnergized = false;
                    _rechargeEndEpoch = systemClockEpoch();
                    char detail[96];
                    snprintf(detail, sizeof(detail), "To-Engine read %.1f%% after the cool down. It must be %.0f%% or less.",
                             sensors.toEngineRH, RECHARGE_TARGET_RH);
                    stopWith(StopReason::SAFETY_FAILED, detail);
                    return;
                }
                setRechargePhase(RechargePhase::SAFETY_PULSE);
            }
            break;
    }
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
    if (toEngineReady()) {
        fahrenheit = celsiusToFahrenheit(sensors.toEngineTemp);
        if (fahrenheit > (float)THERMAL_SENSOR_F) return StopReason::THERMAL_TO_PROBE;
    }
    if (fromEngineReady()) {
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

/* On any recharge failure, keep venting outside and cool the chamber for the
   full cool down window before closing the valves and going idle. */
void StateMachine::rechargeFailureCooldown(StopReason reason) {
    _rechargeFailure = true;
    _rechargeFailureReason = reason;
    _heaterOn = false;
    Serial.print(F("Recharge failure - cool down, valve stays open, reason "));
    Serial.println((int)reason);
    setRechargePhase(RechargePhase::COOLDOWN);
}

/* Valve position verification, judged only on end-of-pulse From-Engine
   samples. Pulse 1 is a flush: the probe was soaking in chamber steam while
   the pump was off and needs a few seconds under flow. Pulses 2-3 set the
   baseline (lowest reading wins). After that, consecutive samples at or above
   baseline + threshold mean regen exhaust is entering the engine. */
bool StateMachine::evaluateValveVerification() {
#if VALVE_TREND_CHECK_ENABLED
    /* Retired humidity-trend check. Kept for reference only. The vent
       pressure check replaced it. These constants are local so the block
       still compiles if it is ever switched back on. */
    const uint8_t baselinePulses = 2;
    const float riseRH = 10.0f;
    const uint8_t consecutive = 2;

    _valvePulseCount++;
    const float fromRH = sensors.fromEngineRH;
    if (_valvePulseCount == 1) {
        Serial.println(F("Valve verify: flush pulse, From-Engine not judged"));
        return true;
    }
    if (_valvePulseCount <= (uint8_t)(1 + baselinePulses)) {
        if (_valveBaselineRH < 0.0f || fromRH < _valveBaselineRH) {
            _valveBaselineRH = fromRH;
        }
        Serial.printf("Valve verify: baseline %.1f%%\n", _valveBaselineRH);
        return true;
    }
    if (fromRH >= _valveBaselineRH + riseRH) {
        if (_valveHighCount < 255) _valveHighCount++;
    } else {
        _valveHighCount = 0;
    }
    if (_valveHighCount >= consecutive) {
        Serial.println(F("Valve verify: From-Engine climbing - exhaust entering engine"));
        stopWith(StopReason::VALVE_SUSPECT);
        return false;
    }
#endif
    return true;
}

/* Learn the idle vent pressure while the pump is not running. The baseline
   must be taken with the line un-pressurised; a pump-on reading would make
   every later pulse look like "no flow". Called every loop while the vent
   line is quiet.

   Ambient-averaged baseline (2026-11-20): sample every 30 s while pump OFF
   (heating and inter-pulse waits). The baseline is the running AVERAGE of
   those samples, not the lowest. Re-learned on every recharge cycle, and
   updated between pulses, so barometric drift cannot fake a good step.
   The first sample seeds the sum; later samples are added to it. */
void StateMachine::learnVentBaseline() {
#if VENT_PRESSURE_CHECK_ENABLED
    if (_pumpOn) return;
    if (!sensors.pressureValid) return;
    if (_ventBaselineValid) {
        if (_ventLastSampleMs != 0 &&
            millis() - _ventLastSampleMs < VENT_PRESSURE_AMBIENT_SAMPLE_MS) return;
        _ventLastSampleMs = millis();
        _ventAmbientSum += (double)sensors.ventPressureRaw;
        _ventAmbientCount++;
        _ventBaseline = (int32_t)(_ventAmbientSum / (double)_ventAmbientCount);
        return;
    }
    if (_ventLearnStartMs == 0) {
        _ventLearnStartMs = millis();
        _ventAmbientSum = 0.0;
        _ventAmbientCount = 0;
        _ventLastSampleMs = millis();
        _ventAmbientSum += (double)sensors.ventPressureRaw;
        _ventAmbientCount++;
        _ventBaseline = (int32_t)(_ventAmbientSum / (double)_ventAmbientCount);
        Serial.printf("Vent ambient sample 1: %ld raw (count %lu)\n",
                      (long)sensors.ventPressureRaw, (unsigned long)_ventAmbientCount);
        return;
    }
    if (millis() - _ventLearnStartMs < VENT_PRESSURE_LEARN_MS) {
        if (_ventLastSampleMs != 0 &&
            millis() - _ventLastSampleMs < VENT_PRESSURE_AMBIENT_SAMPLE_MS) return;
        _ventLastSampleMs = millis();
        _ventAmbientSum += (double)sensors.ventPressureRaw;
        _ventAmbientCount++;
        _ventBaseline = (int32_t)(_ventAmbientSum / (double)_ventAmbientCount);
        Serial.printf("Vent ambient sample %lu: %ld raw (avg %ld)\n",
                      (unsigned long)_ventAmbientCount, (long)sensors.ventPressureRaw,
                      (long)_ventBaseline);
        return;
    }
    _ventBaselineValid = true;
    Serial.printf("Vent baseline learned: %ld raw (avg of %lu)\n",
                  (long)_ventBaseline, (unsigned long)_ventAmbientCount);
#endif
}

/* Vent pressure verification. The pump draws air through the chamber and the
   dump valve sends the regen exhaust out the vent line. With the valve open
   the vent line must show a pressure step against the idle baseline. No step
   means the dump valve did not open, so the exhaust is not leaving the
   system. This is a positive check on the mechanical valve position and does
   not depend on humidity at all.

   The ambient baseline is a running average (see learnVentBaseline), so
   each reading is compared against the live ambient, not a stale lowest.
   A pulse is "good" only if the raw reading rises to >= baseline + 20,000
   counts at any point during the 10 s pump pulse. */
bool StateMachine::evaluateVentPressure() {
#if VENT_PRESSURE_CHECK_ENABLED
    if (!sensors.pressureValid) {
        /* No pressure data. Do not block the cycle; the coil current check
           and the operator are the backups. */
        return true;
    }
    if (!_ventBaselineValid) {
        /* Baseline not learned yet. Do not judge a pulse before the idle
           level is known. */
        return true;
    }
    const int32_t raw = sensors.ventPressureRaw;
    if (raw > _ventPulsePeak) _ventPulsePeak = raw;
    const int32_t peakStep = _ventPulsePeak - _ventBaseline;
    const bool sawStep = peakStep >= VENT_PRESSURE_MIN_STEP;
    if (sawStep) _ventPulseHit = true;
    const int32_t step = raw - _ventBaseline;
    if (sawStep) {
        _ventGoodPulses++;
        _ventBadPulses = 0;
    } else {
        _ventBadPulses++;
    }
    Serial.printf("Vent pressure raw %ld peak %ld step %ld %s good %u bad %u\\n",
                  (long)raw, (long)_ventPulsePeak, (long)peakStep, sawStep ? "OK" : "NO FLOW",
                  _ventGoodPulses, _ventBadPulses);

    // Re-arm for the NEXT pulse. A single bad pulse is a safety shutdown;
    // a good pulse does not hide a later disconnect.
    const bool thisPulseBad = !_ventPulseHit;
    _ventPulsePeak = 0;
    _ventPulseHit = false;

    // The whole 10 s pulse must have shown +20,000 over the ambient average.
    // A single bad pulse is a safety shutdown (test: disconnected line).
    if (thisPulseBad) {
        Serial.println(F("Vent pressure fault - pulse did not rise 20,000 over ambient"));
        char detail[96];
        snprintf(detail, sizeof(detail), "Pump pulse raised the vent pressure %ld counts. It needs %ld.",
                 (long)peakStep, (long)VENT_PRESSURE_MIN_STEP);
        stopWith(StopReason::VALVE_SUSPECT, detail);
        return false;
    }

    if (_ventBadPulses >= VENT_PRESSURE_SAMPLES) {
        Serial.println(F("Vent pressure fault - dump valve did not open"));
        stopWith(StopReason::VALVE_SUSPECT);
        return false;
    }
#endif
    return true;
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
        if (_mode == SystemMode::DRYER_MANUAL || _mode == SystemMode::DRYER_AUTO) {
            if (config.allowAutoRecharge) {
                Serial.println(F("Saturation reached - starting automatic recharge"));
                _rechargeTriggeredByCycle = true;
                _rechargeResumeMode = _mode;
                enterMode(SystemMode::RECHARGE);
                return;
            }
        }
        stopWith(StopReason::SATURATED);
    }
}

void StateMachine::resetSaturation() {
    _saturationTiming = false;
    _saturationMark = 0;
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
            return toEngineReady() && (chamberReady() || chamberRetryPending());
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
    Serial.print(F("Auto phase "));
    Serial.println((int)phase);
}

void StateMachine::setRechargePhase(RechargePhase phase) {
    _rechargePhase = phase;
    _phaseStartTime = millis();
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
                        snprintf(buf, sizeof(buf), "Chamber retry, 6s max");
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
                    snprintf(buf, sizeof(buf), "Cool down %s, vent outside", remain);
                    return buf;
                case RechargePhase::SAFETY_PULSE:
                    formatDuration(remain, sizeof(remain),
                                   phaseRemaining(SAFETY_PULSE_MS));
                    snprintf(buf, sizeof(buf), "Safety pulse %s", remain);
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

uint32_t StateMachine::timeRemainingMs() const {
    if (_mode == SystemMode::IDLE) return 0;
    switch (_mode) {
        case SystemMode::PURGE:
        case SystemMode::DRYER_AUTO:
            return 0xFFFFFFFFUL;
        case SystemMode::DRYER_MANUAL:
            if (_manualPhase == ManualPhase::ON) {
                return phaseRemaining((uint32_t)config.dryerOnMinutes * 60000UL);
            }
            return phaseRemaining((uint32_t)config.dryerOffHours * 3600000UL);
        case SystemMode::RECHARGE: {
            if (!_chamberReachedTarget) return RECHARGE_MAX_MS;  // clock starts at target
            uint32_t elapsed = millis() - _chamberReachedMs;
            if (elapsed >= RECHARGE_MAX_MS) return 0;
            return RECHARGE_MAX_MS - elapsed;
        }
        default:
            return 0;
    }
}

StateMachine stateMachine;

int32_t StateMachine::ventBaseline() const { return _ventBaseline; }
bool StateMachine::ventBaselineValid() const { return _ventBaselineValid; }
int32_t StateMachine::ventPulsePeak() const { return _ventPulsePeak; }
bool StateMachine::ventPulseHit() const { return _ventPulseHit; }
uint32_t StateMachine::ventAmbientCount() const { return _ventAmbientCount; }
uint8_t StateMachine::ventGoodPulses() const { return _ventGoodPulses; }
uint8_t StateMachine::ventBadPulses() const { return _ventBadPulses; }

