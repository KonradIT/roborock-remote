#include "roborock_local.h"

#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void RoborockLocal::configure(const RoborockConfig& cfg) {
    _cfg = cfg;
    if (!_rxBuf) _rxBuf = new uint8_t[RX_BUF_SIZE];
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

bool RoborockLocal::connect() {
    Serial.println("LOG: Local TCP connecting to " + _cfg.dev_ip + ":" + String(PORT));

    if (!_tcp.connect(_cfg.dev_ip.c_str(), PORT, CONNECT_TO)) {
        Serial.println("LOG: TCP connect failed");
        return false;
    }
    Serial.println("LOG: TCP connected, sending hello...");

    if (!doHello()) {
        Serial.println("LOG: Hello handshake failed");
        _tcp.stop();
        return false;
    }

    _connected = true;
    _lastPing  = millis();
    Serial.println("LOG: Local handshake OK — fully connected");
    return true;
}

void RoborockLocal::disconnect() {
    _tcp.stop();
    _connected = false;
}

bool RoborockLocal::isConnected() {
    if (_connected && !_tcp.connected()) {
        _connected = false;
    }
    return _connected;
}

void RoborockLocal::loop() {
    if (!_connected) return;

    if (!_tcp.connected()) {
        _connected = false;
        Serial.println("LOG: Local TCP disconnected");
        return;
    }

    // Read any available data
    while (_tcp.available()) {
        readMessage();
    }

    // Keep-alive ping
    if (millis() - _lastPing >= PING_MS) {
        if (!sendPing()) {
            Serial.println("LOG: Ping failed, disconnecting");
            disconnect();
        }
        _lastPing = millis();
    }
}

// ---------------------------------------------------------------------------
// Hello handshake
// ---------------------------------------------------------------------------

bool RoborockLocal::doHello() {
    uint8_t buf[32];
    uint32_t seq = 1;
    uint32_t rnd = random(10000, 99999);

    // Build HELLO_REQUEST: [4B prefix][header(17B) + CRC32(4B)]
    size_t hdrLen = buildHeader(buf + 4, PROTO_HELLO_REQ, seq, rnd);
    writeBE32(buf + 4 + hdrLen, calcCrc32(buf + 4, hdrLen));
    uint32_t contentLen = hdrLen + 4;
    writeBE32(buf, contentLen);
    size_t total = 4 + contentLen;

    _tcp.write(buf, total);
    _tcp.flush();
    Serial.println("LOG: Hello sent (" + String(total) + "B)");

    // Wait for HELLO_RESPONSE
    unsigned long start = millis();
    while (_tcp.available() < 4 && millis() - start < HELLO_TO) {
        if (!_tcp.connected()) {
            Serial.println("LOG: Hello - TCP closed by device");
            return false;
        }
        delay(10);
    }

    int avail = _tcp.available();
    if (avail < 4) {
        Serial.println("LOG: Hello timeout - avail=" + String(avail) +
                       " connected=" + String(_tcp.connected()));
        return false;
    }

    uint8_t lenBuf[4];
    _tcp.readBytes(lenBuf, 4);
    uint32_t respLen = readBE32(lenBuf);
    Serial.println("LOG: Hello resp len=" + String(respLen));

    if (respLen > 256 || respLen < 17) {
        Serial.println("LOG: Hello bad length");
        return false;
    }

    uint8_t resp[256];
    size_t got = 0;
    start = millis();
    while (got < respLen && millis() - start < HELLO_TO) {
        if (_tcp.available()) {
            got += _tcp.readBytes(resp + got, respLen - got);
        } else {
            delay(5);
        }
    }

    if (got < 17) {
        Serial.println("LOG: Hello incomplete - got=" + String(got));
        return false;
    }

    // Dump first bytes for debugging
    char hex[64];
    int dumpLen = got < 24 ? got : 24;
    for (int i = 0; i < dumpLen; i++) sprintf(hex + i * 2, "%02x", resp[i]);
    hex[dumpLen * 2] = '\0';
    Serial.println("LOG: Hello rx: " + String(hex));

    uint16_t proto = readBE16(resp + 15);
    if (proto != PROTO_HELLO_RSP) {
        Serial.println("LOG: Hello unexpected proto=" + String(proto));
        return false;
    }

    Serial.println("LOG: Hello handshake OK");
    return true;
}

// ---------------------------------------------------------------------------
// Ping keep-alive
// ---------------------------------------------------------------------------

