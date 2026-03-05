#pragma once
#include <M5Unified.h>
#include "roborock_api.h"

class DisplayUI {
public:
    void begin();
    void showStatus(const RobotStatus& status);
    void showMessage(const String& title, const String& msg);
    void showConfigMode();
    void showConnecting(const String& ssid);
    void showRefreshing();

private:
    void drawHeader(const char* text);
    void drawBatteryBar(int y, int percent);
};
