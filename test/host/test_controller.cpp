/* Host simulation of the AeroSentry controller logic.

   Runs the real statemachine.cpp, sensors.cpp, relay.cpp, radio_link.cpp and
   logging.cpp against a simple plant model: a chamber that heats when the
   heater relay is on and cools toward ambient, a vent pressure sensor that
   rises only when the pump runs with the valves open, and a sensor node that
   broadcasts a SensorPacket about once a second. Time is simulated.

   Every step checks the safety invariants:
     I1 heater on  -> recharge, and a valid chamber packet within CHAMBER_RETRY_MS
     I2 pump on in recharge -> valves on, and a valid pressure packet within
        SAMPLE_MAX_AGE_MS (no pump pulse the vent check cannot judge)
     I3 valves on  -> recharge
     I4 idle       -> every relay off
     I5 K1, K7, K8 never driven
   Build and run: python tools/host_test.py */

#include <Arduino.h>
#include <Wire.h>
#include <esp_now.h>
#include <functional>
#include <vector>
#include "config.h"
#include "sensors.h"
#include "relay.h"
#include "statemachine.h"
#include "radio_link.h"
#include "link_packet.h"
#include "sensor_packet.h"
#include "probe_heat.h"
#include "probe_care.h"

extern std::vector<ProbeHeatRequest> hostHeatRequests;

static const uint8_t SENSOR_MAC[6] = {0xF8, 0x5B, 0x1B, 0xBF, 0x3F, 0xEC};
static const uint8_t BRIDGE_MAC[6] = {0x10, 0xBD, 0xA3, 0x5A, 0x7C, 0x94};
static const uint8_t STRANGER_MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

/* ---------- plant ---------- */
struct Plant {
    bool nodeOn = true;
    bool toValid = true, fromValid = true, chamberValid = true, pressureValid = true;
    float toRH = 30.0f, toC = 25.0f, fromRH = 30.0f, fromC = 25.0f;
    float chamberF = 70.0f;
    float ambientF = 70.0f;
    int32_t ambientRaw = 4700000;
    int32_t flowStep = 100000;
    bool valveStuck = false;
    bool heaterBroken = false;
    float heaterRateFps = 0.25f;  // chamber rise per second with the heater on (bench: about 0.4 from cold)
    bool repeatPackets = false;  // send every packet twice, like the sensor node does
    uint32_t seq = 0;
    uint32_t nextPacketMs = 0;
    uint32_t packetPeriodMs = 1050;
    uint32_t lastChamberPacketMs = 0;
    uint32_t lastPressurePacketMs = 0;
    uint32_t hotSince = 0;        // when the chamber first reached about 240 F
    std::function<void(Plant&)> model;  // per-test humidity model, called every step
    // Sensor node heater, the same ProbeHeater code the node runs.
    ProbeHeater toHeater, fromHeater;
    uint8_t heatAck = 0;
    float toDieRiseC = 0.0f, fromDieRiseC = 0.0f;  // heater warming of the die, decays
    float heaterDieRiseC = 20.0f;                   // per pulse
    int toWater = 0, fromWater = 0;                 // drops on the chip: each pulse burns one off
    uint32_t toPulses = 0, fromPulses = 0;
};

static bool bit(uint8_t port, int b) { return (port >> b) & 1; }
static bool pumpRelay() { return bit(hostRelayPort0(), RELAY_BIT_PUMP); }
static bool heaterRelay() { return bit(hostRelayPort0(), RELAY_BIT_HEATER); }
static bool valvesRelay() {
    return bit(hostRelayPort0(), RELAY_BIT_VALVE_TO) && bit(hostRelayPort0(), RELAY_BIT_VALVE_FROM);
}

/* What the SHT40 reports: water on the chip reads at least 99%; a heated die
   reads warmer and drier than the air. */
static float reportedRH(float trueRH, int water, float dieRiseC) {
    if (water > 0) return fmaxf(trueRH, 99.0f);
    return trueRH * expf(-0.06f * dieRiseC);
}

static void sendPacket(Plant& p) {
    const uint32_t now = hostNowMs;
    // Node: take heat requests (each seq once), as nodes/heltec-sensor does.
    for (const ProbeHeatRequest& request : hostHeatRequests) {
        if (request.seq == p.heatAck) continue;
        p.heatAck = request.seq;
        (request.probe == HEAT_PROBE_FROM ? p.fromHeater : p.toHeater).request(request.pulses, now);
    }
    hostHeatRequests.clear();
    SensorPacket packet = {};
    packet.version = AEROSENTRY_PACKET_VERSION;
    packet.sequence = p.seq++;
    if (p.toValid) {
        packet.flags |= SENSOR_FLAG_TO_VALID;
        const float dieC = p.toC + p.toDieRiseC;
        packet.toEngineRhCenti = (int16_t)lroundf(reportedRH(p.toRH, p.toWater, p.toDieRiseC) * 100.0f);
        packet.toEngineTempCenti = (int16_t)lroundf(dieC * 100.0f);
        p.toHeater.observe(dieC, now);
    }
    if (p.fromValid) {
        packet.flags |= SENSOR_FLAG_FROM_VALID;
        const float dieC = p.fromC + p.fromDieRiseC;
        packet.fromEngineRhCenti = (int16_t)lroundf(reportedRH(p.fromRH, p.fromWater, p.fromDieRiseC) * 100.0f);
        packet.fromEngineTempCenti = (int16_t)lroundf(dieC * 100.0f);
        p.fromHeater.observe(dieC, now);
    }
    if (p.toValid && p.toHeater.pulseDue(now, false)) {
        p.toHeater.pulseDone(now);
        p.toDieRiseC += p.heaterDieRiseC;
        if (p.toWater > 0) p.toWater--;
        p.toPulses++;
    }
    if (p.fromValid && p.fromHeater.pulseDue(now, p.toHeater.pulsing())) {
        p.fromHeater.pulseDone(now);
        p.fromDieRiseC += p.heaterDieRiseC;
        if (p.fromWater > 0) p.fromWater--;
        p.fromPulses++;
    }
    packet.toStatus = p.toHeater.status();
    packet.fromStatus = p.fromHeater.status();
    packet.toDriedAgeS = p.toHeater.driedAgeS(now);
    packet.fromDriedAgeS = p.fromHeater.driedAgeS(now);
    packet.heatAck = p.heatAck;
    if (p.chamberValid) {
        packet.flags |= SENSOR_FLAG_CHAMBER_VALID;
        packet.chamberTempCenti = (int16_t)lroundf((p.chamberF - 32.0f) * 5.0f / 9.0f * 100.0f);
        p.lastChamberPacketMs = hostNowMs;
    }
    if (p.pressureValid) {
        packet.flags |= SENSOR_FLAG_PRESSURE_VALID;
        const bool flowing = pumpRelay() && valvesRelay() && !p.valveStuck;
        packet.ventPressureRaw = p.ambientRaw + (flowing ? p.flowStep : 0) + (int32_t)(p.seq % 7) * 90 - 270;
        p.lastPressurePacketMs = hostNowMs;
    }
    hostEspNowReceive(SENSOR_MAC, (const uint8_t*)&packet, sizeof(packet));
    if (p.repeatPackets) hostEspNowReceive(SENSOR_MAC, (const uint8_t*)&packet, sizeof(packet));
}

