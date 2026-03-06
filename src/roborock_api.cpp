#include "roborock_api.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

using namespace RoborockCrypto;
using namespace ApiUtils;

RoborockApi::RoborockApi(const RoborockConfig& config) : _cfg(config) {}

void RoborockApi::updateConfig(const RoborockConfig& config) {
    _cfg = config;
}

// HAWK authentication

String RoborockApi::buildHawkAuth(const String& urlPath) {
    time_t now = time(nullptr);
    String ts    = String((unsigned long)now);
    String nonce = generateNonce();
    String urlmd = md5Hex(urlPath);

    String pre = _cfg.rriot_u + ":" + _cfg.rriot_s + ":" +
                 nonce + ":" + ts + ":" + urlmd + "::";

    String mac = hmacSha256Base64(_cfg.rriot_h, pre);

    return "Hawk id=\"" + _cfg.rriot_u +
           "\",s=\"" + _cfg.rriot_s +
           "\",ts=\"" + ts +
           "\",nonce=\"" + nonce +
           "\",mac=\"" + mac + "\"";
}

// Fetch home data

RobotStatus RoborockApi::fetchHomeData() {
    RobotStatus st;

    String path = "/v3/user/homes/" + _cfg.home_id;
    String url  = _cfg.rriot_a;
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;

    String auth = buildHawkAuth(path);

    WiFiClientSecure tls;
    tls.setInsecure();

    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(tls, url)) {
        st.error = "HTTP begin failed";
        return st;
    }
    http.addHeader("Authorization", auth);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        st.error = "HTTP " + String(code);
        http.end();
        return st;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError je = deserializeJson(doc, body);

    if (je) {
        st.error = "JSON: " + String(je.c_str());
        return st;
    }

    bool success = doc["success"] | false;
    if (!success) {
        st.error = "API: success=false";
        return st;
    }

    JsonObject result = doc["result"];
    JsonArray devices = result["devices"];
    JsonArray received = result["receivedDevices"];

    auto findDevice = [&](JsonArray arr) -> bool {
        for (JsonObject d : arr) {
            if (d["duid"].as<String>() == _cfg.dev_duid) {
                st.name   = d["name"] | _cfg.dev_name;
                st.online = d["online"] | false;

                JsonObject ds = d["deviceStatus"];
                if (!ds.isNull()) {
                    st.battery  = ds["battery"]  | -1;
                    st.state    = ds["state"]    | -1;
                    st.charging = (st.state == 8);
                }
                st.valid = true;
                return true;
            }
        }
        return false;
    };

    if (!findDevice(devices)) findDevice(received);

    if (!st.valid) {
        st.error = "Device " + _cfg.dev_duid + " not found";
    }
    if (st.name.isEmpty()) st.name = _cfg.dev_name;
    return st;
}
