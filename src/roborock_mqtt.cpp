#include "roborock_mqtt.h"

#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <time.h>

// Singleton pointer for PubSubClient static callback
static RoborockMqtt* _instance = nullptr;

static void _mqttCb(char* topic, byte* payload, unsigned int length) {
    if (_instance) _instance->onMessage(topic, payload, length);
}

// ---------------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------------

RoborockMqtt::RoborockMqtt() : _mqtt(_tls) {
    _instance = this;
}

void RoborockMqtt::configure(const RoborockConfig& cfg) {
    _cfg = cfg;
    if (!_rxBuf) _rxBuf = new uint8_t[RX_BUF_SIZE];

    parseMqttUrl(_cfg.rriot_m);
    deriveCredentials();

    _pubTopic = "rr/m/i/" + _cfg.rriot_u + "/" + _mqttUser + "/" + _cfg.dev_duid;
    _subTopic = "rr/m/o/" + _cfg.rriot_u + "/" + _mqttUser + "/" + _cfg.dev_duid;

    _mqtt.setServer(_mqttHost.c_str(), _mqttPort);
    _mqtt.setBufferSize(RX_BUF_SIZE);
    _mqtt.setCallback(_mqttCb);
    _mqtt.setKeepAlive(60);
}

void RoborockMqtt::parseMqttUrl(const String& url) {
    String s = url;
    if (s.startsWith("ssl://")) s = s.substring(6);
    else if (s.startsWith("tcp://")) s = s.substring(6);

    int colon = s.indexOf(':');
    if (colon > 0) {
        _mqttHost = s.substring(0, colon);
        _mqttPort = s.substring(colon + 1).toInt();
    } else {
        _mqttHost = s;
    }
}

