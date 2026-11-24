#ifndef RUN_STATE_H
#define RUN_STATE_H

#include <Arduino.h>

enum class SystemMode;

/* ========================================================================
   RUN STATE — remembers in NVS whether a cycle was running.
   If the controller restarts in the middle of a cycle (power loss, the loop
   watchdog, a crash, a brownout), the next boot reports it with stop code
   S18 instead of silently sitting idle. Written only when a cycle starts
   and when it ends: NVS writes briefly block the PSRAM bus, which can
   glitch the RGB display, so never write it from a periodic path.
   ======================================================================== */

/* Read the saved state and the reset cause, print both, and clear the saved
   state. Returns the mode that was running when the controller restarted,
   or SystemMode::IDLE. Call once in setup(). */
SystemMode runStateBegin();

/* One sentence for why the chip restarted, from esp_reset_reason(). */
const char* runStateResetCause();

/* True when the last reset was a watchdog, a crash or a brownout: worth
   reporting even if no cycle was running. */
bool runStateUnexpectedReset();

/* Record the mode now running (IDLE when a cycle ends). Skips the write
   when the saved value already matches. */
void runStateSave(SystemMode mode);

#endif // RUN_STATE_H
