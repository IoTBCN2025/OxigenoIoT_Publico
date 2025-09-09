// reenviarBackupSD.cpp
// Modo append-only con índice .idx atómico y silencioso (sin errores VFS)
// + Auditoría: por cada envío OK, escribir en /sent/backup_YYYYMMDD.csv con status=ENVIADO,ts_envio

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <stdlib.h>
#include <time.h>
#include "sdlog.h"          // <-- usar logEventoM
#include "ds3231_time.h"    // getUnixSeconds(), getTimestampMicros()
#include "api.h"            // enviarDatoAPI()

#ifndef MAX_REENVIOS_POR_LLAMADA
#define MAX_REENVIOS_POR_LLAMADA 6
#endif
#ifndef RETRY_EVERY_MS
#define RETRY_EVERY_MS 500
#endif
#ifndef SCAN_BACKUPS_EVERY_MS
#define SCAN_BACKUPS_EVERY_MS 1000
#endif

static inline String ensureRootSlash(const String& p) {
  if (p.length() == 0) return "/";
  return (p[0] == '/') ? p : ("/" + p);
}
static inline String idxPathFor(const String& csvPath) { return csvPath + ".idx"; }

static unsigned long long now_us_auditable() {
  unsigned long long ts = getTimestampMicros();
  if (ts != 0ULL && ts != 943920000000000ULL) return ts;
  time_t t = time(nullptr);
  if (t > 0) return (unsigned long long)t * 1000000ULL;
  return (unsigned long long)millis() * 1000ULL;
}

static String baseName(const String& path) {
  int slash = path.lastIndexOf('/');
  return (slash >= 0) ? path.substring(slash + 1) : path;
}
static String sentAuditPathForCsv(const String& csvPath) {
  SD.mkdir("/sent");
  return String("/sent/") + baseName(csvPath);
}
static void ensureAuditHeader(const String& auditPath) {
  if (!SD.exists(auditPath)) {
    File f = SD.open(auditPath, FILE_WRITE);
    if (f) { f.println("timestamp,measurement,sensor,valor,source,status,ts_envio"); f.close(); }
  }
}
static bool parseCsv7(const String& line, String out[7]) {
  int pos = 0;
  for (int i = 0; i < 7; i++) {
    int coma = line.indexOf(',', pos);
    if (coma < 0) coma = line.length();
    out[i] = line.substring(pos, coma);
    pos = (coma < (int)line.length()) ? (coma + 1) : line.length();
  }
  return true;
}
static bool readIdx(const String& idxPath, uint32_t& offsetOut) {
  if (!SD.exists(idxPath)) return false;
  File f = SD.open(idxPath, FILE_READ);
  if (!f) return false;
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  if (s.length() == 0) return false;
  offsetOut = (uint32_t)strtoul(s.c_str(), nullptr, 10);
  return true;
}
static bool writeIdxAtomic(const String& idxPath, uint32_t offset) {
  String tmp = idxPath + ".tmp";
  if (SD.exists(tmp)) SD.remove(tmp);
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  f.seek(0);
  f.println(String(offset));
  f.flush();
  f.close();
  if (SD.exists(idxPath)) SD.remove(idxPath);
  return SD.rename(tmp, idxPath);
}

// ====== Archivo/rotación de CSV consumidos ======
static bool archiveBackupCsv(const String& csvPath) {
  String idxPath = idxPathFor(csvPath);
  if (SD.exists(idxPath)) SD.remove(idxPath);

  SD.mkdir("/sent/raw");
  String base = baseName(csvPath);
  String dest = String("/sent/raw/") + base;
  if (SD.exists(dest)) SD.remove(dest);

  bool ok = SD.rename(csvPath, dest);
  if (ok) {
    logEventoM("SD_BACKUP", "BACKUP_ARCHIVE", String("path=raw/") + base);
  } else {
    logEventoM("SD_BACKUP", "BACKUP_WARN", String("path=") + base + ";op=archive;err=rename_failed");
  }
  return ok;
}

// ====== Auditoría ENVIADO ======
static void appendAuditSent(const String& csvPath, const String c[7]) {
  String audit = sentAuditPathForCsv(csvPath);
  ensureAuditHeader(audit);
  unsigned long long ts_envio = now_us_auditable();
  String linea = c[0] + "," + c[1] + "," + c[2] + "," + c[3] + "," + c[4] + ",ENVIADO," + String(ts_envio);
  File f = SD.open(audit, FILE_APPEND);
  if (!f) { f = SD.open(audit, FILE_WRITE); if (f) f.seek(f.size()); }
  if (f) { f.println(linea); f.flush(); f.close(); }
}

