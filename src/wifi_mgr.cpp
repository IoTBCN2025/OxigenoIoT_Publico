#include "wifi_mgr.h"
#include "config.h"
#include <WiFi.h>
#include "sdlog.h"

static String g_ssid, g_pass;
static volatile bool g_ready = false;
static uint32_t g_lastChangeMs = 0;
static uint32_t g_lastAttemptMs = 0;
static const uint32_t RETRY_MIN_MS = 4000;
static const uint32_t STABILIZE_MS = 2500;

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            WiFi.setAutoReconnect(true);
            WiFi.reconnect();
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            g_ready = true;
            g_lastChangeMs = millis();
            {
                char kv[128];
                snprintf(kv, sizeof(kv), "ip=%s;mac=%s;rssi=%d",
                         WiFi.localIP().toString().c_str(),
                         WiFi.macAddress().c_str(),
                         WiFi.RSSI());
                logEventoM("WIFI", "MOD_UP", kv);
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            g_ready = false;
            g_lastChangeMs = millis();
            logEventoM("WIFI", "MOD_FAIL", "event=disconnect");
            break;
        default: break;
    }
}

void wifiSetup(const char* ssid, const char* pass) {
    g_ssid = ssid;
    g_pass = pass;
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(onWiFiEvent);
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    g_lastAttemptMs = millis();
}

bool wifiReady() {
    if (g_ready && WiFi.status() == WL_CONNECTED) {
        return (millis() - g_lastChangeMs >= STABILIZE_MS);
    }
    return false;
}

void wifiLoop() {
    if (wifiReady()) return;
    const uint32_t now = millis();
    if (now - g_lastAttemptMs < RETRY_MIN_MS) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (WiFi.SSID().length() == 0) {
            WiFi.begin(g_ssid.c_str(), g_pass.c_str());
        } else {
            WiFi.begin();
        }
        g_lastAttemptMs = now;
    }
}