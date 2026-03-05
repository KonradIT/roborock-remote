#include "display_ui.h"
#include <time.h>

static constexpr uint16_t COL_BG      = TFT_BLACK;
static constexpr uint16_t COL_HEADER  = 0x1A74;   // dark teal
static constexpr uint16_t COL_TEXT    = TFT_WHITE;
static constexpr uint16_t COL_DIM     = TFT_DARKGREY;
static constexpr uint16_t COL_ACCENT  = 0x07FF;   // cyan
static constexpr uint16_t COL_GREEN   = 0x07E0;
static constexpr uint16_t COL_YELLOW  = 0xFFE0;
static constexpr uint16_t COL_RED     = 0xF800;
static constexpr uint16_t COL_BAR_BG  = 0x2104;   // very dark grey

void DisplayUI::begin() {
    M5.Display.setRotation(0);
    M5.Display.fillScreen(COL_BG);
}

void DisplayUI::drawHeader(const char* text) {
    int w = M5.Display.width();
    M5.Display.fillRect(0, 0, w, 22, COL_HEADER);
    M5.Display.setTextColor(COL_TEXT, COL_HEADER);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(text, w / 2, 11);
    M5.Display.setTextDatum(TL_DATUM);
}

void DisplayUI::drawBatteryBar(int y, int percent) {
    int w   = M5.Display.width();
    int pad = 10;
    int bw  = w - pad * 2;
    int bh  = 14;

    M5.Display.fillRoundRect(pad, y, bw, bh, 3, COL_BAR_BG);

    if (percent < 0) return;
    int fill = (bw - 2) * percent / 100;
    uint16_t col = percent > 50 ? COL_GREEN : (percent > 20 ? COL_YELLOW : COL_RED);
    M5.Display.fillRoundRect(pad + 1, y + 1, fill, bh - 2, 2, col);
}

void DisplayUI::showStatus(const RobotStatus& status) {
    int w = M5.Display.width();
    M5.Display.fillScreen(COL_BG);
    drawHeader("ROBOROCK");

    int y = 30;
    M5.Display.setTextSize(1);

    // Robot name
    M5.Display.setTextColor(COL_ACCENT);
    String name = status.name.isEmpty() ? "Unknown" : status.name;
    if (name.length() > 22) name = name.substring(0, 20) + "..";
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(name, w / 2, y);
    M5.Display.setTextDatum(TL_DATUM);
    y += 18;

    M5.Display.drawFastHLine(10, y, w - 20, COL_DIM);
    y += 8;

    // Battery
    M5.Display.setTextColor(COL_TEXT);
    if (status.battery >= 0) {
        M5.Display.drawString("Battery: " + String(status.battery) + "%", 10, y);
    } else {
        M5.Display.setTextColor(COL_DIM);
        M5.Display.drawString("Battery: N/A", 10, y);
        M5.Display.setTextColor(COL_TEXT);
    }
    y += 14;
    drawBatteryBar(y, status.battery);
    y += 22;

    // Charging / state
    if (status.charging) {
        M5.Display.setTextColor(COL_GREEN);
        M5.Display.drawString("Charging", 10, y);
    } else if (status.state >= 0) {
        const char* stateText = "Idle";
        switch (status.state) {
            case 1:  stateText = "Initiating"; break;
            case 2:  stateText = "Sleeping";   break;
            case 3:  stateText = "Waiting";    break;
            case 5:  stateText = "Cleaning";   break;
            case 6:  stateText = "Returning";  break;
            case 7:  stateText = "Manual";     break;
            case 8:  stateText = "Charging";   break;
            case 9:  stateText = "Charge err"; break;
            case 10: stateText = "Paused";     break;
            case 11: stateText = "Spot clean"; break;
            case 12: stateText = "In error";   break;
            case 14: stateText = "Updating";   break;
            case 15: stateText = "Docking";    break;
            case 17: stateText = "Zone clean"; break;
            case 18: stateText = "Segment cl"; break;
            case 100:stateText = "Full";       break;
        }
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString(String("State: ") + stateText, 10, y);
    } else {
        M5.Display.setTextColor(COL_DIM);
        M5.Display.drawString("State: N/A", 10, y);
    }
    y += 18;

    // Online
    M5.Display.setTextColor(status.online ? COL_GREEN : COL_RED);
    M5.Display.drawString(status.online ? "Online" : "Offline", 10, y);
    y += 20;

    M5.Display.drawFastHLine(10, y, w - 20, COL_DIM);
    y += 8;

    // Last update time
    M5.Display.setTextColor(COL_DIM);
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "Updated %02d:%02d", t.tm_hour, t.tm_min);
    M5.Display.drawString(buf, 10, y);
    y += 18;

    // Error (if any)
    if (!status.error.isEmpty()) {
        M5.Display.setTextColor(COL_RED);
        String err = status.error;
        if (err.length() > 22) err = err.substring(0, 20) + "..";
        M5.Display.drawString(err, 10, y);
        y += 14;
    }

    // Hint
    y = M5.Display.height() - 12;
    M5.Display.setTextColor(COL_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("[A] Refresh", w / 2, y);
    M5.Display.setTextDatum(TL_DATUM);
}

void DisplayUI::showMessage(const String& title, const String& msg) {
    int w = M5.Display.width();
    M5.Display.fillScreen(COL_BG);
    drawHeader(title.c_str());

    M5.Display.setTextColor(COL_TEXT);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(msg, w / 2, M5.Display.height() / 2);
    M5.Display.setTextDatum(TL_DATUM);
}

void DisplayUI::showConfigMode() {
    showMessage("SETUP", "Send config via serial");
}

void DisplayUI::showConnecting(const String& ssid) {
    showMessage("CONNECTING", ssid);
}

void DisplayUI::showRefreshing() {
    int w = M5.Display.width();
    int h = M5.Display.height();
    M5.Display.setTextColor(COL_ACCENT, COL_BG);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("Refreshing...", w / 2, h - 28);
    M5.Display.setTextDatum(TL_DATUM);
}