bool RoborockLocal::sendPing() {
    uint8_t buf[32];
    uint32_t seq = random(100000, 999999);
    uint32_t rnd = random(10000, 99999);

    size_t hdrLen = buildHeader(buf + 4, PROTO_PING_REQ, seq, rnd);
    writeBE32(buf + 4 + hdrLen, calcCrc32(buf + 4, hdrLen));
    uint32_t contentLen = hdrLen + 4;
    writeBE32(buf, contentLen);
    size_t total = 4 + contentLen;

    size_t written = _tcp.write(buf, total);
    _tcp.flush();
    return written == total;
}

// ---------------------------------------------------------------------------
// Build helpers
// ---------------------------------------------------------------------------

size_t RoborockLocal::buildHeader(uint8_t* buf, uint16_t protocol, uint32_t seq, uint32_t rnd) {
    uint32_t ts = (uint32_t)time(nullptr);
    size_t off = 0;
    buf[off++] = '1'; buf[off++] = '.'; buf[off++] = '0';
    writeBE32(buf + off, seq); off += 4;
    writeBE32(buf + off, rnd); off += 4;
    writeBE32(buf + off, ts);  off += 4;
    writeBE16(buf + off, protocol); off += 2;
    return off; // 17 bytes
}

size_t RoborockLocal::buildRpc(uint8_t* buf, size_t maxLen,
                                const String& method, const String& paramsJson) {
    uint32_t seq = random(100000, 999999);
    uint32_t rnd = random(10000, 99999);
    uint32_t ts  = (uint32_t)time(nullptr);
    _pendingRpcId = random(10000, 32767);

    String innerStr = "{\"id\":" + String(_pendingRpcId) +
                      ",\"method\":\"" + method +
                      "\",\"params\":" + paramsJson + "}";

    JsonDocument outer;
    outer["dps"]["101"] = innerStr;
    outer["t"]          = ts;
    String payloadStr;
    serializeJson(outer, payloadStr);

    size_t pLen   = payloadStr.length();
    size_t padLen = ((pLen / 16) + 1) * 16;
    uint8_t* padded = (uint8_t*)malloc(padLen);
    if (!padded) return 0;
    memcpy(padded, payloadStr.c_str(), pLen);
    memset(padded + pLen, (uint8_t)(padLen - pLen), padLen - pLen);

    uint8_t token[16];
    deriveToken(ts, _cfg.local_key.c_str(), token);
    uint8_t* enc = (uint8_t*)malloc(padLen);
    if (!enc) { free(padded); return 0; }
    aesEcbEncrypt(padded, padLen, token, enc);
    free(padded);

    // Content: header(17) + payloadLen(2) + encrypted(padLen) + CRC(4) = 23 + padLen
    size_t contentLen = 17 + 2 + padLen + 4;
    if (4 + contentLen > maxLen) { free(enc); return 0; }

    // 4-byte length prefix
    writeBE32(buf, (uint32_t)contentLen);
    size_t off = 4;

    // Header
    buf[off++] = '1'; buf[off++] = '.'; buf[off++] = '0';
    writeBE32(buf + off, seq); off += 4;
    writeBE32(buf + off, rnd); off += 4;
    writeBE32(buf + off, ts);  off += 4;
    writeBE16(buf + off, PROTO_GENERAL_REQ); off += 2;

    // Encrypted payload with length prefix
    writeBE16(buf + off, (uint16_t)padLen); off += 2;
    memcpy(buf + off, enc, padLen); off += padLen;
    free(enc);

    // CRC32 over the content (after the 4-byte length prefix)
    writeBE32(buf + off, calcCrc32(buf + 4, off - 4)); off += 4;
    return off;
}

// ---------------------------------------------------------------------------
// Generic RPC
// ---------------------------------------------------------------------------

bool RoborockLocal::sendRpc(const String& method, const String& paramsJson) {
    // Auto-reconnect if TCP dropped
    if (_connected && !_tcp.connected()) {
        Serial.println("LOG: Local TCP dropped, reconnecting...");
        _connected = false;
    }
    if (!_connected) {
        if (!connect()) {
            Serial.println("LOG: Local reconnect failed");
            return false;
        }
    }
    _hasRpc = false;

    uint8_t buf[1024];
    size_t len = buildRpc(buf, sizeof(buf), method, paramsJson);
    if (len == 0) return false;

    size_t written = _tcp.write(buf, len);
    _tcp.flush();
    bool ok = (written == len);
    Serial.println(ok ? ("LOG: Local RPC -> " + method + " (rpcId=" + String(_pendingRpcId) + ")")
                      : "LOG: Local write failed");
    return ok;
}

bool   RoborockLocal::hasRpcResponse() const { return _hasRpc; }
String RoborockLocal::takeRpcResult()         { _hasRpc = false; return _rpcResult; }

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

