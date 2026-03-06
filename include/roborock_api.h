#pragma once
#include <Arduino.h>
#include "config_store.h"
#include "roborock_crypto.h"
#include "api_utils.h"

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

// Roborock cloud HTTP API client using HAWK authentication.
class RoborockApi {
public:
    explicit RoborockApi(const RoborockConfig& config);

    // Replace the stored configuration (e.g. after serial re-provisioning).
    void updateConfig(const RoborockConfig& config);

    // Fetch device status from the cloud /v3/user/homes endpoint.
    RobotStatus fetchHomeData();

private:
    RoborockConfig _cfg;

    // Build a HAWK Authorization header for the given URL path.
    String buildHawkAuth(const String& urlPath);
};
