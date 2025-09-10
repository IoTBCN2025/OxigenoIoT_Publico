#include "sdlog.h"
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <time.h>
#include <string.h>
#include "ntp.h"             // getTimestamp() en segundos (fallback)
#include "ds3231_time.h"     // getUnixSeconds(), getTimestampMicros()

#ifndef SD_CS_PIN
#define SD_CS_PIN 5
#endif

// ====================== Configuración v2 ======================
// Formato CSV v2: ts_iso,ts_us,level,mod,code,fsm,kv
// - level: ERROR/WARN/DEBUG/INFO (palabra completa)
// - mod: módulo emisor real (WIFI, NTP, API, RTC, SD_BACKUP, YF-S201, etc.)
// - code: evento (parámetro 2 de logEventoM)
// - fsm: "-" (placeholder)
// - kv: detalle (parámetro 3), saneado y truncado

// Cola circular RAM (anti-fragmentación)
static const uint16_t LOG_LINE_MAX = 240;   // tamaño máx. de línea CSV (evitar colisión con LINE_MAX de <limits.h>)
static const uint8_t  Q_SIZE       = 16;    // líneas en cola
static char q_buf[Q_SIZE][LOG_LINE_MAX];
static volatile uint8_t q_head = 0, q_tail = 0;

// Estado SD
static bool sd_ready = false;
static bool in_flush = false;

// Día actual para rotación implícita por nombre de archivo
static int curY = 0, curM = 0, curD = 0;

// Throttling/coalescing por (mod|code)
struct Rate {
  char  key[40];       // clave: "mod|code"
  uint32_t last_ms;
  uint16_t count;      // acumulados dentro de la ventana
};
static Rate rateTbl[8];           // 8 claves con throttling
static const uint32_t RATE_WIN_MS = 2000;  // ventana coalescing (2 s)

// ====================== Utilidades de tiempo ======================
static uint32_t ms_now() { return millis(); }

static unsigned long long ts_us_now() {
  unsigned long long ts = getTimestampMicros();          // DS3231 en µs
  if (ts != 0ULL && ts != 943920000000000ULL) return ts; // centinelas evitadas
  time_t t = getTimestamp();                              // NTP
  if (t > 0) return (unsigned long long)t * 1000000ULL;
  return (unsigned long long)millis() * 1000ULL;         // degradado
}

static void ts_iso_now(char* out, size_t n) {
  uint32_t s = getUnixSeconds();
  if (s == 0) { snprintf(out, n, "1970-01-01 00:00:00"); return; }
  time_t t = (time_t)s;
  struct tm* tmv = localtime(&t);
  if (!tmv) { snprintf(out, n, "1970-01-01 00:00:00"); return; }
  snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d",
           tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
           tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
}

static void current_ymd(int& y, int& m, int& d) {
  uint32_t s = getUnixSeconds();
  if (s == 0) { y = m = d = 0; return; }
  time_t t = (time_t)s;
  struct tm* tmv = localtime(&t);
  if (!tmv) { y = m = d = 0; return; }
  y = tmv->tm_year + 1900; m = tmv->tm_mon + 1; d = tmv->tm_mday;
}

// ====================== Helpers de archivo ======================
static void ensure_header(const char* path) {
  if (!SD.exists(path)) {
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.println("ts_iso,ts_us,level,mod,code,fsm,kv");
      f.close();
    }
  }
}

static void path_for_today(char* out, size_t n) {
  int y, m, d; current_ymd(y, m, d);
  if (y == 0) { snprintf(out, n, "/eventlog_unknown.csv"); return; }
  snprintf(out, n, "/eventlog_%04d.%02d.%02d.csv", y, m, d);
}

static void rotate_if_changed() {
  int y, m, d; current_ymd(y, m, d);
  if (y == 0) return; // sin fecha fiable no rotamos
  if (y != curY || m != curM || d != curD) {
    curY = y; curM = m; curD = d;
    // La cabecera se asegura en flush
  }
}

// ====================== Cola circular ======================
static void q_push(const char* line) {
  size_t L = strnlen(line, LOG_LINE_MAX - 1);
  strncpy(q_buf[q_head], line, LOG_LINE_MAX - 1);
  q_buf[q_head][ (L < LOG_LINE_MAX - 1) ? L : (LOG_LINE_MAX - 1) ] = '\0';
  q_head = (q_head + 1) % Q_SIZE;
  if (q_head == q_tail) { // overflow: descartamos la más antigua
    q_tail = (q_tail + 1) % Q_SIZE;
  }
}

static void flush_queue() {
  if (in_flush || !sd_ready || q_head == q_tail) return;
  in_flush = true;

  rotate_if_changed();

  char path[40]; path_for_today(path, sizeof(path));
  ensure_header(path);

  File f = SD.open(path, FILE_APPEND);
  if (!f) { in_flush = false; sd_ready = false; return; }

  while (q_tail != q_head) {
    f.println(q_buf[q_tail]);
    q_tail = (q_tail + 1) % Q_SIZE;
  }
  f.close();
  in_flush = false;
}

