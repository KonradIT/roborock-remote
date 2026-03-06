#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include "config_store.h"
#include "roborock_api.h"
#include "roborock_mqtt.h"
#include "roborock_crypto.h"

// Roborock local TCP client (port 58867, V1 protocol, AES-ECB encryption).
class RoborockLocal {
public:
    // Store device credentials and allocate the receive buffer.
    void configure(const RoborockConfig& cfg);

    // Open a TCP connection to the robot and perform the hello handshake.
    bool connect();

    // Close the TCP connection.
    void disconnect();

    // True if the TCP socket is still connected.
    bool isConnected();

    // Read available TCP data and send keep-alive pings; call from the main loop.
    void loop();

    // Send a JSON-RPC command over local TCP and return true if written.
    bool   sendRpc(const String& method, const String& paramsJson = "[]");

    // True once a response matching the last sendRpc arrives.
    bool   hasRpcResponse() const;

    // Consume and return the raw JSON result string.
    String takeRpcResult();

    // Send a get_status RPC request.
    bool requestStatus();

    // True when a new status update (RPC response or DPS push) is available.
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

private:
    static constexpr uint16_t PORT            = 58867;
    static constexpr size_t   RX_BUF_SIZE     = 4096;
    static constexpr unsigned long PING_MS     = 10000;
    static constexpr unsigned long CONNECT_TO  = 5000;
    static constexpr unsigned long HELLO_TO    = 5000;

    static constexpr uint16_t PROTO_HELLO_REQ    = 0;
    static constexpr uint16_t PROTO_HELLO_RSP    = 1;
    static constexpr uint16_t PROTO_PING_REQ     = 2;
    static constexpr uint16_t PROTO_PING_RSP     = 3;
    static constexpr uint16_t PROTO_GENERAL_REQ  = 4;
    static constexpr uint16_t PROTO_GENERAL_RSP  = 5;
    static constexpr uint16_t PROTO_RPC_REQ      = 101;
    static constexpr uint16_t PROTO_RPC_RSP      = 102;

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

    size_t  buildHeader(uint8_t* buf, uint16_t protocol, uint32_t seq, uint32_t rnd);
    size_t  buildRpc(uint8_t* buf, size_t maxLen, const String& method, const String& paramsJson);
    bool    sendPing();
    bool    doHello();

    bool   readMessage();
    bool   parseMessage(const uint8_t* data, size_t len);
};
