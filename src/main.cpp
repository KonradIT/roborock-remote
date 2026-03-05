#include <M5Unified.h>
#include <WiFi.h>

#include "config_store.h"
#include "roborock_api.h"
#include "roborock_mqtt.h"
#include "display_ui.h"
#include "serial_handler.h"

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static constexpr unsigned long REFRESH_MS     = 5UL * 60 * 1000;
static constexpr unsigned long WIFI_TIMEOUT   = 15000;
static constexpr unsigned long NTP_TIMEOUT    = 10000;
static constexpr unsigned long MQTT_STATUS_TIMEOUT = 8000;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
enum class State { BOOT, NO_CONFIG, CONNECTING, SYNCING_TIME, MQTT_CONNECT, FETCHING, SHOWING, ERR };

static ConfigStore    store;
static RoborockConfig cfg;
static RoborockApi*   api = nullptr;
static RoborockMqtt   mqtt;
static DisplayUI      ui;
static SerialHandler  serial_handler;

static State          state        = State::BOOT;
static unsigned long  lastRefresh  = 0;
static unsigned long  stateEnterMs = 0;
static RobotStatus    lastStatus;
static bool           mqttAvailable = false;

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
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

static bool syncNtp() {
    configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
    unsigned long start = millis();
    while (time(nullptr) < 1000000000UL && millis() - start < NTP_TIMEOUT) {
        delay(250);
    }
    return time(nullptr) >= 1000000000UL;
}

// ---------------------------------------------------------------------------
// State handlers
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
    syncNtp();
    Serial.println("LOG: Time synced");

    if (cfg.mqttReady()) {
        enterState(State::MQTT_CONNECT);
    } else {
        Serial.println("LOG: MQTT credentials missing, skipping");
        mqttAvailable = false;
        enterState(State::FETCHING);
    }
}

static void onMqttConnect() {
    Serial.println("LOG: Connecting MQTT...");
    mqtt.configure(cfg);
    if (mqtt.connect()) {
        mqttAvailable = true;
    } else {
        Serial.println("LOG: MQTT failed, continuing HTTP-only");
        mqttAvailable = false;
    }
    enterState(State::FETCHING);
}

static void onFetching() {
    ui.showRefreshing();
    Serial.println("LOG: Fetching robot status...");

    if (!api) api = new RoborockApi(cfg);
    else      api->updateConfig(cfg);

    lastStatus  = api->fetchHomeData();
    lastRefresh = millis();

    // Enrich with MQTT real-time data
    if (mqttAvailable && mqtt.isConnected()) {
        mqtt.requestStatus();
        unsigned long start = millis();
        while (!mqtt.hasNewStatus() && millis() - start < MQTT_STATUS_TIMEOUT) {
            mqtt.loop();
            delay(50);
        }
        if (mqtt.hasNewStatus()) {
            RobotStatus ms = mqtt.takeStatus();
            if (ms.battery >= 0) lastStatus.battery  = ms.battery;
            if (ms.state >= 0)   lastStatus.state    = ms.state;
            lastStatus.charging = ms.charging;
        }
    }

    if (lastStatus.valid) {
        Serial.println("LOG: Status — bat=" + String(lastStatus.battery) +
                       " state=" + String(lastStatus.state) +
                       " online=" + String(lastStatus.online));
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
        enterState(State::FETCHING);
    }
    // Process any push notifications from the device
    if (mqttAvailable && mqtt.hasNewStatus()) {
        RobotStatus ms = mqtt.takeStatus();
        if (ms.battery >= 0) lastStatus.battery  = ms.battery;
        if (ms.state >= 0)   lastStatus.state    = ms.state;
        lastStatus.charging = ms.charging;
        ui.showStatus(lastStatus);
    }
}

static void onError() {
    if (millis() - stateEnterMs >= REFRESH_MS || M5.BtnA.wasPressed()) {
        if (WiFi.status() == WL_CONNECTED) {
            enterState(State::FETCHING);
        } else {
            enterState(State::CONNECTING);
            ui.showConnecting(cfg.wifi_ssid);
        }
    }
}

static void handleNewConfig() {
    cfg = serial_handler.takeConfig();
    store.save(cfg);
    Serial.println("LOG: Config saved — rebooting into connect");

    mqtt.disconnect();
    mqttAvailable = false;
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
    if (mqttAvailable) mqtt.loop();

    if (serial_handler.hasNewConfig()) {
        handleNewConfig();
        return;
    }

    switch (state) {
        case State::BOOT:         onBoot();         break;
        case State::NO_CONFIG:    /* wait */        break;
        case State::CONNECTING:   onConnecting();   break;
        case State::SYNCING_TIME: onSyncingTime();  break;
        case State::MQTT_CONNECT: onMqttConnect();  break;
        case State::FETCHING:     onFetching();     break;
        case State::SHOWING:      onDisplay();      break;
        case State::ERR:          onError();        break;
    }

    delay(20);
}