bool RoborockLocal::requestStatus() { return sendRpc("get_status"); }
bool RoborockLocal::hasNewStatus() const { return _hasStatus; }
RobotStatus RoborockLocal::takeStatus() { _hasStatus = false; return _lastStatus; }

// ---------------------------------------------------------------------------
// High-level commands
// ---------------------------------------------------------------------------

bool RoborockLocal::fetchRooms(Room* rooms, int maxRooms, int& count, unsigned long timeoutMs) {
    count = 0;
    if (!sendRpc("get_room_mapping")) return false;

    unsigned long start = millis();
    while (!_hasRpc && millis() - start < timeoutMs) {
        loop();
        delay(50);
    }
    if (!_hasRpc) {
        Serial.println("LOG: get_room_mapping timed out after " + String(millis() - start) + "ms");
        return false;
    }

    String result = takeRpcResult();
    JsonDocument doc;
    if (deserializeJson(doc, result)) return false;

    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant entry : arr) {
        if (count >= maxRooms) break;
        JsonArray pair = entry.as<JsonArray>();
        if (pair.size() >= 2) {
            rooms[count].id   = pair[0].as<int>();
            rooms[count].name = pair[1].as<String>();
            count++;
        }
    }
    Serial.println("LOG: Got " + String(count) + " rooms (local)");
    return count > 0;
}

bool RoborockLocal::setFanPower(int power) {
    return sendRpc("set_custom_mode", "[" + String(power) + "]");
}

bool RoborockLocal::setWaterBoxMode(int mode) {
    return sendRpc("set_water_box_custom_mode", "[" + String(mode) + "]");
}

bool RoborockLocal::startSegmentClean(const int* roomIds, int roomCount, int repeat) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    JsonObject obj = arr.add<JsonObject>();
    JsonArray segs = obj["segments"].to<JsonArray>();
    for (int i = 0; i < roomCount; i++) segs.add(roomIds[i]);
    obj["repeat"] = repeat;
    obj["clean_order_mode"] = 0;

    String params;
    serializeJson(doc, params);
    return sendRpc("app_segment_clean", params);
}

// ---------------------------------------------------------------------------
// Reading incoming TCP data
// ---------------------------------------------------------------------------

bool RoborockLocal::readMessage() {
    if (_tcp.available() < 4) return false;

    uint8_t lenBuf[4];
    _tcp.readBytes(lenBuf, 4);
    uint32_t contentLen = readBE32(lenBuf);

    if (contentLen > RX_BUF_SIZE || contentLen < 17) {
        Serial.println("LOG: Local bad frame len=" + String(contentLen));
        // Drain remaining data
        while (_tcp.available()) _tcp.read();
        return false;
    }

    size_t got = 0;
    unsigned long start = millis();
    while (got < contentLen && millis() - start < 3000) {
        if (_tcp.available()) {
            got += _tcp.readBytes(_rxBuf + got, contentLen - got);
        } else {
            delay(1);
        }
    }

    if (got < 17) return false;

    uint16_t proto = readBE16(_rxBuf + 15);

    // PING_RESPONSE — just acknowledge
    if (proto == PROTO_PING_RSP) return true;

    // HELLO_RESPONSE — shouldn't arrive here, but ignore
    if (proto == PROTO_HELLO_RSP) return true;

    // GENERAL_RESPONSE (5) or RPC_RESPONSE (102) or push notification
    if (got >= 19) {
        return parseMessage(_rxBuf, got);
    }

    return false;
}

// ---------------------------------------------------------------------------
// Message parsing (same logic as RoborockMqtt::parseMessage)
// ---------------------------------------------------------------------------

