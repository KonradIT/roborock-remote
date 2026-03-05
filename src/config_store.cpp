#include "config_store.h"

void ConfigStore::begin() {
    _prefs.begin(NS, false);
}

RoborockConfig ConfigStore::load() {
    RoborockConfig c;
    _prefs.begin(NS, true);
    c.wifi_ssid = _prefs.getString("wifi_ssid", "");
    c.wifi_pass = _prefs.getString("wifi_pass", "");
    c.email     = _prefs.getString("email", "");
    c.token     = _prefs.getString("token", "");
    c.base_url  = _prefs.getString("base_url", "");
    c.rriot_u   = _prefs.getString("rriot_u", "");
    c.rriot_s   = _prefs.getString("rriot_s", "");
    c.rriot_h   = _prefs.getString("rriot_h", "");
    c.rriot_k   = _prefs.getString("rriot_k", "");
    c.rriot_a   = _prefs.getString("rriot_a", "");
    c.home_id   = _prefs.getString("home_id", "");
    c.dev_duid  = _prefs.getString("dev_duid", "");
    c.dev_name  = _prefs.getString("dev_name", "");
    c.local_key = _prefs.getString("local_key", "");
    c.rriot_m   = _prefs.getString("rriot_m", "");
    c.dev_ip    = _prefs.getString("dev_ip", "");
    _prefs.end();
    return c;
}

void ConfigStore::save(const RoborockConfig& c) {
    _prefs.begin(NS, false);
    _prefs.putString("wifi_ssid", c.wifi_ssid);
    _prefs.putString("wifi_pass", c.wifi_pass);
    _prefs.putString("email",     c.email);
    _prefs.putString("token",     c.token);
    _prefs.putString("base_url",  c.base_url);
    _prefs.putString("rriot_u",   c.rriot_u);
    _prefs.putString("rriot_s",   c.rriot_s);
    _prefs.putString("rriot_h",   c.rriot_h);
    _prefs.putString("rriot_k",   c.rriot_k);
    _prefs.putString("rriot_a",   c.rriot_a);
    _prefs.putString("home_id",   c.home_id);
    _prefs.putString("dev_duid",  c.dev_duid);
    _prefs.putString("dev_name",  c.dev_name);
    _prefs.putString("local_key", c.local_key);
    _prefs.putString("rriot_m",   c.rriot_m);
    _prefs.putString("dev_ip",    c.dev_ip);
    _prefs.end();
}

void ConfigStore::clear() {
    _prefs.begin(NS, false);
    _prefs.clear();
    _prefs.end();
}

String ConfigStore::loadRooms() {
    _prefs.begin(NS, true);
    String s = _prefs.getString("rooms", "");
    _prefs.end();
    return s;
}

void ConfigStore::saveRooms(const String& json) {
    _prefs.begin(NS, false);
    _prefs.putString("rooms", json);
    _prefs.end();
}

void ConfigStore::clearRooms() {
    _prefs.begin(NS, false);
    _prefs.remove("rooms");
    _prefs.end();
}

String ConfigStore::loadHomeRooms() {
    _prefs.begin(NS, true);
    String s = _prefs.getString("home_rooms", "");
    _prefs.end();
    return s;
}

void ConfigStore::saveHomeRooms(const String& json) {
    _prefs.begin(NS, false);
    _prefs.putString("home_rooms", json);
    _prefs.end();
}