// ====== Núcleo IDX: procesa un CSV con índice .idx ======
static void procesarBackupConIdx(const String& csvPathIn) {
  static unsigned long lastRetryMs = 0;
  if (!WiFi.isConnected()) {
    logEventoM("SD_BACKUP", "REINTENTO_SKIP_WIFI", "wifi=0");
    return;
  }
  if (millis() - lastRetryMs < RETRY_EVERY_MS) return;
  lastRetryMs = millis();

  String csvPath = ensureRootSlash(csvPathIn);
  String idxPath = idxPathFor(csvPath);

  File fsize0 = SD.open(csvPath, FILE_READ);
  if (!fsize0) {
    logEventoM("SD_BACKUP", "REINTENTO_ERR", String("op=size;path=") + csvPath);
    return;
  }
  uint32_t size0 = (uint32_t)fsize0.size();
  fsize0.close();

  uint32_t offset = 0;
  bool haveIdx = readIdx(idxPath, offset);

  if (!haveIdx) {
    File f0 = SD.open(csvPath, FILE_READ);
    if (!f0) {
      logEventoM("SD_BACKUP", "REINTENTO_ERR", String("op=init_idx;path=") + csvPath);
      return;
    }
    (void)f0.readStringUntil('\n');  // saltar header
    offset = (uint32_t)f0.position();
    f0.close();
    if (writeIdxAtomic(idxPath, offset)) {
      logEventoM("SD_BACKUP", "REINTENTO_INFO", String("init_idx=") + String(offset) + ";path=" + csvPath);
    }
  }

  if (offset >= size0) {
    logEventoM("SD_BACKUP", "REINTENTO_EOF", String("idx=") + String(offset) + ";size=" + String(size0) + ";path=" + csvPath);
    (void)archiveBackupCsv(csvPath);
    return;
  }

  logEventoM("SD_BACKUP", "REINTENTO_DEBUG", String("path=") + csvPath);

  File f = SD.open(csvPath, FILE_READ);
  if (!f) {
    logEventoM("SD_BACKUP", "REINTENTO_ERR", String("op=open;path=") + csvPath);
    return;
  }
  if (!f.seek(offset)) {
    f.close();
    File f2 = SD.open(csvPath, FILE_READ);
    if (!f2) return;
    (void)f2.readStringUntil('\n');
    uint32_t off0 = (uint32_t)f2.position();
    f2.close();
    writeIdxAtomic(idxPath, off0);
    logEventoM("SD_BACKUP", "REINTENTO_FIX", String("reset_idx=") + String(off0) + ";path=" + csvPath);
    return;
  }

  int enviados = 0;
  int saltados = 0;
  uint32_t newOffset = offset;

  while (WiFi.isConnected() && f.available() && enviados < MAX_REENVIOS_POR_LLAMADA) {
    uint32_t lineStart = (uint32_t)f.position();
    String line = f.readStringUntil('\n');
    newOffset = (uint32_t)f.position();

    line.trim();
    if (line.length() < 5) { saltados++; continue; }

    String c[7]; parseCsv7(line, c);
    const String& tsS    = c[0];
    const String& meas   = c[1];
    const String& sens   = c[2];
    const String& valS   = c[3];
    const String& status = c[5];

    if (status != "PENDIENTE") { saltados++; continue; }

    unsigned long long ts = strtoull(tsS.c_str(), nullptr, 10);
    float val = valS.toFloat();

    if (enviarDatoAPI(meas, sens, val, ts, "backup")) {
      appendAuditSent(csvPath, c);
      enviados++;
    } else {
      newOffset = lineStart; // mantener posición para reintento
      break;
    }
  }
  f.close();

  File fsize1 = SD.open(csvPath, FILE_READ);
  uint32_t size1 = 0;
  if (fsize1) { size1 = (uint32_t)fsize1.size(); fsize1.close(); }

  if (newOffset != offset) {
    if (writeIdxAtomic(idxPath, newOffset)) {
      logEventoM("SD_BACKUP", "REINTENTO_OK",
                 String("idx=") + String(newOffset) + ";enviados=" + String(enviados) + ";saltados=" + String(saltados) + ";path=" + csvPath);
    }
    logEventoM("SD_BACKUP", "REINTENTO",
               String("enviados=") + String(enviados) + ";saltados=" + String(saltados) + ";path=" + csvPath);
  } else {
    if (!WiFi.isConnected()) {
      logEventoM("SD_BACKUP", "REINTENTO_SKIP_WIFI", String("path=") + csvPath);
      return;
    }
    if (newOffset >= size1) {
      logEventoM("SD_BACKUP", "REINTENTO_EOF", String("idx=") + String(newOffset) + ";size=" + String(size1) + ";path=" + csvPath);
      (void)archiveBackupCsv(csvPath);
      return;
    }
    logEventoM("SD_BACKUP", "REINTENTO_HOLD", String("path=") + csvPath + ";reason=api_fail");
  }
}

// ====== Escáner de backups ======
static bool esBackupCsvValido(const String& name) {
  if (!name.startsWith("backup_")) return false;
  if (!name.endsWith(".csv")) return false;
  if (name.indexOf("1970") >= 0) return false;
  return true;
}

void reenviarDatosDesdeBackup() {
  static unsigned long lastScan = 0;
  if (millis() - lastScan < SCAN_BACKUPS_EVERY_MS) return;
  lastScan = millis();

  if (!WiFi.isConnected()) {
    logEventoM("SD_BACKUP", "REINTENTO_WAIT", "wifi=0");
    return;
  }

  logEventoM("SD_BACKUP", "REINTENTO_INFO", "scan=1");

  File root = SD.open("/");
  if (!root) {
    logEventoM("SD_BACKUP", "SD_ERR", "err=open_root");
    return;
  }

  uint16_t candidatos = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = entry.name();
    entry.close();

    String base = baseName(name);
    if (!esBackupCsvValido(base)) continue;
    candidatos++;
    String path = ensureRootSlash(base);
    procesarBackupConIdx(path);
  }
  root.close();

  logEventoM("SD_BACKUP", "REINTENTO_SUMMARY", String("candidatos=") + String(candidatos));
}
