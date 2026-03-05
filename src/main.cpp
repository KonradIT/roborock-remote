#include <M5Unified.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "config_store.h"
#include "roborock_api.h"
#include "roborock_mqtt.h"
#include "roborock_local.h"
#include "display_ui.h"
#include "serial_handler.h"

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static constexpr unsigned long REFRESH_MS          = 5UL * 60 * 1000;
static constexpr unsigned long WIFI_TIMEOUT        = 15000;
static constexpr unsigned long NTP_TIMEOUT         = 10000;
static constexpr unsigned long MQTT_STATUS_TIMEOUT = 8000;
static constexpr unsigned long CLEAN_UI_REFRESH_MS = 2000;

// ---------------------------------------------------------------------------
// Cleaning configuration tables
// ---------------------------------------------------------------------------
static const String MODE_NAMES[]    = {"Vac & Mop", "Vacuum", "Mop"};
static constexpr int MODE_COUNT     = 3;

static const String SUCTION_NAMES[] = {"Quiet", "Balanced", "Turbo", "Max"};
static const int    SUCTION_VALUES[]= {101, 102, 103, 104};
static constexpr int SUCTION_COUNT  = 4;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
enum class State {
    BOOT, NO_CONFIG, CONNECTING, SYNCING_TIME, DEVICE_CONNECT,
    FETCHING, SHOWING, ERR,
    LOAD_ROOMS, ROOM_SELECT, MODE_SELECT, SUCTION_SELECT,
    CONFIRM_CLEAN, CLEANING
};

static ConfigStore    store;
static RoborockConfig cfg;
static RoborockApi*   api = nullptr;
static RoborockMqtt   mqtt;
static RoborockLocal  local;
static DisplayUI      ui;
static SerialHandler  serial_handler;

static State          state        = State::BOOT;
static unsigned long  lastRefresh  = 0;
static unsigned long  stateEnterMs = 0;
static RobotStatus    lastStatus;
static bool           deviceReady   = false;
static bool           useLocal      = false;

// Cleaning flow
static Room           rooms[MAX_ROOMS];
static int            roomCount       = 0;
static int            selectedRoom    = 0;
static int            selectedMode    = 0;
static int            selectedSuction = 1; // Balanced
static unsigned long  cleanStartMs    = 0;
static unsigned long  lastCleanRedraw = 0;
static unsigned long  lastStatusPoll  = 0;

static void enterState(State s) {
    state = s;
    stateEnterMs = millis();
}

// ---------------------------------------------------------------------------
// WiFi / NTP helpers
// ---------------------------------------------------------------------------

static bool connectWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT)
        delay(250);
    return WiFi.status() == WL_CONNECTED;
}

static bool syncNtp() {
    configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
    unsigned long start = millis();
    while (time(nullptr) < 1000000000UL && millis() - start < NTP_TIMEOUT)
        delay(250);
    return time(nullptr) >= 1000000000UL;
}

// ---------------------------------------------------------------------------
// Transport abstraction — local TCP vs cloud MQTT
// ---------------------------------------------------------------------------

static bool devSendRpc(const String& method, const String& params = "[]") {
    return useLocal ? local.sendRpc(method, params) : mqtt.sendRpc(method, params);
}
static bool devHasRpc() {
    return useLocal ? local.hasRpcResponse() : mqtt.hasRpcResponse();
}
static String devTakeRpc() {
    return useLocal ? local.takeRpcResult() : mqtt.takeRpcResult();
}
static bool devRequestStatus() {
    return useLocal ? local.requestStatus() : mqtt.requestStatus();
}
static bool devHasNewStatus() {
    return useLocal ? local.hasNewStatus() : mqtt.hasNewStatus();
}
static RobotStatus devTakeStatus() {
    return useLocal ? local.takeStatus() : mqtt.takeStatus();
}
static bool devIsConnected() {
    return useLocal ? local.isConnected() : mqtt.isConnected();
}
static void devLoop() {
    if (useLocal) local.loop(); else if (deviceReady) mqtt.loop();
}
static bool devFetchRooms(Room* r, int max, int& cnt, unsigned long to = 10000) {
    return useLocal ? local.fetchRooms(r, max, cnt, to) : mqtt.fetchRooms(r, max, cnt, to);
}
static bool devSetFanPower(int p) {
    return useLocal ? local.setFanPower(p) : mqtt.setFanPower(p);
}
static bool devSetWaterBoxMode(int m) {
    return useLocal ? local.setWaterBoxMode(m) : mqtt.setWaterBoxMode(m);
}
static bool devStartSegmentClean(const int* ids, int cnt, int rep = 1) {
    return useLocal ? local.startSegmentClean(ids, cnt, rep) : mqtt.startSegmentClean(ids, cnt, rep);
}