/* ---------- test bookkeeping ---------- */
static int failures = 0;
static int checks = 0;
static const char* currentTest = "";
static bool invariantReported = false;

#define EXPECT(cond, ...) do { if (skipping) break; checks++; if (!(cond)) { failures++; \
    printf("  FAIL [%s] line %d: ", currentTest, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void invariant(bool ok, const char* what) {
    if (ok || invariantReported) return;
    failures++;
    invariantReported = true;  // report the first violation per test only
    printf("  FAIL [%s] invariant at %lu ms: %s (mode %s, port0 0x%02X)\n", currentTest,
           (unsigned long)hostNowMs, what, systemModeName(stateMachine.mode()), hostRelayPort0());
}

static const char* onlyFilter = nullptr;  // run only scenarios whose title contains this
static bool skipping = false;

static void step(Plant& p, uint32_t dt) {
    if (skipping) {  // a filtered-out scenario: let time pass, do nothing else
        hostNowMs += dt;
        if (stateMachine.mode() != SystemMode::IDLE) stateMachine.command(Command::STOP);
        return;
    }
    // Physics.
    const float seconds = dt / 1000.0f;
    if (heaterRelay() && !p.heaterBroken) p.chamberF += p.heaterRateFps * seconds;
    const float k = pumpRelay() ? 6.0e-4f : 2.78e-4f;
    p.chamberF -= k * (p.chamberF - p.ambientF) * seconds;
    const float dieDecay = expf(-(float)dt / 3000.0f);  // SHT40 die cools with a few-second time constant
    p.toDieRiseC *= dieDecay;
    p.fromDieRiseC *= dieDecay;
    if (p.model) p.model(p);
    // Radio.
    if (p.nodeOn && hostNowMs >= p.nextPacketMs) {
        sendPacket(p);
        p.nextPacketMs = hostNowMs + p.packetPeriodMs;
    }
    // One pass of loop().
    sensorsUpdate();
    probeCareUpdate(stateMachine.mode());  // same order as loop() in main.cpp
    relay.update();
    radioLinkUpdate();
    stateMachine.update();
    // Invariants.
    const uint8_t port0 = hostRelayPort0();
    const SystemMode mode = stateMachine.mode();
    if (heaterRelay()) {
        invariant(mode == SystemMode::RECHARGE, "I1 heater on outside recharge");
        invariant(p.lastChamberPacketMs != 0 && hostNowMs - p.lastChamberPacketMs <= CHAMBER_RETRY_MS + 100,
                  "I1 heater on without a recent chamber reading");
    }
    if (pumpRelay() && mode == SystemMode::RECHARGE) {
        invariant(valvesRelay(), "I2 recharge pump on with valves closed");
        invariant(p.lastPressurePacketMs != 0 && hostNowMs - p.lastPressurePacketMs <= SAMPLE_MAX_AGE_MS + 100,
                  "I2 recharge pump on without a recent vent pressure reading");
    }
    if (valvesRelay()) invariant(mode == SystemMode::RECHARGE, "I3 valves energized outside recharge");
    if (!relay.testActive()) {  // an Admin relay test pulse drives one pin on purpose
        if (mode == SystemMode::IDLE) invariant(port0 == 0, "I4 relay on while idle");
        invariant((port0 & ((1 << RELAY_BIT_AUX_12V) | (1 << RELAY_BIT_AUX_AC1) | (1 << RELAY_BIT_AUX_AC2))) == 0,
                  "I5 aux relay driven");
    }
    hostNowMs += dt;
}

static void runFor(Plant& p, uint32_t ms, uint32_t dt = 25) {
    const uint32_t end = hostNowMs + ms;
    while (hostNowMs < end) step(p, dt);
}

static bool runUntil(Plant& p, const std::function<bool()>& done, uint32_t maxMs, uint32_t dt = 25) {
    const uint32_t end = hostNowMs + maxMs;
    while (hostNowMs < end) {
        step(p, dt);
        if (done()) return true;
    }
    return false;
}

static bool logHas(const char* text) { return Serial.log.find(text) != std::string::npos; }
static size_t logCount(const char* text) {
    size_t n = 0, pos = 0;
    while ((pos = Serial.log.find(text, pos)) != std::string::npos) { n++; pos++; }
    return n;
}
static size_t logMark() { return Serial.log.size(); }
static bool logHasAfter(size_t mark, const char* text) {
    return Serial.log.find(text, mark) != std::string::npos;
}
static bool idle() { return stateMachine.mode() == SystemMode::IDLE; }

/* Print the last lines of the controller log, without the per-packet lines. */
static void dumpLogTail(size_t lines) {
    std::vector<std::string> kept;
    size_t start = 0;
    while (start < Serial.log.size()) {
        size_t end = Serial.log.find('\n', start);
        if (end == std::string::npos) end = Serial.log.size();
        std::string line = Serial.log.substr(start, end - start);
        if (line.find("ESP-NOW seq") == std::string::npos) kept.push_back(line);
        start = end + 1;
    }
    const size_t from = kept.size() > lines ? kept.size() - lines : 0;
    for (size_t i = from; i < kept.size(); ++i) printf("    | %s\n", kept[i].c_str());
}

static std::string takeMessage() {
    const char* m = stateMachine.takeStopMessage();
    return m ? std::string(m) : std::string();
}

static void reset(const char* name) {
    currentTest = name;
    skipping = onlyFilter != nullptr && strstr(name, onlyFilter) == nullptr;
    invariantReported = false;
    hostNowMs = 1000;
    Serial.log.clear();
    stateMachine = StateMachine();
    hostHeatRequests.clear();
    probeCareBegin();
    relay.begin();
    sensorsBegin();
    stateMachine.begin();
    if (!skipping) printf("%s\n", name);
}

/* Humidity model for a recharge that dries: To-Engine falls 1 point a minute
   after the chamber is hot, and reads dry once the chamber has cooled. */
static void dryingModel(Plant& p) {
    if (p.chamberF >= 240.0f && p.hotSince == 0) p.hotSince = hostNowMs;
    if (p.hotSince == 0) { p.toRH = 40.0f; return; }
    if (p.chamberF < 130.0f) { p.toRH = 10.0f; return; }
    const float minutes = (hostNowMs - p.hotSince) / 60000.0f;
    p.toRH = fmaxf(8.0f, 90.0f - minutes);
}

static bool startRecharge(Plant& p) {
    runFor(p, 40000);  // packets flowing, baseline candidates
    return stateMachine.command(Command::START_RECHARGE);
}

/* ---------- scenarios ---------- */

static void testNormalRecharge() {
    reset("Recharge completes and every pulse is vent-checked");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, idle, 6UL * 3600000UL), "recharge did not finish in 6 h");
    EXPECT(logHas("Stop: S16 RECHARGE_COMPLETE"), "no S16 stop");
    EXPECT(!logHas("NO FLOW"), "a pulse failed the vent check");
    EXPECT(logHas("cool down pulse"), "no cool down pulse was judged");
    EXPECT(logHas("safety pulse"), "safety pulses were not judged");
    EXPECT(logHas("Cool down done after"), "cool down end not logged");
    std::string m = takeMessage();
    EXPECT(m.find("Code S16") != std::string::npos, "message lacks Code S16: %s", m.c_str());
}

