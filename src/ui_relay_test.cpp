/* Relay test (Admin > Relay test). Bench tool for the K1 investigation.

   Every XL9535 pin on both ports gets a button. A tap drives that one pin
   high for RELAY_TEST_PULSE_MS, then the outputs return to normal (all off
   while idle). Watch and listen: the click and the relay LED are the result.

   Register read-back is shown but NOT trusted: on this bus the CH422G also
   answers at the XL9535 address and its bits are ANDed into every read
   (proven by the polarity-register readback test, 2026-11-24). Writes are
   clean, so the commanded line is what the pins are told to do.
     P0.0 clicks K1             -> K1 works; "dead K1" was the read-back fault
     P0.0 driven, no click      -> relay board fault, or K1 on another pin
     another pin clicks K1      -> K1 is mapped to that pin
   Only while idle: cycle starts are refused during a pulse. K3 is the mains
   heater and K7/K8 are mains outputs; a 2 s pulse is a bench check only. */

#include "ui.h"
#include "relay.h"

static lv_obj_t* testScreen = nullptr;
static lv_obj_t* resultLabel = nullptr;
static lv_obj_t* busLabel = nullptr;
static bool testVisible = false;
static char lastPin[8] = "";

static void cbPulse(lv_event_t* e) {
    const uint32_t code = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    const uint8_t port = (uint8_t)(code >> 4);
    const uint8_t bit = (uint8_t)(code & 0x0F);
    if (stateMachine.isRunning()) {
        lv_label_set_text(resultLabel, "A cycle is running. Stop it before testing relays.");
        return;
    }
    if (!relay.isReady()) {
        lv_label_set_text(resultLabel, "Relay board not answering at 0x21.");
        return;
    }
    if (!relay.testPulseStart(port, bit, RELAY_TEST_PULSE_MS)) {
        lv_label_set_text(resultLabel, "A test pulse is already running.");
        return;
    }
    snprintf(lastPin, sizeof(lastPin), "P%u.%u", port, bit);
    uiUpdateRelayTest();
}

static void cbBack(lv_event_t* e) {
    (void)e;
    testVisible = false;
    uiShowAdmin();
}

#if LOOP_WATCHDOG_ENABLED
/* Freeze loop() on purpose. The task watchdog should reset the chip within
   about 5 s, setup() turns every relay off, and the boot screen shows S18.
   Run it during a Purge to see the pump relay drop after a hang. */
static void cbWatchdogFreeze(lv_event_t* e) {
    (void)e;
    Serial.println(F("Watchdog test: loop() frozen on purpose"));
    Serial.flush();
    while (true) delay(100);  // loop() never returns, so the watchdog is never fed
}

static void cbWatchdogTest(lv_event_t* e) {
    (void)e;
    uiShowChoice("Watchdog test.\n"
                 "The controller will freeze on purpose. The watchdog should\n"
                 "restart it within about 5 s, every relay should switch off,\n"
                 "and the boot screen should show code S18.\n"
                 "Run it during a Purge to check that the pump switches off.",
                 cbWatchdogFreeze, "Freeze now",
                 [](lv_event_t*) { uiShowRelayTest(); }, "Back");
}
#endif

static void cbRescan(lv_event_t* e) {
    (void)e;
    char found[160];
    RelayDriver::scanBus(found, sizeof(found));
    char readback[120] = "relay board not answering";
    bool clean = false;
    if (relay.isReady() && !relay.testActive()) clean = relay.readbackTest(readback, sizeof(readback));
    char text[360];
    snprintf(text, sizeof(text),
             "I2C bus: %s\n"
             "Relay readback (polarity register): %s -> %s",
             found, readback,
             clean ? "clean" : "MISMATCH: another device answers at the relay address, readings above may be wrong");
    lv_label_set_text(busLabel, text);
}

static lv_obj_t* makeButton(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h,
                            const char* text, lv_event_cb_t cb, uint32_t code, uint32_t color) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void*)(uintptr_t)code);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return btn;
}