// ====================== Throttling / coalescing ======================
static bool throttle_hold_and_accumulate(const char* key, uint16_t& out_count) {
  // Busca/actualiza entrada de rate limit
  for (size_t i = 0; i < sizeof(rateTbl)/sizeof(rateTbl[0]); ++i) {
    Rate& r = rateTbl[i];
    if (strncmp(r.key, key, sizeof(r.key)) == 0) {
      uint32_t now = ms_now();
      if (now - r.last_ms < RATE_WIN_MS) {
        r.count++;
        out_count = r.count;
        return true;  // dentro de ventana → coalesce
      }
      // Ventana vencida → devolver último count y reiniciar
      out_count = r.count;
      r.count = 0;
      r.last_ms = now;
      return false;
    }
  }
  // No encontrada → usa slot 0 como reemplazo simple (LRU-lite)
  strncpy(rateTbl[0].key, key, sizeof(rateTbl[0].key) - 1);
  rateTbl[0].key[sizeof(rateTbl[0].key) - 1] = '\0';
  rateTbl[0].last_ms = ms_now();
  rateTbl[0].count = 0;
  out_count = 0;
  return false;
}

// Mapear el nombre de evento a nivel (palabra completa)
// Reglas: *_ERR / *ERROR / *FAIL           => ERROR
//         *WARN / *WARNING / RESPALDO      => WARN
//         *DEBUG                           => DEBUG
//         caso especial TS_INVALID_BACKUP  => WARN
//         por defecto                      => INFO
static const char* level_from_code_word(const char* code) {
  if (!code) return "INFO";

  // ERROR
  if (strstr(code, "ERROR") != nullptr) return "ERROR";
  if (strstr(code, "ERR")   != nullptr) return "ERROR";
  if (strstr(code, "FAIL")  != nullptr) return "ERROR";

  // WARN
  if (strstr(code, "WARNING")          != nullptr) return "WARN";
  if (strstr(code, "WARN")             != nullptr) return "WARN";
  if (strstr(code, "RESPALDO")         != nullptr) return "WARN";
  if (strstr(code, "TS_INVALID_BACKUP")!= nullptr) return "WARN";

  // DEBUG
  if (strstr(code, "DEBUG") != nullptr) return "DEBUG";

  // INFO
  return "INFO";
}

// Saneado del detalle para kv (sin comas, sin saltos, sin espacios grandes)
static void sanitize_kv(char* s) {
  for (char* p = s; *p; ++p) {
    if (*p == ',')  *p = '.';     // proteger CSV
    if (*p == '\n' || *p == '\r') *p = ' ';
  }
  // LINE_MAX ya protege de desbordes al construir la línea
}

// ====================== API pública (compat) ======================
void inicializarSD() {
  sd_ready = SD.begin(SD_CS_PIN);
  // Resetea día actual
  int y, m, d; current_ymd(y, m, d);
  curY = y; curM = m; curD = d;

  // Intentar volcar buffer si hay algo en cola de arranque
  flush_queue();

  // Mensaje informativo mínimo por Serial
  if (sd_ready) {
    Serial.println("SD inicializada correctamente (logger v2)");
  } else {
    Serial.println("SD no detectada (logger v2 en RAM/Serial)");
  }
}

// Nueva API con módulo explícito
void logEventoM(const String& mod, const String& evento, const String& detalle) {
  // Clave de throttling: "mod|code"
  char key[sizeof(((Rate*)0)->key)];
  snprintf(key, sizeof(key), "%.18s|%.18s", mod.c_str(), evento.c_str()); // limite defensivo

  uint16_t count = 0;
  bool hold = throttle_hold_and_accumulate(key, count);
  if (hold) {
    // Coalesce silencioso. Al vencer la ventana, se emitirá una línea con count acumulado.
    return;
  }

  char iso[24]; ts_iso_now(iso, sizeof(iso));
  unsigned long long us = ts_us_now();
  const char* lvl = level_from_code_word(evento.c_str());

  // kv: detalle saneado + si hubo acumulación previa, incluir "count=N"
  char kv[128];
  strncpy(kv, detalle.c_str(), sizeof(kv)-1);
  kv[sizeof(kv)-1] = '\0';
  sanitize_kv(kv);

  char kv2[160];
  if (count > 0) {
    snprintf(kv2, sizeof(kv2), "%s%scount=%u",
             kv, (kv[0] ? ";" : ""), (unsigned)count);
  } else {
    snprintf(kv2, sizeof(kv2), "%s", kv);
  }

  const char* fsm = "-";

  // Línea final CSV
  char line[LOG_LINE_MAX];
  snprintf(line, sizeof(line), "%s,%llu,%s,%s,%s,%s,%.120s",
           iso, us, lvl, mod.c_str(), evento.c_str(), fsm, kv2);

  // Encolar y volcar
  q_push(line);
  flush_queue();

  // Espejo en Serial sólo para WARN/ERROR (reduce ruido)
  if (strcmp(lvl, "ERROR") == 0 || strcmp(lvl, "WARN") == 0) {
    Serial.print("LOG "); Serial.print(lvl);
    Serial.print(" ["); Serial.print(mod); Serial.print("] ");
    Serial.print(evento);
    Serial.print(" -> "); Serial.println(kv2);
  }
}

// Compat: versión legacy sin módulo (se mantiene para fuentes antiguas)
// imprime mod="LEG"
//void logEvento(const String& codigo, const String& mensaje) {
//  logEventoM("LEG", codigo, mensaje);
//}

void reintentarLogsPendientes() {
  if (!sd_ready) {
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD no disponible en reintento (logger v2)");
      return;
    }
    sd_ready = true;
    Serial.println("SD montada en reintento (logger v2)");
  }

  // Asegura cabecera del archivo del día y vuelca cola
  char path[40]; path_for_today(path, sizeof(path));
  ensure_header(path);
  flush_queue();

  Serial.print("reintentarLogsPendientes(): listo: ");
  Serial.println(path);
}