static void testNodeSilentMidPulse() {
    reset("Sensor node goes silent mid-pulse: pump stops, cool down advances, stops S04");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logHas("Recharge phase 1"); }, 3600000UL), "never reached PULSE_ON");
    runFor(p, 3000);
    p.nodeOn = false;
    const uint32_t silentAt = hostNowMs;
    runFor(p, SAMPLE_MAX_AGE_MS + 200);
    EXPECT(!pumpRelay(), "pump still on %lu ms after the node went silent",
           (unsigned long)(hostNowMs - silentAt));
    EXPECT(logHas("Recharge pulse cut short"), "cut pulse not reported");
    bool pumpRan = false;
    const bool stopped = runUntil(p, [&] { if (pumpRelay()) pumpRan = true; return idle(); },
                                  RECHARGE_COOLDOWN_MS + 600000UL);
    EXPECT(stopped, "failure cool down never finished");
    EXPECT(!pumpRan, "pump ran with no vent pressure");
    EXPECT(logCount("Recharge failure SENSOR_NODE_SILENT") == 1, "failure logged %u times",
           (unsigned)logCount("Recharge failure SENSOR_NODE_SILENT"));
    EXPECT(logHas("Stop: S04 SENSOR_NODE_SILENT"), "no S04 stop");
    const uint32_t took = hostNowMs - silentAt;
    EXPECT(took >= RECHARGE_COOLDOWN_MS && took <= RECHARGE_COOLDOWN_MS + SENSOR_LOSS_GRACE_MS + 10000UL,
           "stop came %lu s after silence", (unsigned long)(took / 1000));
    std::string m = takeMessage();
    EXPECT(m.find("cooled first") != std::string::npos, "message lacks the cool down line: %s", m.c_str());
}

static void testShortDropoutRecovers() {
    reset("10 s sensor dropout mid-pulse: pulse not judged, recharge continues and completes");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logHas("Recharge phase 1"); }, 3600000UL), "never reached PULSE_ON");
    runFor(p, 2000);
    p.nodeOn = false;
    runFor(p, 10000);
    p.nodeOn = true;
    p.nextPacketMs = hostNowMs;
    EXPECT(runUntil(p, idle, 6UL * 3600000UL), "recharge did not finish");
    EXPECT(!logHas("Recharge failure"), "a 10 s dropout caused a failure");
    EXPECT(logHas("Stop: S16 RECHARGE_COMPLETE"), "did not complete");
    EXPECT(!logHas("NO FLOW"), "the cut pulse was judged as NO FLOW");
}

