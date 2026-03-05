#include "roborock_api.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <time.h>

RoborockApi::RoborockApi(const RoborockConfig& config) : _cfg(config) {}

void RoborockApi::updateConfig(const RoborockConfig& config) {
    _cfg = config;
}

// ---------------------------------------------------------------------------
// Crypto helpers
// ---------------------------------------------------------------------------

String RoborockApi::md5Hex(const String& input) {
    uint8_t hash[16];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t*)input.c_str(), input.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[32] = '\0';
    return String(hex);
}

String RoborockApi::hmacSha256Base64(const String& key, const String& data) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    return base64Encode(hash, 32);
}

String RoborockApi::base64Encode(const uint8_t* input, size_t len) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, input, len);
    uint8_t* buf = new uint8_t[olen + 1];
    mbedtls_base64_encode(buf, olen + 1, &olen, input, len);
    buf[olen] = '\0';
    String result((char*)buf);
    delete[] buf;
    return result;
}

String RoborockApi::generateNonce() {
    uint8_t raw[6];
    for (int i = 0; i < 6; i++) raw[i] = (uint8_t)(esp_random() & 0xFF);
    String b64 = base64Encode(raw, 6);
    b64.replace("+", "-");
    b64.replace("/", "_");
    b64.replace("=", "");
    return b64;
}

// ---------------------------------------------------------------------------
// HAWK authentication (matches python-roborock's _get_hawk_authentication)
// ---------------------------------------------------------------------------

String RoborockApi::buildHawkAuth(const String& urlPath) {
    time_t now = time(nullptr);
    String ts    = String((unsigned long)now);
    String nonce = generateNonce();
    String urlmd = md5Hex(urlPath);

    // prestring: u:s:nonce:ts:md5(url):params_str:formdata_str
    // params_str and formdata_str are empty for simple GET
    String pre = _cfg.rriot_u + ":" + _cfg.rriot_s + ":" +
                 nonce + ":" + ts + ":" + urlmd + "::";

    String mac = hmacSha256Base64(_cfg.rriot_h, pre);

    return "Hawk id=\"" + _cfg.rriot_u +
           "\",s=\"" + _cfg.rriot_s +
           "\",ts=\"" + ts +
           "\",nonce=\"" + nonce +
           "\",mac=\"" + mac + "\"";
}

// ---------------------------------------------------------------------------
// Fetch home data and extract device status
// ---------------------------------------------------------------------------

RobotStatus RoborockApi::fetchHomeData() {
    RobotStatus st;

    String path = "/v3/user/homes/" + _cfg.home_id;
    String url  = _cfg.rriot_a;
    // strip trailing slash
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