bool RoborockLocal::parseMessage(const uint8_t* data, size_t len) {
    if (len < 19) return false;

    uint32_t ts     = readBE32(data + 11);
    uint16_t proto  = readBE16(data + 15);
    uint16_t payLen = readBE16(data + 17);

    if (19 + (size_t)payLen > len || payLen == 0) return false;

    size_t msgEnd = 19 + payLen;
    if (msgEnd + 4 <= len) {
        uint32_t expected = readBE32(data + msgEnd);
        uint32_t actual   = calcCrc32(data, msgEnd);
        if (expected != actual)
            Serial.println("LOG: Local CRC32 mismatch");
    }

    uint8_t token[16];
    deriveToken(ts, _cfg.local_key.c_str(), token);

    uint8_t* dec = (uint8_t*)malloc(payLen + 1);
    if (!dec) return false;
    aesEcbDecrypt(data + 19, payLen, token, dec);

    uint8_t padVal = dec[payLen - 1];
    size_t actualLen = payLen;
    if (padVal > 0 && padVal <= 16) actualLen = payLen - padVal;
    dec[actualLen] = '\0';

    if (actualLen >= 2 && dec[0] == 0x1f && dec[1] == 0x8b) {
        Serial.println("LOG: Local gzip — not yet supported");
        free(dec);
        return false;
    }

    Serial.println("LOG: Local rx (" + String(actualLen) + "B) proto=" + String(proto));

    JsonDocument doc;
    if (deserializeJson(doc, (char*)dec, actualLen)) {
        Serial.println("LOG: Local JSON parse fail");
        free(dec);
        return false;
    }
    free(dec);

    JsonObject dps = doc["dps"];
    if (dps.isNull()) return false;

    if (dps["102"].is<const char*>()) {
        String rpcStr = dps["102"].as<String>();
        JsonDocument rpc;
        if (!deserializeJson(rpc, rpcStr)) {
            int id = rpc["id"] | 0;
            Serial.println("LOG: Local RPC resp id=" + String(id));

            if (id == _pendingRpcId) {
                _rpcResult = "";
                serializeJson(rpc["result"], _rpcResult);
                _hasRpc = true;
            }

            JsonArray result = rpc["result"];
            if (!result.isNull() && result.size() > 0 && result[0].is<JsonObject>()) {
                JsonObject s = result[0];
                if (s["battery"].is<int>())       _lastStatus.battery      = s["battery"].as<int>();
                if (s["state"].is<int>())          _lastStatus.state        = s["state"].as<int>();
                if (s["clean_percent"].is<int>())  _lastStatus.cleanPercent = s["clean_percent"].as<int>();
                _lastStatus.charging = (_lastStatus.state == 8);
                _lastStatus.valid    = true;
                _hasStatus = true;
            }
            return true;
        }
    }

    // Push notification with individual DPS values
    bool found = false;
    if (dps["121"].is<int>()) { _lastStatus.state        = dps["121"].as<int>(); found = true; }
    if (dps["122"].is<int>()) { _lastStatus.battery      = dps["122"].as<int>(); found = true; }
    if (dps["133"].is<int>()) { _lastStatus.charging     = dps["133"].as<int>() == 1; found = true; }
    if (dps["141"].is<int>()) { _lastStatus.cleanPercent = dps["141"].as<int>(); found = true; }
    if (found) {
        if (_lastStatus.state == 8) _lastStatus.charging = true;
        _lastStatus.valid = true;
        _hasStatus = true;
        Serial.println("LOG: Local push — bat=" + String(_lastStatus.battery) +
                       " state=" + String(_lastStatus.state));
    }
    return found;
}

// ---------------------------------------------------------------------------
// Crypto primitives (identical to RoborockMqtt)
// ---------------------------------------------------------------------------

String RoborockLocal::md5Hex(const String& input) {
    uint8_t hash[16];
    md5Raw((const uint8_t*)input.c_str(), input.length(), hash);
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[32] = '\0';
    return String(hex);
}

void RoborockLocal::md5Raw(const uint8_t* input, size_t len, uint8_t out[16]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, input, len);
    mbedtls_md_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

void RoborockLocal::encodeTimestamp(uint32_t ts, char out[9]) {
    char hex[9];
    snprintf(hex, sizeof(hex), "%08x", ts);
    static const int order[] = {5, 6, 3, 7, 1, 2, 0, 4};
    for (int i = 0; i < 8; i++) out[i] = hex[order[i]];
    out[8] = '\0';
}

void RoborockLocal::deriveToken(uint32_t ts, const char* localKey, uint8_t token[16]) {
    char tsEnc[9];
    encodeTimestamp(ts, tsEnc);
    String combined = String(tsEnc) + String(localKey) + String(SALT);
    md5Raw((const uint8_t*)combined.c_str(), combined.length(), token);
}

void RoborockLocal::aesEcbEncrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    for (size_t i = 0; i < len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

void RoborockLocal::aesEcbDecrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_dec(&ctx, key, 128);
    for (size_t i = 0; i < len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

uint32_t RoborockLocal::calcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

void     RoborockLocal::writeBE16(uint8_t* d, uint16_t v) { d[0] = v >> 8; d[1] = v; }
void     RoborockLocal::writeBE32(uint8_t* d, uint32_t v) { d[0] = v >> 24; d[1] = v >> 16; d[2] = v >> 8; d[3] = v; }
uint16_t RoborockLocal::readBE16(const uint8_t* s) { return (s[0] << 8) | s[1]; }
uint32_t RoborockLocal::readBE32(const uint8_t* s) { return ((uint32_t)s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]; }
