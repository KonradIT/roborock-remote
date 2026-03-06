#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "config_store.h"
#include "roborock_api.h"
#include "roborock_crypto.h"

static constexpr int MAX_ROOMS = 20;

struct Room {
    int id = 0;
    String name;
};

// Roborock cloud MQTT client (TLS, binary V1 protocol, AES-ECB encryption).
class RoborockMqtt {
public:
    RoborockMqtt();

    // Store credentials and set up the MQTT client.
    void configure(const RoborockConfig& cfg);

    // Connect to the Roborock MQTT broker over TLS.
    bool connect();

    // Disconnect from the broker.
    void disconnect();

    // True if the underlying MQTT connection is alive.
    bool isConnected();

    // Process incoming MQTT messages; call from the main loop.
    void loop();

    // Send a JSON-RPC command and return true if published.
    bool   sendRpc(const String& method, const String& paramsJson = "[]");

    // True once a response matching the last sendRpc arrives.
    bool   hasRpcResponse() const;

    // Consume and return the raw JSON result string.
    String takeRpcResult();

    // Send a get_status RPC request.
    bool requestStatus();

    // True when a new status update (push or RPC response) is available.
    bool hasNewStatus() const;

    // Consume and return the latest RobotStatus.
    RobotStatus takeStatus();

    // Fetch the segment-to-cloud-room-id mapping via get_room_mapping RPC.
    bool fetchRooms(Room* rooms, int maxRooms, int& count, unsigned long timeoutMs = 10000);

    // Set vacuum suction power level.
    bool setFanPower(int power);

    // Set mop water flow mode.
    bool setWaterBoxMode(int mode);

    // Start segment-based cleaning for the given room IDs.
    bool startSegmentClean(const int* roomIds, int roomCount, int repeat = 1);

    // PubSubClient callback; public so the static trampoline can reach it.
    void onMessage(const char* topic, const uint8_t* data, size_t len);

private:
    static constexpr size_t RX_BUF_SIZE = 4096;

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

    bool _hasStatus = false;
    RobotStatus _lastStatus;

    int    _pendingRpcId = 0;
    bool   _hasRpc       = false;
    String _rpcResult;

    void parseMqttUrl(const String& url);
    void deriveCredentials();

    size_t buildRpc(uint8_t* buf, size_t maxLen, const String& method, const String& paramsJson);
    bool   parseMessage(const uint8_t* data, size_t len);
};