void RoborockMqtt::deriveCredentials() {
    _mqttUser = md5Hex(_cfg.rriot_u + ":" + _cfg.rriot_k).substring(2, 10);
    _mqttPass = md5Hex(_cfg.rriot_s + ":" + _cfg.rriot_k).substring(16);
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

bool RoborockMqtt::connect() {
    _tls.setInsecure();

    String clientId = "esp32_" + _mqttUser;
    Serial.println("LOG: MQTT connecting to " + _mqttHost + ":" + String(_mqttPort));

    if (_mqtt.connect(clientId.c_str(), _mqttUser.c_str(), _mqttPass.c_str())) {
        Serial.println("LOG: MQTT connected, subscribing to " + _subTopic);
        _mqtt.subscribe(_subTopic.c_str());
        return true;
    }
    Serial.println("LOG: MQTT connect failed, rc=" + String(_mqtt.state()));
    return false;
}

void RoborockMqtt::disconnect() {
    _mqtt.disconnect();
}

bool RoborockMqtt::isConnected() {
    return _mqtt.connected();
}

void RoborockMqtt::loop() {
    if (_mqtt.connected()) _mqtt.loop();

    if (_rxReady) {
        _rxReady = false;
        parseMessage(_rxBuf, _rxLen);
    }
}

// ---------------------------------------------------------------------------
// Status request / response
// ---------------------------------------------------------------------------

bool RoborockMqtt::requestStatus() {
    if (!_mqtt.connected()) return false;

    uint8_t buf[512];
    size_t len = buildStatusRequest(buf, sizeof(buf));
    if (len == 0) return false;

    bool ok = _mqtt.publish(_pubTopic.c_str(), buf, len);
    Serial.println(ok ? "LOG: MQTT GET_STATUS sent" : "LOG: MQTT publish failed");
    return ok;
}

bool RoborockMqtt::hasNewStatus() const { return _hasStatus; }

RobotStatus RoborockMqtt::takeStatus() {
    _hasStatus = false;
    return _lastStatus;
}

void RoborockMqtt::onMessage(const char*, const uint8_t* data, size_t len) {
    if (len > RX_BUF_SIZE) return;
    memcpy(_rxBuf, data, len);
    _rxLen = len;
    _rxReady = true;
}

// ---------------------------------------------------------------------------
// Roborock binary protocol — message building
// ---------------------------------------------------------------------------

size_t RoborockMqtt::buildStatusRequest(uint8_t* buf, size_t maxLen) {
    uint32_t seq = random(100000, 999999);
    uint32_t rnd = random(10000, 99999);
    uint32_t ts  = (uint32_t)time(nullptr);
    int reqId    = random(100000, 999999);

    // Inner JSON: {"id":N,"method":"get_status","params":[]}
    JsonDocument inner;
    inner["id"]     = reqId;
    inner["method"] = "get_status";
    inner["params"].to<JsonArray>();
    String innerStr;
    serializeJson(inner, innerStr);

    // Outer DPS wrapper: {"dps":{"101":"<inner>"},"t":N}
    JsonDocument outer;
    outer["dps"]["101"] = innerStr;
    outer["t"]          = ts;
    String payloadStr;
    serializeJson(outer, payloadStr);

    // PKCS7 pad
    size_t pLen   = payloadStr.length();
    size_t padLen = ((pLen / 16) + 1) * 16;
    uint8_t* padded = (uint8_t*)malloc(padLen);
    if (!padded) return 0;
    memcpy(padded, payloadStr.c_str(), pLen);
    memset(padded + pLen, (uint8_t)(padLen - pLen), padLen - pLen);

    // Derive AES key and encrypt
    uint8_t token[16];
    deriveToken(ts, _cfg.local_key.c_str(), token);
    uint8_t* enc = (uint8_t*)malloc(padLen);
    if (!enc) { free(padded); return 0; }
    aesEcbEncrypt(padded, padLen, token, enc);
    free(padded);

    // Verify buffer space: header(19) + payload + crc(4)
    if (19 + padLen + 4 > maxLen) { free(enc); return 0; }

    size_t off = 0;
    buf[off++] = '1'; buf[off++] = '.'; buf[off++] = '0';   // version
    writeBE32(buf + off, seq); off += 4;                      // seq
    writeBE32(buf + off, rnd); off += 4;                      // random
    writeBE32(buf + off, ts);  off += 4;                      // timestamp
    writeBE16(buf + off, 101); off += 2;                      // protocol: RPC_REQUEST
    writeBE16(buf + off, (uint16_t)padLen); off += 2;         // payload length
    memcpy(buf + off, enc, padLen); off += padLen;            // encrypted payload
    free(enc);
    writeBE32(buf + off, calcCrc32(buf, off)); off += 4;      // CRC32

    return off;
}

// ---------------------------------------------------------------------------
// Roborock binary protocol — message parsing
// ---------------------------------------------------------------------------

bool RoborockMqtt::parseMessage(const uint8_t* data, size_t len) {
    if (len < 19) return false;

    uint32_t ts     = readBE32(data + 11);
    uint16_t proto  = readBE16(data + 15);
    uint16_t payLen = readBE16(data + 17);

    if (19 + (size_t)payLen > len) return false;
    if (payLen == 0) return false;

    // Verify CRC32 if present
    size_t msgEnd = 19 + payLen;
    if (msgEnd + 4 <= len) {
        uint32_t expected = readBE32(data + msgEnd);
        uint32_t actual   = calcCrc32(data, msgEnd);
        if (expected != actual) {
            Serial.println("LOG: MQTT CRC32 mismatch");
        }
    }

    // Decrypt
    uint8_t token[16];
    deriveToken(ts, _cfg.local_key.c_str(), token);

    uint8_t* dec = (uint8_t*)malloc(payLen + 1);
    if (!dec) return false;
    aesEcbDecrypt(data + 19, payLen, token, dec);

    // PKCS7 unpad
    uint8_t padVal = dec[payLen - 1];
    size_t actual = payLen;
    if (padVal > 0 && padVal <= 16) actual = payLen - padVal;
    dec[actual] = '\0';

    // Detect gzip (magic bytes 1f 8b)
    if (actual >= 2 && dec[0] == 0x1f && dec[1] == 0x8b) {
        Serial.println("LOG: MQTT gzip response — not yet decompressed");
        free(dec);
        return false;
    }

    Serial.println("LOG: MQTT decrypted (" + String(actual) + "B) proto=" + String(proto));

    JsonDocument doc;
    if (deserializeJson(doc, (char*)dec, actual)) {
        Serial.println("LOG: MQTT JSON parse failed");
        free(dec);
        return false;
    }
    free(dec);

    JsonObject dps = doc["dps"];
    if (dps.isNull()) return false;

    // RPC response (key "102")
    if (dps["102"].is<const char*>()) {
        String rpcStr = dps["102"].as<String>();
        JsonDocument rpc;
        if (!deserializeJson(rpc, rpcStr)) {
            JsonArray result = rpc["result"];
            if (!result.isNull() && result.size() > 0) {
                JsonObject s = result[0];
                _lastStatus.battery  = s["battery"]  | _lastStatus.battery;
                _lastStatus.state    = s["state"]    | _lastStatus.state;
                _lastStatus.charging = (_lastStatus.state == 8);
                _lastStatus.valid    = true;
                _hasStatus = true;
                Serial.println("LOG: MQTT RPC status — bat=" + String(_lastStatus.battery) +
                               " state=" + String(_lastStatus.state));
                return true;
            }
        }
    }

    // Push notification with individual DPS values
    bool found = false;
    if (dps["121"].is<int>()) { _lastStatus.state    = dps["121"].as<int>(); found = true; }
    if (dps["122"].is<int>()) { _lastStatus.battery  = dps["122"].as<int>(); found = true; }
    if (dps["133"].is<int>()) { _lastStatus.charging = dps["133"].as<int>() == 1; found = true; }
    if (found) {
        if (_lastStatus.state == 8) _lastStatus.charging = true;
        _lastStatus.valid = true;
        _hasStatus = true;
        Serial.println("LOG: MQTT push status — bat=" + String(_lastStatus.battery) +
                       " state=" + String(_lastStatus.state));
    }
    return found;
}

// ---------------------------------------------------------------------------
// Crypto primitives
// ---------------------------------------------------------------------------

String RoborockMqtt::md5Hex(const String& input) {
    uint8_t hash[16];
    md5Raw((const uint8_t*)input.c_str(), input.length(), hash);
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[32] = '\0';
    return String(hex);
}

void RoborockMqtt::md5Raw(const uint8_t* input, size_t len, uint8_t out[16]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, input, len);
    mbedtls_md_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

void RoborockMqtt::encodeTimestamp(uint32_t ts, char out[9]) {
    char hex[9];
    snprintf(hex, sizeof(hex), "%08x", ts);
    static const int order[] = {5, 6, 3, 7, 1, 2, 0, 4};
    for (int i = 0; i < 8; i++) out[i] = hex[order[i]];
    out[8] = '\0';
}

void RoborockMqtt::deriveToken(uint32_t ts, const char* localKey, uint8_t token[16]) {
    char tsEnc[9];
    encodeTimestamp(ts, tsEnc);
    String combined = String(tsEnc) + String(localKey) + String(SALT);
    md5Raw((const uint8_t*)combined.c_str(), combined.length(), token);
}

void RoborockMqtt::aesEcbEncrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    for (size_t i = 0; i < len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

void RoborockMqtt::aesEcbDecrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_dec(&ctx, key, 128);
    for (size_t i = 0; i < len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

uint32_t RoborockMqtt::calcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

void    RoborockMqtt::writeBE16(uint8_t* d, uint16_t v) { d[0] = v >> 8; d[1] = v; }
void    RoborockMqtt::writeBE32(uint8_t* d, uint32_t v) { d[0] = v >> 24; d[1] = v >> 16; d[2] = v >> 8; d[3] = v; }
uint16_t RoborockMqtt::readBE16(const uint8_t* s) { return (s[0] << 8) | s[1]; }
uint32_t RoborockMqtt::readBE32(const uint8_t* s) { return ((uint32_t)s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]; }
