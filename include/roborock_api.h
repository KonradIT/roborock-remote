#pragma once
#include <Arduino.h>
#include "config_store.h"

struct RobotStatus {
    String name;
    int battery      = -1;
    int state        = -1;
    int cleanPercent = -1;
    bool charging    = false;
    bool online      = false;
    bool valid       = false;
    String error;
};

class RoborockApi {
public:
    explicit RoborockApi(const RoborockConfig& config);
    void updateConfig(const RoborockConfig& config);
    RobotStatus fetchHomeData();

private:
    RoborockConfig _cfg;

    String buildHawkAuth(const String& urlPath);
    static String md5Hex(const String& input);
    static String hmacSha256Base64(const String& key, const String& data);
    static String base64Encode(const uint8_t* input, size_t len);
    static String generateNonce();
};
