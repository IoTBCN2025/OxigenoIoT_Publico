#include "api.h"
#include "sdlog.h"
#include "sdbackup.h"
#include <WiFi.h>
#include <HTTPClient.h>

// === Helpers de URL encoding (simple) ===
static String urlEncode(const String& s) {
  String out; out.reserve(s.length() * 3);
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (('a'<=c && c<='z') || ('A'<=c && c<='Z') || ('0'<=c && c<='9') ||
        c=='-' || c=='_' || c=='.' || c=='~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

const String API_ENDPOINT = "http://iotbcn.com/IoT/api.php";
const String API_KEY = "123456789ABCDEF";

static unsigned long ultimoLogWifi = 0;
static unsigned long ultimoLogFallo = 0;
static const unsigned long API_ERR_LOG_EVERY_MS = 30000; // 30 s

// Marcar MOD_UP(API) una sola vez al primer éxito
static bool g_apiUpLogged = false;

bool enviarDatoAPI(const String& measurement, const String& sensor, float valor, unsigned long long timestamp, const String& source) {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long ahora = millis();
    if (ahora - ultimoLogWifi > 10000) {
      logEventoM("API", "API_SKIP", "wifi=0");
      ultimoLogWifi = ahora;
    }
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  String mac = WiFi.macAddress(); mac.replace(":", "");

  String url = API_ENDPOINT +
               "?api_key="    + urlEncode(API_KEY) +
               "&measurement="+ urlEncode(measurement) +
               "&sensor="     + urlEncode(sensor) +
               "&valor="      + urlEncode(String(valor, 2)) +
               "&ts="         + urlEncode(String(timestamp)) +
               "&mac="        + urlEncode(mac) +
               "&source="     + urlEncode(source);

  http.setReuse(false);
  http.setTimeout(7000);
  http.begin(client, url);
  http.addHeader("Connection", "close");

  int httpCode = http.GET();
  String payload = http.getString();
  http.end();

  if (httpCode == 200 && payload.indexOf("OK") >= 0) {
    if (!g_apiUpLogged) {
      logEventoM("API", "MOD_UP", "endpoint=/IoT/api.php");
      g_apiUpLogged = true;
    }
    return true;
  }

  unsigned long ahora = millis();
  if (ahora - ultimoLogFallo > API_ERR_LOG_EVERY_MS) {
    String kv = String("http=") + String(httpCode) + ";resp=" + payload;
    logEventoM("API", "API_5XX", kv);
    ultimoLogFallo = ahora;
  }
  return false;
}