static void testNoPressureAtStart() {
    reset("No vent pressure: recharge refused at start");
    Plant p; p.pressureValid = false;
    runFor(p, 40000);
    const char* block = stateMachine.startBlockReason(Command::START_RECHARGE);
    EXPECT(block != nullptr && strstr(block, "vent pressure") != nullptr, "start not blocked: %s",
           block ? block : "(null)");
    EXPECT(!stateMachine.command(Command::START_RECHARGE), "command accepted");
    runFor(p, 1000);
    EXPECT(idle(), "a recharge started");
}

static void testPressureLostWhileHeating() {
    reset("Vent pressure lost while heating: fault cool down, no pump, stop S08");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    runFor(p, 120000);
    p.pressureValid = false;
    bool pumpRan = false;
    EXPECT(runUntil(p, [&] { if (pumpRelay()) pumpRan = true; return idle(); }, 3UL * 3600000UL),
           "never stopped");
    EXPECT(!pumpRan, "pump ran without vent pressure");
    EXPECT(logHas("Recharge failure VENT_SENSOR_LOST"), "no vent failure");
    EXPECT(logHas("Stop: S08 VENT_SENSOR_LOST"), "no S08 stop");
}

static void testValveStuck() {
    reset("Valve stuck from the start: first pulse fails, immediate stop S09");
    Plant p; p.model = dryingModel; p.valveStuck = true;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, idle, 3600000UL), "never stopped");
    EXPECT(logHas("Stop: S09 VALVE_SUSPECT"), "no S09 stop");
    EXPECT(logCount("Vent pressure raw") == 1, "%u pulses judged, expected 1",
           (unsigned)logCount("Vent pressure raw"));
    EXPECT(hostRelayPort0() == 0, "outputs left on");
}

static void testValveSticksInCooldown() {
    reset("Valve sticks during cool down: the cool down pulse catches it, stop S09");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logHas("Recharge phase 3"); }, 4UL * 3600000UL), "never reached cool down");
    p.valveStuck = true;
    EXPECT(runUntil(p, idle, 3600000UL), "never stopped");
    EXPECT(logHas("Stop: S09 VALVE_SUSPECT"), "no S09 stop");
}

static void testToEngineLostInCooldown() {
    reset("To-Engine lost in cool down: pressure still confirms pulses, stop S05 after cool down");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logHas("Recharge phase 3"); }, 4UL * 3600000UL), "never reached cool down");
    runFor(p, 60000);
    p.toValid = false;
    const size_t mark = logMark();
    EXPECT(runUntil(p, idle, 3UL * 3600000UL), "never stopped");
    EXPECT(logHas("Recharge failure TO_ENGINE_LOST"), "no To-Engine failure");
    EXPECT(logHasAfter(mark, "cool down pulse"), "cool down pulses stopped although pressure was fine");
    EXPECT(logHas("Stop: S05 TO_ENGINE_LOST"), "no S05 stop");
}

static void testHeaterDeadAtStart() {
    reset("Heater dead from the start: heater response check stops S19 within minutes");
    Plant p; p.model = dryingModel; p.heaterBroken = true;
    EXPECT(startRecharge(p), "recharge start refused");
    const uint32_t started = hostNowMs;
    EXPECT(runUntil(p, idle, RECHARGE_HEATUP_MAX_MS), "never stopped");
    EXPECT(logHas("Recharge failure HEATER_NO_RISE"), "no heater failure");
    EXPECT(logHas("Stop: S19 HEATER_NO_RISE"), "no S19 stop");
    EXPECT(hostNowMs - started < 12UL * 60000UL, "took %lu min", (unsigned long)((hostNowMs - started) / 60000UL));
    EXPECT(!logHas("Recharge phase 1"), "pulsed without reaching target");
}

static void testHeaterDiesWhileHolding() {
    reset("Heater dies after reaching target: S19 within minutes, not at the 6 h limit");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logHas("Recharge phase 2"); }, 3600000UL), "never pulsed");
    runFor(p, 20UL * 60000UL);
    EXPECT(!logHas("HEATER_NO_RISE"), "false heater fault while the heater worked");
    p.heaterBroken = true;
    const uint32_t died = hostNowMs;
    EXPECT(runUntil(p, [] { return logHas("Recharge failure HEATER_NO_RISE"); }, 30UL * 60000UL),
           "dead heater not caught within 30 min");
    EXPECT(hostNowMs - died < 15UL * 60000UL, "caught after %lu min", (unsigned long)((hostNowMs - died) / 60000UL));
    EXPECT(runUntil(p, idle, RECHARGE_COOLDOWN_MS + 600000UL), "failure cool down never ended");
    EXPECT(logHas("Stop: S19 HEATER_NO_RISE"), "no S19 stop");
}

static void testWeakHeater() {
    reset("Weak heater that cannot reach 245 F: stops S14 or S19, never runs on");
    Plant p; p.model = dryingModel; p.heaterRateFps = 0.05f;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, idle, RECHARGE_HEATUP_MAX_MS + RECHARGE_COOLDOWN_MS + 600000UL), "never stopped");
    EXPECT(logHas("Stop: S14 HEATUP_TIMEOUT") || logHas("Stop: S19 HEATER_NO_RISE"), "no S14/S19 stop");
}

static void testThermalTrip() {
    reset("Chamber over 255 F: immediate stop S11, everything off");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    runFor(p, 60000);
    p.chamberF = 260.0f;
    p.model = nullptr;
    EXPECT(runUntil(p, idle, 5000), "no thermal stop");
    EXPECT(logHas("Stop: S11 THERMAL_CHAMBER"), "no S11 stop");
    EXPECT(hostRelayPort0() == 0, "outputs left on");
    std::string m = takeMessage();
    EXPECT(m.find("260 F") != std::string::npos, "message lacks the reading: %s", m.c_str());
}