// ---------------------------------------------------------------------------
// Room loading (cache → MQTT fallback)
// ---------------------------------------------------------------------------

static bool isNumeric(const String& s) {
    if (s.length() == 0) return true;
    for (unsigned int i = 0; i < s.length(); i++)
        if (!isDigit(s[i])) return false;
    return true;
}

static void fixRoomNames() {
    for (int i = 0; i < roomCount; i++) {
        if (rooms[i].name.isEmpty() || isNumeric(rooms[i].name))
            rooms[i].name = "Room " + String(rooms[i].id);
    }
}

static bool loadRoomsFromCache() {
    String cached = store.loadRooms();
    if (cached.length() == 0) return false;

    JsonDocument doc;
    if (deserializeJson(doc, cached)) return false;

    roomCount = 0;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant v : arr) {
        if (roomCount >= MAX_ROOMS) break;
        if (v.is<JsonObject>()) {
            rooms[roomCount].id   = v["id"].as<int>();
            rooms[roomCount].name = v["name"].as<String>();
            roomCount++;
        }
    }
    return roomCount > 0;
}

static void cacheRooms() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < roomCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"]   = rooms[i].id;
        obj["name"] = rooms[i].name;
    }
    String s;
    serializeJson(doc, s);
    store.saveRooms(s);
}

// Look up a cloud room ID in the homeData rooms JSON, return the human name.
// homeData format: [{"id":21148765,"name":"Master bedroom"},...]
// get_room_mapping returns cloud IDs as strings, homeData stores them as ints.
static String lookupRoomName(const String& cloudRoomId, JsonArray& homeRooms) {
    long targetId = cloudRoomId.toInt();
    for (JsonVariant v : homeRooms) {
        if (v.is<JsonObject>()) {
            long hid = v["id"].as<long>();
            if (hid == targetId) return v["name"].as<String>();
        }
    }
    return "";
}

// Fetch segment mapping, merge with homeData names, populate rooms[].
static bool fetchAndMergeRooms() {
    if (!deviceReady || !devIsConnected()) return false;

    Room mapped[MAX_ROOMS];
    int mappedCount = 0;
    if (!devFetchRooms(mapped, MAX_ROOMS, mappedCount, 10000) || mappedCount == 0)
        return false;

    // mapped[i].id   = segment_id  (e.g. 16)
    // mapped[i].name = cloud_room_id as string (e.g. "21148765")

    String homeJson = store.loadHomeRooms();
    JsonDocument homeDoc;
    bool hasHome = (homeJson.length() > 0 && !deserializeJson(homeDoc, homeJson));
    JsonArray homeArr = hasHome ? homeDoc.as<JsonArray>() : JsonArray();

    roomCount = 0;
    for (int i = 0; i < mappedCount && roomCount < MAX_ROOMS; i++) {
        rooms[roomCount].id = mapped[i].id; // segment ID for app_segment_clean

        String humanName;
        if (hasHome) humanName = lookupRoomName(mapped[i].name, homeArr);

        rooms[roomCount].name = humanName.length() > 0
            ? humanName
            : "Room " + String(mapped[i].id);
        roomCount++;
    }

    Serial.println("LOG: Merged " + String(roomCount) + " rooms (segments + homeData names)");
    return roomCount > 0;
}

// ---------------------------------------------------------------------------
// Selector display helpers
// ---------------------------------------------------------------------------

static void drawRoomSelector() {
    ui.showSelector("SELECT ROOM", rooms[selectedRoom].name,
                    selectedRoom, roomCount);
}

static void drawModeSelector() {
    ui.showSelector("SELECT MODE", MODE_NAMES[selectedMode],
                    selectedMode, MODE_COUNT);
}

static void drawSuctionSelector() {
    ui.showSelector("SUCTION POWER", SUCTION_NAMES[selectedSuction],
                    selectedSuction, SUCTION_COUNT);
}

