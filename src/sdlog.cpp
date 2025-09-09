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
// CSV: ts_iso,ts_us,level,mod,code,fsm,kv
static const uint16_t LOG_LINE_MAX = 240;
static const uint8_t  Q_SIZE       = 16;
static char q_buf[Q_SIZE][LOG_LINE_MAX];
static volatile uint8_t q_head = 0, q_tail = 0;

static bool sd_ready = false;
static bool in_flush = false;
static int curY=0, curM=0, curD=0;

struct Rate {
  char  key[24];
  uint32_t last_ms;
  uint16_t count;
};
static Rate rateTbl[8];
static const uint32_t RATE_WIN_MS = 2000;

static uint32_t ms_now() { return millis(); }

static unsigned long long ts_us_now() {
  unsigned long long ts = getTimestampMicros();
  if (ts != 0ULL && ts != 943920000000000ULL) return ts;
  time_t t = getTimestamp();
  if (t > 0) return (unsigned long long)t * 1000000ULL;
  return (unsigned long long)millis() * 1000ULL;
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

static void ensure_header(const char* path) {
  if (!SD.exists(path)) {
    File f = SD.open(path, FILE_WRITE);
    if (f) { f.println("ts_iso,ts_us,level,mod,code,fsm,kv"); f.close(); }
  }
}

static void path_for_today(char* out, size_t n) {
  int y, m, d; current_ymd(y, m, d);
  if (y == 0) { snprintf(out, n, "/eventlog_unknown.csv"); return; }
  snprintf(out, n, "/eventlog_%04d.%02d.%02d.csv", y, m, d);
}

static void rotate_if_changed() {
  int y, m, d; current_ymd(y, m, d);
  if (y == 0) return;
  if (y != curY || m != curM || d != curD) { curY = y; curM = m; curD = d; }
}

static void q_push(const char* line) {
  size_t L = strnlen(line, LOG_LINE_MAX - 1);
  strncpy(q_buf[q_head], line, LOG_LINE_MAX - 1);
  q_buf[q_head][ (L < LOG_LINE_MAX - 1) ? L : (LOG_LINE_MAX - 1) ] = '\0';
  q_head = (q_head + 1) % Q_SIZE;
  if (q_head == q_tail) q_tail = (q_tail + 1) % Q_SIZE;
}

static void flush_queue() {
  if (in_flush || !sd_ready || q_head == q_tail) return;
  in_flush = true;
  rotate_if_changed();
  char path[40]; path_for_today(path, sizeof(path));
  ensure_header(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) { in_flush = false; sd_ready = false; return; }
  while (q_tail != q_head) { f.println(q_buf[q_tail]); q_tail = (q_tail + 1) % Q_SIZE; }
  f.close();
  in_flush = false;
}

static bool throttle_hold_and_accumulate(const char* key, uint16_t& out_count) {
  for (size_t i = 0; i < sizeof(rateTbl)/sizeof(rateTbl[0]); ++i) {
    Rate& r = rateTbl[i];
    if (strncmp(r.key, key, sizeof(r.key)) == 0) {
      uint32_t now = ms_now();
      if (now - r.last_ms < RATE_WIN_MS) { r.count++; out_count = r.count; return true; }
      out_count = r.count; r.count = 0; r.last_ms = now; return false;
    }
  }
  strncpy(rateTbl[0].key, key, sizeof(rateTbl[0].key) - 1);
  rateTbl[0].key[sizeof(rateTbl[0].key) - 1] = '\0';
  rateTbl[0].last_ms = ms_now();
  rateTbl[0].count = 0;
  out_count = 0;
  return false;
}

static const char* level_from_code(const char* code) {
  if (strstr(code, "ERR"))   return "E";
  if (strstr(code, "WARN"))  return "W";
  if (strstr(code, "DEBUG")) return "D";
  return "I";
}

static void sanitize_kv(char* s) {
  for (char* p = s; *p; ++p) {
    if (*p == ',')  *p = '.';
    if (*p == '\n' || *p == '\r') *p = ' ';
  }
}

// ===== API pública =====
void inicializarSD() {
  sd_ready = SD.begin(SD_CS_PIN);
  int y, m, d; current_ymd(y, m, d); curY = y; curM = m; curD = d;
  flush_queue();
  if (sd_ready) Serial.println("SD inicializada correctamente (logger v2)");
  else          Serial.println("SD no detectada (logger v2 en RAM/Serial)");
}

void logEventoM(const String& mod, const String& code, const String& detalle) {
  // Throttling por 'code' (si quieres por mod|code, concaténalos aquí)
  char key[24];
  strncpy(key, code.c_str(), sizeof(key)-1);
  key[sizeof(key)-1] = '\0';
  uint16_t count = 0;
  if (throttle_hold_and_accumulate(key, count)) return;

  char iso[24]; ts_iso_now(iso, sizeof(iso));
  unsigned long long us = ts_us_now();
  const char* lvl = level_from_code(code.c_str());

  char kv[128];
  strncpy(kv, detalle.c_str(), sizeof(kv)-1);
  kv[sizeof(kv)-1] = '\0';
  sanitize_kv(kv);

  char kv2[160];
  if (count > 0) snprintf(kv2, sizeof(kv2), "%s%scount=%u", kv, (kv[0] ? ";" : ""), (unsigned)count);
  else           snprintf(kv2, sizeof(kv2), "%s", kv);

  const char* fsm = "-";

  char line[LOG_LINE_MAX];
  snprintf(line, sizeof(line), "%s,%llu,%s,%s,%s,%s,%.120s",
           iso, us, lvl, mod.c_str(), code.c_str(), fsm, kv2);

  q_push(line);
  flush_queue();

  if (*lvl == 'E' || *lvl == 'W') {
    Serial.print("LOG "); Serial.print(lvl);
    Serial.print(" "); Serial.print(mod); Serial.print(":"); Serial.print(code);
    Serial.print(" -> "); Serial.println(kv2);
  }
}

// (Wrapper legacy se declara inline en el .h)

void reintentarLogsPendientes() {
  if (!sd_ready) {
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD no disponible en reintento (logger v2)");
      return;
    }
    sd_ready = true;
    Serial.println("SD montada en reintento (logger v2)");
  }
  char path[40]; path_for_today(path, sizeof(path));
  ensure_header(path);
  flush_queue();
  Serial.print("reintentarLogsPendientes(): listo: ");
  Serial.println(path);
}
