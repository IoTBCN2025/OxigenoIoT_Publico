#pragma once
#include <Arduino.h>

enum LogLevel { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

// Inicialización y reintento (compatibles con tu código)
void inicializarSD();
void reintentarLogsPendientes();

// === Contexto estático/dinámico para enriquecer cada línea ===
void logSetStaticContext(const String& mac, const String& fwVer);
void logSetState(const String& fsmState);
void logSetWifiRssi(int rssi);       // -127 si no aplica
void logSetHeapFree(size_t bytes);   // heap libre

uint32_t    logGetSeq();             // contador monotónico de eventos
const String& logGetBootId();        // id de sesión (arranque)

// === Logger extendido con niveles y código estable ===
void logEventoEx(LogLevel lvl,
                 const String& code,
                 const String& detail,
                 const String& kv = "");

// === Macros convenientes por nivel ===
#define LOGT(code,detail,kv) logEventoEx(LOG_TRACE, code, detail, kv)
#define LOGD(code,detail,kv) logEventoEx(LOG_DEBUG, code, detail, kv)
#define LOGI(code,detail,kv) logEventoEx(LOG_INFO,  code, detail, kv)
#define LOGW(code,detail,kv) logEventoEx(LOG_WARN,  code, detail, kv)
#define LOGE(code,detail,kv) logEventoEx(LOG_ERROR, code, detail, kv)

// === Compatibilidad: tu firma previa ===
// Se mapea a INFO con code = evento y detail = detalle, kv vacío
void logEvento(const String& evento, const String& detalle);
