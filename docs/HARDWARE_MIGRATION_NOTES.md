# Hardware migration discussion - 2026-11-25

These are planning notes, not an implemented or bench-validated design. They
supplement `NEXT_SESSION.md`, `README.md`, and `docs/PROJECT_NOTES.md`. No
firmware, wiring, safety limit, or running hardware was changed in this
discussion.

## Motivation and direction

- The user reports K1 is not defective: its problem is the communication
  conflict between the current Waveshare display and relay board. The
  CH422G/XL9535 shared-address and masked-read behavior are documented in
  `docs/PROJECT_NOTES.md`. Do not describe this as a failed relay coil.
- The project has evolved from hardware assembled around available parts.
  The user is considering making an integrated relay controller the sole owner
  of the control logic and directly connected sensors, with a separate display.
- Direct sensor connections would remove the current wireless sensor-data hop.
  Keeping a Heltec as a radio-only device is acceptable.
- The user confirms heater and pump loads are well within the relay ratings.
  Relay behavior, wiring protection, and safety still need bench verification.

## Chosen controller candidate

**Waveshare ESP32-S3-ETH-8DI-8RO, standard Ethernet (non-PoE) version.**

https://www.waveshare.com/product/esp32-related/industrial-controller/esp32-s3-eth-8di-8ro.htm

It combines an ESP32-S3, eight relay outputs, isolated RS485, an RTC and
DIN-rail mounting. The normal Ethernet port can remain unused. PoE is not
needed for the current plan; an existing suitable DC supply is expected.
The onboard RTC is a PCF85063, so the existing DS3231-specific clock driver
needs adaptation. Relay initialization and fail-safe behavior need testing.

The similar **8DI-8DO** is not a relay board: its transistor outputs need
external relays for AC loads. The **8DI-8RO-C** replaces RS485 with CAN.

## Proposed architecture and communication

```
SHT40s + thermistor + HX711 --> relay controller --> pump/heater/fan/valves
                                    |
                                  RS485
                                    |
                           separate touchscreen

Heltec radio <--> controller: existing or revised local radio link, TBD
```

- The controller owns the state machine, sensor validation, interlocks, relay
  outputs, clock, and decisions. The screen sends requests and displays
  acknowledged status; it is not the safety controller.
- RS485 is the preferred wired touchscreen link and requires no network.
  Specify a protocol for status, settings, start/stop, acknowledgements,
  sequence/retry handling, and link-loss indication.
- Preserve the distinction between an operator at the unit and a remote
  radio/phone command: remote Recharge remains prohibited and remote starts
  must not arm Auto Recharge. Define and verify the local-panel trust boundary.
- A lost display link must not disable sensor monitoring or safety functions.
  STOP needs acknowledgement and a separate dependable local means to stop.
  Decide and test the behavior on loss of the display link.
- A Heltec may remain as a radio-only bridge. The long-range LoRa/MeshCore path
  is a future integration, not something established by this discussion.

## Direct sensors: preliminary pin feasibility

Waveshare board pinout:
https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO

The ESP32-S3 has two hardware I2C controllers. This is a proposed allocation,
not an electrically verified wiring diagram:

| Function | Candidate GPIO | Notes |
| --- | --- | --- |
| First SHT40 | SDA 42, SCL 41 | Shares onboard RTC and relay-expander I2C bus. |
| Second SHT40 | SDA 43, SCL 44 | Separate I2C bus; these are default UART0 pins. |
| Chamber thermistor | GPIO 1 | ADC1 input; retain a suitable 3.3 V divider. |
| HX711 pressure | GPIO 2 and GPIO 21 | Digital data and clock; confirm assignment. |
| Optional valve-current ADC | GPIO 3 | ADC1, but a strapping pin; verify boot behavior. |

The probes have the same I2C address, so they cannot simply share one
unmodified bus. An alternative worth investigating is an I2C multiplexer on
the second controller bus, giving each SHT40 a separate downstream channel and
keeping sensor wiring off the onboard relay/RTC bus. This adds a small module;
check fault isolation, bus recovery and timeout behavior rather than assuming
the multiplexer makes failures safe.

