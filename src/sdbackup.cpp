#include "sdbackup.h"
#include "sdlog.h"
#include "ds3231_time.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <time.h>

static bool g_sdbackup_announced_ok = false;
static bool g_sdbackup_announced_fail = false;
static unsigned long g_last_fail_log_ms = 0;

static inline String ensureRootSlash(const String& p) {
  if (p.length() == 0) return "/";
  return (p[0] == '/') ? p : ("/" + p);
}

String generarNombreArchivoBackup() {
  uint32_t unixS = getUnixSeconds();
  bool rtc_ok = rtcIsPresent() && rtcIsTimeValid()
                && (unixS >= 1609459200UL) && (unixS <= 4102444800UL);

  if (!rtc_ok) return "/backup_unsync.csv";

  time_t t = (time_t)unixS;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);

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

  if (!SD.exists(nombreArchivo)) {
    File f = SD.open(nombreArchivo, FILE_WRITE);
    if (!f) {
      if (!g_sdbackup_announced_fail || (millis() - g_last_fail_log_ms) > 10000) {
        logEventoM("SD_BACKUP", "MOD_FAIL", "op=create;path=" + nombreArchivo + ";err=open_failed");
        g_sdbackup_announced_fail = true;
        g_last_fail_log_ms = millis();
      }
      logEventoM("SD_BACKUP", "SD_ERR", "reason=create_failed");
      return;
    }
    f.println("timestamp,measurement,sensor,valor,source,status,ts_envio");
    f.flush(); f.close();
  }

  File f = SD.open(nombreArchivo, FILE_APPEND);
  if (!f) {
    f = SD.open(nombreArchivo, FILE_WRITE);
    if (f) f.seek(f.size());
  }

  if (f) {
    String fila = String(timestamp) + "," + measurement + "," + sensor + "," +
                  String(valor, 2) + "," + source + ",PENDIENTE,";
    f.println(fila);
    f.flush(); f.close();

    if (!g_sdbackup_announced_ok) {
      logEventoM("SD_BACKUP", "MOD_UP", "cs=" + String(config.pins.SD_CS) + ";fs=SD;mode=append");
      g_sdbackup_announced_ok = true;
      g_sdbackup_announced_fail = false;
    }

    logEventoM("SD_BACKUP", "BACKUP_OK",
               "sensor=" + sensor + ";valor=" + String(valor, 2) + ";src=" + source);
  } else {
    if (!g_sdbackup_announced_fail || (millis() - g_last_fail_log_ms) > 10000) {
      logEventoM("SD_BACKUP", "MOD_FAIL", "op=append;path=" + nombreArchivo + ";err=open_failed");
      g_sdbackup_announced_fail = true;
      g_last_fail_log_ms = millis();
    }
    logEventoM("SD_BACKUP", "SD_ERR", "reason=append_failed");
  }
}