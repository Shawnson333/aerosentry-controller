#ifndef PROBE_CARE_H
#define PROBE_CARE_H

#include <Arduino.h>
#include "probe_heat.h"

enum class SystemMode;

/* ========================================================================
   PROBE CARE — when the SHT40 probes are dried with their heaters, and
   which readings can be trusted (2026-11-24).

   Water on an SHT40 makes it read high (up to 100%) until it evaporates.
   Condensation forms whenever the probe or its stainless tube is at or below
   the dew point of the air around it, which happens well before the reading
   says 100%. So the rules are:
     R1 Decision readings come from a freshly dried probe: the state machine
        asks for a burn-off before every recharge sample, safety sample,
        auto dryer sample and saturation stop (probeDry()).
     R2 Condensation zone: a probe within PROBE_CONDENSE_SPREAD_C of its dew
        point is dried every PROBE_ZONE_INTERVAL_MS, in any mode.
     R3 Wet history: after a recharge, or a reading of PROBE_WET_RH or more,
        dry every PROBE_POST_RECHARGE_INTERVAL_MS for PROBE_POST_RECHARGE_MS,
        then every PROBE_WET_INTERVAL_MS for PROBE_WET_HOLD_MS.
     R4 Hard limits (probe under 60 C, 10% duty, gaps) are enforced on the
        sensor node by ProbeHeater (common/probe_heat.h).
     R5 Display: the user screens show a smoothed value made only from clean
        readings, held steady while a probe is heating or recovering.
   Water can only push a reading HIGH, so a refused or failed burn-off errs
   toward more drying or a re-run, never toward a false "dry".
   ======================================================================== */

enum class Probe : uint8_t { TO_ENGINE = HEAT_PROBE_TO, FROM_ENGINE = HEAT_PROBE_FROM };

void probeCareBegin();

/* Call every loop after sensorsUpdate(): resends unacknowledged requests,
   runs R2 and R3, and updates the display values. */
void probeCareUpdate(SystemMode mode);

/* Ask the node to dry a probe (1-3 pulses). True when a burn-off was sent or
   one is already running. reason goes to the log. */
bool probeDry(Probe probe, uint8_t pulses, const char* reason);

/* Heating, recovering, or a request on its way: the reading is not for decisions. */
bool probeBusy(Probe probe);

/* The probe has a valid reading, is not busy, and was dried within maxAgeMs. */
bool probeDriedWithin(Probe probe, uint32_t maxAgeMs);

/* The node refused the last burn-off (probe too hot, or duty budget). */
bool probeRefused(Probe probe);

/* A recharge just ended (any way): start the R3 drying series. */
void probeCareRechargeEnded();

/* Smoothed humidity for the user screens. False when there is nothing to show. */
bool probeDisplayRH(Probe probe, float &rh);

/* One line for the Admin / bench screen: state, dew point spread, dried age. */
void probeStateText(Probe probe, char* buf, size_t size);

#endif // PROBE_CARE_H
