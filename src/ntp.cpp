#include "ntp.h"
#include "sdlog.h"
#include "wifi_mgr.h"     // <--- usa el nuevo manager
#include <time.h>

const char* ntpServidor = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 2;  // GMT+2
const int daylightOffset_sec = 0;

bool sincronizarNTP(uint8_t intentosMax, uint16_t tiempoEntreIntentos) {
  // Espera breve a que haya WiFi (sin bloquear minutos)
  uint32_t t0 = millis();
  while (!wifiReady() && millis() - t0 < 5000) {
    delay(100);
  }
  if (!wifiReady()) {
    Serial.println("[NTP] No hay WiFi; omito sincronización.");
    logEventoM("NTP", "NTP_SKIP", "reason=no_wifi");
    return false;
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServidor);

  struct tm timeinfo;
  for (uint8_t i = 0; i < intentosMax; i++) {
    delay(tiempoEntreIntentos);
    if (getLocalTime(&timeinfo)) {
      Serial.println("Tiempo sincronizado:");
      Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
      logEventoM("NTP", "MOD_UP", "phase=sync");
      return true;
    }
  }

  Serial.println("[NTP] Fallo al sincronizar con NTP");
  logEventoM("NTP", "NTP_ERR", "err=sync_timeout");
  logEventoM("NTP", "MOD_FAIL", "err=sync_timeout");
  return false;
}

time_t getTimestamp() {
  return time(nullptr);
}
