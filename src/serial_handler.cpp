#include "serial_handler.h"

void SerialHandler::begin(unsigned long baud) {
    Serial.begin(baud);
}

void SerialHandler::process() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            _lineBuf.trim();
            if (_lineBuf.length() > 0) processLine(_lineBuf);
            _lineBuf = "";
        } else {
            _lineBuf += c;
        }
    }
}

bool SerialHandler::hasNewConfig() const {
    return _configReady;
}

RoborockConfig SerialHandler::takeConfig() {
    _configReady = false;
    return _pending;
}

void SerialHandler::processLine(const String& line) {
    if (line == "CONFIG_BEGIN") {
        _inConfigBlock = true;
        _pending = RoborockConfig();
        Serial.println("OK CONFIG_BEGIN");
        return;
    }

    if (line == "CONFIG_END") {
        _inConfigBlock = false;
        _configReady = true;
        Serial.println("OK CONFIG_END");
        return;
    }

    if (line == "PING") {
        Serial.println("PONG");
        return;
    }

    if (line == "RESET_CONFIG") {
        _configReady = false;
        _pending = RoborockConfig();
        Serial.println("OK RESET");
        return;
    }

    if (_inConfigBlock) {
        int eq = line.indexOf('=');
        if (eq > 0) {
            String key = line.substring(0, eq);
            String val = line.substring(eq + 1);
            key.trim();
            val.trim();
            handleKeyValue(key, val);
        }
    }
}

void SerialHandler::handleKeyValue(const String& key, const String& value) {
    if      (key == "wifi_ssid") _pending.wifi_ssid = value;
    else if (key == "wifi_pass") _pending.wifi_pass = value;
    else if (key == "email")     _pending.email     = value;
    else if (key == "token")     _pending.token     = value;
    else if (key == "base_url")  _pending.base_url  = value;
    else if (key == "rriot_u")   _pending.rriot_u   = value;
    else if (key == "rriot_s")   _pending.rriot_s   = value;
    else if (key == "rriot_h")   _pending.rriot_h   = value;
    else if (key == "rriot_k")   _pending.rriot_k   = value;
    else if (key == "rriot_a")   _pending.rriot_a   = value;
    else if (key == "home_id")   _pending.home_id   = value;
    else if (key == "dev_duid")  _pending.dev_duid  = value;
    else if (key == "dev_name")  _pending.dev_name  = value;
    else if (key == "local_key") _pending.local_key = value;
    else if (key == "rriot_m")   _pending.rriot_m   = value;
    else {
        Serial.println("WARN unknown key: " + key);
    }
}