static void testDryerDropoutGrace() {
    reset("Manual dryer: 10 s To-Engine dropout holds, 35 s dropout stops S05");
    Plant p;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_DRYER_MANUAL), "manual start refused");
    runFor(p, 20000);
    p.toValid = false;
    runFor(p, 10000);
    p.toValid = true;
    runFor(p, 5000);
    EXPECT(!idle(), "10 s dropout stopped the dryer");
    p.toValid = false;
    EXPECT(runUntil(p, idle, 60000), "35 s dropout did not stop the dryer");
    EXPECT(logHas("Stop: S05 TO_ENGINE_LOST"), "no S05 stop");
}

static void testAutoDryerSamples() {
    reset("Auto dryer starts with a sample: pump 10 s, settle 10 s, then decide");
    Plant p; p.fromRH = 12.0f;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_DRYER_AUTO), "auto start refused");
    runFor(p, 1000);
    EXPECT(logHas("Auto phase 4") && pumpRelay(), "did not start in SAMPLE_RUN with the pump on");
    runFor(p, AUTO_SAMPLE_RUN_MS);
    EXPECT(logHas("Auto phase 5") && !pumpRelay(), "no settle with the pump off");
    runFor(p, AUTO_SAMPLE_SETTLE_MS + 1500);
    EXPECT(logHas("Auto phase 3"), "dry sample did not go to WAITING");
}

static void testAutoRechargeBlocked() {
    reset("Saturation with Auto Recharge on but no vent pressure: stop S03, no recharge");
    Plant p; p.pressureValid = false;
    stateMachine.config.allowAutoRecharge = true;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_DRYER_MANUAL), "manual start refused");
    runFor(p, 5000);
    p.toRH = 55.0f;
    EXPECT(runUntil(p, idle, 150000), "no saturation stop");
    EXPECT(logHas("Stop: S03 SATURATED"), "no S03 stop");
    EXPECT(!logHas("Mode -> Recharge"), "a recharge started without vent pressure");
    std::string m = takeMessage();
    EXPECT(m.find("Auto Recharge could not start") != std::string::npos, "message: %s", m.c_str());
}

static void testRemoteStop() {
    reset("Remote STOP from the bridge: stop S02 with an on-screen message");
    Plant p;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_PURGE), "purge start refused");
    runFor(p, 5000);
    LinkCommand cmd = {};
    cmd.magic = AEROSENTRY_LINK_MAGIC;
    cmd.kind = LINK_KIND_COMMAND;
    cmd.command = REMOTE_STOP;
    cmd.sequence = 7;
    hostEspNowReceive(BRIDGE_MAC, (const uint8_t*)&cmd, sizeof(cmd));
    runFor(p, 200);
    EXPECT(idle(), "purge still running");
    EXPECT(logHas("Stop: S02 REMOTE_STOP"), "no S02 stop");
    std::string m = takeMessage();
    EXPECT(m.find("remote page") != std::string::npos, "message: %s", m.c_str());
}

static void testFailureDuringSafetyCheck() {
    reset("Node silent during the safety check: stops S04 at once (already cooled)");
    Plant p; p.model = dryingModel;
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logHas("Recharge phase 5"); }, 6UL * 3600000UL), "never reached SAFETY_WAIT");
    p.nodeOn = false;
    const uint32_t silentAt = hostNowMs;
    EXPECT(runUntil(p, idle, 120000), "did not stop");
    EXPECT(hostNowMs - silentAt <= SENSOR_LOSS_GRACE_MS + 6000UL, "took %lu s",
           (unsigned long)((hostNowMs - silentAt) / 1000));
    EXPECT(logHas("Stop: S04 SENSOR_NODE_SILENT"), "no S04 stop");
}

static void testMaxRunTime() {
    reset("Beads never dry: 6 h limit, cool down, safety check, stop S17");
    Plant p;
    p.model = [](Plant& q) { q.toRH = q.chamberF < 130.0f ? 10.0f : 50.0f; };
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, idle, 10UL * 3600000UL, 50), "never stopped");
    EXPECT(logHas("Recharge 6h max at temperature"), "no max-time latch");
    EXPECT(logHas("Stop: S17 RECHARGE_MAX_TIME"), "no S17 stop");
}

static void testFlakyPressure() {
    reset("Pressure drops every 5th packet for a whole recharge: no false stop");
    Plant p; p.model = [](Plant& q) {
        dryingModel(q);
        q.pressureValid = (q.seq % 5) != 0;
    };
    EXPECT(startRecharge(p), "recharge start refused");
    const bool finished = runUntil(p, idle, 6UL * 3600000UL);
    EXPECT(finished, "recharge did not finish");
    if (!finished) dumpLogTail(40);
    EXPECT(logHas("Stop: S16 RECHARGE_COMPLETE"), "did not complete");
    EXPECT(!logHas("NO FLOW"), "false NO FLOW");
    EXPECT(!logHas("Recharge failure"), "false failure");
}

static void testFlakyToEngine() {
    reset("To-Engine fails one read in five for a whole recharge: no false stop");
    Plant p; p.model = [](Plant& q) {
        dryingModel(q);
        q.toValid = (q.seq % 5) != 0;
    };
    EXPECT(startRecharge(p), "recharge start refused");
    const bool finished = runUntil(p, idle, 6UL * 3600000UL);
    EXPECT(finished, "recharge did not finish");
    if (!finished) dumpLogTail(30);
    EXPECT(logHas("Stop: S16 RECHARGE_COMPLETE"), "did not complete");
    EXPECT(!logHas("Recharge failure"), "false failure");
}

static void sendRemote(const uint8_t* mac, uint8_t code, uint8_t autoRecharge = 0) {
    static uint16_t sequence = 100;
    LinkCommand cmd = {};
    cmd.magic = AEROSENTRY_LINK_MAGIC;
    cmd.kind = LINK_KIND_COMMAND;
    cmd.command = code;
    cmd.dryerOnMinutes = 3;
    cmd.dryerOffHours = 1;
    cmd.dryerTargetRH = 20;
    cmd.autoRecharge = autoRecharge;
    cmd.sequence = sequence++;
    hostEspNowReceive(mac, (const uint8_t*)&cmd, sizeof(cmd));
}

