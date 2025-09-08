// sdbackup.cpp (forense + índice de pendientes)
// - LOGs estructurados (una sola línea BACKUP_STORE, con reason=...)
// - Menor presión de heap/pila: snprintf + buffers locales
// - Manejo de E/S robusto con fallback WRITE+seek
// - Mantenimiento de /pendientes.idx (sin duplicados) al crear PENDIENTE

#include "sdbackup.h"
#include "sdlog.h"
#include "ds3231_time.h"   // getUnixSeconds(), rtcIsPresent(), rtcIsTimeValid()
#include <SD.h>
#include <SPI.h>
#include <time.h>

// Pines SD (ajusta según tu hardware si no usas SD.begin por defecto)
#define SD_CS   5
#define SCK     18
#define MISO    19
#define MOSI    23

// ================== Helpers ==================
static inline String ensureRootSlash(const String& p) {
  if (p.length() == 0) return "/";
  return (p[0] == '/') ? p : ("/" + p);
}

static bool fileEnsureHeader(const String& path, const char* header) {
  if (SD.exists(path)) return true;
  File f = SD.open(path, FILE_WRITE);   // crear
  if (!f) return false;
  f.println(header);
  f.flush();
  f.close();
  return true;
}

// Añade una línea al índice de pendientes si no existe ya.
// Formato: una ruta por línea (p. ej. "/backup_20250821.csv").
static void addToPendientesIdx(const String& path) {
  static const char* IDX = "/pendientes.idx";

  // Si el archivo no existe, créalo directamente con la entrada
  if (!SD.exists(IDX)) {
    File nf = SD.open(IDX, FILE_WRITE);
    if (!nf) {
      LOGW("IDX_ERR", "No se pudo crear pendientes.idx", "");
      return;
    }
    nf.println(path);
    nf.close();
    char kv[96];
    snprintf(kv, sizeof(kv), "file=%s", path.c_str());
    LOGD("IDX_ADD", "Creado pendientes.idx con entrada", kv);
    return;
  }

  // Verifica si ya existe la entrada
  bool exists = false;
  {
    File rf = SD.open(IDX, FILE_READ);
    if (!rf) {
      LOGW("IDX_ERR", "No se pudo leer pendientes.idx", "");
    } else {
      while (rf.available()) {
        String line = rf.readStringUntil('\n');
        line.trim();
        if (line == path) { exists = true; break; }
      }
      rf.close();
    }
  }
  if (exists) return;

  // Append seguro
  File af = SD.open(IDX, FILE_APPEND);
  if (!af) {
    // Fallback WRITE+seek
    af = SD.open(IDX, FILE_WRITE);
    if (af) af.seek(af.size());
  }
  if (!af) {
    LOGW("IDX_ERR", "No se pudo abrir pendientes.idx para append", "");
    return;
  }
  af.println(path);
  af.close();
  char kv[96];
  snprintf(kv, sizeof(kv), "file=%s", path.c_str());
  LOGD("IDX_ADD", "Añadido a pendientes.idx", kv);
}

// ================== Nombre de archivo ==================
// Genera "/backup_YYYYMMDD.csv" usando SIEMPRE el RTC (DS3231).
// Si el RTC no es válido, usa "/backup_unsync.csv" para evitar "1970".
String generarNombreArchivoBackup() {
  uint32_t unixS = getUnixSeconds();   // desde DS3231

  // Sanidad: válido aprox. 2021..2100
  bool rtc_ok = rtcIsPresent() && rtcIsTimeValid()
                && (unixS >= 1609459200UL) && (unixS <= 4102444800UL);

  if (!rtc_ok) {
    return "/backup_unsync.csv";
  }

  time_t t = (time_t)unixS;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);               // UTC para nombre estable

  char nombre[32];
  snprintf(nombre, sizeof(nombre), "/backup_%04d%02d%02d.csv",
           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday);
  return String(nombre);
}

// ================== Guardado en backup ==================
void guardarEnBackupSD(const String& measurement,
                       const String& sensor,
                       float valor,
                       unsigned long long timestamp,
                       const String& source,
                       const char* reason) {
  static const char* HEADER = "timestamp,measurement,sensor,valor,source,status,ts_envio";

  String nombreArchivo = ensureRootSlash(generarNombreArchivoBackup());

  // 1) Cabecera asegurada
  if (!fileEnsureHeader(nombreArchivo, HEADER)) {
    char kv[96];
    snprintf(kv, sizeof(kv), "file=%s", nombreArchivo.c_str());
    LOGE("SD_WRITE_ERR", "No se pudo crear archivo de backup", kv);
    return;
  }

  // 2) Apertura para append
  File f = SD.open(nombreArchivo, FILE_APPEND);
  if (!f) {
    // Fallback: algunos FS no soportan FILE_APPEND → WRITE + seek
    f = SD.open(nombreArchivo, FILE_WRITE);
    if (f) f.seek(f.size());
  }
  if (!f) {
    char kv[96];
    snprintf(kv, sizeof(kv), "file=%s", nombreArchivo.c_str());
    LOGE("SD_WRITE_ERR", "Fallo al abrir para backup", kv);
    return;
  }

  // 3) Construir línea PENDIENTE (ts_envio vacío)
  //    Formato: timestamp,measurement,sensor,valor,source,status,ts_envio
  char linea[192];
  int n = snprintf(linea, sizeof(linea),
                   "%llu,%s,%s,%.2f,%s,%s,",
                   (unsigned long long)timestamp,
                   measurement.c_str(),
                   sensor.c_str(),
                   (double)valor,
                   source.c_str(),
                   "PENDIENTE");

  if (n <= 0 || n >= (int)sizeof(linea)) {
    f.close();
    LOGE("SD_FMT_ERR", "Buffer CSV insuficiente al formatear", "");
    return;
  }

  // 4) Escribir
  uint32_t posBefore = (uint32_t)f.position();
  size_t wrote = f.println(linea);   // añade \r\n
  f.flush();
  uint32_t posAfter  = (uint32_t)f.position();
  f.close();

  if (wrote == 0 || posAfter <= posBefore) {
    char kv[128];
    snprintf(kv, sizeof(kv), "file=%s;len=%d", nombreArchivo.c_str(), n);
    LOGE("SD_WRITE_ERR", "Fallo al escribir backup", kv);
    return;
  }

  // 5) Métricas y logging (UNA sola línea con reason=...)
  //    Log estructurado con kv compacta para no fragmentar heap
  {
    char kv[192];
    // file, bytes, m, s, v, ts, reason
    snprintf(kv, sizeof(kv),
             "file=%s;bytes=%lu;m=%s;s=%s;v=%.2f;ts=%llu;reason=%s",
             nombreArchivo.c_str(),
             (unsigned long)(posAfter - posBefore),
             measurement.c_str(),
             sensor.c_str(),
             (double)valor,
             (unsigned long long)timestamp,
             (reason && reason[0] ? reason : "unspecified"));
    LOGI("BACKUP_STORE", "Respaldado en SD", kv);
  }

  // 6) Mantener índice de pendientes (para detección rápida)
  addToPendientesIdx(nombreArchivo);
}

// Utilidad de prueba manual (usa RTC para el timestamp)
void testBackup() {
  uint32_t unixS = getUnixSeconds();
  if (unixS == 0) {
    // Fallback extremo si el RTC no está: usa time() (evita 0)
    unixS = (uint32_t)time(nullptr);
  }
  unsigned long long ts_us = (unsigned long long)unixS * 1000000ULL;
  guardarEnBackupSD("agua", "YF-S201", 3.14f, ts_us, "backup", "test");
}
