/* Admin diagnostics. Opened by tapping the AeroSentry title ten times. */

#include "ui.h"
#include "relay.h"

static lv_obj_t* adminScreen = nullptr;
static lv_obj_t* adminBody = nullptr;
static lv_obj_t* adminDiagBtnLabel = nullptr;
static bool adminVisible = false;
static bool diagnosticsVisible = true;

static void cbAdminBack(lv_event_t* e) {
    (void)e;
    adminVisible = false;
    uiShowMain();
}

static void cbToggleDiag(lv_event_t* e) {
    (void)e;
    diagnosticsVisible = !diagnosticsVisible;
    lv_label_set_text(adminDiagBtnLabel,
                      diagnosticsVisible ? "Hide main diag line" : "Show main diag line");
}

void uiSetDiagnosticsVisible(bool visible) { diagnosticsVisible = visible; }
bool uiDiagnosticsVisible() { return diagnosticsVisible; }
bool uiAdminVisible() { return adminVisible; }

void uiShowAdmin() {
    if (adminScreen == nullptr) {
        adminScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(adminScreen, lv_color_hex(UI_BG_COLOR), 0);

        lv_obj_t* title = lv_label_create(adminScreen);
        lv_label_set_text(title, "Admin");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

        adminBody = lv_label_create(adminScreen);
        lv_obj_set_width(adminBody, 740);
        lv_obj_align(adminBody, LV_ALIGN_TOP_LEFT, 30, 64);
        lv_label_set_long_mode(adminBody, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(adminBody, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(adminBody, &lv_font_montserrat_20, 0);

        lv_obj_t* diagBtn = lv_btn_create(adminScreen);
        lv_obj_set_size(diagBtn, 280, 60);
        lv_obj_align(diagBtn, LV_ALIGN_BOTTOM_LEFT, 40, -24);
        lv_obj_set_style_bg_color(diagBtn, lv_color_hex(0x1A2E4A), 0);
        lv_obj_add_event_cb(diagBtn, cbToggleDiag, LV_EVENT_CLICKED, nullptr);
        adminDiagBtnLabel = lv_label_create(diagBtn);
        lv_label_set_text(adminDiagBtnLabel, "Hide main diag line");
        lv_obj_center(adminDiagBtnLabel);
        lv_obj_set_style_text_font(adminDiagBtnLabel, &lv_font_montserrat_20, 0);

        lv_obj_t* back = lv_btn_create(adminScreen);
        lv_obj_set_size(back, 180, 60);
        lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -40, -24);
        lv_obj_set_style_bg_color(back, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_add_event_cb(back, cbAdminBack, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* backLabel = lv_label_create(back);
        lv_label_set_text(backLabel, "Back");
        lv_obj_center(backLabel);
        lv_obj_set_style_text_color(backLabel, lv_color_hex(0x0A1628), 0);
        lv_obj_set_style_text_font(backLabel, &lv_font_montserrat_20, 0);
    }

    adminVisible = true;
    uiUpdateAdmin();
    lv_scr_load(adminScreen);
}

void uiUpdateAdmin() {
    if (!adminVisible || adminBody == nullptr) return;

    auto reading = [](char* buf, size_t size, bool valid, float value, const char* unit) {
        if (valid) snprintf(buf, size, "%.0f%s", value, unit);
        else snprintf(buf, size, "--");
    };
    char toRh[16], toF[16], fromRh[16], fromF[16], chamber[16];
    reading(toRh, sizeof(toRh), toEngineReady(), sensors.toEngineRH, "%");
    reading(toF, sizeof(toF), toEngineReady(),
            sensors.toEngineTemp * 9.0f / 5.0f + 32.0f, "F");
    reading(fromRh, sizeof(fromRh), fromEngineReady(), sensors.fromEngineRH, "%");
    reading(fromF, sizeof(fromF), fromEngineReady(),
            sensors.fromEngineTemp * 9.0f / 5.0f + 32.0f, "F");
    reading(chamber, sizeof(chamber), chamberReady(),
            sensors.chamberTemp * 9.0f / 5.0f + 32.0f, "F");

    uint8_t port0 = 0, port1 = 0;
    const bool relaysLive = relay.isReady() && relay.readPair(0x02, port0, port1);
    char relays[96] = "offline";
    if (relaysLive) {
        snprintf(relays, sizeof(relays),
                 "K1 %s  K2 %s  K3 %s  K4 %s  K5 %s  K6 %s  K7 %s  K8 %s",
                 (port0 & 0x01) ? "ON" : "off",
                 (port0 & 0x02) ? "ON" : "off",
                 (port0 & 0x04) ? "ON" : "off",
                 (port0 & 0x08) ? "ON" : "off",
                 (port0 & 0x10) ? "ON" : "off",
                 (port0 & 0x20) ? "ON" : "off",
                 (port0 & 0x40) ? "ON" : "off",
                 (port0 & 0x80) ? "ON" : "off");
    }

    char purge[40] = "none since power-up";
    if (stateMachine.hasPurgeRecord()) {
        const uint32_t minutes = stateMachine.purgeAgeMs() / 60000UL;
        snprintf(purge, sizeof(purge), "%lu min ago", (unsigned long)minutes);
    }

    char chipText[16] = "--";
    const float chip = stateMachine.chipTempC();
    if (chip == chip) snprintf(chipText, sizeof(chipText), "%.0fC", chip);

    char body[480];
    snprintf(body, sizeof(body),
             "To-Engine   %s   %s\n"
             "From-Engine %s   %s\n"
             "Chamber     %s\n"
             "Chip        %s\n"
             "Relays      %s\n"
             "Last purge  %s\n"
             "K1 unused. K2 pump, K3 heater, K4 fan, K5 AUX.",
             toRh, toF, fromRh, fromF, chamber, chipText, relays, purge);
    lv_label_set_text(adminBody, body);
}
