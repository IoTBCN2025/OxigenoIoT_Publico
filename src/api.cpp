
#include "api.h"
#include "sdlog.h"
#include "sdbackup.h"
#include <WiFi.h>
#include <HTTPClient.h>

const String API_ENDPOINT = "http://iotbcn.com/IoT/api.php";
const String API_KEY = "123456789ABCDEF";

static unsigned long ultimoLogError = 0;

bool enviarDatoAPI(const String& measurement, const String& sensor, float valor, unsigned long long timestamp, const String& source) {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long ahora = millis();
    if (ahora - ultimoLogError > 10000) {
      logEvento("API_ERR", "WiFi no conectado para envío a API");
      ultimoLogError = ahora;
    }
    return false;
  }

  HTTPClient http;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String url = API_ENDPOINT + "?api_key=" + API_KEY +
               "&measurement=" + measurement +
               "&sensor=" + sensor +
               "&valor=" + String(valor, 2) +
               "&ts=" + String(timestamp) +
               "&mac=" + mac +
               "&source=" + source;

  http.setTimeout(5000);
  http.begin(url);
  int httpCode = http.GET();
  String payload = http.getString();
  http.end();

  if (httpCode == 200 && payload.indexOf("OK") >= 0) {
    return true;
  }

  logEvento("API_ERR", "Fallo envío HTTP (" + String(httpCode) + "): " + payload);
  return false;
}
