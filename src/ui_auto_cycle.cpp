/* ========================================================================
   UI — Auto Cycle Screen
   
   Target % +/- (15–30)
   "Start Auto", "Back"
   ======================================================================== */

#include "ui.h"

static lv_obj_t* autoScreen = nullptr;
static lv_obj_t* targetLabel = nullptr;
static uint8_t targetValue = 20;  // % RH

static void cbPlusTarget(lv_event_t* e) {
    (void)e;
    if (targetValue < 30) {
        targetValue++;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", targetValue);
        lv_label_set_text(targetLabel, buf);
        stateMachine.config.dryerTargetRH = targetValue;
    }
}

static void cbMinusTarget(lv_event_t* e) {
    (void)e;
    if (targetValue > 15) {
        targetValue--;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", targetValue);
        lv_label_set_text(targetLabel, buf);
        stateMachine.config.dryerTargetRH = targetValue;
    }
}

static void cbStartAuto(lv_event_t* e) {
    (void)e;
    uiHandleStartResult(Command::START_DRYER_AUTO);
}

static void cbBack(lv_event_t* e) {
    (void)e;
    uiShowMain();
}

static void makePlusMinus(lv_obj_t* parent, int16_t x, int16_t y,
                          lv_event_cb_t cbPlus, lv_event_cb_t cbMinus) {
    lv_obj_t* btnPlus = lv_btn_create(parent);
    lv_obj_set_size(btnPlus, 70, 60);
    lv_obj_set_pos(btnPlus, x + 85, y);
    lv_obj_set_style_bg_color(btnPlus, lv_color_hex(0x1A2E4A), 0);
    lv_obj_add_event_cb(btnPlus, cbPlus, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lblPlus = lv_label_create(btnPlus);
    lv_label_set_text(lblPlus, "+");
    lv_obj_center(lblPlus);
    lv_obj_set_style_text_font(lblPlus, &lv_font_montserrat_28, 0);

    lv_obj_t* btnMinus = lv_btn_create(parent);
    lv_obj_set_size(btnMinus, 70, 60);
    lv_obj_set_pos(btnMinus, x, y);
    lv_obj_set_style_bg_color(btnMinus, lv_color_hex(0x1A2E4A), 0);
    lv_obj_add_event_cb(btnMinus, cbMinus, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lblMinus = lv_label_create(btnMinus);
    lv_label_set_text(lblMinus, "-");
    lv_obj_center(lblMinus);
    lv_obj_set_style_text_font(lblMinus, &lv_font_montserrat_28, 0);
}

void uiShowAutoCycle() {
    if (autoScreen == nullptr) {
        autoScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(autoScreen, lv_color_hex(UI_BG_COLOR), 0);

        lv_obj_t* title = lv_label_create(autoScreen);
        lv_label_set_text(title, "Auto Cycle");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);

        lv_obj_t* hint = lv_label_create(autoScreen);
        lv_label_set_text(hint, "Dries From-Engine, then samples hourly.\nTo-Engine still stops the cycle at 40%.");
        lv_obj_set_width(hint, 700);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 80);
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);

        lv_obj_t* lblTargetTitle = lv_label_create(autoScreen);
        lv_label_set_text(lblTargetTitle, "Target RH:");
        lv_obj_align(lblTargetTitle, LV_ALIGN_TOP_LEFT, 50, 160);
        lv_obj_set_style_text_color(lblTargetTitle, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(lblTargetTitle, &lv_font_montserrat_28, 0);

        targetLabel = lv_label_create(autoScreen);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", targetValue);
        lv_label_set_text(targetLabel, buf);
        lv_obj_align(targetLabel, LV_ALIGN_TOP_MID, 0, 160);
        lv_obj_set_style_text_color(targetLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(targetLabel, &lv_font_montserrat_28, 0);
        makePlusMinus(autoScreen, 560, 145, cbPlusTarget, cbMinusTarget);

        int16_t btnW = 300, btnH = 70, gap = 30;
        int16_t startX = (DISPLAY_WIDTH - (btnW * 2 + gap)) / 2;

        lv_obj_t* btnStart = lv_btn_create(autoScreen);
        lv_obj_set_size(btnStart, btnW, btnH);
        lv_obj_set_pos(btnStart, startX + btnW + gap, 350);
        lv_obj_set_style_bg_color(btnStart, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_radius(btnStart, 8, 0);
        lv_obj_add_event_cb(btnStart, cbStartAuto, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lblStart = lv_label_create(btnStart);
        lv_label_set_text(lblStart, "Start Auto");
        lv_obj_center(lblStart);
        lv_obj_set_style_text_font(lblStart, &lv_font_montserrat_28, 0);

        lv_obj_t* btnBack = lv_btn_create(autoScreen);
        lv_obj_set_size(btnBack, btnW, btnH);
        lv_obj_set_pos(btnBack, startX, 350);
        lv_obj_set_style_bg_color(btnBack, lv_color_hex(0x1A2E4A), 0);
        lv_obj_set_style_radius(btnBack, 8, 0);
        lv_obj_add_event_cb(btnBack, cbBack, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lblBack = lv_label_create(btnBack);
        lv_label_set_text(lblBack, "Back");
        lv_obj_center(lblBack);
        lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_28, 0);
    }

    lv_scr_load(autoScreen);
    lv_obj_clear_flag(autoScreen, LV_OBJ_FLAG_HIDDEN);
}