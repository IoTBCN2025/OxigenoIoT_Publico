#include "wifi_mgr.h"
#include <WiFi.h>
#include "sdlog.h"   // LOGI/LOGW/LOGD + logSetWifiRssi

static String g_ssid, g_pass;
static volatile bool g_ready = false;
static uint32_t g_lastChangeMs = 0;
static uint32_t g_lastAttemptMs = 0;
static const uint32_t RETRY_MIN_MS   = 4000;   // backoff entre intentos
static const uint32_t STABILIZE_MS   = 2500;   // espera tras GOT_IP antes de usar red

// RSSI logging control
static int g_lastRssi = -127;
static int g_lastRssiLogged = -127;
static uint32_t g_lastRssiLogMs = 0;
static const uint32_t RSSI_LOG_EVERY_MS = 60000; // 60s for heartbeat-like RSSI
static const int RSSI_DELTA_DB = 5;              // log si cambia >=5 dB

// Throttle for repeated state logs
static uint32_t g_lastStateLogMs = 0;
static const uint32_t STATE_LOG_THROTTLE_MS = 5000;

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
      if (millis() - g_lastStateLogMs > STATE_LOG_THROTTLE_MS) {
        LOGI("WIFI_CONNECTING", "Iniciando STA", String("ssid=") + g_ssid);
        g_lastStateLogMs = millis();
      }
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      g_ready = true;
      g_lastChangeMs = millis();
      IPAddress ip = WiFi.localIP();
      LOGI("WIFI_GOT_IP", "IP adquirida", String("ip=") + ip.toString());
      // Actualiza RSSI contexto si disponible
      if (WiFi.status() == WL_CONNECTED) {
        g_lastRssi = WiFi.RSSI();
        logSetWifiRssi(g_lastRssi);
      }
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      g_ready = false;
      g_lastChangeMs = millis();
      if (millis() - g_lastStateLogMs > STATE_LOG_THROTTLE_MS) {
        LOGW("WIFI_LOST", "Conexión perdida", "");
        g_lastStateLogMs = millis();
      }
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
  WiFi.onEvent(onWiFiEvent); // firmas distintas, misma idea
#endif

  // Primer intento
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  g_lastAttemptMs = millis();
  LOGI("WIFI_CONNECTING", "Conectando", String("ssid=") + g_ssid);
}

bool wifiReady() {
  // además de flag, exigimos un tiempo de estabilización
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

int wifiLastRssi() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
}

void wifiLoop() {
  const uint32_t now = millis();

  // Actualiza RSSI y log si hay cambios significativos o cada 60s
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    g_lastRssi = rssi;
    logSetWifiRssi(rssi);

    bool bigDelta = (g_lastRssiLogged == -127) || (abs(rssi - g_lastRssiLogged) >= RSSI_DELTA_DB);
    bool periodic = (now - g_lastRssiLogMs > RSSI_LOG_EVERY_MS);

    if (bigDelta || periodic) {
      LOGD("WIFI_RSSI", "Cambio RSSI", String("rssi=") + String(rssi));
      g_lastRssiLogged = rssi;
      g_lastRssiLogMs = now;
    }
  } else {
    // Si estamos desconectados, anota -127 solo si hubo cambio
    if (g_lastRssiLogged != -127 && now - g_lastRssiLogMs > STATE_LOG_THROTTLE_MS) {
      g_lastRssiLogged = -127;
      g_lastRssiLogMs = now;
      logSetWifiRssi(-127);
      LOGD("WIFI_RSSI", "RSSI no disponible", "rssi=-127");
    }
  }

  // Si ya está OK, nada que hacer
  if (wifiReady()) return;

  // throttle de intentos para evitar spam
  if (now - g_lastAttemptMs < RETRY_MIN_MS) return;

  // Reintento suave
  if (WiFi.status() != WL_CONNECTED) {
    LOGI("WIFI_RETRY", "Reintentando", String("status=") + wifiStatusStr());
    if (WiFi.SSID().length() == 0) {
      WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    } else {
      WiFi.begin(); // reutiliza credenciales persistentes
    }
    g_lastAttemptMs = now;
  }
}
