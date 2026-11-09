/* ========================================================================
   UI — Main Screen (AeroSentry)
   
   Shows title, date/time, mode, sensor readings, and mode buttons.
   When a cycle runs: mode buttons hide, red "Stop Cycle" button shows.
   ======================================================================== */

#include "ui.h"
#include "config.h"

static lv_obj_t* mainScreen = nullptr;
static lv_obj_t* titleLabel = nullptr;
static lv_obj_t* dateTimeBtn = nullptr;
static lv_obj_t* dateTimeLabel = nullptr;
static lv_obj_t* modeLabel = nullptr;
static lv_obj_t* statusLabel = nullptr;
static lv_obj_t* toEngineLabel = nullptr;
static lv_obj_t* fromEngineLabel = nullptr;
static lv_obj_t* outputLabel = nullptr;
static lv_obj_t* btnPurge = nullptr;
static lv_obj_t* btnDryer = nullptr;
static lv_obj_t* btnRecharge = nullptr;
static lv_obj_t* btnStop = nullptr;

static void cbPurge(lv_event_t* e);
static void cbDryer(lv_event_t* e);
static void cbRecharge(lv_event_t* e);
static void cbStop(lv_event_t* e);
static void cbDateTime(lv_event_t* e);
static void cbTitle(lv_event_t* e);

static lv_obj_t* makeButton(lv_obj_t* parent, int16_t x, int16_t y,
                            uint16_t w, uint16_t h,
                            uint32_t bgColor, const char* text,
                            lv_event_cb_t callback) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    return btn;
}

