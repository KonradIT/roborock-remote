#include "rc_control.h"
#include <M5Unified.h>

static constexpr unsigned long RC_SEND_MS = 300;
static constexpr float GYRO_DEAD_ZONE = 0.08f;
static constexpr float FILTER_ALPHA = 0.2f;

// 0-50% input: smooth/linear; 50-100%: exponentially tighter
static float applyExpo(float y) {
    float ay = fabsf(y);
    float sign = (y >= 0) ? 1.0f : -1.0f;
    float out;
    if (ay <= 0.5f) {
        out = ay;  // linear in lower half
    } else {
        float t = (ay - 0.5f) / 0.5f;
        float e = expf(2.0f * t);
        out = 0.5f + 0.5f * (e - 1.0f) / (expf(2.0f) - 1.0f);
    }
    return sign * out;
}

bool RcControl::start() {
    if (!_cb.sendRpc || !_cb.isConnected || !_cb.ui) return false;
    if (!_cb.isConnected()) return false;
    _seqNum = 0;
    _lastSend = 0;
    _filteredX = 0;
    _filteredY = 0;
    return _cb.sendRpc("app_rc_start", "[]");
}

void RcControl::end() {
    if (!_cb.sendRpc) return;
    _cb.sendRpc("app_rc_stop", "[]");
    _cb.sendRpc("app_rc_end", "[]");
}

bool RcControl::update(bool btnBPressed) {
    if (btnBPressed) return false;
    if (!_cb.sendRpc || !_cb.ui) return true;

    float ax = 0, ay = 0, az = 0;
    M5.Imu.getAccelData(&ax, &ay, &az);

    float rawX = -ay;
    float rawY = ax;

    if (fabsf(rawX) < GYRO_DEAD_ZONE) rawX = 0;
    if (fabsf(rawY) < GYRO_DEAD_ZONE) rawY = 0;

    rawX = (rawX < -1.0f) ? -1.0f : (rawX > 1.0f ? 1.0f : rawX);
    rawY = (rawY < -1.0f) ? -1.0f : (rawY > 1.0f ? 1.0f : rawY);

    _filteredX = FILTER_ALPHA * rawX + (1.0f - FILTER_ALPHA) * _filteredX;
    _filteredY = FILTER_ALPHA * rawY + (1.0f - FILTER_ALPHA) * _filteredY;

    float stickX = _filteredX;
    float stickY = applyExpo(_filteredY);

    if (_cb.ui) _cb.ui->showGyroControl(stickX, stickY);

    if (_cb.loop) _cb.loop();

    unsigned long now = millis();
    if (now - _lastSend >= RC_SEND_MS) {
        _lastSend = now;
        float velocity = stickY * 0.3f;
        float omega = stickX * -3.14159f;
        _seqNum++;

        char params[128];
        snprintf(params, sizeof(params),
                 "[{\"omega\":%.2f,\"velocity\":%.2f,\"duration\":%d,\"seqnum\":%d}]",
                 omega, velocity, (int)RC_SEND_MS, _seqNum);
        _cb.sendRpc("app_rc_move", String(params));
    }
    return true;
}
