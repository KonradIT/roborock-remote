#pragma once
#include <Arduino.h>
#include "config_store.h"

class SerialHandler {
public:
    void begin(unsigned long baud = 115200);
    void process();

    bool hasNewConfig() const;
    RoborockConfig takeConfig();
    String takeRoomsJson();

private:
    bool _configReady     = false;
    bool _inConfigBlock   = false;
    RoborockConfig _pending;
    String _pendingRooms;
    String _lineBuf;

    void processLine(const String& line);
    void handleKeyValue(const String& key, const String& value);
};
