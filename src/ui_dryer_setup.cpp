/* ========================================================================
   UI — Dryer Setup Screen
   On-time +/- (1–5 min), Off-time +/- (1–3 hr)
   "Auto Cycle", "Start", "Back"
   ======================================================================== */

#include "ui.h"

// Forward declaration
void uiShowAutoCycle();

static lv_obj_t* setupScreen = nullptr;
static lv_obj_t* onTimeLabel = nullptr;
static lv_obj_t* offTimeLabel = nullptr;
static uint8_t onTimeValue = 3;
static uint16_t offTimeValue = 1;

static void cbPlusOn(lv_event_t* e) {
    (void)e;
    if (onTimeValue < 5) {
        onTimeValue++;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d min", onTimeValue);
        lv_label_set_text(onTimeLabel, buf);
        stateMachine.config.dryerOnMinutes = onTimeValue;
    }
}

static void cbMinusOn(lv_event_t* e) {
    (void)e;
    if (onTimeValue > 1) {
        onTimeValue--;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d min", onTimeValue);
        lv_label_set_text(onTimeLabel, buf);
        stateMachine.config.dryerOnMinutes = onTimeValue;
    }
}

static void cbPlusOff(lv_event_t* e) {
    (void)e;
    if (offTimeValue < 3) {
        offTimeValue++;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d hr", offTimeValue);
        lv_label_set_text(offTimeLabel, buf);
        stateMachine.config.dryerOffHours = offTimeValue;
    }
}

static void cbMinusOff(lv_event_t* e) {
    (void)e;
    if (offTimeValue > 1) {
        offTimeValue--;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d hr", offTimeValue);
        lv_label_set_text(offTimeLabel, buf);
        stateMachine.config.dryerOffHours = offTimeValue;
    }
}

static void cbAutoCycle(lv_event_t* e) {
    (void)e;
    uiShowAutoCycle();
}

static void cbStartDryer(lv_event_t* e) {
    (void)e;
    uiHandleStartResult(Command::START_DRYER_MANUAL);
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

void uiShowDryerSetup() {
    if (setupScreen == nullptr) {
        setupScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(setupScreen, lv_color_hex(UI_BG_COLOR), 0);

        lv_obj_t* title = lv_label_create(setupScreen);
        lv_label_set_text(title, "Manual Dryer Setup");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);

        lv_obj_t* hint = lv_label_create(setupScreen);
        lv_label_set_text(hint, "Repeats until Stop or 40% To-Engine");
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);

        lv_obj_t* lblOnTitle = lv_label_create(setupScreen);
        lv_label_set_text(lblOnTitle, "On Time:");
        lv_obj_align(lblOnTitle, LV_ALIGN_TOP_LEFT, 50, 120);
        lv_obj_set_style_text_color(lblOnTitle, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(lblOnTitle, &lv_font_montserrat_28, 0);

        onTimeLabel = lv_label_create(setupScreen);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d min", onTimeValue);
        lv_label_set_text(onTimeLabel, buf);
        lv_obj_align(onTimeLabel, LV_ALIGN_TOP_MID, 0, 120);
        lv_obj_set_style_text_color(onTimeLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(onTimeLabel, &lv_font_montserrat_28, 0);
        makePlusMinus(setupScreen, 560, 105, cbPlusOn, cbMinusOn);

        lv_obj_t* lblOffTitle = lv_label_create(setupScreen);
        lv_label_set_text(lblOffTitle, "Off Time:");
        lv_obj_align(lblOffTitle, LV_ALIGN_TOP_LEFT, 50, 235);
        lv_obj_set_style_text_color(lblOffTitle, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(lblOffTitle, &lv_font_montserrat_28, 0);

        offTimeLabel = lv_label_create(setupScreen);
        snprintf(buf, sizeof(buf), "%d hr", offTimeValue);
        lv_label_set_text(offTimeLabel, buf);
        lv_obj_align(offTimeLabel, LV_ALIGN_TOP_MID, 0, 235);
        lv_obj_set_style_text_color(offTimeLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(offTimeLabel, &lv_font_montserrat_28, 0);
        makePlusMinus(setupScreen, 560, 220, cbPlusOff, cbMinusOff);

        int16_t btnW = 220, btnH = 60, gap = 20;
        int16_t bottomStartX = (DISPLAY_WIDTH - (btnW * 2 + gap)) / 2;

        lv_obj_t* btnAuto = lv_btn_create(setupScreen);
        lv_obj_set_size(btnAuto, btnW, btnH);
        lv_obj_set_pos(btnAuto, (DISPLAY_WIDTH - btnW) / 2, 300);
        lv_obj_set_style_bg_color(btnAuto, lv_color_hex(0x3399FF), 0);
        lv_obj_set_style_radius(btnAuto, 8, 0);
        lv_obj_add_event_cb(btnAuto, cbAutoCycle, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lblAuto = lv_label_create(btnAuto);
        lv_label_set_text(lblAuto, "Auto Cycle");
        lv_obj_center(lblAuto);
        lv_obj_set_style_text_color(lblAuto, lv_color_black(), 0);
        lv_obj_set_style_text_font(lblAuto, &lv_font_montserrat_28, 0);

        lv_obj_t* btnStart = lv_btn_create(setupScreen);
        lv_obj_set_size(btnStart, btnW, btnH);
        lv_obj_set_pos(btnStart, bottomStartX + btnW + gap, 390);
        lv_obj_set_style_bg_color(btnStart, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_radius(btnStart, 8, 0);
        lv_obj_add_event_cb(btnStart, cbStartDryer, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lblStart = lv_label_create(btnStart);
        lv_label_set_text(lblStart, "Start");
        lv_obj_center(lblStart);
        lv_obj_set_style_text_font(lblStart, &lv_font_montserrat_28, 0);

        lv_obj_t* btnBack = lv_btn_create(setupScreen);
        lv_obj_set_size(btnBack, btnW, btnH);
        lv_obj_set_pos(btnBack, bottomStartX, 390);
        lv_obj_set_style_bg_color(btnBack, lv_color_hex(0x1A2E4A), 0);
        lv_obj_set_style_radius(btnBack, 8, 0);
        lv_obj_add_event_cb(btnBack, cbBack, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lblBack = lv_label_create(btnBack);
        lv_label_set_text(lblBack, "Back");
        lv_obj_center(lblBack);
        lv_obj_set_style_text_font(lblBack, &lv_font_montserrat_28, 0);
    }

    lv_scr_load(setupScreen);
    lv_obj_clear_flag(setupScreen, LV_OBJ_FLAG_HIDDEN);
}