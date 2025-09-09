// sdbackup.cpp
#include "sdbackup.h"
#include "sdlog.h"
#include "ds3231_time.h"   // getUnixSeconds(), rtcIsPresent(), rtcIsTimeValid()
#include <SD.h>
#include <SPI.h>
#include <time.h>

// Pines SD (ajusta según tu hardware)
#define SD_CS   5
#define SCK     18
#define MISO    19
#define MOSI    23

// ---- Estado de anuncio para no hacer spam en logs
static bool g_sdbackup_announced_ok = false;
static bool g_sdbackup_announced_fail = false;
static unsigned long g_last_fail_log_ms = 0;

static inline String ensureRootSlash(const String& p) {
  if (p.length() == 0) return "/";
  return (p[0] == '/') ? p : ("/" + p);
}

// Genera "/backup_YYYYMMDD.csv" usando SIEMPRE el RTC (DS3231).
// Si el RTC no es válido, usa "/backup_unsync.csv" para evitar "1970".
String generarNombreArchivoBackup() {
  uint32_t unixS = getUnixSeconds();   // desde DS3231

  // Sanidad: válido aprox. 2021..2100
  bool rtc_ok = rtcIsPresent() && rtcIsTimeValid()
                && (unixS >= 1609459200UL) && (unixS <= 4102444800UL);

  if (!rtc_ok) {
    // No usamos 1970: nombre neutro hasta que haya hora válida
    return "/backup_unsync.csv";
  }

  time_t t = (time_t)unixS;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);               // a UTC para nombre estable

  char nombre[32];
  snprintf(nombre, sizeof(nombre), "/backup_%04d%02d%02d.csv",
           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday);
  return String(nombre);
}

void guardarEnBackupSD(const String& measurement,
                       const String& sensor,
                       float valor,
                       unsigned long long timestamp,
                       const String& source) {
  String nombreArchivo = ensureRootSlash(generarNombreArchivoBackup());

  // Crea con cabecera sólo si no existe
  if (!SD.exists(nombreArchivo)) {
    File f = SD.open(nombreArchivo, FILE_WRITE);   // crear
    if (!f) {
      // MOD_FAIL del subsistema de backup/SD (una vez por boot con throttle)
      if (!g_sdbackup_announced_fail || (millis() - g_last_fail_log_ms) > 10000) {
        logEventoM("SD_BACKUP", "MOD_FAIL",
                   String("op=create;path=" + nombreArchivo + ";err=open_failed"));
        g_sdbackup_announced_fail = true;
        g_last_fail_log_ms = millis();
      }
      logEventoM("SD_BACKUP", "SD_ERR", "reason=create_failed");
      return;
    }
    f.println("timestamp,measurement,sensor,valor,source,status,ts_envio");
    f.flush();
    f.close();
  }

  // Append (una línea por registro)
  File f = SD.open(nombreArchivo, FILE_APPEND);
  if (!f) {
    // Fallback: algunos FS no soportan FILE_APPEND, intentamos WRITE + seek
    f = SD.open(nombreArchivo, FILE_WRITE);
    if (f) f.seek(f.size());
  }

  if (f) {
    String fila = String(timestamp) + "," + measurement + "," + sensor + "," +
                  String(valor, 2) + "," + source + ",PENDIENTE,";
    f.println(fila);   // Print::println escribe CRLF
    f.flush();
    f.close();

    // Primer éxito → MOD_UP del módulo de backup/SD
    if (!g_sdbackup_announced_ok) {
      logEventoM("SD_BACKUP", "MOD_UP", "cs=5;fs=SD;mode=append");
      g_sdbackup_announced_ok = true;
      g_sdbackup_announced_fail = false; // limpia bandera de fallo previo
    }

    logEventoM("SD_BACKUP", "BACKUP_OK",
               String("sensor=" + sensor + ";valor=" + String(valor, 2) + ";src=" + source));
  } else {
    // MOD_FAIL (con throttle)
    if (!g_sdbackup_announced_fail || (millis() - g_last_fail_log_ms) > 10000) {
      logEventoM("SD_BACKUP", "MOD_FAIL",
                 String("op=append;path=" + nombreArchivo + ";err=open_failed"));
      g_sdbackup_announced_fail = true;
      g_last_fail_log_ms = millis();
    }
    logEventoM("SD_BACKUP", "SD_ERR", "reason=append_failed");
  }
}

// Utilidad de prueba manual (usa RTC para el timestamp)
void testBackup() {
  uint32_t unixS = getUnixSeconds();
  if (unixS == 0) {
    // fallback extremo si el RTC no está: usa time() (no ideal, pero evita 0)
    unixS = (uint32_t)time(nullptr);
  }
  unsigned long long ts_us = (unsigned long long)unixS * 1000000ULL;
  guardarEnBackupSD("agua", "YF-S201", 3.14, ts_us, "backup");
}
