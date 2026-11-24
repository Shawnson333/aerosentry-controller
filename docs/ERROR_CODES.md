# Stop codes

Every way a cycle can end has a permanent code. The touch screen shows it on the last line of the
message ("Recharge ended. Code S09"). The serial log prints one line per stop:

```
Stop: S09 VALVE_SUSPECT mode=Recharge | Valve position suspect. | Pump pulse raised the vent pressure 2238 counts. It needs 20000. | ...
```

The Admin page lists the last three stops. The future SD event log uses the same code and name
(`epoch,code,name,mode,message`, see `src/logging.cpp`). Codes are never renumbered or reused. A new
cause takes the next free number. The source of truth is `enum class StopReason` in
`include/statemachine.h` and the text table in `src/statemachine.cpp`.

"Failure cool down" means the heater is off, the valves stay open (venting outside), the case fan runs,
and the pump pulses 10 s every 10 min only while the vent pressure check can confirm each pulse. It ends at
120 F or after 2 h. Then the valves close and the cycle stops with the code. The message adds "The chamber
was cooled first".

| Code | Name | When | What the controller does | What to check |
|---|---|---|---|---|
| S01 | USER_STOP | Stop pressed on the screen | Everything off, valves closed. No message. | |
| S02 | REMOTE_STOP | Stop from the phone page (any sender is accepted) | Everything off. Message on the screen so someone at the unit knows why. | |
| S03 | SATURATED | To-Engine above 40% for 30 s with the pump on (Purge, Manual, Auto) | Everything off. With Auto Recharge armed it starts a recharge instead; if a recharge sensor is missing it stops and says so. | Run a Recharge. |
| S04 | SENSOR_NODE_SILENT | No packets from the sensor node for 30 s | Purge and dryers: stop. Recharge: failure cool down, pump off (no pressure to confirm it). | Sensor node power, antenna, USB cable. |
| S05 | TO_ENGINE_LOST | No valid To-Engine reading for 30 s | Same as S04. In recharge the cool down still pulses if vent pressure is present. | To-Engine probe and cable. |
| S06 | FROM_ENGINE_LOST | No valid From-Engine reading for 30 s (Auto dryer only) | Stop. | From-Engine probe and cable. |
| S07 | CHAMBER_LOST | No valid chamber temperature for 30 s (Recharge) | Failure cool down. The heater is never newly turned on without a fresh reading and is off within 6 s of the last good one (`CHAMBER_RETRY_MS`). | Thermistor, 10k resistor, cable. |
| S08 | VENT_SENSOR_LOST | No vent pressure reading for 30 s during a recharge | Failure cool down with the pump OFF: the valve position cannot be confirmed. A recharge will not start without it. | HX711 module (3.3 V), DOUT 48 / SCK 47, the 2.5 mm tube. |
| S09 | VALVE_SUSPECT | A recharge, cool down or safety pump pulse did not raise the vent pressure 20,000 counts over the ambient average | Immediate stop: pump, heater and valves off. Moisture may have reached the engine. | Both valves, their 12 V supply, the vent line. |
| S10 | VALVE_NO_CURRENT | Valve coil current below 150 mV while energized | Immediate stop. Disabled until the shunt is wired (`VALVE_CURRENT_CHECK_ENABLED`). | Valve wiring and 12 V. |
| S11 | THERMAL_CHAMBER | Chamber above 255 F | Immediate stop, everything off, starts refused until it cools. Also while idle. | Heater control, thermistor placement. The 266 F fuse is the hardware backstop. |
| S12 | THERMAL_TO_PROBE | To-Engine probe above 250 F | Same as S11. | Where the probe is mounted. |
| S13 | THERMAL_FROM_PROBE | From-Engine probe above 250 F | Same as S11. | Where the probe is mounted. |
| S14 | HEATUP_TIMEOUT | Chamber not at 245 F within 1 h of starting | Failure cool down, then stop. | Heater mat, its fuse, its relay (K3), the thermistor. |
| S15 | SAFETY_FAILED | A safety sample after the cool down was above 15% | Stop, valves closed. | The desiccant is still wet. Run another Recharge. |
| S16 | RECHARGE_COMPLETE | Three dry samples, cool down, two safety samples at or below 15% | Normal end. With a dryer cycle waiting (Auto Recharge) it resumes that cycle instead. | |
| S17 | RECHARGE_MAX_TIME | 6 h at temperature without three dry samples; the cool down and safety check then passed | Normal end. | The beads may not be fully dry. |
| S18 | CONTROLLER_RESTART | At boot: power was lost during a cycle, or the last reset was a watchdog, crash or brownout (even when idle) | Everything was turned off at boot. The message gives the reset cause. | Power supply, then the serial log around the reset. |

Starts that are refused (missing sensor, thermal limit, relay test running, remote Recharge) are not stops.
They show their reason at the moment of the request and have no code.