static void drawConfirm() {
    String suction = (selectedMode == 2) ? "N/A" : SUCTION_NAMES[selectedSuction];
    ui.showCleanConfirm(rooms[selectedRoom].name,
                        MODE_NAMES[selectedMode], suction);
}

// ---------------------------------------------------------------------------
// State handlers — connectivity
// ---------------------------------------------------------------------------

static void onBoot() {
    store.begin();
    cfg = store.load();
    if (cfg.isValid()) {
        enterState(State::CONNECTING);
        ui.showConnecting(cfg.wifi_ssid);
    } else {
        enterState(State::NO_CONFIG);
        ui.showConfigMode();
        Serial.println("LOG: No config — send CONFIG_BEGIN over serial");
    }
}

static void onConnecting() {
    Serial.println("LOG: Connecting to WiFi...");
    if (connectWiFi()) {
        Serial.println("LOG: WiFi connected — " + WiFi.localIP().toString());
        enterState(State::SYNCING_TIME);
    } else {
        Serial.println("LOG: WiFi failed");
        ui.showMessage("ERROR", "WiFi failed");
        enterState(State::ERR);
    }
}

static void onSyncingTime() {
    Serial.println("LOG: Syncing NTP...");
    bool ntpOk = syncNtp();
    Serial.println(ntpOk ? "LOG: Time synced" : "LOG: NTP failed (continuing)");

    useLocal = cfg.localReady();
    ui.setMode(useLocal);
    if (useLocal || cfg.mqttReady()) {
        enterState(State::DEVICE_CONNECT);
    } else {
        Serial.println("LOG: No device creds, HTTP-only");
        deviceReady = false;
        enterState(State::FETCHING);
    }
}

static void onDeviceConnect() {
    if (useLocal) {
        Serial.println("LOG: Connecting local TCP to " + cfg.dev_ip + "...");
        ui.showMessage("LOCAL", "Connecting to\n" + cfg.dev_ip);
        local.configure(cfg);
        if (local.connect()) {
            deviceReady = true;
            lastStatus.online = true;
        } else {
            Serial.println("LOG: Local connect failed");
            deviceReady = false;
        }
    } else {
        Serial.println("LOG: Connecting cloud MQTT...");
        mqtt.configure(cfg);
        if (mqtt.connect()) {
            deviceReady = true;
            lastStatus.online = true;
        } else {
            Serial.println("LOG: MQTT failed, HTTP-only");
            deviceReady = false;
        }
    }
    enterState(State::FETCHING);
}

// ---------------------------------------------------------------------------
// State handlers — fetching & display
// ---------------------------------------------------------------------------

static void onFetching() {
    ui.showRefreshing();
    Serial.println("LOG: Fetching robot status...");
    lastRefresh = millis();

    if (deviceReady && devIsConnected()) {
        devRequestStatus();
        unsigned long start = millis();
        while (!devHasNewStatus() && millis() - start < MQTT_STATUS_TIMEOUT) {
            devLoop();
            delay(50);
        }
        if (devHasNewStatus()) {
            RobotStatus ms = devTakeStatus();
            lastStatus.name    = cfg.dev_name;
            lastStatus.online  = true;
            if (ms.battery >= 0) lastStatus.battery  = ms.battery;
            if (ms.state >= 0)   lastStatus.state    = ms.state;
            lastStatus.charging = ms.charging;
            lastStatus.valid    = true;
        }
    }

    if (!lastStatus.valid) {
        // Fall back to HTTP cloud API if available
        if (cfg.rriot_u.length() > 0) {
            if (!api) api = new RoborockApi(cfg);
            else      api->updateConfig(cfg);
            lastStatus = api->fetchHomeData();
        } else {
            lastStatus.name  = cfg.dev_name;
            lastStatus.error = "No status source";
        }
    }

    if (lastStatus.valid) {
        Serial.println("LOG: Status - bat=" + String(lastStatus.battery) +
                       " state=" + String(lastStatus.state));
        ui.showStatus(lastStatus);
        enterState(State::SHOWING);
    } else {
        Serial.println("LOG: Fetch error — " + lastStatus.error);
        ui.showStatus(lastStatus);
        enterState(State::ERR);
    }
}

