/* ========================================================================
   UI — Warning / Confirmation Screen
   
   Shown before starting any mode and for errors.
   Large text + green "Continue" button.
   ======================================================================== */

#include "ui.h"

static lv_obj_t* warningScreen = nullptr;
static lv_obj_t* warningMsgLabel = nullptr;
static lv_obj_t* warningContinueBtn = nullptr;
static lv_obj_t* warningContinueLabel = nullptr;
static lv_obj_t* warningAltBtn = nullptr;
static lv_obj_t* warningAltLabel = nullptr;
static lv_event_cb_t warningContinueCallback = nullptr;
static lv_event_cb_t warningAltCallback = nullptr;

static void cbContinue(lv_event_t* e) {
    lv_event_cb_t callback = warningContinueCallback;
    warningContinueCallback = nullptr;
    warningAltCallback = nullptr;
    if (callback != nullptr) callback(e);
    else uiShowMain();
}

static void cbAlternate(lv_event_t* e) {
    lv_event_cb_t callback = warningAltCallback;
    warningContinueCallback = nullptr;
    warningAltCallback = nullptr;
    if (callback != nullptr) callback(e);
    else uiShowMain();
}

static void layoutWarningButtons(bool twoButtons) {
    if (twoButtons) {
        lv_obj_set_size(warningContinueBtn, 220, 70);
        lv_obj_align(warningContinueBtn, LV_ALIGN_CENTER, -160, 90);
        lv_obj_clear_flag(warningAltBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(warningAltBtn, LV_ALIGN_CENTER, 170, 90);
    } else {
        lv_obj_set_size(warningContinueBtn, 280, 70);
        lv_obj_align(warningContinueBtn, LV_ALIGN_CENTER, 0, 80);
        lv_obj_add_flag(warningAltBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

void uiShowWarning(const char* message, lv_event_cb_t onContinue, const char* buttonText) {
    if (warningScreen == nullptr) {
        warningScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(warningScreen, lv_color_hex(UI_BG_COLOR), 0);

        warningMsgLabel = lv_label_create(warningScreen);
        lv_obj_set_size(warningMsgLabel, 650, 240);
        lv_obj_align(warningMsgLabel, LV_ALIGN_CENTER, 0, -40);
        lv_obj_set_style_text_align(warningMsgLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(warningMsgLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(warningMsgLabel, &lv_font_montserrat_28, 0);

        warningContinueBtn = lv_btn_create(warningScreen);
        lv_obj_set_size(warningContinueBtn, 240, 70);
        lv_obj_align(warningContinueBtn, LV_ALIGN_CENTER, 0, 60);
        lv_obj_set_style_bg_color(warningContinueBtn, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_radius(warningContinueBtn, 10, 0);
        lv_obj_add_event_cb(warningContinueBtn, cbContinue, LV_EVENT_CLICKED, nullptr);

        warningContinueLabel = lv_label_create(warningContinueBtn);
        lv_label_set_text(warningContinueLabel, "Continue");
        lv_obj_center(warningContinueLabel);
        lv_obj_set_style_text_color(warningContinueLabel, lv_color_hex(0x0A1628), 0);
        lv_obj_set_style_text_font(warningContinueLabel, &lv_font_montserrat_20, 0);

        warningAltBtn = lv_btn_create(warningScreen);
        lv_obj_set_size(warningAltBtn, 300, 70);
        lv_obj_align(warningAltBtn, LV_ALIGN_CENTER, 170, 90);
        lv_obj_set_style_bg_color(warningAltBtn, lv_color_hex(0x1A2E4A), 0);
        lv_obj_set_style_radius(warningAltBtn, 10, 0);
        lv_obj_add_event_cb(warningAltBtn, cbAlternate, LV_EVENT_CLICKED, nullptr);
        warningAltLabel = lv_label_create(warningAltBtn);
        lv_label_set_text(warningAltLabel, "Continue");
        lv_obj_center(warningAltLabel);
        lv_obj_set_style_text_color(warningAltLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(warningAltLabel, &lv_font_montserrat_20, 0);
        lv_obj_add_flag(warningAltBtn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(warningMsgLabel, message);
    lv_label_set_text(warningContinueLabel, buttonText != nullptr ? buttonText : "Continue");
    lv_obj_center(warningContinueLabel);
    warningContinueCallback = onContinue;
    warningAltCallback = nullptr;
    layoutWarningButtons(false);

    lv_scr_load(warningScreen);
    lv_obj_clear_flag(warningScreen, LV_OBJ_FLAG_HIDDEN);
}

void uiShowChoice(const char* message,
                  lv_event_cb_t onLeft, const char* leftText,
                  lv_event_cb_t onRight, const char* rightText) {
    uiShowWarning(message, onLeft, leftText);
    lv_label_set_text(warningAltLabel, rightText != nullptr ? rightText : "Continue");
    lv_obj_center(warningAltLabel);
    warningAltCallback = onRight;
    layoutWarningButtons(true);
}