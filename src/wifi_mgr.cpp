#include "wifi_mgr.h"
#include <WiFi.h>

static String g_ssid, g_pass;
static volatile bool g_ready = false;
static uint32_t g_lastChangeMs = 0;
static uint32_t g_lastAttemptMs = 0;
static const uint32_t RETRY_MIN_MS = 4000;   // backoff entre intentos
static const uint32_t STABILIZE_MS = 2500;   // espera tras GOT_IP antes de usar red

// --- Callback de eventos (UNA sola definición en todo el proyecto)
static void onWiFiEvent(
#if ARDUINO_ESP32_MAJOR >= 2
  WiFiEvent_t event, WiFiEventInfo_t info
#else
  WiFiEvent_t event
#endif
) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      WiFi.setAutoReconnect(true);
      WiFi.reconnect();
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_ready = true;
      g_lastChangeMs = millis();
      Serial.printf("[WiFi] GOT_IP %s\n", WiFi.localIP().toString().c_str());
      break;

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_ready = false;
      g_lastChangeMs = millis();
      Serial.println("[WiFi] DISCONNECTED");
      // no bloqueamos aquí; el watchdog decide cuándo reintentar
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
#if ARDUINO_ESP32_MAJOR >= 2
  WiFi.onEvent(onWiFiEvent);
#else
  WiFi.onEvent(onWiFiEvent); // firmas distintas, pero misma idea
#endif
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  g_lastAttemptMs = millis();
}

bool wifiReady() {
  // además de flag, exigimos que haya pasado un pequeño tiempo de estabilización
  if (g_ready && WiFi.status() == WL_CONNECTED) {
    if (millis() - g_lastChangeMs >= STABILIZE_MS) return true;
  }
  return false;
}

const char* wifiStatusStr() {
  wl_status_t s = WiFi.status();
  switch (s) {
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_IDLE_STATUS: return "WL_IDLE";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
    default: return "WL_UNKNOWN";
  }
}

void wifiLoop() {
  // Si ya está OK, nada que hacer
  if (wifiReady()) return;

  // throttle de intentos para evitar spam de "Host is unreachable"
  const uint32_t now = millis();
  if (now - g_lastAttemptMs < RETRY_MIN_MS) return;

  // Reintento suave
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WiFi] Reintentando... status=%s\n", wifiStatusStr());
    if (WiFi.SSID().length() == 0) {
      WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    } else {
      WiFi.begin(); // reutiliza credenciales persistentes
    }
    g_lastAttemptMs = now;
  }
}
