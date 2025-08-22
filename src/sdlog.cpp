#include "sdlog.h"

#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <time.h>
#include <vector>

#include <esp_system.h> // esp_random()
#include "ntp.h"             // getTimestamp() (seg)
#include "ds3231_time.h"     // getUnixSeconds(), getTimestampMicros()

#ifndef SD_CS_PIN
#define SD_CS_PIN 5
#endif

// ================== Estado interno ==================
static bool g_sd_ready = false;
static String g_logFileName;
static int g_currentY = 0, g_currentM = 0, g_currentD = 0;
static std::vector<String> g_bufferRAM;

// Contexto de logging
static String   g_bootId;
static uint32_t g_seq = 0;
static String   g_mac = "000000000000";
static String   g_fw  = "unknown";
static String   g_state = "INIT";
static int      g_rssi  = -127;
static size_t   g_heap  = 0;

// Salud / métricas
static uint32_t g_io_err = 0, g_sd_remount = 0, g_ram_drop = 0;

// Deduplicación y rate-limit
struct Dedup {
  String key;
  uint32_t last_ms = 0;
  uint32_t count = 0;
};
static Dedup g_dedup;
static const uint32_t DEDUP_WINDOW_MS = 20000;  // Ventana donde comprimimos repetidos
static const uint32_t DEDUP_HINT_MS   = 5000;   // Emite hint cada ~5s si sigue repitiéndose

static uint32_t g_lastSecond = 0;
static uint16_t g_linesThisSecond = 0;
static const uint16_t MAX_LINES_PER_SECOND = 20; // evita tormenta

// Rotación por tamaño además de diaria
static const uint32_t MAX_LOG_BYTES = 10 * 1024 * 1024; // 10MB
static uint16_t g_part = 0;

// ------------------------ helpers ------------------------

static String lvlToStr(LogLevel l) {
  switch (l) {
    case LOG_TRACE: return "TRACE";
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    default:        return "ERROR";
  }
}

static String genBootId() {
  uint32_t r = (uint32_t)esp_random();
  char buf[9]; snprintf(buf, sizeof(buf), "%08X", (unsigned)r);
  return String(buf);
}

static time_t unix_to_time_t(uint32_t unixS) {
  return (time_t)unixS;
}

static uint32_t unix_actual_segundos() {
  // Prioriza DS3231 → NTP → time()
  uint32_t s = getUnixSeconds(); // DS3231
  if (s == 0) {
    time_t ntp_s = getTimestamp(); // NTP
    if (ntp_s > 0) s = (uint32_t)ntp_s;
  }
  if (s == 0) {
    time_t t = time(nullptr);
    if (t > 0) s = (uint32_t)t;
  }
  return s;
}

static unsigned long long timestamp_us_actual() {
  // DS3231 µs → NTP*1e6 → millis()*1000
  unsigned long long ts = getTimestampMicros();
  if (ts != 0ULL && ts != 943920000000000ULL) return ts;

  time_t ntp_s = getTimestamp();
  if (ntp_s > 0) return (unsigned long long)ntp_s * 1000000ULL;

  return (unsigned long long)millis() * 1000ULL;
}

static void formatearFechaYNombre(time_t t, String& outName, int& y, int& m, int& d) {
  struct tm* now = localtime(&t);
  if (!now) { // fallback
    outName = "/eventlog_unknown.csv";
    y = m = d = 0;
    return;
  }
  y = now->tm_year + 1900; m = now->tm_mon + 1; d = now->tm_mday;
  char nombre[48];
  snprintf(nombre, sizeof(nombre), "/eventlog_%04d.%02d.%02d.csv", y, m, d);
  outName = String(nombre);
}

static void asegurarCabeceraCSV(const String& path) {
  if (!SD.exists(path)) {
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      // Cabecera nueva
      f.println("iso8601,ts_us,level,code,state,detail,kv,uptime_ms,heap_free,wifi_rssi,boot_id,seq,mac,fw_ver");
      f.close();
    }
  }
}

static bool montarSD() {
  if (g_sd_ready) return true;
  if (SD.begin(SD_CS_PIN)) {
    g_sd_ready = true;
    g_sd_remount++;
  } else {
    g_sd_ready = false;
  }
  return g_sd_ready;
}

static void prepararArchivoDelDia() {
  uint32_t s = unix_actual_segundos();
  if (s == 0) {
    g_logFileName = "/eventlog_unknown.csv";
    asegurarCabeceraCSV(g_logFileName);
    g_currentY = g_currentM = g_currentD = 0;
    return;
  }
  time_t t = unix_to_time_t(s);
  formatearFechaYNombre(t, g_logFileName, g_currentY, g_currentM, g_currentD);
  asegurarCabeceraCSV(g_logFileName);
}

