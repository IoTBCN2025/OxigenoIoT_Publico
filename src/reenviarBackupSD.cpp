// reenviarBackupSD.cpp
// Modo append-only con índice .idx atómico.
// Instrumentado con LOG* (sdlog) + métricas, evitando ruido repetitivo.

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <stdlib.h>   // strtoul/strtoull

#include "sdlog.h"     // LOGI/LOGW/LOGD
#include "api.h"       // enviarDatoAPI_ex(...)
#include "reenviarBackupSD.h"

// ====== Ajustes ======
#ifndef MAX_REENVIOS_POR_LLAMADA
#define MAX_REENVIOS_POR_LLAMADA 6      // fallback si no usas _ex()
#endif

#ifndef RETRY_EVERY_MS
#define RETRY_EVERY_MS 500              // antirrebote interno por archivo
#endif

#ifndef SCAN_BACKUPS_EVERY_MS
#define SCAN_BACKUPS_EVERY_MS 1000      // antirrebote del escaneo
#endif

// Throttle local para logs de WiFi e info
static unsigned long s_lastScanInfoMs = 0;
static const unsigned long INFO_THROTTLE_MS = 10000;

// ====== Declaraciones externas existentes ======
extern String generarNombreArchivoBackup();  // p.ej. "backup_YYYYMMDD.csv"

// ====== Helpers ======
static inline String ensureRootSlash(const String& p) {
  if (p.length() == 0) return "/";
  return (p[0] == '/') ? p : ("/" + p);
}

static inline String idxPathFor(const String& csvPath) {
  return csvPath + ".idx";
}

// Split CSV en 7 campos (timestamp,measurement,sensor,valor,source,status,ts_envio)
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

// ====== Lectura/Escritura de índice .idx (atómico y silencioso) ======
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

  SD.mkdir("/sent");
  String base = csvPath;
  int slash = base.lastIndexOf('/');
  if (slash >= 0) base = base.substring(slash + 1);

  String dest = String("/sent/") + base;
  if (SD.exists(dest)) SD.remove(dest);  // asegurar rename

  bool ok = SD.rename(csvPath, dest);
  if (ok) {
    LOGI("BACKUP_ARCHIVE", "Archivado", String("file=") + base);
  } else {
    LOGW("BACKUP_WARN", "No se pudo archivar", String("file=") + base);
  }
  return ok;
}

