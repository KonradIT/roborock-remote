#pragma once
#include <Arduino.h>
#include "display_ui.h"

struct RcControlCallbacks {
    bool (*sendRpc)(const String& method, const String& params);
    void (*loop)();
    bool (*isConnected)();
    DisplayUI* ui;
};

class RcControl {
public:
    void setCallbacks(const RcControlCallbacks& cb) { _cb = cb; }
    bool start();
    bool update(bool btnBPressed);
    void end();

private:
    RcControlCallbacks _cb;
    int _seqNum = 0;
    unsigned long _lastSend = 0;
    float _filteredX = 0, _filteredY = 0;
};
