/* ========================================================================
   UI — Controller (ties all screens together)
   
   uiBegin() and uiLoop() are implemented here.
   Individual screen functions live in ui_main.cpp, ui_warning.cpp, etc.
   ======================================================================== */

#include "ui.h"
#include "lvgl_v8_port.h"

void uiBegin() {
    uiShowMain();  // Defined in ui_main.cpp
}

void uiLoop() {
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