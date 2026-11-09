#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "statemachine.h"

/* ========================================================================
   USER INTERFACE — LVGL screens for Waveshare 4.3" touch
   Screens: Main, Warning, Dryer Setup, Auto Cycle, Clock
   ======================================================================== */

/* Initialize LVGL and show the Main screen. Call once in setup(). */
void uiBegin();

/* Call from loop() to keep LVGL running (lv_timer_handler). */
void uiLoop();

/* Show a warning or confirmation.
   buttonText defaults to "Continue"; use "OK" for faults. */
void uiShowWarning(const char* message, lv_event_cb_t onContinue,
                   const char* buttonText = "Continue");

/* Return to the Main screen. */
void uiShowMain();
void uiShowDryerSetup();
void uiShowAutoCycle();
void uiShowClock();

/* Start a cycle, or show why the required sensor is unavailable. */
void uiHandleStartResult(Command cmd);

/* Update the main-screen software clock text. */
void uiUpdateDateTime(const char *text);

/* Update sensor values shown on the Main screen. Invalid readings display "--". */
void uiUpdateSensors(float toRH, float fromRH, bool toValid, bool fromValid);

/* Update the phase/status line on the Main screen. */
void uiUpdateStatus(const char* text);

/* Update the mode label on the Main screen. */
void uiUpdateMode(SystemMode mode);

/* Show/hide Stop Cycle button based on whether a cycle is running. */
void uiSetRunning(bool running);

#endif // UI_H