void uiShowMain() {
    if (mainScreen == nullptr) {
        mainScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(mainScreen, lv_color_hex(UI_BG_COLOR), 0);

        titleLabel = lv_label_create(mainScreen);
        lv_label_set_text(titleLabel, "AeroSentry");
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_36, 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_add_flag(titleLabel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(titleLabel, cbTitle, LV_EVENT_CLICKED, nullptr);

        dateTimeBtn = lv_btn_create(mainScreen);
        lv_obj_set_size(dateTimeBtn, 280, 50);
        lv_obj_align(dateTimeBtn, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(dateTimeBtn, lv_color_hex(0x1A2E4A), 0);
        lv_obj_set_style_radius(dateTimeBtn, 8, 0);
        lv_obj_add_event_cb(dateTimeBtn, cbDateTime, LV_EVENT_CLICKED, nullptr);
        dateTimeLabel = lv_label_create(dateTimeBtn);
        lv_label_set_text(dateTimeLabel, "--:--  --/--/----");
        lv_obj_center(dateTimeLabel);
        lv_obj_set_style_text_color(dateTimeLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(dateTimeLabel, &lv_font_montserrat_20, 0);

        modeLabel = lv_label_create(mainScreen);
        lv_label_set_text(modeLabel, "Mode: Idle");
        lv_obj_align(modeLabel, LV_ALIGN_TOP_MID, 0, 155);
        lv_obj_set_style_text_color(modeLabel, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_text_font(modeLabel, &lv_font_montserrat_28, 0);

        statusLabel = lv_label_create(mainScreen);
        lv_label_set_text(statusLabel, "Sensors offline");
        lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 198);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_20, 0);

        toEngineLabel = lv_label_create(mainScreen);
        lv_label_set_text(toEngineLabel, "To Engine: -- %");
        lv_obj_align(toEngineLabel, LV_ALIGN_TOP_LEFT, 40, 240);
        lv_obj_set_style_text_color(toEngineLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(toEngineLabel, &lv_font_montserrat_28, 0);

        fromEngineLabel = lv_label_create(mainScreen);
        lv_label_set_text(fromEngineLabel, "From Engine: -- %");
        lv_obj_align(fromEngineLabel, LV_ALIGN_TOP_RIGHT, -40, 240);
        lv_obj_set_style_text_color(fromEngineLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(fromEngineLabel, &lv_font_montserrat_28, 0);

        outputLabel = lv_label_create(mainScreen);
        lv_label_set_text(outputLabel, "K2 pump off   K3 heat off   K4 fan off   K5 AUX off");
        lv_obj_align(outputLabel, LV_ALIGN_TOP_MID, 0, 285);
        lv_obj_set_style_text_color(outputLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(outputLabel, &lv_font_montserrat_20, 0);

        int16_t btnW = 230, btnH = 100, gap = 20;
        int16_t startX = (DISPLAY_WIDTH - (btnW * 3 + gap * 2)) / 2;

        btnPurge = makeButton(mainScreen, startX, 320, btnW, btnH,
                              UI_ACCENT_COLOR, "Purge", cbPurge);
        btnDryer = makeButton(mainScreen, startX + btnW + gap, 320, btnW, btnH,
                              UI_ACCENT_COLOR, "Dryer", cbDryer);
        btnRecharge = makeButton(mainScreen, startX + (btnW + gap) * 2, 320, btnW, btnH,
                                 UI_ACCENT_COLOR, "Recharge", cbRecharge);

        btnStop = makeButton(mainScreen, startX, 320, btnW * 3 + gap * 2, btnH,
                             UI_WARN_COLOR, "STOP CYCLE", cbStop);
        lv_obj_add_flag(btnStop, LV_OBJ_FLAG_HIDDEN);
    }

    lv_scr_load(mainScreen);
    lv_obj_clear_flag(mainScreen, LV_OBJ_FLAG_HIDDEN);
    uiSetRunning(stateMachine.isRunning());
}

void uiSetRunning(bool running) {
    if (btnPurge == nullptr) return;
    if (running) {
        lv_obj_add_flag(btnPurge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btnDryer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btnRecharge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btnStop, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(btnPurge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btnDryer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btnRecharge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btnStop, LV_OBJ_FLAG_HIDDEN);
    }
}

void uiUpdateDateTime(const char *text) {
    if (dateTimeLabel != nullptr) {
        lv_label_set_text(dateTimeLabel, text);
    }
}

void uiUpdateStatus(const char* text) {
    if (statusLabel != nullptr && text != nullptr) {
        lv_label_set_text(statusLabel, text);
        lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 198);
    }
}

void uiUpdateOutputs(bool pumpOn, bool heaterOn, bool fanOn, float chipC) {
    if (outputLabel == nullptr) return;
    if (!uiDiagnosticsVisible()) {
        lv_obj_add_flag(outputLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(outputLabel, LV_OBJ_FLAG_HIDDEN);
    char buf[96];
    if (chipC == chipC) {
        snprintf(buf, sizeof(buf), "K2 pump %s   K3 heat %s   K4 fan %s   chip %.0fC",
                 pumpOn ? "ON" : "off",
                 heaterOn ? "ON" : "off",
                 fanOn ? "ON" : "off",
                 chipC);
    } else {
        snprintf(buf, sizeof(buf), "K2 pump %s   K3 heat %s   K4 fan %s   K5 AUX off",
                 pumpOn ? "ON" : "off",
                 heaterOn ? "ON" : "off",
                 fanOn ? "ON" : "off");
    }
    lv_label_set_text(outputLabel, buf);
}

void uiUpdateSensors(float toRH, float fromRH, bool toValid, bool fromValid) {
    char buf[40];
    if (toEngineLabel != nullptr) {
        if (toValid) snprintf(buf, sizeof(buf), "To Engine: %.0f %%", toRH);
        else snprintf(buf, sizeof(buf), "To Engine: -- %%");
        lv_label_set_text(toEngineLabel, buf);
    }
    if (fromEngineLabel != nullptr) {
        if (fromValid) snprintf(buf, sizeof(buf), "From Engine: %.0f %%", fromRH);
        else snprintf(buf, sizeof(buf), "From Engine: -- %%");
        lv_label_set_text(fromEngineLabel, buf);
    }
}

static void cbTitle(lv_event_t* e) {
    (void)e;
    static uint8_t taps = 0;
    static uint32_t lastTap = 0;
    const uint32_t now = millis();
    if (now - lastTap > 3000UL) taps = 0;
    lastTap = now;
    if (++taps >= 10) {
        taps = 0;
        uiShowAdmin();
    }
}

static void promptPurge() {
    uiShowWarning("Connect To Engine Line.\nLeave From Engine Line Disconnected.",
                  [](lv_event_t*) {
                      uiHandleStartResult(Command::START_PURGE);
                  });
}

static void promptDryer() {
    uiShowWarning("Connect both To Engine and From Engine Lines.",
                  [](lv_event_t*) { uiShowDryerSetup(); });
}

static void cbPurge(lv_event_t* e) {
    (void)e;
    promptPurge();
}

static void cbDryer(lv_event_t* e) {
    (void)e;
    if (stateMachine.purgeRecent()) {
        promptDryer();
        return;
    }
    uiShowChoice("No purge cycle detected.\nRecommend Purge Cycle.",
                 [](lv_event_t*) { promptPurge(); }, "Purge",
                 [](lv_event_t*) { promptDryer(); }, "Continue to Dryer");
}

static void cbRecharge(lv_event_t* e) {
    (void)e;
    uiShowWarning("Disconnect both engine lines.\nClear area around desiccant tube.\nHeaters will be HOT.",
                  [](lv_event_t*) {
                      uiHandleStartResult(Command::START_RECHARGE);
                  });
}

static void cbStop(lv_event_t* e) {
    (void)e;
    stateMachine.command(Command::STOP);
    uiShowMain();
}

static void cbDateTime(lv_event_t* e) {
    (void)e;
    uiShowClock();
}

void uiUpdateMode(SystemMode mode) {
    if (modeLabel == nullptr) return;
    const char* text;
    switch (mode) {
        case SystemMode::IDLE:        text = "Mode: Idle"; break;
        case SystemMode::PURGE:       text = "Mode: Purge"; break;
        case SystemMode::DRYER_MANUAL:text = "Mode: Dryer (Manual)"; break;
        case SystemMode::DRYER_AUTO:  text = "Mode: Dryer (Auto)"; break;
        case SystemMode::RECHARGE:    text = "Mode: Recharge"; break;
        default:                      text = "Mode: ?"; break;
    }
    lv_label_set_text(modeLabel, text);
}