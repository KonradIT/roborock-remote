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
static constexpr uint16_t COL_ORANGE  = 0xFD20;

// Helpers

const char* DisplayUI::stateText(int state) {
    switch (state) {
        case 1:  return "Initiating";
        case 2:  return "Sleeping";
        case 3:  return "Waiting";
        case 5:  return "Cleaning";
        case 6:  return "Returning";
        case 7:  return "Manual";
        case 8:  return "Charging";
        case 9:  return "Charge err";
        case 10: return "Paused";
        case 11: return "Spot clean";
        case 12: return "In error";
        case 14: return "Updating";
        case 15: return "Docking";
        case 17: return "Zone clean";
        case 18: return "Segment cl";
        case 100:return "Full";
        default: return "Idle";
    }
}

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

void DisplayUI::drawCircularGauge(int cx, int cy, int rOuter, int rInner,
                                   int percent, uint16_t color) {
    M5.Display.fillArc(cx, cy, rOuter, rInner, 0, 360, COL_BAR_BG);
    if (percent > 0) {
        float endAngle = 270.0f + percent * 3.6f;
        M5.Display.fillArc(cx, cy, rOuter, rInner, 270, endAngle, color);
    }

    M5.Display.setTextColor(COL_TEXT, COL_BG);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(3);
    M5.Display.fillRect(cx - rInner + 2, cy - 14, (rInner - 2) * 2, 28, COL_BG);
    M5.Display.drawString(String(percent) + "%", cx, cy);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(TL_DATUM);
}

// Status screen

void DisplayUI::showStatus(const RobotStatus& status) {
    int w = M5.Display.width();
    M5.Display.fillScreen(COL_BG);
    drawHeader(_local ? "LOCAL" : "CLOUD");

    int y = 30;
    M5.Display.setTextSize(1);

    M5.Display.setTextColor(COL_ACCENT);
    String name = status.name.isEmpty() ? "Unknown" : status.name;
    if (name.length() > 22) name = name.substring(0, 20) + "..";
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(name, w / 2, y);
    M5.Display.setTextDatum(TL_DATUM);
    y += 18;

    M5.Display.drawFastHLine(10, y, w - 20, COL_DIM);
    y += 8;

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

    if (status.charging) {
        M5.Display.setTextColor(COL_GREEN);
        M5.Display.drawString("Charging", 10, y);
    } else if (status.state >= 0) {
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString(String("State: ") + stateText(status.state), 10, y);
    } else {
        M5.Display.setTextColor(COL_DIM);
        M5.Display.drawString("State: N/A", 10, y);
    }
    y += 18;

    M5.Display.setTextColor(status.online ? COL_GREEN : COL_RED);
    M5.Display.drawString(status.online ? "Online" : "Offline", 10, y);
    y += 20;

    M5.Display.drawFastHLine(10, y, w - 20, COL_DIM);
    y += 8;

    M5.Display.setTextColor(COL_DIM);
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "Updated %02d:%02d", t.tm_hour, t.tm_min);
    M5.Display.drawString(buf, 10, y);
    y += 18;

    if (!status.error.isEmpty()) {
        M5.Display.setTextColor(COL_RED);
        String err = status.error;
        if (err.length() > 22) err = err.substring(0, 20) + "..";
        M5.Display.drawString(err, 10, y);
    }

    y = M5.Display.height() - 12;
    M5.Display.setTextColor(COL_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("[A] Clean  [B] Refresh", w / 2, y);
    M5.Display.setTextDatum(TL_DATUM);
}

// Selector screen

void DisplayUI::showSelector(const char* title, const String& item,
                              int index, int total) {
    int w = M5.Display.width();
    int h = M5.Display.height();
    M5.Display.fillScreen(COL_BG);
    drawHeader(title);

    M5.Display.setTextColor(COL_DIM);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);
    String counter = String(index + 1) + " / " + String(total);
    M5.Display.drawString(counter, w / 2, 40);

    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextSize(2);
    String display = item;
    int maxChars = (w - 10) / 12;
    if ((int)display.length() > maxChars)
        display = display.substring(0, maxChars - 1) + "~";
    M5.Display.drawString(display, w / 2, h / 2 - 10);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(COL_DIM);
    M5.Display.drawString("<", 8, h / 2 - 10);
    M5.Display.drawString(">", w - 14, h / 2 - 10);

    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(COL_DIM);
    M5.Display.setTextSize(1);
    M5.Display.drawString("[B] Next", w / 2, h - 24);
    M5.Display.drawString("[A] Select", w / 2, h - 10);
    M5.Display.setTextDatum(TL_DATUM);
}

// Clean confirmation screen

