/* Boot splash and idle screen using the AeroSentry mark. */

#include "ui.h"
#include "display.h"
#include "logo_img.h"

static lv_obj_t* logoScreen = nullptr;
static bool logoVisible = false;
static bool bootSplash = true;
static uint32_t splashShownAt = 0;

static void dismissLogo(lv_event_t* e) {
    (void)e;
    bootSplash = false;
    logoVisible = false;
    uiShowMain();
}

void uiShowLogo() {
    if (logoScreen == nullptr) {
        logoScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(logoScreen, lv_color_hex(0x050B16), 0);
        lv_obj_add_event_cb(logoScreen, dismissLogo, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_flag(logoScreen, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* image = lv_img_create(logoScreen);
        lv_img_set_src(image, &aerosentry_logo);
        lv_obj_center(image);
        lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(image, dismissLogo, LV_EVENT_CLICKED, nullptr);
    }
    logoVisible = true;
    splashShownAt = millis();
    lv_scr_load(logoScreen);
}

bool uiLogoVisible() {
    return logoVisible;
}

void uiLogoTick() {
    if (logoVisible) {
        if (bootSplash && millis() - splashShownAt >= 3500UL) {
            dismissLogo(nullptr);
        }
        return;
    }
    if (stateMachine.isRunning()) return;
    if (millis() < 20000UL) return;
    const uint32_t touched = displayLastTouchMs();
    const uint32_t idleFor = millis() - (touched == 0 ? 0 : touched);
    if (idleFor >= 180000UL) uiShowLogo();
}
