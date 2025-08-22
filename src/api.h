#ifndef API_H
#define API_H

#include <Arduino.h>

// === Resultado extendido para trazabilidad y métricas ===
struct ApiResult {
  bool ok;               // éxito lógico del envío
  int http_code;         // código HTTP (o <=0 en fallo de conexión/timeout)
  uint32_t latency_ms;   // latencia total del request
  size_t bytes_sent;     // bytes aproximados del payload/URL
  String err;            // razón corta si falla (begin_fail, conn_or_timeout, http_XXX)
};

// Versión extendida con métricas (recomendada)
ApiResult enviarDatoAPI_ex(const String& measurement,
                           const String& sensor,
                           float valor,
                           unsigned long long timestamp,
                           const String& source);

// Compatibilidad: firma antigua que retorna solo bool
bool enviarDatoAPI(const String& measurement,
                   const String& sensor,
                   float valor,
                   unsigned long long timestamp,
                   const String& source);

#endif