static void testRemoteRechargeRefused() {
    reset("Remote Recharge from the bridge is refused");
    Plant p;
    runFor(p, 40000);
    sendRemote(BRIDGE_MAC, REMOTE_RECHARGE);
    runFor(p, 500);
    EXPECT(idle(), "a remote recharge started");
    EXPECT(logHas("Recharge must be started at the unit"), "refusal text missing");
}

static void testStrangerStartIgnored() {
    reset("Start from a device that is not the bridge is ignored");
    Plant p;
    runFor(p, 5000);
    sendRemote(STRANGER_MAC, REMOTE_PURGE);
    runFor(p, 500);
    EXPECT(idle(), "a stranger started the pump");
    EXPECT(logHas("ignored (not the bridge)"), "ignored start not logged");
}

static void testStrangerStopAccepted() {
    reset("STOP from any device is accepted");
    Plant p;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_PURGE), "purge start refused");
    runFor(p, 3000);
    sendRemote(STRANGER_MAC, REMOTE_STOP);
    runFor(p, 500);
    EXPECT(idle(), "stranger STOP ignored");
    EXPECT(logHas("Stop: S02 REMOTE_STOP"), "no S02 stop");
}

static void testRemoteDryerNeverArmsAutoRecharge() {
    reset("Remote dryer start with Auto Recharge on: saturation stops, no unattended recharge");
    Plant p;
    stateMachine.config.allowAutoRecharge = true;
    runFor(p, 5000);
    sendRemote(BRIDGE_MAC, REMOTE_DRYER_MANUAL, 1);
    runFor(p, 5000);
    EXPECT(stateMachine.mode() == SystemMode::DRYER_MANUAL, "remote manual dryer did not start");
    EXPECT(logHas("Auto Recharge stays off for remote starts"), "ack does not say Auto Recharge is off");
    EXPECT(!stateMachine.autoRechargeArmed(), "remote start armed Auto Recharge");
    p.toRH = 55.0f;
    EXPECT(runUntil(p, idle, 150000), "no saturation stop");
    EXPECT(logHas("Stop: S03 SATURATED"), "no S03 stop");
    EXPECT(!logHas("Mode -> Recharge"), "remote start led to a recharge");
}

static void testRemoteCannotChangeAutoRechargeSetting() {
    reset("Remote commands never change the Auto Recharge setting");
    Plant p;
    stateMachine.config.allowAutoRecharge = false;
    runFor(p, 5000);
    sendRemote(BRIDGE_MAC, REMOTE_STOP, 1);
    sendRemote(BRIDGE_MAC, REMOTE_RECHARGE, 1);
    runFor(p, 500);
    sendRemote(BRIDGE_MAC, REMOTE_DRYER_AUTO, 1);
    runFor(p, 500);
    EXPECT(!stateMachine.config.allowAutoRecharge, "remote turned Auto Recharge on");
}

static void testLocalDryerArmsAutoRecharge() {
    reset("Local dryer start with Auto Recharge on: saturation starts a recharge");
    Plant p;
    stateMachine.config.allowAutoRecharge = true;
    runFor(p, 40000);
    EXPECT(stateMachine.command(Command::START_DRYER_MANUAL), "manual start refused");
    runFor(p, 5000);
    EXPECT(stateMachine.autoRechargeArmed(), "local start did not arm Auto Recharge");
    p.toRH = 55.0f;
    EXPECT(runUntil(p, [] { return stateMachine.mode() == SystemMode::RECHARGE; }, 60000),
           "saturation did not start a recharge");
}

static void testRelayTestPulse() {
    reset("Relay test drives exactly one pin for 2 s, blocks starts, then all off");
    Plant p;
    runFor(p, 5000);
    EXPECT(relay.testPulseStart(0, 0, RELAY_TEST_PULSE_MS), "P0.0 pulse refused");
    runFor(p, 100);
    EXPECT(hostXl9535[2] == 0x01 && hostXl9535[3] == 0x00, "outputs %02X %02X, expected 01 00",
           hostXl9535[2], hostXl9535[3]);
    EXPECT((hostXl9535[0] & 0x01) == 0x01, "input register does not show P0.0 high");
    const char* block = stateMachine.startBlockReason(Command::START_PURGE);
    EXPECT(block != nullptr && strstr(block, "relay test") != nullptr, "start not blocked during a pulse");
    EXPECT(!relay.testPulseStart(0, 1, RELAY_TEST_PULSE_MS), "second pulse accepted while one runs");
    runFor(p, RELAY_TEST_PULSE_MS + 100);
    EXPECT(hostXl9535[2] == 0x00 && hostXl9535[3] == 0x00, "outputs not cleared after the pulse");
    EXPECT(relay.testPulseStart(1, 0, RELAY_TEST_PULSE_MS), "P1.0 pulse refused");
    runFor(p, 100);
    EXPECT(hostXl9535[2] == 0x00 && hostXl9535[3] == 0x01, "P1.0 pulse wrote %02X %02X",
           hostXl9535[2], hostXl9535[3]);
    runFor(p, RELAY_TEST_PULSE_MS + 100);
    EXPECT(hostXl9535[3] == 0x00, "port 1 not cleared");
}

static void testDuplicatePacketsCountedOnce() {
    reset("Sensor node sends every packet twice: the controller keeps one copy");
    Plant p; p.repeatPackets = true;
    runFor(p, 20000);
    EXPECT(logCount("ESP-NOW seq 5 ") == 1, "sequence 5 logged %u times", (unsigned)logCount("ESP-NOW seq 5 "));
    EXPECT(logCount("ESP-NOW seq 12 ") == 1, "sequence 12 logged %u times", (unsigned)logCount("ESP-NOW seq 12 "));
    EXPECT(stateMachine.command(Command::START_DRYER_AUTO), "auto start refused with duplicated packets");
}

