/* Clock/date adjustment screen for the software clock. */
#include "ui.h"
#include "system_clock.h"

static lv_obj_t* clockScreen = nullptr;
static lv_obj_t* valueLabels[5] = {};
static const ClockField fields[5] = {
    ClockField::HOUR, ClockField::MINUTE, ClockField::YEAR,
    ClockField::MONTH, ClockField::DAY
};
static const char* fieldNames[5] = {"Hour", "Minute", "Year", "Month", "Day"};

static void refreshClockValues() {
    for (int i = 0; i < 5; ++i) {
        char text[8];
        if (fields[i] == ClockField::YEAR) {
            snprintf(text, sizeof(text), "%04d", systemClockGet(fields[i]));
        } else {
            snprintf(text, sizeof(text), "%02d", systemClockGet(fields[i]));
        }
        lv_label_set_text(valueLabels[i], text);
    }
}

static void cbAdjust(lv_event_t* e) {
    const uintptr_t code = (uintptr_t)lv_event_get_user_data(e);
    const int fieldIndex = (int)(code / 2);
    const int delta = (code % 2 == 0) ? -1 : 1;
    systemClockAdjust(fields[fieldIndex], delta);
    refreshClockValues();
}

static void cbDone(lv_event_t* e) {
    (void)e;
    uiShowMain();
}

static lv_obj_t* makeAdjustButton(lv_obj_t* parent, int16_t x, int16_t y,
                                  const char* text, uintptr_t code) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, 70, 50);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1A2E4A), 0);
    lv_obj_add_event_cb(button, cbAdjust, LV_EVENT_CLICKED, (void*)code);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    return button;
}

static void makeClockRow(lv_obj_t* parent, int index) {
    const int16_t y = 65 + index * 60;
    lv_obj_t* name = lv_label_create(parent);
    lv_label_set_text(name, fieldNames[index]);
    lv_obj_set_pos(name, 50, y + 10);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_28, 0);

    valueLabels[index] = lv_label_create(parent);
    lv_obj_align(valueLabels[index], LV_ALIGN_TOP_MID, 0, y + 10);
    lv_obj_set_style_text_color(valueLabels[index], lv_color_hex(UI_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(valueLabels[index], &lv_font_montserrat_28, 0);

    makeAdjustButton(parent, 560, y, "-", index * 2);
    makeAdjustButton(parent, 645, y, "+", index * 2 + 1);
}

void uiShowClock() {
    if (clockScreen == nullptr) {
        clockScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(clockScreen, lv_color_hex(UI_BG_COLOR), 0);

        lv_obj_t* title = lv_label_create(clockScreen);
        lv_label_set_text(title, "Set Clock");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);

        for (int i = 0; i < 5; ++i) makeClockRow(clockScreen, i);

        lv_obj_t* btnDone = lv_btn_create(clockScreen);
        lv_obj_set_size(btnDone, 180, 60);
        lv_obj_align(btnDone, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_bg_color(btnDone, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_radius(btnDone, 8, 0);
        lv_obj_add_event_cb(btnDone, cbDone, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lblDone = lv_label_create(btnDone);
        lv_label_set_text(lblDone, "Done");
        lv_obj_center(lblDone);
        lv_obj_set_style_text_font(lblDone, &lv_font_montserrat_28, 0);
    }

    refreshClockValues();
    lv_scr_load(clockScreen);
    lv_obj_clear_flag(clockScreen, LV_OBJ_FLAG_HIDDEN);
}