// ====== Núcleo IDX: procesa un CSV con índice .idx ======
static void procesarBackupConIdx(const String& csvPathIn, ReenvioStats& st) {
  static unsigned long lastRetryMs = 0;

  if (!WiFi.isConnected()) {
    // 1 línea por evento de escaneo cuando no hay WiFi (el logger central también deduplica)
    if (millis() - s_lastScanInfoMs > INFO_THROTTLE_MS) {
      LOGI("REINTENTO_WAIT", "Sin WiFi, difiriendo reenvio", "");
      s_lastScanInfoMs = millis();
    }
    return;
  }
  if (millis() - lastRetryMs < RETRY_EVERY_MS) return;   // antirrebote por llamada
  lastRetryMs = millis();

  String csvPath = ensureRootSlash(csvPathIn);
  String idxPath = idxPathFor(csvPath);

  // Medir tamaño actual del archivo (no cacheado)
  File fsize0 = SD.open(csvPath, FILE_READ);
  if (!fsize0) {
    LOGW("REINTENTO_ERR", "No se pudo abrir (size)", String("file=") + csvPath);
    return;
  }
  uint32_t size0 = (uint32_t)fsize0.size();
  fsize0.close();

  uint32_t offset = 0;
  bool haveIdx = readIdx(idxPath, offset);

  // Si no hay .idx, inicializar al byte después de encabezado
  if (!haveIdx) {
    File f0 = SD.open(csvPath, FILE_READ);
    if (!f0) {
      LOGW("REINTENTO_ERR", "No se pudo abrir para inicializar idx", String("file=") + csvPath);
      return;
    }
    (void)f0.readStringUntil('\n');       // saltar encabezado
    offset = (uint32_t)f0.position();
    f0.close();
    if (writeIdxAtomic(idxPath, offset)) {
      LOGD("REINTENTO_INFO", "Inicializando idx", String("file=") + csvPath + ";off=" + String(offset));
    }
  }

  // Si el offset ya está al final del archivo, archivar y salir
  if (offset >= size0) {
    LOGD("REINTENTO_EOF", "Sin pendientes", String("file=") + csvPath + ";off=" + String(offset) + ";size=" + String(size0));
    (void)archiveBackupCsv(csvPath);
    st.archivos_cerrados++;
    return;
  }

  // Log mínimo “procesando” (no en cada iteración)
  LOGD("BACKUP_FILE", "Procesando", String("file=") + csvPath);

  File f = SD.open(csvPath, FILE_READ);
  if (!f) {
    LOGW("REINTENTO_ERR", "No se pudo abrir para lectura", String("file=") + csvPath);
    return;
  }
  if (!f.seek(offset)) {
    // Reinicializar idx al inicio de datos si el seek falla
    f.close();
    File f2 = SD.open(csvPath, FILE_READ);
    if (!f2) return;
    (void)f2.readStringUntil('\n');
    uint32_t off0 = (uint32_t)f2.position();
    f2.close();
    writeIdxAtomic(idxPath, off0);
    LOGW("REINTENTO_FIX", "Seek fallido; reini idx", String("file=") + csvPath + ";off=" + String(off0));
    return;
  }

  uint16_t enviados = 0;
  uint16_t saltados = 0;
  uint32_t newOffset = offset;

  while (WiFi.isConnected() && f.available() && enviados < MAX_REENVIOS_POR_LLAMADA) {
    uint32_t lineStart = (uint32_t)f.position();
    String line = f.readStringUntil('\n');
    newOffset = (uint32_t)f.position();

    line.trim();
    if (line.length() < 5) {              // línea vacía o basura corta
      saltados++;
      continue;
    }

    String c[7];
    parseCsv7(line, c);
    const String& tsS    = c[0];
    const String& meas   = c[1];
    const String& sens   = c[2];
    const String& valS   = c[3];
    const String& status = c[5];

    if (status != "PENDIENTE") {
      saltados++;
      continue;                            // consumimos igual
    }

    unsigned long long ts = strtoull(tsS.c_str(), nullptr, 10);
    float val = valS.toFloat();

    // Envío con métricas
    ApiResult r = enviarDatoAPI_ex(meas, sens, val, ts, "backup");
    st.lineas_procesadas++;

    if (r.ok) {
      enviados++; st.ok++;
      // Avanzamos (dejamos consumida la línea pendiente)
      LOGI("BACKUP_OK", "Reenvio OK",
           String("file=") + csvPath +
           ";m=" + meas + ";s=" + sens +
           ";ts=" + String(ts) +
           ";http=" + String(r.http_code) +
           ";lat=" + String(r.latency_ms));
    } else {
      st.err++;
      // fallo: NO avanzamos idx (retrocedemos a inicio de línea)
      newOffset = lineStart;
      LOGW("BACKUP_ERR", "Reenvio fallo",
           String("file=") + csvPath +
           ";m=" + meas + ";s=" + sens +
           ";ts=" + String(ts) +
           ";err=" + r.err +
           ";http=" + String(r.http_code));
      break; // salimos para reintentar en próxima pasada
    }
  }
  f.close();

  // Volver a medir tamaño al final (archivo pudo crecer)
  File fsize1 = SD.open(csvPath, FILE_READ);
  uint32_t size1 = 0;
  if (fsize1) { size1 = (uint32_t)fsize1.size(); fsize1.close(); }

  if (newOffset != offset) {
    if (writeIdxAtomic(idxPath, newOffset)) {
      LOGD("REINTENTO_OK", "Avance idx",
           String("file=") + csvPath +
           ";off=" + String(newOffset) +
           ";sent=" + String(enviados) +
           ";skip=" + String(saltados));
    }
  } else {
    if (!WiFi.isConnected()) {
      LOGI("REINTENTO_WAIT", "WiFi cayó durante el reenvio", String("file=") + csvPath);
      return;
    }
    if (newOffset >= size1) {
      LOGD("REINTENTO_EOF", "Sin pendientes (post)", String("file=") + csvPath + ";off=" + String(newOffset) + ";size=" + String(size1));
      (void)archiveBackupCsv(csvPath);
      st.archivos_cerrados++;
      return;
    }
    // Hubo fallo HTTP en la primera línea pendiente
    LOGD("REINTENTO_HOLD", "Se conserva posición (HTTP err)", String("file=") + csvPath + ";off=" + String(newOffset));
  }
}

// ====== Escáner de backups ======
static bool esBackupCsvValido(const String& name) {
  // Sólo archivos tipo backup_YYYYMMDD.csv; excluir 1970 y todo lo demás
  if (!name.startsWith("backup_")) return false;
  if (!name.endsWith(".csv")) return false;
  if (name.indexOf("1970") >= 0) return false;          // legacy/sentinela
  return true;
}

ReenvioStats reenviarDatosDesdeBackup_ex(uint16_t max_lineas_por_ciclo) {
  ReenvioStats st;

  // Información de comienzo de ciclo (con throttling local anti-ruido)
  if (millis() - s_lastScanInfoMs > INFO_THROTTLE_MS) {
    LOGI("BACKUP_RETRY", "Intento de reenvio desde SD", String("max_lines=") + String(max_lineas_por_ciclo));
    s_lastScanInfoMs = millis();
  }

  if (!WiFi.isConnected()) {
    LOGI("REINTENTO_WAIT", "Sin WiFi, difiriendo reenvio/escaneo", "");
    return st;
  }

  File root = SD.open("/");
  if (!root) {
    LOGW("SD_ERR", "No se pudo abrir root de SD", "");
    return st;
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    String name = entry.name();
    entry.close();

    // Normaliza a base (sin directorio)
    String base = name;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(slash + 1);

    // Filtra sólo backups válidos
    if (!esBackupCsvValido(base)) continue;

    st.archivos_visitados++;
    String path = ensureRootSlash(base);
    procesarBackupConIdx(path, st);

    // Política no bloqueante: procesa incrementalmente, 1 archivo por scan
    if (st.lineas_procesadas >= max_lineas_por_ciclo) break;
  }
  root.close();

  // Resumen de ciclo (1 línea)
  LOGI("BACKUP_RESULT", "Reenvio procesado",
       String("files=") + String(st.archivos_visitados) +
       ";closed=" + String(st.archivos_cerrados) +
       ";lines=" + String(st.lineas_procesadas) +
       ";ok=" + String(st.ok) +
       ";err=" + String(st.err));

  return st;
}

void reenviarDatosDesdeBackup() {
  (void)reenviarDatosDesdeBackup_ex(MAX_REENVIOS_POR_LLAMADA);
}