static void onDisplay() {
    if (millis() - lastRefresh >= REFRESH_MS) {
        enterState(State::FETCHING);
        return;
    }
    if (M5.BtnA.wasPressed()) {
        enterState(State::LOAD_ROOMS);
        return;
    }
    if (M5.BtnB.wasPressed()) {
        enterState(State::FETCHING);
        return;
    }
    // Live push notifications
    if (deviceReady && devHasNewStatus()) {
        RobotStatus ms = devTakeStatus();
        if (ms.battery >= 0) lastStatus.battery  = ms.battery;
        if (ms.state >= 0)   lastStatus.state    = ms.state;
        lastStatus.charging = ms.charging;
        ui.showStatus(lastStatus);
    }
}

static void onError() {
    if (millis() - stateEnterMs >= REFRESH_MS || M5.BtnA.wasPressed()) {
        if (WiFi.status() == WL_CONNECTED)
            enterState(State::FETCHING);
        else {
            enterState(State::CONNECTING);
            ui.showConnecting(cfg.wifi_ssid);
        }
    }
}

// ---------------------------------------------------------------------------
// State handlers — cleaning flow
// ---------------------------------------------------------------------------

static bool loadRoomsFromHomeData() {
    String homeJson = store.loadHomeRooms();
    if (homeJson.length() == 0) return false;

    JsonDocument doc;
    if (deserializeJson(doc, homeJson)) return false;

    roomCount = 0;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant v : arr) {
        if (roomCount >= MAX_ROOMS) break;
        if (v.is<JsonObject>()) {
            rooms[roomCount].id   = v["id"].as<int>();
            rooms[roomCount].name = v["name"].as<String>();
            roomCount++;
        }
    }
    Serial.println("LOG: Loaded " + String(roomCount) + " rooms from homeData (cloud IDs)");
    return roomCount > 0;
}

static void onLoadRooms() {
    ui.showMessage("LOADING", "Fetching rooms...");

    if (!loadRoomsFromCache()) {
        if (fetchAndMergeRooms()) {
            cacheRooms();
        } else {
            // Last resort: use preloaded homeData rooms (cloud IDs, names)
            loadRoomsFromHomeData();
        }
    }

    fixRoomNames();

    if (roomCount > 0) {
        selectedRoom = 0;
        drawRoomSelector();
        enterState(State::ROOM_SELECT);
    } else {
        ui.showMessage("ERROR", "No rooms found");
        enterState(State::ERR);
    }
}

static void onRoomSelect() {
    if (M5.BtnB.wasPressed()) {
        selectedRoom = (selectedRoom + 1) % roomCount;
        drawRoomSelector();
    }
    if (M5.BtnA.wasPressed()) {
        selectedMode = 0;
        drawModeSelector();
        enterState(State::MODE_SELECT);
    }
}

static void onModeSelect() {
    if (M5.BtnB.wasPressed()) {
        selectedMode = (selectedMode + 1) % MODE_COUNT;
        drawModeSelector();
    }
    if (M5.BtnA.wasPressed()) {
        if (selectedMode == 2) {
            // Mop only → skip suction
            drawConfirm();
            enterState(State::CONFIRM_CLEAN);
        } else {
            selectedSuction = 1; // default Balanced
            drawSuctionSelector();
            enterState(State::SUCTION_SELECT);
        }
    }
}

static void onSuctionSelect() {
    if (M5.BtnB.wasPressed()) {
        selectedSuction = (selectedSuction + 1) % SUCTION_COUNT;
        drawSuctionSelector();
    }
    if (M5.BtnA.wasPressed()) {
        drawConfirm();
        enterState(State::CONFIRM_CLEAN);
    }
}

static void onConfirmClean() {
    if (M5.BtnB.wasPressed()) {
        // Cancel → back to status
        ui.showStatus(lastStatus);
        enterState(State::SHOWING);
        return;
    }
    if (M5.BtnA.wasPressed()) {
        ui.showMessage("STARTING", "Sending commands...");

        int fanPower = (selectedMode == 2) ? 101 : SUCTION_VALUES[selectedSuction];
        devSetFanPower(fanPower);
        delay(300);
        devLoop();

        int waterMode = (selectedMode == 1) ? 200 : 202;
        devSetWaterBoxMode(waterMode);
        delay(300);
        devLoop();

        int roomId = rooms[selectedRoom].id;
        devStartSegmentClean(&roomId, 1);

        lastStatus.cleanPercent = 0;
        cleanStartMs    = millis();
        lastCleanRedraw = 0;
        lastStatusPoll  = 0;
        enterState(State::CLEANING);
    }
}

