#include "ntp.h"
#include "sdlog.h"
#include <WiFi.h>
#include <time.h>

//Ajusta según tu red
const char* ssid     = "Jose Monje Ruiz";
const char* password = "QWERTYUI2022";

const char* ntpServidor = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 2;  // GMT+2
const int daylightOffset_sec = 0;

bool sincronizarNTP(uint8_t intentosMax, uint16_t tiempoEntreIntentos) {
  uint8_t intentos = 0;

  while (WiFi.status() != WL_CONNECTED && intentos < intentosMax) {
    Serial.printf("🔌 Intento %d/%d: Conectando a WiFi...\n", intentos + 1, intentosMax);
    WiFi.begin(ssid, password);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi conectado");
      Serial.print("IP local: ");
      Serial.println(WiFi.localIP());
      break;
    }

    Serial.println("\nFallo al conectar a WiFi");
    logEvento("WIFI_ERR", "Fallo intento de conexión WiFi");
    intentos++;
    delay(tiempoEntreIntentos);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se pudo conectar a WiFi después de múltiples intentos");
    return false;
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServidor);

  struct tm timeinfo;
  for (int i = 0; i < 10; i++) {
    delay(500);
    if (getLocalTime(&timeinfo)) {
      Serial.println("Tiempo sincronizado:");
      Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
      return true;
    }
  }

  Serial.println("Fallo al sincronizar con NTP");
  return false;
}

time_t getTimestamp() {
  return time(nullptr);
}
