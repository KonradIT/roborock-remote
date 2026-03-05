#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "config_store.h"
#include "roborock_api.h"

static constexpr int MAX_ROOMS = 20;

struct Room {
    int id = 0;
    String name;
};

class RoborockMqtt {
public:
    RoborockMqtt();
    void configure(const RoborockConfig& cfg);
    bool connect();
    void disconnect();
    bool isConnected();
    void loop();

    // Generic RPC
    bool   sendRpc(const String& method, const String& paramsJson = "[]");
    bool   hasRpcResponse() const;
    String takeRpcResult();

    // Status (push notifications + get_status response)
    bool requestStatus();
    bool hasNewStatus() const;
    RobotStatus takeStatus();

    // High-level commands
    bool fetchRooms(Room* rooms, int maxRooms, int& count, unsigned long timeoutMs = 10000);
    bool setFanPower(int power);
    bool setWaterBoxMode(int mode);
    bool startSegmentClean(const int* roomIds, int roomCount, int repeat = 1);

    void onMessage(const char* topic, const uint8_t* data, size_t len);

private:
    static constexpr size_t RX_BUF_SIZE = 4096;
    static constexpr const char* SALT = "TXdfu$jyZ#TZHsg4";

    RoborockConfig _cfg;
    WiFiClientSecure _tls;
    PubSubClient _mqtt;

    String _mqttHost;
    int    _mqttPort = 8883;
    String _mqttUser;
    String _mqttPass;
    String _pubTopic;
    String _subTopic;

    uint8_t* _rxBuf  = nullptr;
    size_t   _rxLen  = 0;
    bool     _rxReady = false;

    // Status tracking
    bool _hasStatus = false;
    RobotStatus _lastStatus;

    // Generic RPC tracking
    int    _pendingRpcId = 0;
    bool   _hasRpc       = false;
    String _rpcResult;

    void parseMqttUrl(const String& url);
    void deriveCredentials();

    size_t buildRpc(uint8_t* buf, size_t maxLen, const String& method, const String& paramsJson);
    bool   parseMessage(const uint8_t* data, size_t len);

    static String   md5Hex(const String& input);
    static void     md5Raw(const uint8_t* input, size_t len, uint8_t out[16]);
    static void     encodeTimestamp(uint32_t ts, char out[9]);
    static void     deriveToken(uint32_t ts, const char* localKey, uint8_t token[16]);
    static void     aesEcbEncrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out);
    static void     aesEcbDecrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out);
    static uint32_t calcCrc32(const uint8_t* data, size_t len);
    static void     writeBE16(uint8_t* dst, uint16_t v);
    static void     writeBE32(uint8_t* dst, uint32_t v);
    static uint16_t readBE16(const uint8_t* src);
    static uint32_t readBE32(const uint8_t* src);
};