static void rotarSiCambioDeDia() {
  uint32_t s = unix_actual_segundos();
  if (s == 0) return;
  time_t t = unix_to_time_t(s);
  struct tm* now = localtime(&t);
  if (!now) return;
  int y = now->tm_year + 1900, m = now->tm_mon + 1, d = now->tm_mday;
  if (y != g_currentY || m != g_currentM || d != g_currentD) {
    String nuevo;
    formatearFechaYNombre(t, nuevo, g_currentY, g_currentM, g_currentD);
    g_logFileName = nuevo;
    g_part = 0; // reinicia partición al cambiar de día
    asegurarCabeceraCSV(g_logFileName);
  }
}

static void volcarBufferRAM() {
  if (!g_sd_ready || g_bufferRAM.empty()) return;
  // Seleccionar archivo efectivo con part si aplica
  String path = g_logFileName;
  if (g_part > 0) path.replace(".csv", String("_part") + String(g_part) + ".csv");

  File f = SD.open(path, FILE_APPEND);
  if (!f) { g_io_err++; return; }
  for (auto &ln : g_bufferRAM) f.println(ln);
  f.close();
  g_bufferRAM.clear();
}

// ================== API pública ==================

void logSetStaticContext(const String& mac, const String& fwVer){ g_mac=mac; g_fw=fwVer; }
void logSetState(const String& fsmState){ g_state=fsmState; }
void logSetWifiRssi(int rssi){ g_rssi=rssi; }
void logSetHeapFree(size_t bytes){ g_heap=bytes; }
uint32_t logGetSeq(){ return g_seq; }
const String& logGetBootId(){ return g_bootId; }

void inicializarSD() {
  if (!montarSD()) {
    Serial.println(F("SD no detectada (se loguea en RAM y Serial hasta que esté lista)"));
    // incluso sin SD generamos bootId para correlación
    g_bootId = genBootId();
    return;
  }
  Serial.println(F("SD inicializada correctamente"));
  prepararArchivoDelDia();
  g_bootId = genBootId();
  volcarBufferRAM();

  // Marca de arranque
  logEventoEx(LOG_INFO, "BOOT", "Inicio del dispositivo",
              String("sd=OK;remounts=") + String(g_sd_remount));
}

void reintentarLogsPendientes() {
  if (!g_sd_ready) {
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println(F("SD no disponible en reintento"));
      return;
    }
    g_sd_ready = true;
    g_sd_remount++;
    Serial.println(F("SD montada en reintento"));
  }

  if (g_logFileName.length() == 0) {
    prepararArchivoDelDia();
  }

  asegurarCabeceraCSV(g_logFileName);
  rotarSiCambioDeDia();

  // Tocar archivo para verificar acceso
  File f = SD.open(g_logFileName, FILE_APPEND);
  if (!f) {
    g_sd_ready = false;
    Serial.println(F("reintentarLogsPendientes(): fallo al abrir archivo; se reintentará"));
    return;
  }
  f.close();

  volcarBufferRAM();

  Serial.println(String(F("reintentarLogsPendientes(): SD lista, archivo: ")) + g_logFileName);
}

// === Compat: mapea a INFO con code=evento, detalle=detalle ===
void logEvento(const String& evento, const String& detalle) {
  // 1) Timestamp ISO (reutiliza buffers fijos)
  uint32_t s = unix_actual_segundos();
  char fecha[32] = "1970-01-01 00:00:00";
  if (s > 0) {
    time_t t = unix_to_time_t(s);
    struct tm* now = localtime(&t);
    if (now) {
      snprintf(fecha, sizeof(fecha), "%04d-%02d-%02d %02d:%02d:%02d",
               now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
               now->tm_hour, now->tm_min, now->tm_sec);
    }
  }

  // 2) µs actuales
  unsigned long long ts_us = timestamp_us_actual();

  // 3) Construcción sin explosión de temporales: reserva y concatena
  String detalle_ext;
  detalle_ext.reserve(detalle.length() + 24);
  detalle_ext = detalle;
  detalle_ext += " ts_us=";
  detalle_ext += String((uint32_t)(ts_us % 1000000ULL));

  String linea;
  linea.reserve(64 + evento.length() + detalle_ext.length());
  linea  = String(fecha);
  linea += ",";
  linea += evento;
  linea += ",";
  linea += detalle_ext;
  linea += ",";
  linea += String(ts_us);

  // 4) Si SD no está lista → buffer RAM + Serial con throttle
  static unsigned long s_lastRamLog = 0;
  if (!g_sd_ready) {
    g_bufferRAM.push_back(linea);
    if (millis() - s_lastRamLog > 500) {
      Serial.println("LOG(RAM): " + linea);
      s_lastRamLog = millis();
    }
    return;
  }

  // 5) Rotación diaria si cambió de día
  rotarSiCambioDeDia();

  // 6) Escritura segura + volcado de RAM si es posible
  File f = SD.open(g_logFileName, FILE_APPEND);
  if (!f) {
    g_sd_ready = false; // fuerza re-montaje
    g_bufferRAM.push_back(linea);
    // no hagas Serial largo aquí para no comer pila
    return;
  }
  f.println(linea);
  f.close();

  // 7) Vacía pendientes de RAM (si los hay)
  volcarBufferRAM();

  // 8) Evita prints largos en Serie en cada log (se queda sólo CSV)
  // Si quieres un latido: throttle opcional
  // static unsigned long s_lastInfo = 0;
  // if (millis() - s_lastInfo > 2000) {
  //   Serial.println("LOG(SD): " + evento);
  //   s_lastInfo = millis();
  // }
}