void DisplayUI::showCleanConfirm(const String& room, const String& mode,
                                  const String& suctionStr, const String& waterStr, const String& routeStr) {
    int w = M5.Display.width();
    int h = M5.Display.height();
    M5.Display.fillScreen(COL_BG);
    drawHeader("START CLEAN");

    int y = 36;
    M5.Display.setTextSize(1);

    M5.Display.setTextColor(COL_DIM);
    M5.Display.drawString("Room:", 10, y);
    y += 14;
    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(MC_DATUM);
    String rName = room;
    if (rName.length() > 11) rName = rName.substring(0, 10) + "~";
    M5.Display.drawString(rName, w / 2, y + 4);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextSize(1);
    y += 28;

    M5.Display.drawFastHLine(10, y, w - 20, COL_DIM);
    y += 8;

    M5.Display.setTextColor(COL_DIM);
    M5.Display.drawString("Mode:", 10, y);
    y += 14;
    M5.Display.setTextColor(COL_TEXT);
    M5.Display.drawString("  " + mode, 10, y);
    y += 16;

    if (!suctionStr.isEmpty()) {
        M5.Display.setTextColor(COL_DIM);
        M5.Display.drawString("Suct:", 10, y);
        y += 14;
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString("  " + suctionStr, 10, y);
        y += 16;
    }
    if (!waterStr.isEmpty()) {
        M5.Display.setTextColor(COL_DIM);
        M5.Display.drawString("Water:", 10, y);
        y += 14;
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString("  " + waterStr, 10, y);
        y += 16;
    }
    if (!routeStr.isEmpty()) {
        M5.Display.setTextColor(COL_DIM);
        M5.Display.drawString("Route:", 10, y);
        y += 14;
        M5.Display.setTextColor(COL_TEXT);
        M5.Display.drawString("  " + routeStr, 10, y);
        y += 16;
    }

    M5.Display.drawFastHLine(10, y, w - 20, COL_DIM);
    y += 12;

    int btnW = 90, btnH = 28;
    int btnX = (w - btnW) / 2, btnY = y;
    M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, 6, COL_GREEN);
    M5.Display.setTextColor(COL_BG, COL_GREEN);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("START", w / 2, btnY + btnH / 2);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextSize(1);

    M5.Display.setTextColor(COL_DIM);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("[A] Start  [B] Cancel", w / 2, h - 10);
    M5.Display.setTextDatum(TL_DATUM);
}

// Cleaning progress screen

void DisplayUI::showCleaningProgress(int cleanPercent, int batteryPercent,
                                      int state, unsigned long elapsedMs) {
    int w = M5.Display.width();
    int h = M5.Display.height();
    M5.Display.fillScreen(COL_BG);
    drawHeader("CLEANING");

    int cx = w / 2;
    int cy = 95;
    int rOuter = 48;
    int rInner = 36;

    int gaugeVal = (cleanPercent >= 0) ? cleanPercent : 0;
    uint16_t gaugeColor = COL_ACCENT;
    if (gaugeVal > 75)      gaugeColor = COL_GREEN;
    else if (gaugeVal > 40) gaugeColor = COL_ACCENT;
    else                    gaugeColor = COL_ORANGE;

    drawCircularGauge(cx, cy, rOuter, rInner, gaugeVal, gaugeColor);

    int y = cy + rOuter + 12;

    M5.Display.setTextColor(COL_ACCENT);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(stateText(state), cx, y);
    y += 16;

    unsigned long secs = elapsedMs / 1000;
    unsigned long mins = secs / 60;
    secs %= 60;
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%lu:%02lu", mins, secs);
    M5.Display.setTextColor(COL_DIM);
    M5.Display.drawString(timeBuf, cx, y);
    y += 16;

    if (batteryPercent >= 0) {
        uint16_t batCol = batteryPercent > 50 ? COL_GREEN : (batteryPercent > 20 ? COL_YELLOW : COL_RED);
        M5.Display.setTextColor(batCol);
        M5.Display.drawString("Bat: " + String(batteryPercent) + "%", cx, y);
    }

    M5.Display.setTextColor(COL_DIM);
    M5.Display.drawString("[B] Home", cx, h - 10);
    M5.Display.setTextDatum(TL_DATUM);
}

// Simple message screens

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

// Gyro joystick control screen

void DisplayUI::showGyroControl(float stickX, float stickY) {
    int w = M5.Display.width();
    int h = M5.Display.height();
    M5.Display.fillScreen(COL_BG);
    drawHeader("RC CONTROL");

    int cx = w / 2;
    int cy = h / 2 - 5;
    int rOuter = 50;

    M5.Display.drawCircle(cx, cy, rOuter, COL_DIM);
    M5.Display.drawCircle(cx, cy, rOuter / 2, COL_BAR_BG);
    M5.Display.drawFastHLine(cx - rOuter, cy, rOuter * 2, COL_BAR_BG);
    M5.Display.drawFastVLine(cx, cy - rOuter, rOuter * 2, COL_BAR_BG);

    float clampX = stickX;
    float clampY = stickY;
    if (clampX < -1.0f) clampX = -1.0f;
    if (clampX >  1.0f) clampX =  1.0f;
    if (clampY < -1.0f) clampY = -1.0f;
    if (clampY >  1.0f) clampY =  1.0f;

    int dotX = cx + (int)(clampX * rOuter);
    int dotY = cy - (int)(clampY * rOuter);
    M5.Display.fillCircle(dotX, dotY, 6, COL_ACCENT);

    float mag = sqrtf(clampX * clampX + clampY * clampY);
    uint16_t dotCol = (mag > 0.7f) ? COL_RED : (mag > 0.3f) ? COL_YELLOW : COL_GREEN;
    M5.Display.fillCircle(dotX, dotY, 4, dotCol);

    int yInfo = cy + rOuter + 14;
    M5.Display.setTextColor(COL_DIM);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);

    char buf[24];
    snprintf(buf, sizeof(buf), "V:%.2f  R:%.0f", clampY * 0.3f, clampX * -180.0f);
    M5.Display.drawString(buf, cx, yInfo);

    M5.Display.drawString("[B] Exit", cx, h - 10);
    M5.Display.setTextDatum(TL_DATUM);
}
