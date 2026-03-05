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
    String dev_ip;

    bool isValid() const {
        return wifi_ssid.length() > 0 &&
               wifi_pass.length() > 0 &&
               dev_duid.length() > 0;
    }

    bool localReady() const {
        return local_key.length() > 0 &&
               dev_ip.length() > 0;
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

    String loadRooms();
    void   saveRooms(const String& json);
    void   clearRooms();

    String loadHomeRooms();
    void   saveHomeRooms(const String& json);

private:
    Preferences _prefs;
    static constexpr const char* NS = "roborock";
};
