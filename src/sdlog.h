#pragma once
#ifndef SDLOG_H
#define SDLOG_H

#include <Arduino.h>

// ========================= API DE CONTEXTO =========================
void logSetStaticContext(const String& mac_hex, const String& fw_ver); // (e.g., "e05a1be37348", "dev")
void logSetState(const char* state);
void logSetHeapFree(uint32_t heap_free);
void logSetWifiRssi(int rssi);

// ========================= CONTROL SD / FLUSH ======================
/**
 * Monta la tarjeta SD (SPI) y asegura que el fichero de log diario existe
 * con su cabecera. Idempotente: si ya está montada no hace nada.
 */
void inicializarSD();

/** Vuelve a intentar volcar al fichero las líneas que quedaron en RAM. */
void reintentarLogsPendientes();

// ========================= LOGGING ================================
enum LogLevel : uint8_t { LOG_DEBUG=0, LOG_INFO=1, LOG_WARN=2, LOG_ERROR=3 };

/** Emite una línea de log (overloads para const char* y String). */
void sdlog_emit(LogLevel lvl, const char* code, const char* state, const char* detail, const char* kv);
void sdlog_emit(LogLevel lvl, const char* code, const char* state, const char* detail, const String& kv);

// Atajos tipo macro
#define LOGD(code, detail, kv) sdlog_emit(LOG_DEBUG, (code), log__state(), (detail), (kv))
#define LOGI(code, detail, kv) sdlog_emit(LOG_INFO,  (code), log__state(), (detail), (kv))
#define LOGW(code, detail, kv) sdlog_emit(LOG_WARN,  (code), log__state(), (detail), (kv))
#define LOGE(code, detail, kv) sdlog_emit(LOG_ERROR, (code), log__state(), (detail), (kv))

// Exponer estado actual (solo para macros)
const char* log__state();

#endif // SDLOG_H
