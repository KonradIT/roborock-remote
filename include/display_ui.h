#pragma once
#include <M5Unified.h>
#include "roborock_api.h"

class DisplayUI {
public:
    void begin();
    void setMode(bool local) { _local = local; }
    void showStatus(const RobotStatus& status);
    void showMessage(const String& title, const String& msg);
    void showConfigMode();
    void showConnecting(const String& ssid);
    void showRefreshing();

    void showSelector(const char* title, const String& item,
                      int index, int total);
    void showCleanConfirm(const String& room, const String& mode,
                          const String& suctionStr, const String& waterStr, const String& routeStr);
    void showCleaningProgress(int cleanPercent, int batteryPercent,
                              int state, unsigned long elapsedMs);

    // Gyro joystick screen: stickX/stickY in -1.0..1.0 range.
    void showGyroControl(float stickX, float stickY);

private:
    bool _local = false;
    void drawHeader(const char* text);
    void drawBatteryBar(int y, int percent);
    void drawCircularGauge(int cx, int cy, int rOuter, int rInner,
                           int percent, uint16_t color);
    static const char* stateText(int state);
};
