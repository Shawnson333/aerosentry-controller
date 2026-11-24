/* ========================================================================
   UI — Controller (ties all screens together)
   
   uiBegin() and uiLoop() are implemented here.
   Individual screen functions live in ui_main.cpp, ui_warning.cpp, etc.
   ======================================================================== */

#include "ui.h"
#include "lvgl_v8_port.h"

void uiBegin() {
    uiShowLogo();
}

void uiLoop() {
    uiLogoTick();
    lvgl_port_loop();
}

void uiHandleStartResult(Command cmd) {
    const char* block = stateMachine.startBlockReason(cmd);
    if (block == nullptr && stateMachine.command(cmd)) {
        uiShowMain();
        return;
    }
    uiShowWarning(block != nullptr ? block : "Unable to start cycle.",
                  [](lv_event_t*) { uiShowMain(); },
                  "OK");
}

/* Recharge safety text (user wording 2026-11-24). The old "Disconnect both
   engine lines" prompt predates the two isolation valves. */
const char* uiRechargeSafetyText() {
    static char text[200];
    snprintf(text, sizeof(text),
             "Ensure the system is in a safe location.\n"
             "The chamber heats to %d F and the unit gets HOT.\n"
             "Water will be expelled from the discharge port.",
             RECHARGE_TARGET_TEMP_F);
    return text;
}

/* Shown before a dryer cycle starts with Auto Recharge on: that cycle may run
   a recharge on its own, unattended. */
const char* uiAutoRechargeSafetyText() {
    static char text[320];
    snprintf(text, sizeof(text),
             "Auto Recharge is ON.\n"
             "If the desiccant saturates, this cycle starts a\n"
             "Recharge by itself, unattended.\n"
             "Ensure the system is in a safe location.\n"
             "The chamber heats to %d F and the unit gets HOT.\n"
             "Water will be expelled from the discharge port.",
             RECHARGE_TARGET_TEMP_F);
    return text;
}