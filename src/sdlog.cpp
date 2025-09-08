#include "sdlog.h"
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "ds3231_time.h" // getUnixSeconds(), getTimestampMicros()

// ========================= PINES SD (ajusta si tu HW cambia) ======
#ifndef SD_CS
  #define SD_CS   5
#endif
#ifndef SD_SCK
  #define SD_SCK  18
#endif
#ifndef SD_MISO
  #define SD_MISO 19
#endif
#ifndef SD_MOSI
  #define SD_MOSI 23
#endif

// ========================= CONFIGURACIÓN =========================
static const uint16_t LOG_RING_CAP = 128;      // tamaño ring RAM
static const uint16_t LOG_LINE_MAX  = 320;     // bytes por línea formateada
static const uint16_t PATH_MAXLEN   = 48;

static const char* LOG_HEADER =
  "iso8601,ts_us,level,code,state,detail,kv,uptime_ms,heap_free,wifi_rssi,boot_id,seq,mac,fw_ver";

// Nivel → texto
static inline const char* lvl2txt(LogLevel l) {
  switch (l) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    default:        return "ERROR";
  }
}

// ========================= ESTADO GLOBAL =========================
static volatile bool  g_logging_in_progress = false; // anti‑reentrancia
static bool           g_sd_ok = false;
static char           g_log_path[PATH_MAXLEN] = {0};

static char           g_state[24] = "INIT";
static char           g_mac[16]   = "000000000000";
static char           g_fw[16]    = "unknown";
static uint32_t       g_boot_id   = 0;
static uint32_t       g_seq       = 0;
static uint32_t       g_heap_free = 0;
static int            g_wifi_rssi = -127;

static SPIClass*      g_spi = nullptr;         // SPI para la SD (VSPI)

// Ring buffer RAM
struct LogSlot {
  char line[LOG_LINE_MAX];
  bool used;
};
static LogSlot g_ring[LOG_RING_CAP];
static uint16_t g_r_head = 0, g_r_tail = 0;

// ========================= HELPERS ===============================
static inline bool ring_empty() { return g_r_head == g_r_tail; }
static inline bool ring_full()  { return (uint16_t)(g_r_head + 1) % LOG_RING_CAP == g_r_tail; }

static void ring_push(const char* s) {
  uint16_t nxt = (uint16_t)(g_r_head + 1) % LOG_RING_CAP;
  if (nxt == g_r_tail) { // full → descarta el más antiguo
    g_r_tail = (uint16_t)(g_r_tail + 1) % LOG_RING_CAP;
  }
  LogSlot& slot = g_ring[g_r_head];
  memset(slot.line, 0, sizeof(slot.line));
  strncpy(slot.line, s, sizeof(slot.line)-1);
  slot.used = true;
  g_r_head = nxt;
}

static bool ring_pop(char* out, size_t outlen) {
  if (ring_empty()) return false;
  LogSlot& slot = g_ring[g_r_tail];
  if (!slot.used) return false;
  strncpy(out, slot.line, outlen-1);
  out[outlen-1] = 0;
  slot.used = false;
  g_r_tail = (uint16_t)(g_r_tail + 1) % LOG_RING_CAP;
  return true;
}

static inline bool isValidEpoch(uint32_t s) { return s >= 946684800UL; } // >= 2000-01-01

static void format_iso8601(uint32_t unixS, char* buf, size_t n) {
  if (!isValidEpoch(unixS)) {
    snprintf(buf, n, "1970-01-01 00:00:00");
    return;
  }
  time_t t = (time_t)unixS;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);
  snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
           tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
}

static void ensure_daily_path() {
  // /eventlog_YYYY.MM.DD.csv (UTC)
  uint32_t s = getUnixSeconds();
  if (!isValidEpoch(s)) {
    strncpy(g_log_path, "/eventlog_unsync.csv", sizeof(g_log_path)-1);
    return;
  }
  time_t t = (time_t)s;
  struct tm tm_utc; gmtime_r(&t, &tm_utc);
  snprintf(g_log_path, sizeof(g_log_path),
           "/eventlog_%04d.%02d.%02d.csv",
           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday);
}

static void ensure_header_if_new(File& f) {
  if (!f) return;
  if (f.size() == 0) {
    f.println(LOG_HEADER);
    f.flush();
  }
}

// -------------------- MONTAJE SD --------------------
static bool sd_mount_if_needed() {
  if (g_sd_ok) return true;

  // Inicializa SPI (VSPI) y monta SD
  if (!g_spi) {
    g_spi = new SPIClass(VSPI);
    g_spi->begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  }

  // Importante: usar la instancia SPI creada
  if (!SD.begin(SD_CS, *g_spi)) {
    g_sd_ok = false;
    return false;
  }

  g_sd_ok = true;
  return true;
}