void uiShowRelayTest() {
    if (testScreen == nullptr) {
        testScreen = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(testScreen, lv_color_hex(UI_BG_COLOR), 0);

        lv_obj_t* title = lv_label_create(testScreen);
        lv_label_set_text(title, "Relay test - tap a pin, watch the relay");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_ACCENT_COLOR), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

        // Port 0: the relay map (K1 = P0.0 ... K8 = P0.7). Port 1: unused pins.
        static const char* port0Names[8] = {
            "P0.0\nK1 aux", "P0.1\nK2 pump", "P0.2\nK3 HEAT", "P0.3\nK4 fan",
            "P0.4\nK5 valve", "P0.5\nK6 valve", "P0.6\nK7 AC", "P0.7\nK8 AC"};
        static const char* port1Names[8] = {
            "P1.0", "P1.1", "P1.2", "P1.3", "P1.4", "P1.5", "P1.6", "P1.7"};
        const int16_t w = 90, h = 56, gap = 6;
        const int16_t x0 = (DISPLAY_WIDTH - (w * 8 + gap * 7)) / 2;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            makeButton(testScreen, x0 + bit * (w + gap), 50, w, h, port0Names[bit], cbPulse,
                       (0u << 4) | bit, bit == 0 ? UI_WARN_COLOR : UI_PANEL_COLOR);
            makeButton(testScreen, x0 + bit * (w + gap), 50 + h + gap, w, h, port1Names[bit], cbPulse,
                       (1u << 4) | bit, UI_PANEL_COLOR);
        }

        resultLabel = lv_label_create(testScreen);
        lv_obj_set_width(resultLabel, 760);
        lv_label_set_long_mode(resultLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(resultLabel, 20, 185);
        lv_obj_set_style_text_color(resultLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(resultLabel, &lv_font_montserrat_14, 0);

        busLabel = lv_label_create(testScreen);
        lv_obj_set_width(busLabel, 760);
        lv_label_set_long_mode(busLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(busLabel, 20, 330);
        lv_obj_set_style_text_color(busLabel, lv_color_hex(UI_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(busLabel, &lv_font_montserrat_14, 0);

        makeButton(testScreen, 40, 400, 200, 60, "Scan I2C bus", cbRescan, 0, UI_PANEL_COLOR);
#if LOOP_WATCHDOG_ENABLED
        makeButton(testScreen, (DISPLAY_WIDTH - 200) / 2, 400, 200, 60, "Watchdog test", cbWatchdogTest, 0,
                   UI_WARN_COLOR);
#endif
        makeButton(testScreen, DISPLAY_WIDTH - 240, 400, 200, 60, "Back", cbBack, 0, UI_PANEL_COLOR);
    }
    lv_scr_load(testScreen);
    testVisible = true;
    lastPin[0] = '\0';
    cbRescan(nullptr);
    uiUpdateRelayTest();
}

bool uiRelayTestVisible() {
    // Any other screen (a stop message, Main) may have replaced this one.
    if (testVisible && lv_scr_act() != testScreen) testVisible = false;
    return testVisible;
}

void uiUpdateRelayTest() {
    if (!uiRelayTestVisible() || resultLabel == nullptr) return;
    uint8_t out0 = 0, out1 = 0, in0 = 0, in1 = 0, cfg0 = 0, cfg1 = 0, pol0 = 0, pol1 = 0;
    const bool ok = relay.isReady() &&
                    relay.readPairTrue(0x02, out0, out1) && relay.readPairTrue(0x00, in0, in1) &&
                    relay.readPairTrue(0x06, cfg0, cfg1) && relay.readPairTrue(0x04, pol0, pol1);
    char text[420];
    if (!ok) {
        snprintf(text, sizeof(text), "Relay board not answering at 0x%02X.", XL9535_ADDR);
    } else {
        snprintf(text, sizeof(text),
                 "%s%s.  Commanded: P0 0x%02X  P1 0x%02X\n"
                 "Chip (two-order read, CH422G mask removed):\n"
                 "  output P0 0x%02X P1 0x%02X   input (real pin level) P0 0x%02X P1 0x%02X\n"
                 "  config %02X %02X (00 = outputs)   polarity %02X %02X\n"
                 "P0.0 output 1 and input 1 but no click: the relay board's K1 channel is faulty.\n"
                 "P0.0 output 1, input 0: something on the board holds the pin low.",
                 relay.testActive() ? "Driving " : (lastPin[0] ? "Last pulse " : "Idle. Tap a pin."),
                 lastPin, relay.commandedPort0(), relay.commandedPort1(),
                 out0, out1, in0, in1, cfg0, cfg1, pol0, pol1);
        if (relay.testActive()) {
            Serial.printf("Relay test %s: output %02X %02X input %02X %02X config %02X %02X\n",
                          lastPin, out0, out1, in0, in1, cfg0, cfg1);
        }
    }
    lv_label_set_text(resultLabel, text);
}
