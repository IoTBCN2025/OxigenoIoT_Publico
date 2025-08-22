#include "ntp.h"
#include "sdlog.h"
#include "wifi_mgr.h"     // usa el nuevo manager
#include <time.h>

static const char* ntpServidor = "pool.ntp.org";
static const long gmtOffset_sec = 3600 * 2;  // GMT+2
static const int daylightOffset_sec = 0;

bool sincronizarNTP(uint8_t intentosMax, uint16_t tiempoEntreIntentos) {
  // Espera breve a que haya WiFi (sin bloquear minutos)
  const uint32_t t0 = millis();
  while (!wifiReady() && millis() - t0 < 5000) {
    delay(100);
  }
  if (!wifiReady()) {
    LOGI("NTP_SKIP", "No hay WiFi", "");
    return false;
  }

  LOGI("NTP_SYNC_START", "Iniciando sincronizacion NTP",
       String("server=") + ntpServidor +
       ";attempts=" + String(intentosMax) +
       ";delay_ms=" + String(tiempoEntreIntentos));

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServidor);

  struct tm timeinfo;
  for (uint8_t i = 0; i < intentosMax; i++) {
    const uint32_t tAttempt = millis();
    delay(tiempoEntreIntentos);

    if (getLocalTime(&timeinfo)) {
      time_t unixNow = time(nullptr);
      uint32_t lat = millis() - tAttempt; // latencia aproximada de intento
      LOGI("NTP_SYNC", "Sincronizacion OK",
           String("unix=") + String((uint32_t)unixNow) +
           ";attempt=" + String(i + 1) +
           ";lat_ms=" + String(lat));
      return true;
    } else {
      LOGW("NTP_TRY_FAIL", "Intento NTP sin exito",
           String("attempt=") + String(i + 1));
    }
  }

  LOGW("NTP_ERR", "Fallo al sincronizar con NTP",
       String("attempts=") + String(intentosMax));
  return false;
}

time_t getTimestamp() {
  return time(nullptr);
}
