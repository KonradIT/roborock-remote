#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include "config_store.h"
#include "roborock_api.h"
#include "roborock_mqtt.h"

class RoborockLocal {
public:
    void configure(const RoborockConfig& cfg);
    bool connect();
    void disconnect();
    bool isConnected();
    void loop();

    bool   sendRpc(const String& method, const String& paramsJson = "[]");
    bool   hasRpcResponse() const;
    String takeRpcResult();

    bool requestStatus();
    bool hasNewStatus() const;
    RobotStatus takeStatus();

    bool fetchRooms(Room* rooms, int maxRooms, int& count, unsigned long timeoutMs = 10000);
    bool setFanPower(int power);
    bool setWaterBoxMode(int mode);
    bool startSegmentClean(const int* roomIds, int roomCount, int repeat = 1);

private:
    static constexpr uint16_t PORT            = 58867;
    static constexpr size_t   RX_BUF_SIZE     = 4096;
    static constexpr unsigned long PING_MS     = 10000;
    static constexpr unsigned long CONNECT_TO  = 5000;
    static constexpr unsigned long HELLO_TO    = 5000;
    static constexpr const char*  SALT         = "TXdfu$jyZ#TZHsg4";

    // Protocol IDs matching python-roborock RoborockMessageProtocol
    static constexpr uint16_t PROTO_HELLO_REQ    = 0;
    static constexpr uint16_t PROTO_HELLO_RSP    = 1;
    static constexpr uint16_t PROTO_PING_REQ     = 2;
    static constexpr uint16_t PROTO_PING_RSP     = 3;
    static constexpr uint16_t PROTO_GENERAL_REQ  = 4;   // local TCP
    static constexpr uint16_t PROTO_GENERAL_RSP  = 5;   // local TCP
    static constexpr uint16_t PROTO_RPC_REQ      = 101;  // MQTT only
    static constexpr uint16_t PROTO_RPC_RSP      = 102;  // MQTT only

    RoborockConfig _cfg;
    WiFiClient     _tcp;
    bool           _connected   = false;
    unsigned long  _lastPing    = 0;

    uint8_t*       _rxBuf       = nullptr;

    bool           _hasStatus   = false;
    RobotStatus    _lastStatus;

    int            _pendingRpcId = 0;
    bool           _hasRpc       = false;
    String         _rpcResult;

    // Build a message with 4-byte length prefix
    size_t  buildHeader(uint8_t* buf, uint16_t protocol, uint32_t seq, uint32_t rnd);
    size_t  buildRpc(uint8_t* buf, size_t maxLen, const String& method, const String& paramsJson);
    bool    sendPing();
    bool    doHello();

    // Read and parse incoming messages
    bool   readMessage();
    bool   parseMessage(const uint8_t* data, size_t len);

    // Crypto — same as RoborockMqtt
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