static void onCleaning() {
    if (deviceReady && devHasNewStatus()) {
        RobotStatus ms = devTakeStatus();
        if (ms.battery >= 0)      lastStatus.battery      = ms.battery;
        if (ms.state >= 0)        lastStatus.state        = ms.state;
        if (ms.cleanPercent >= 0) lastStatus.cleanPercent  = ms.cleanPercent;
        lastStatus.charging = ms.charging;
        lastCleanRedraw = 0;
    }

    if (deviceReady && devIsConnected() && millis() - lastStatusPoll >= 15000) {
        devRequestStatus();
        lastStatusPoll = millis();
    }

    // Redraw periodically
    if (millis() - lastCleanRedraw >= CLEAN_UI_REFRESH_MS) {
        lastCleanRedraw = millis();
        ui.showCleaningProgress(lastStatus.cleanPercent, lastStatus.battery,
                                lastStatus.state, millis() - cleanStartMs);
    }

    // Auto-return when cleaning is done (charging or idle)
    if (lastStatus.state == 8 || lastStatus.state == 100 ||
        lastStatus.state == 2 || lastStatus.state == 3) {
        if (millis() - cleanStartMs > 10000) {
            // Only auto-return after 10s to avoid false triggers at start
            ui.showStatus(lastStatus);
            enterState(State::SHOWING);
            return;
        }
    }

    if (M5.BtnB.wasPressed()) {
        devSendRpc("app_charge");
        ui.showMessage("RETURNING", "Sending home...");
        delay(1000);
        ui.showStatus(lastStatus);
        enterState(State::SHOWING);
    }
}

// ---------------------------------------------------------------------------
// Config handling
// ---------------------------------------------------------------------------

static void handleNewConfig() {
    RoborockConfig oldCfg = cfg;
    cfg = serial_handler.takeConfig();
    store.save(cfg);

    String roomsJson = serial_handler.takeRoomsJson();
    if (roomsJson.length() > 0) {
        store.saveHomeRooms(roomsJson);
        // Only clear merged room cache if the device changed
        if (cfg.dev_duid != oldCfg.dev_duid) {
            store.clearRooms();
            Serial.println("LOG: Device changed — room cache cleared");
        }
        Serial.println("LOG: HomeData rooms saved (" + String(roomsJson.length()) + " bytes)");
    }

    Serial.println("LOG: Config saved — rebooting into connect");
    if (useLocal) local.disconnect(); else mqtt.disconnect();
    deviceReady = false;
    roomCount = 0;
    if (api) { delete api; api = nullptr; }
    enterState(State::CONNECTING);
    ui.showConnecting(cfg.wifi_ssid);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    auto m5cfg = M5.config();
    M5.begin(m5cfg);
    serial_handler.begin(115200);
    ui.begin();
    Serial.println("LOG: Roborock Remote booting...");
    enterState(State::BOOT);
}

void loop() {
    M5.update();
    serial_handler.process();
    devLoop();

    if (serial_handler.hasNewConfig()) {
        handleNewConfig();
        return;
    }

    switch (state) {
        case State::BOOT:           onBoot();           break;
        case State::NO_CONFIG:      /* wait */          break;
        case State::CONNECTING:     onConnecting();     break;
        case State::SYNCING_TIME:   onSyncingTime();    break;
        case State::DEVICE_CONNECT: onDeviceConnect();  break;
        case State::FETCHING:       onFetching();       break;
        case State::SHOWING:        onDisplay();        break;
        case State::ERR:            onError();          break;
        case State::LOAD_ROOMS:     onLoadRooms();      break;
        case State::ROOM_SELECT:    onRoomSelect();     break;
        case State::MODE_SELECT:    onModeSelect();     break;
        case State::SUCTION_SELECT: onSuctionSelect();  break;
        case State::CONFIRM_CLEAN:  onConfirmClean();   break;
        case State::CLEANING:       onCleaning();       break;
    }

    delay(20);
}