static void testRelayBusOverlap() {
    reset("CH422G answers at the relay address: read-back test flags it, outputs stay right");
    Plant p;
    runFor(p, 2000);
    char report[120];
    EXPECT(relay.readbackTest(report, sizeof(report)), "two-order read did not recover the writes: %s", report);
    uint8_t t0 = 0, t1 = 0;
    relay.testPulseStart(0, 0, RELAY_TEST_PULSE_MS);
    EXPECT(relay.readPairTrue(0x02, t0, t1) && t0 == 0x01 && t1 == 0x00, "true output read %02X %02X", t0, t1);
    EXPECT(relay.readPairTrue(0x00, t0, t1) && (t0 & 0x01), "true input does not show P0.0 high");
    relay.allOff();
    EXPECT(strstr(report, "wrote A5 5A read 24 58 recovered A5 5A") != nullptr, "report: %s", report);
    EXPECT(hostXl9535[4] == 0x00 && hostXl9535[5] == 0x00, "polarity not restored");
    // The driver never reads back, so K1 and the bits the CH422G clears stay as commanded.
    relay.aux12vOn();
    relay.fanOn();
    relay.pumpOn();
    EXPECT(hostXl9535[2] == ((1 << RELAY_BIT_AUX_12V) | (1 << RELAY_BIT_FAN) | (1 << RELAY_BIT_PUMP)),
           "outputs %02X after three writes", hostXl9535[2]);
    EXPECT(relay.commandedPort0() == hostXl9535[2], "commanded state differs from the outputs");
    relay.allOff();
    hostCh422Overlap = false;
    EXPECT(relay.readbackTest(report, sizeof(report)), "read-back test failed on a clean bus: %s", report);
    hostCh422Overlap = true;
}

static void testRestartReport() {
    reset("Restart during a cycle: S18 message names the cycle and the cause");
    stateMachine.reportRestart(SystemMode::RECHARGE, "The controller froze and the watchdog restarted it.");
    std::string m = takeMessage();
    EXPECT(m.find("Recharge ended. Code S18") != std::string::npos, "message: %s", m.c_str());
    EXPECT(m.find("watchdog") != std::string::npos, "cause missing: %s", m.c_str());
    stateMachine.reportRestart(SystemMode::IDLE, "The controller crashed and restarted.");
    m = takeMessage();
    EXPECT(m.find("Code S18") != std::string::npos && m.find("ended") == std::string::npos,
           "idle restart message: %s", m.c_str());
}

/* ---------- probe heater scenarios ---------- */

static void testSaturationConfirmedOnDriedProbe() {
    reset("Saturation: the probe is burned off first, then the stop is confirmed (S03)");
    Plant p;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_DRYER_MANUAL), "manual start refused");
    runFor(p, 5000);
    p.toRH = 55.0f;
    EXPECT(runUntil(p, idle, 150000), "no saturation stop");
    EXPECT(logHas("confirm saturation"), "no burn-off before the stop");
    EXPECT(p.toPulses >= 1, "the heater never pulsed (%lu)", (unsigned long)p.toPulses);
    EXPECT(logHas("Stop: S03 SATURATED"), "no S03 stop");
}

static void testWetProbeDoesNotFalseSaturate() {
    reset("Wet probe reads 99% in dry air: the burn-off clears it, no false S03");
    Plant p; p.toRH = 20.0f;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_DRYER_MANUAL), "manual start refused");
    runFor(p, 5000);
    p.toWater = 2;  // condensation one 2-pulse burn-off can clear
    runFor(p, 150000);
    EXPECT(!idle(), "a wet probe stopped the dryer");
    EXPECT(!logHas("Stop: S03"), "false saturation stop");
    EXPECT(p.toWater == 0, "chip still wet (%d)", p.toWater);
    EXPECT(logHas("confirm saturation"), "the wet reading was not checked with a burn-off");
}

static void testProbeTooHotSaturationStops() {
    reset("Probe too hot to heat: saturation stops on the reading, S03, after the wait");
    Plant p; p.toC = 62.0f;
    runFor(p, 5000);
    EXPECT(stateMachine.command(Command::START_DRYER_MANUAL), "manual start refused");
    runFor(p, 5000);
    p.toRH = 55.0f;
    EXPECT(runUntil(p, idle, 150000), "no saturation stop");
    EXPECT(p.toPulses == 0, "heater pulsed a 62 C probe (%lu)", (unsigned long)p.toPulses);
    EXPECT(logHas("Stop: S03 SATURATED"), "no S03 stop");
}

static void testPostRechargeSeries() {
    reset("After a recharge both probes are dried every 3 minutes for 30 minutes");
    Plant p;
    runFor(p, 5000);
    probeCareRechargeEnded();
    runFor(p, PROBE_POST_RECHARGE_MS - 60000UL);
    const size_t during = logCount("post-recharge series");
    EXPECT(during >= 2 * 8, "only %u post-recharge burn-offs in 29 min", (unsigned)during);
    EXPECT(p.toPulses >= 8 * PROBE_DRY_PULSES_LONG - 3, "To heater pulsed %lu times", (unsigned long)p.toPulses);
    EXPECT(p.fromPulses >= 8 * PROBE_DRY_PULSES_LONG - 3, "From heater pulsed %lu times", (unsigned long)p.fromPulses);
    EXPECT(!logHas("refused"), "a burn-off was refused");
    runFor(p, 5UL * 60000UL);
    const size_t after = logCount("post-recharge series");
    runFor(p, 10UL * 60000UL);
    EXPECT(logCount("post-recharge series") == after, "series kept running after 30 min");
    EXPECT(logHas("wet history"), "no wet-history drying after the series");
}

