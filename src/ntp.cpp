#include "ntp.h"
#include "sdlog.h"
#include "wifi_mgr.h"
#include "config.h"
#include <time.h>

bool sincronizarNTP(uint8_t intentosMax, uint16_t tiempoEntreIntentos) {
  uint32_t t0 = millis();
  while (!wifiReady() && millis() - t0 < 5000) delay(100);
  if (!wifiReady()) {
    Serial.println("[NTP] No hay WiFi; omito sincronización.");
    logEventoM("NTP", "NTP_SKIP", "reason=no_wifi");
    return false;
  }

  configTime(config.ntp.gmtOffset, config.ntp.dstOffset, config.ntp.servidor.c_str());

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