// === Logger central ===
void logEventoEx(LogLevel lvl, const String& code, const String& detail, const String& kv) {
  // Rate limit global por segundo
  uint32_t nowS = millis()/1000;
  if (nowS != g_lastSecond) { g_lastSecond = nowS; g_linesThisSecond = 0; }
  if (g_linesThisSecond++ > MAX_LINES_PER_SECOND) {
    // Silencio, pero sumemos drop RAM para saber que esto ocurrió (al emitir heartbeat)
    return;
  }

  // Timestamps
  uint32_t s = unix_actual_segundos();
  char iso[32] = "1970-01-01 00:00:00";
  if (s > 0) {
    time_t t = unix_to_time_t(s);
    struct tm* now = localtime(&t);
    if (now) snprintf(iso, sizeof(iso), "%04d-%02d-%02d %02d:%02d:%02d",
      now->tm_year+1900, now->tm_mon+1, now->tm_mday,
      now->tm_hour, now->tm_min, now->tm_sec);
  }
  unsigned long long ts_us = timestamp_us_actual();

  // Deduplicación simple por (level|code|state|detail)
  String key = String(lvlToStr(lvl)) + "|" + code + "|" + g_state + "|" + detail;
  uint32_t ms = millis();
  bool emitNow = true;
  if (g_dedup.key == key && (ms - g_dedup.last_ms) < DEDUP_WINDOW_MS) {
    g_dedup.count++;
    // Solo emite cada ~DEDUP_HINT_MS para representar que sigue ocurriendo
    emitNow = ((ms - g_dedup.last_ms) >= DEDUP_HINT_MS);
    if (emitNow) g_dedup.last_ms = ms;
  } else {
    // Si cerramos bloque repetido, emite resumen
    if (g_dedup.count > 0) {
      String kvD = String("dedup=flush;repeated=") + String(g_dedup.count);
      // Llamada recursiva controlada para resumen (INFO)
      // Evita la dedupe del propio resumen cambiando la clave (code=DEDUP)
      String prevKey = g_dedup.key; g_dedup.key = "#";
      logEventoEx(LOG_INFO, "DEDUP", "Resumen eventos repetidos", kvD);
      g_dedup.key = prevKey;
      g_dedup.count = 0;
    }
    g_dedup.key = key; g_dedup.last_ms = ms; g_dedup.count = 0;
  }

  if (!emitNow) return;

  // Construcción de línea CSV nueva
  String line = String(iso) + "," +
                String(ts_us) + "," +
                lvlToStr(lvl) + "," +
                code + "," +
                g_state + "," +
                detail + "," +
                (kv.length()?kv:"-") + "," +
                String(millis()) + "," +
                String((uint32_t)g_heap) + "," +
                String(g_rssi) + "," +
                g_bootId + "," +
                String(++g_seq) + "," +
                g_mac + "," +
                g_fw;

  // SD no lista → RAM
  if (!g_sd_ready) {
    if (g_bufferRAM.size() >= 512) { g_bufferRAM.erase(g_bufferRAM.begin()); g_ram_drop++; }
    g_bufferRAM.push_back(line);
    Serial.println("LOG(RAM): " + line);
    return;
  }

  // Rotación por día y tamaño
  rotarSiCambioDeDia();

  String path = g_logFileName;
  // Rotación por tamaño (partes)
  File cur = SD.open(path, FILE_READ);
  if (cur) {
    if ((uint32_t)cur.size() > MAX_LOG_BYTES) {
      g_part++;
    }
    cur.close();
  }
  if (g_part > 0) {
    path.replace(".csv", String("_part") + String(g_part) + ".csv");
  }
  asegurarCabeceraCSV(path);

  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    g_sd_ready = false; g_io_err++;
    Serial.println("LOG(FSERR): " + line);
    if (g_bufferRAM.size() >= 512) { g_bufferRAM.erase(g_bufferRAM.begin()); g_ram_drop++; }
    g_bufferRAM.push_back(line);
    return;
  }
  f.println(line);
  f.close();

  // Intentar vaciar buffer si lo hubiera
  volcarBufferRAM();
}
