#include "api.h"
#include "sdlog.h"
#include "sdbackup.h"

#include <WiFi.h>
#include <HTTPClient.h>

static const String API_ENDPOINT = "http://iotbcn.com/IoT/api.php";
static const String API_KEY      = "123456789ABCDEF";

// Pequeño throttle local para eventos de WiFi caído (el logger central también deduplica)
static unsigned long s_ultimoLogWifiDown = 0;
static const unsigned long WIFI_ERR_THROTTLE_MS = 10000;

// Trunca strings largos para no inyectar ruido excesivo en logs
static String truncar(const String& s, size_t maxlen) {
  if (s.length() <= maxlen) return s;
  return s.substring(0, maxlen);
}

ApiResult enviarDatoAPI_ex(const String& measurement,
                           const String& sensor,
                           float valor,
                           unsigned long long timestamp,
                           const String& source)
{
  ApiResult r{false, -1, 0, 0, ""};

  // WiFi previo
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long ahora = millis();
    if (ahora - s_ultimoLogWifiDown > WIFI_ERR_THROTTLE_MS) {
      LOGW("API_WIFI_DOWN", "WiFi no conectado para envío a API",
           String("m=") + measurement + ";s=" + sensor + ";src=" + source);
      s_ultimoLogWifiDown = ahora;
    }
    r.err = "wifi_down";
    return r;
  }

  // MAC compacta (sin ':') — tu API la aprovecha
  String mac = WiFi.macAddress();
  mac.replace(":", "");

  // Construir URL GET (manteniendo compatibilidad con tu API)
  // Nota: si en el futuro migras a POST, aquí es el lugar
  String url = API_ENDPOINT +
               "?api_key=" + API_KEY +
               "&measurement=" + measurement +
               "&sensor=" + sensor +
               "&valor=" + String(valor, 2) +
               "&ts=" + String(timestamp) +
               "&mac=" + mac +
               "&source=" + source;

  r.bytes_sent = url.length();

  HTTPClient http;
  http.setTimeout(5000);  // 5 s como pediste

  uint32_t t0 = millis();
  if (!http.begin(url)) {
    r.err = "begin_fail";
    LOGW("API_BEGIN_ERR", "HTTP begin() fallo", String("len=") + String(url.length()));
    return r;
  }

  int code = http.GET();         // si cambias a POST, ajusta aquí
  r.latency_ms = millis() - t0;
  r.http_code = code;

  String payload;
  if (code > 0) {
    // Solo intentamos leer payload si hubo respuesta válida
    payload = http.getString();
  }
  http.end();

  // Clasificación del resultado
  // Tu API generalmente responde 200 con "OK" (también aceptamos 204)
  if ((code == 200 && payload.indexOf("OK") >= 0) || code == 204) {
    r.ok = true;
    LOGI("API_OK", "Envio correcto",
         String("http=") + String(code) +
         ";lat=" + String(r.latency_ms) +
         ";bytes=" + String(r.bytes_sent) +
         ";m=" + measurement + ";s=" + sensor + ";src=" + source);
    return r;
  }

  // Fallos
  if (code <= 0) {
    r.err = "conn_or_timeout";
    LOGW("API_ERR", "HTTP fallo (conn/timeout)",
         String("code=") + String(code) +
         ";lat=" + String(r.latency_ms) +
         ";m=" + measurement + ";s=" + sensor + ";src=" + source);
    return r;
  }

  // HTTP no-OK
  r.err = String("http_") + String(code);
  LOGW("API_ERR", "HTTP no OK",
       String("http=") + String(code) +
       ";lat=" + String(r.latency_ms) +
       ";bytes=" + String(r.bytes_sent) +
       ";m=" + measurement + ";s=" + sensor + ";src=" + source +
       ";resp=" + truncar(payload, 60));
  return r;
}

// Compatibilidad con tu firma antigua
bool enviarDatoAPI(const String& measurement,
                   const String& sensor,
                   float valor,
                   unsigned long long timestamp,
                   const String& source)
{
  ApiResult r = enviarDatoAPI_ex(measurement, sensor, valor, timestamp, source);
  return r.ok;
}
