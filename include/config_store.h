#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct RoborockConfig {
    String wifi_ssid;
    String wifi_pass;
    String email;
    String token;
    String base_url;
    String rriot_u;
    String rriot_s;
    String rriot_h;
    String rriot_k;
    String rriot_a;
    String home_id;
    String dev_duid;
    String dev_name;
    String local_key;
    String rriot_m;

    bool isValid() const {
        return wifi_ssid.length() > 0 &&
               wifi_pass.length() > 0 &&
               rriot_u.length() > 0 &&
               rriot_s.length() > 0 &&
               rriot_h.length() > 0 &&
               rriot_a.length() > 0 &&
               home_id.length() > 0 &&
               dev_duid.length() > 0;
    }

    bool mqttReady() const {
        return local_key.length() > 0 &&
               rriot_m.length() > 0 &&
               rriot_k.length() > 0 &&
               dev_duid.length() > 0;
    }
};

class ConfigStore {
public:
    void begin();
    RoborockConfig load();
    void save(const RoborockConfig& config);
    void clear();

private:
    Preferences _prefs;
    static constexpr const char* NS = "roborock";
};