// ========================= INTERFAZ CONTEXTO ======================
void logSetStaticContext(const String& mac_hex, const String& fw_ver) {
  strncpy(g_mac, mac_hex.c_str(), sizeof(g_mac)-1);
  strncpy(g_fw,  fw_ver.c_str(),  sizeof(g_fw)-1);
  // boot_id simple: xor de MAC con millis inicial
  uint32_t m = 0;
  for (size_t i=0;i<strlen(g_mac);++i) m = (m<<5) ^ (uint8_t)g_mac[i] ^ (m>>2);
  g_boot_id = m ^ (uint32_t)millis();
}

void logSetState(const char* state) {
  if (!state) return;
  strncpy(g_state, state, sizeof(g_state)-1);
}
const char* log__state() { return g_state; }

void logSetHeapFree(uint32_t heap_free) { g_heap_free = heap_free; }
void logSetWifiRssi(int rssi) { g_wifi_rssi = rssi; }

// ========================= SD CONTROL =============================
void inicializarSD() {
  // 1) Montar SD (idempotente)
  if (!sd_mount_if_needed()) {
    // No spameamos aquí; los LOG* del resto del sistema ya reportan el reintento.
    g_sd_ok = false;
    return;
  }

  // 2) Asegurar fichero diario y cabecera
  ensure_daily_path();
  File f = SD.open(g_log_path, FILE_APPEND);
  if (!f) {
    f = SD.open(g_log_path, FILE_WRITE);
    if (f) f.seek(f.size());
  }
  if (!f) {
    g_sd_ok = false;
    return;
  }
  ensure_header_if_new(f);
  f.flush(); f.close();

  // 3) Intentar volcar lo pendiente
  reintentarLogsPendientes();
}

void reintentarLogsPendientes() {
  if (!sd_mount_if_needed()) return;
  ensure_daily_path();

  File f = SD.open(g_log_path, FILE_APPEND);
  if (!f) {
    f = SD.open(g_log_path, FILE_WRITE);
    if (f) f.seek(f.size());
  }
  if (!f) { g_sd_ok = false; return; }
  ensure_header_if_new(f);

  char line[LOG_LINE_MAX];
  uint16_t drained = 0;
  while (ring_pop(line, sizeof(line))) {
    f.println(line);
    ++drained;
    if ((drained % 16) == 0) { f.flush(); delay(1); }
  }
  f.flush();
  f.close();
}

// ========================= EMISIÓN ================================
static void sdlog_emit_impl(LogLevel lvl, const char* code, const char* state, const char* detail, const char* kv_cstr) {
  if (g_logging_in_progress) return;
  g_logging_in_progress = true;

  // --- 1) Captura de tiempo y campos
  uint32_t unixS = getUnixSeconds();
  unsigned long long ts_us = getTimestampMicros();  // si es 0, lo aceptamos
  char iso[24]; format_iso8601(unixS, iso, sizeof(iso));

  const char* p_code   = code   ? code   : "-";
  const char* p_state  = state  ? state  : "-";
  const char* p_detail = detail ? detail : "-";
  const char* p_kv     = kv_cstr ? kv_cstr : "";

  // --- 2) Formateo CSV en buffer plano
  char line[LOG_LINE_MAX];
  int n = snprintf(line, sizeof(line),
                   "%s,%llu,%s,%s,%s,%s,%s,%lu,%lu,%d,%lu,%lu,%s,%s",
                   iso,
                   (unsigned long long)ts_us,
                   lvl2txt(lvl),
                   p_code,
                   p_state,
                   p_detail,
                   p_kv,
                   (unsigned long)millis(),
                   (unsigned long)g_heap_free,
                   g_wifi_rssi,
                   (unsigned long)g_boot_id,
                   (unsigned long)++g_seq,
                   g_mac,
                   g_fw);
  if (n <= 0 || n >= (int)sizeof(line)) {
    line[sizeof(line)-2] = '\0';
  }

  // --- 3) Intento SD si está OK; si no, ring + Serial (LOG(RAM))
  bool wrote = false;
  if (sd_mount_if_needed()) {
    ensure_daily_path();
    File f = SD.open(g_log_path, FILE_APPEND);
    if (!f) {
      f = SD.open(g_log_path, FILE_WRITE);
      if (f) f.seek(f.size());
    }
    if (f) {
      ensure_header_if_new(f);
      if (f.println(line) > 0) wrote = true;
      f.flush();
      f.close();
    } else {
      g_sd_ok = false; // degradar a RAM
    }
  }

  if (!wrote) {
    Serial.print(F("LOG(RAM): "));
    Serial.println(line);
    ring_push(line);
  }

  g_logging_in_progress = false;
}

void sdlog_emit(LogLevel lvl, const char* code, const char* state, const char* detail, const char* kv) {
  sdlog_emit_impl(lvl, code, state, detail, kv);
}

void sdlog_emit(LogLevel lvl, const char* code, const char* state, const char* detail, const String& kv) {
  sdlog_emit_impl(lvl, code, state, detail, kv.c_str());
}
