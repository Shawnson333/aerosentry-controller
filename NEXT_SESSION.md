# Where we left off (2026-11-25)

Branch `v1-beta` (main is the same commit). Read this, then `README.md`, `docs/BENCH_TEST_CHECKLIST.md`
(section 10 is new) and the last entries of `docs/PROJECT_NOTES.md`.

## State of the hardware
- Controller (Waveshare, COM4) is flashed with the current `v1-beta` build, read back OK. It includes the Admin fix
  (commit 783e75c): the first Admin build crashed because a format string was left with two `%s` too many.
- Sensor node (Heltec, COM10, USB serial number = MAC F8:5B:1B:BF:3F:EC) runs packet v4. Controller and node must be
  flashed together whenever `common/sensor_packet.h` changes.
- Nothing from the SHT40 heater work has been seen on a real recharge yet.

## 2026-11-25 update: vent check redesigned (committed, NOT flashed)
- Live recharge stopped twice with S09 (steps 10,185 and 14,649). Real pulse rises were only 21,000-28,000 counts
  (earlier runs 70,000-130,000), and the average baseline made it look worse. Cause of the weak flow is unknown:
  check valves, tubing, pump, pressure tap.
- New: each pulse is judged against the median of the pump-off readings from the 6 s before it. No steady reference =
  no pump (S21 after 2 min). Rise of 500,000+ = S20 VENT_BLOCKED (the user's finger over the tube gave 1.65 million).
  Limit is now 10,000 (user decision 2026-11-25; wrong valve ~2,000, soft silicone vent line ~21,000, old stiff line 70,000+). Sim: 40 scenarios, 185 checks, 0 failures. The controller on COM4 still runs the OLD check;
  flash it only when the user says so.
- The recharge capture is `captures/recharge-20261124-2358.txt` (local file, may be committed). Heater burn-offs
  during that run looked normal but were not reviewed in detail.

## Done today
- Admin screen: raw probe readout and "Dry To" / "Dry From" buttons.
- Host sim: six heater scenarios. 37 scenarios, 169 checks, 0 failures (`python tools/host_test.py`, about 4 min).
- Docs, bench checklist section 10 (probe heaters), cyan outlined UI, stamped serial capture tool
  (`tools/capture_serial_stamped.py`).

## Open
1. **Live recharge test** (the user runs it). First do checklist 10.1-10.5 in Admin. Watch for refused / too-hot
   burn-offs and any stop code. Unknown: whether one 2-pulse burn-off clears a really wet probe; if not, the
   saturation confirm could stop on a false S03.
2. **Screen glitches** (one-frame dim sweeps, occasional colour flash). Not caused by the clock flash write (ruled
   out; RTC is present). Cause unknown. Next experiment: `DISPLAY_PCLK_HZ` 14 MHz -> 12 MHz in `include/config.h`
   (controller-only flash). Stamped capture of one boot is in `captures/glitch-stamped.txt`.
3. UI restyle is conservative (cyan accent, outlined buttons). Not seen in detail by the user; more "Jarvis" is
   approved if wanted.
4. Update the checklist results column after the recharge, update HANDOFF.md for the probe heater work.
5. K1 is closed: the CH422G corrupts writes to P0.0 at 0x21. Leave it.

## Working rules the user set
- Never flash, start or stop a bench test without being told; always pin the upload port.
- Capture serial logs, do not poll them. Do not run the full sim repeatedly (about 4 min; run once, read once).
- Say plainly what is unverified. Every distinct error state gets its own message and code.
- Keep replies short. Write edit scripts with backslashes to a file, not a heredoc (backslashes collapse), and
  check that every replace actually matched (my Admin crash came from a replace that silently did nothing).

## Cause of the weak vent pressure (user, 2026-11-25)
The vent tube. Earlier tests used a ~2 ft coiled bench piece; the swap was a straight 1 ft piece. A 2 ft silicone piece
gave ~60,000 at the first pulse (drifting to ~47,000). The rise depends on tube length and coil. When the final enclosure
and tube length exist, calibrate `VENT_PRESSURE_MIN_STEP` (now 10,000) from real recharges. Overnight capture:
`captures/recharge3-20261125-0101.txt` (git-ignored).

## 2026-11-25 later (committed, NOT flashed; the board runs the older build)
- Target 240 F (hold 235-240). A passed cool down safety check is a pass even after the 6 h limit (S16; S17 retired).
- NEW after the safety check passes: 15 min vent purge outside, valves open, pump in three 5 min segments of steady air, pressure watched live (S09 if it falls, S20 if blocked); then
  seal, and a dryer cycle that asked for the recharge resumes (also after the time limit). Sim 201 checks pass (43 scenarios).
- TODO test: unlimited recharge (no 6 h limit) from a saturated chamber to see the true time to dry (checklist 8.6).
- Observed: after the recharge the closed-loop dryer showed From-Engine above 30% vs To-Engine 6-7%; likely condensate in
  the chamber-to-valve line. Check that the purge fixes it (checklist 8.5).

## Picking up on another computer
`git clone https://github.com/Shawnson333/aerosentry-controller.git`, `git checkout v1-beta` (main is identical), install
PlatformIO. Nothing needed lives outside git except: the serial captures (`captures/recharge*.txt`, git-ignored) and Claude's
local memory. To flash from there, the boards must be plugged in (check the COM ports: controller = Waveshare, node =
USB serial number F8:5B:1B:BF:3F:EC). Host sim needs `pio pkg install -g --tool "platformio/toolchain-gccmingw32"`.