If a sensor on the onboard relay bus holds it low, relay commands could be
affected. Validate the complete failure mode and any independent heater
protection. GPIO 43/44 UART reassignment, USB programming/logging, 3.3 V
levels, pull-ups, sensor cable length, thermistor accuracy, HX711 wiring, and
pressure calibration all remain unverified. GPIO 19/20 are used for USB;
GPIO 45/47/48 are associated with the card slot. Do not assume unused pins
are available without checking the actual board and schematic.

## Header breakout

The internal expansion header is 2x14 at 2.54 mm. A short 28-conductor ribbon
to a DIN-rail screw-terminal breakout is attractive. Possible custom-assembled
parts researched:

- https://www.peconnectors.com/male-idc-headers-plugs-.100/hws15893/
- https://www.bipom.com/products/us/4379948.html (DTB3528V1-DIN)

The breakout has a 0.6-inch DIP footprint, not the controller's 2x14 header
spacing. A suitable 2x14 female IDC connector, ribbon, and IDC-to-DIP transition
would need assembly, and the breakout likely needs soldering. This is not a
verified plug-and-play assembly. Check physical clearance, case exit, strain
relief, signal mapping with a continuity meter, and separation from mains.

## Display direction

The current 4.3-inch Type B display could be a remote UI over its built-in
RS485, but its dim sweeps/color flashes remain unresolved. Moving control
away may reduce workload; it is not proof the glitch will disappear. Notes
mention a 12 MHz pixel-clock experiment that has not been validated.

The user prefers a **wide screen without another communications module** and
wants to eliminate the display-glitch concern. A promising candidate, not yet
an explicitly final purchase decision:

**Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, standard version, SKU 32510**
https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm

It is a 7-inch 1024x600 landscape capacitive touchscreen with MIPI DSI and
onboard RS485. No separate RS485 adapter is needed. It has a different display
path, but glitch-free behavior has not been verified. Confirm enclosure fit,
display power, connector pinout and temperature suitability. The screen
firmware, display driver, layout and touch handling will require porting to
ESP32-P4; Waveshare recommends ESP-IDF, although its repository also provides
Arduino examples. Existing UI uses LVGL 8.3 and 800x480.

There is no requirement to use Waveshare for both devices: RS485 permits a
different vendor if its hardware and software are suitable. One alternative
identified is VIEWE's 7-inch ESP32-P4 MIPI-DSI/RS485 display:
https://www.displaywiki.com/products/esp32-p4-wifi6-7inch-1024x600-touch-uart-display-hmi/
Its suitability and support have not been independently validated.

## Software and validation

Most existing cycle logic, stop codes and host scenarios are candidates for
reuse, not guaranteed drop-in. Adapt the relay and RTC drivers and bring the
Heltec sensor and probe-heater logic onto the controller. Existing probe-heater
operations can block for about a second on the sensor node; direct acquisition
must not delay controller safety or STOP handling.

The current touchscreen directly accesses controller objects, settings and
diagnostics; splitting it into a separate UI needs a new communication
boundary and changes beyond swapping the display driver. Keep the existing
remote-command authorization and safety invariants.

Develop alongside the existing system. Validate sensor loss, stale readings,
I2C faults, relay state on boot/reset and failed writes, watchdog response,
display disconnect/reconnect, STOP, thermal protections, vent pressure, and
complete dryer/recharge sequences before switching over. The current mechanical
thermal fuse remains a hardware backstop. Any proposed extra independent
heater cutoff requires proper design and verification.

An earlier rough estimate was several focused days of coding plus several
days of integration and testing, potentially 1-3 weeks elapsed depending on
hardware results and long recharge tests. This is an estimate, not a promise.

**Never flash a board or start/stop a bench test without the user's explicit
instruction.** Do not change safety thresholds, relay mapping, or sensor
calibration without the user's decision.
