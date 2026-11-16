/* ========================================================================
   MODE STATE MACHINE
   Purge / manual: To-Engine 40% hard-stop only.
   Auto: both sensors. Recharge: To-Engine temp + RH, 40% ignored.
   ======================================================================== */

#include "statemachine.h"
#include "relay.h"
#include "logging.h"
#include "system_clock.h"

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
    else if (cmd == Command::START_RECHARGE) enterMode(SystemMode::RECHARGE);
    return true;
}

void StateMachine::update() {
    checkThermal();
    if (handleCommand()) {
        updateFan();
        applyOutputs();
        return;
    }
    if (_mode != SystemMode::IDLE) {
        if (!sensorsOkForMode()) {
            stopWith(StopReason::SENSOR_LOST);
        } else {
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
    if (cmd == Command::STOP || cmd == Command::NONE) return "Invalid start.";
    if (_mode != SystemMode::IDLE) return "A cycle is already running.";
    if (thermalExceeded()) {
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
    if (cmd != Command::STOP && startBlockReason(cmd) != nullptr) {
        Serial.println(startBlockReason(cmd));
        return false;
    }
    _pendingCmd = cmd;
    return true;
}

const char* StateMachine::takeStopMessage() {
    const char* message = _stopMessage;
    _stopReason = StopReason::NONE;
    _stopMessage = nullptr;
    return message;
}

void StateMachine::enterMode(SystemMode newMode) {
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
            _autoPhase = AutoPhase::DRYING;
            _pumpOn = true;
            break;
        case SystemMode::RECHARGE:
            _rechargePhase = RechargePhase::HEATING;
            _heaterOn = true;
            _valveEnergized = true;
            setValve(true);
            _rechargeStartEpoch = systemClockEpoch();
            Serial.println(F("Recharge started - valve open loop"));
            break;
        default:
            break;
    }
    logger.logModeChange(SystemMode::IDLE, newMode);
    Serial.print(F("Mode -> "));
    Serial.println(systemModeName(newMode));
}

void StateMachine::stopWith(StopReason reason) {
    SystemMode oldMode = _mode;
    bool toOk = toEngineReady();
    bool chamberOk = chamberReady();
    _mode = SystemMode::IDLE;
    _pumpOn = false;
    _heaterOn = false;
    _fanOn = false;
    _valveEnergized = false;
    setValve(false);
    _inGracePeriod = false;
    _pendingCmd = Command::NONE;
    resetSaturation();
    relay.allOff();
    _appliedPump = false;
    _appliedHeater = false;
    _appliedFan = false;
    _stopReason = reason;
    if (oldMode == SystemMode::RECHARGE) {
        _rechargeEndEpoch = systemClockEpoch();
        Serial.println(F("Recharge ended - valve closed loop"));
    }
    if (reason == StopReason::USER || oldMode == SystemMode::IDLE) {
        _stopMessage = nullptr;
    } else if (reason == StopReason::SATURATED) {
        _stopMessage = "Desiccant saturated.\nRecharge cycle recommended.";
    } else if (reason == StopReason::SENSOR_LOST) {
        if (oldMode == SystemMode::DRYER_AUTO && toOk) {
            _stopMessage = "From-Engine sensor lost.\nAuto Cycle stopped.";
        } else if (oldMode == SystemMode::DRYER_AUTO) {
            _stopMessage = "To-Engine sensor lost.\nAuto Cycle stopped.";
        } else if (oldMode == SystemMode::RECHARGE && toOk && !chamberOk) {
            _stopMessage = "Chamber thermistor lost.\nRecharge stopped.";
        } else if (oldMode == SystemMode::RECHARGE) {
            _stopMessage = "To-Engine sensor lost.\nRecharge stopped.";
        } else {
            _stopMessage = "To-Engine sensor lost.\nCycle stopped.";
        }
    } else if (reason == StopReason::RECHARGE_COMPLETE) {
        _stopMessage = "Recharge complete.\nThree readings in a row were below 15%.";
    } else if (reason == StopReason::RECHARGE_MAX_TIME) {
        _stopMessage = "Recharge stopped.\n3 hour limit reached.";
    } else if (reason == StopReason::THERMAL) {
        _stopMessage = "Thermal limit reached.\nHeater and pump are off.";
    } else {
        _stopMessage = "Cycle stopped.";
    }
    if (oldMode != SystemMode::IDLE) {
        logger.logModeChange(oldMode, SystemMode::IDLE);
        Serial.print(F("Stop: "));
        Serial.println(_stopMessage != nullptr ? _stopMessage : "user");
    }
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
            if (fromEngineSampleReady() && sensors.fromEngineRH <= target) {
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
            _pumpOn = false;
            if (phaseElapsed() < AUTO_SAMPLE_RUN_MS) break;
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
    if (millis() - _modeStartTime >= RECHARGE_MAX_MS) {
        stopWith(StopReason::RECHARGE_MAX_TIME);
        return;
    }
    regulateHeater();

    /* Valve position safety check while energized (open loop).
       From-Engine should read ambient, well below To-Engine. */
    if (_valveEnergized && toEngineReady() && fromEngineReady()) {
        float diff = sensors.toEngineRH - sensors.fromEngineRH;
        if (diff < VALVE_LEAK_THRESHOLD_RH) {
            Serial.println(F("Valve leak suspected - From-Engine near To-Engine"));
            stopWith(StopReason::SENSOR_LOST);
            return;
        }
    }

    switch (_rechargePhase) {
        case RechargePhase::HEATING:
            _pumpOn = false;
            if (chamberReady() &&
                celsiusToFahrenheit(sensors.chamberTemp) >= (float)RECHARGE_TARGET_TEMP_F) {
                setRechargePhase(RechargePhase::PULSE_ON);
            }
            break;
        case RechargePhase::PULSE_ON:
            _pumpOn = true;
            if (phaseElapsed() < RECHARGE_PULSE_ON_MS || !toEngineSampleReady()) break;
            if (sensors.toEngineRH <= RECHARGE_TARGET_RH) {
                if (_rechargeGoodCycles < 255) _rechargeGoodCycles++;
            } else {
                _rechargeGoodCycles = 0;
            }
            Serial.printf("Recharge sample %.1f%% good %u/%u\n",
                          sensors.toEngineRH, _rechargeGoodCycles, RECHARGE_GOOD_CYCLES);
            if (_rechargeGoodCycles >= RECHARGE_GOOD_CYCLES) {
                setRechargePhase(RechargePhase::COOLDOWN);
                return;
            }
            setRechargePhase(RechargePhase::PULSE_WAIT);
            break;
        case RechargePhase::PULSE_WAIT:
            _pumpOn = false;
            if (phaseElapsed() >= RECHARGE_PULSE_WAIT_MS) {
                setRechargePhase(RechargePhase::PULSE_ON);
            }
            break;
        case RechargePhase::COOLDOWN:
            _heaterOn = false;
            _pumpOn = true;
            _fanOn = true;
            if (phaseElapsed() >= RECHARGE_COOLDOWN_MS) {
                _safetyCheckCount = 0;
                setRechargePhase(RechargePhase::SAFETY_PULSE);
            }
            break;
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
                        setValve(false);
                        _rechargeEndEpoch = systemClockEpoch();
                        stopWith(StopReason::RECHARGE_COMPLETE);
                        return;
                    }
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

bool StateMachine::thermalExceeded() const {
    if (chamberReady() &&
        celsiusToFahrenheit(sensors.chamberTemp) > (float)THERMAL_CHAMBER_F) {
        return true;
    }
    if (toEngineReady() &&
        celsiusToFahrenheit(sensors.toEngineTemp) > (float)THERMAL_SENSOR_F) {
        return true;
    }
    if (fromEngineReady() &&
        celsiusToFahrenheit(sensors.fromEngineTemp) > (float)THERMAL_SENSOR_F) {
        return true;
    }
    return false;
}

void StateMachine::checkThermal() {
    if (!thermalExceeded()) {
        _thermalLatched = false;
        return;
    }
    if (_thermalLatched) return;
    _thermalLatched = true;
    Serial.println(F("Thermal limit reached"));
    if (_mode != SystemMode::IDLE) {
        stopWith(StopReason::THERMAL);
    } else {
        _pumpOn = false;
        _heaterOn = false;
        _fanOn = false;
        relay.allOff();
        _appliedPump = false;
        _appliedHeater = false;
        _appliedFan = false;
        _stopMessage = "Thermal limit reached.\nHeater and pump are off.";
    }
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
    if (fahrenheit >= (float)RECHARGE_TARGET_TEMP_F) {
        _heaterOn = false;
    } else if (fahrenheit <= (float)(RECHARGE_TARGET_TEMP_F - RECHARGE_TEMP_HYSTERESIS_F)) {
        _heaterOn = true;
    }
}

void StateMachine::setValve(bool on) {
    if (on) relay.setChannel(RELAY_BIT_VALVE, true);
    else relay.setChannel(RELAY_BIT_VALVE, false);
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
    if (_mode == SystemMode::IDLE || _inGracePeriod) {
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
        if (_valveEnergized) relay.setChannel(RELAY_BIT_VALVE, true);
        else relay.setChannel(RELAY_BIT_VALVE, false);
        _appliedValve = _valveEnergized;
        Serial.println(_valveEnergized ? F("Valve OPEN loop") : F("Valve CLOSED loop"));
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
                                   phaseRemaining(AUTO_SAMPLE_RUN_MS));
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
            uint32_t elapsed = millis() - _modeStartTime;
            if (elapsed >= RECHARGE_MAX_MS) return 0;
            return RECHARGE_MAX_MS - elapsed;
        }
        default:
            return 0;
    }
}

StateMachine stateMachine;