static void testIdleCondensationZone() {
    reset("Idle: near the dew point a probe is dried every 10 min; in dry air never");
    Plant p; p.toRH = 90.0f; p.fromRH = 30.0f;  // 90% at 25 C is under 2 C from the dew point
    runFor(p, 35UL * 60000UL);
    EXPECT(logCount("condensation zone") >= 3, "only %u zone burn-offs in 35 min",
           (unsigned)logCount("condensation zone"));
    EXPECT(p.fromPulses == 0, "the dry probe was heated (%lu)", (unsigned long)p.fromPulses);
    EXPECT(idle(), "left idle");
}

static void testDisplayHoldsWhileHeating() {
    reset("Display value stays steady while a probe heats and recovers");
    Plant p; p.toRH = 30.0f;
    runFor(p, 60000);
    float before = 0;
    EXPECT(probeDisplayRH(Probe::TO_ENGINE, before), "no display value");
    float worst = 0;
    probeDry(Probe::TO_ENGINE, PROBE_DRY_PULSES, "test");
    bool sawBusy = false;
    for (int i = 0; i < 2400; ++i) {  // 60 s
        step(p, 25);
        if (probeBusy(Probe::TO_ENGINE)) sawBusy = true;
        float now = 0;
        if (probeDisplayRH(Probe::TO_ENGINE, now)) worst = fmaxf(worst, fabsf(now - before));
    }
    EXPECT(sawBusy, "the probe never went busy");
    EXPECT(worst < 1.5f, "display moved %.1f points during a burn-off", worst);
}

/* ---------- vent pressure reference scenarios (2026-11-25) ---------- */

static void testVentBlockedStopsS20() {
    reset("Blocked vent line (pulse rises far too much): immediate stop S20");
    Plant p; p.model = dryingModel;
    p.flowStep = 800000;  // a finger over the tube, a closed valve
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, idle, 3UL * 3600000UL), "recharge did not stop");
    EXPECT(logHas("Stop: S20 VENT_BLOCKED"), "no S20 stop");
    EXPECT(logHas("BLOCKED"), "pulse not logged as BLOCKED");
    EXPECT(logCount("Vent pressure raw") == 1, "%u pulses judged, expected 1",
           (unsigned)logCount("Vent pressure raw"));
    std::string m = takeMessage();
    EXPECT(m.find("Code S20") != std::string::npos, "message lacks Code S20: %s", m.c_str());
}

static void testVentUnsteadyFailsClosed() {
    reset("Vent pressure jumps around: no pump pulse is ever run, fails closed with S21");
    Plant p; p.model = [](Plant& q) {
        dryingModel(q);
        q.ambientRaw = 4700000 + ((q.seq & 1) ? 30000 : 0);
    };
    EXPECT(startRecharge(p), "recharge start refused");
    bool pumpRan = false;
    const bool stopped = runUntil(p, [&] { if (pumpRelay()) pumpRan = true; return idle(); },
                                  4UL * 3600000UL);
    EXPECT(stopped, "never stopped");
    EXPECT(!pumpRan, "the pump ran with no steady vent reference");
    EXPECT(logHas("Recharge failure VENT_UNSTABLE"), "no vent failure");
    EXPECT(logHas("Stop: S21 VENT_UNSTABLE"), "no S21 stop");
}

static void testIdleDriftDoesNotFakeFailure() {
    reset("Idle vent pressure drifting up 24,000 a minute: weak but real pulses still pass");
    Plant p; p.model = dryingModel;
    p.flowStep = 30000;  // only 1.5x the limit, like the bench on 2026-11-25
    p.model = [](Plant& q) {
        dryingModel(q);
        q.ambientRaw = 4690000 + (int32_t)((hostNowMs - 1000UL) / 1000UL) * 400;
    };
    EXPECT(startRecharge(p), "recharge start refused");
    EXPECT(runUntil(p, [] { return logCount("OK good") >= 8 || idle(); }, 2UL * 3600000UL),
           "fewer than 8 pulses in 2 h");
    EXPECT(!logHas("NO FLOW"), "drift caused a NO FLOW");
    EXPECT(!logHas("Stop: S09"), "drift stopped the recharge");
    EXPECT(logHas("Vent reference"), "no reference was logged");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // show progress even if a scenario hangs
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) Serial.echo = true;
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) onlyFilter = argv[++i];
    }
    testNormalRecharge();
    testNodeSilentMidPulse();
    testShortDropoutRecovers();
    testNoPressureAtStart();
    testPressureLostWhileHeating();
    testValveStuck();
    testValveSticksInCooldown();
    testToEngineLostInCooldown();
    testHeaterDeadAtStart();
    testHeaterDiesWhileHolding();
    testWeakHeater();
    testThermalTrip();
    testDryerDropoutGrace();
    testAutoDryerSamples();
    testAutoRechargeBlocked();
    testRemoteStop();
    testFailureDuringSafetyCheck();
    testMaxRunTime();
    testFlakyPressure();
    testFlakyToEngine();
    testRemoteRechargeRefused();
    testStrangerStartIgnored();
    testStrangerStopAccepted();
    testRemoteDryerNeverArmsAutoRecharge();
    testRemoteCannotChangeAutoRechargeSetting();
    testLocalDryerArmsAutoRecharge();
    testRelayTestPulse();
    testDuplicatePacketsCountedOnce();
    testRelayBusOverlap();
    testRestartReport();
    testSaturationConfirmedOnDriedProbe();
    testWetProbeDoesNotFalseSaturate();
    testProbeTooHotSaturationStops();
    testPostRechargeSeries();
    testIdleCondensationZone();
    testDisplayHoldsWhileHeating();
    testVentBlockedStopsS20();
    testVentUnsteadyFailsClosed();
    testIdleDriftDoesNotFakeFailure();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
