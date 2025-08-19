// reenviarBackupSD.cpp

// Modo append-only con índice .idx atómico y silencioso (sin errores VFS)

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <stdlib.h>   // strtoul/strtoull

// ====== Ajustes ======
#ifndef MAX_REENVIOS_POR_LLAMADA
#define MAX_REENVIOS_POR_LLAMADA 6      // envíos por pasada
#endif

#ifndef RETRY_EVERY_MS
#define RETRY_EVERY_MS 500              // antirrebote interno del procesador por archivo
#endif

#ifndef SCAN_BACKUPS_EVERY_MS
#define SCAN_BACKUPS_EVERY_MS 1000      // antirrebote del escaneo de backups
#endif

// ====== Declaraciones del proyecto ======
extern void logEvento(const String& tipo, const String& mensaje);
extern bool enviarDatoAPI(const String& measurement,
                          const String& sensor,
                          float valor,
                          unsigned long long timestamp,
                          const String& source);
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
    logEvento("BACKUP_ARCHIVE", "Archivado: " + base);
  } else {
    logEvento("BACKUP_WARN", "No se pudo archivar: " + base);
  }
  return ok;
}

// ====== Núcleo IDX: procesa un CSV con índice .idx ======
static void procesarBackupConIdx(const String& csvPathIn) {
  static unsigned long lastRetryMs = 0;
  if (!WiFi.isConnected()) {
    // Separado para diagnóstico claro (sin spam por archivo)
    logEvento("REINTENTO_SKIP_WIFI", "WiFi no disponible; no se procesa backup");
    return;
  }
  if (millis() - lastRetryMs < RETRY_EVERY_MS) return;   // antirrebote por llamada
  lastRetryMs = millis();

  String csvPath = ensureRootSlash(csvPathIn);
  String idxPath = idxPathFor(csvPath);

  // Medir tamaño actual del archivo (no cacheado)
  File fsize0 = SD.open(csvPath, FILE_READ);
  if (!fsize0) {
    logEvento("REINTENTO_ERR", "No se pudo abrir " + csvPath + " (size)");
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
      logEvento("REINTENTO_ERR", "No se pudo abrir " + csvPath + " para inicializar idx");
      return;
    }
    (void)f0.readStringUntil('\n');       // saltar encabezado
    offset = (uint32_t)f0.position();
    f0.close();
    if (writeIdxAtomic(idxPath, offset)) {
      logEvento("REINTENTO_INFO", "Inicializando idx en " + String(offset) + " para " + csvPath);
    }
  }

  // Si el offset ya está al final del archivo, archivar y salir
  if (offset >= size0) {
    logEvento("REINTENTO_EOF", String("Sin pendientes en ") + csvPath +
                               " (idx=" + String(offset) +
                               ", size=" + String(size0) + ")");
    (void)archiveBackupCsv(csvPath);
    return;
  }

  // Log “Procesando (IDX)” ya con throttle aplicado
  logEvento("REINTENTO_DEBUG", "Procesando (IDX) archivo: " + csvPath);

  File f = SD.open(csvPath, FILE_READ);
  if (!f) {
    logEvento("REINTENTO_ERR", "No se pudo abrir " + csvPath + " para lectura");
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
    logEvento("REINTENTO_FIX", "Reinicializado idx a " + String(off0) + " por seek fallido");
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
    if (line.length() < 5) {              // línea vacía o basura corta
      saltados++;
      continue;                            // avanzamos con newOffset
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

    if (enviarDatoAPI(meas, sens, val, ts, "backup")) {
      enviados++;                          // ok: dejamos newOffset (avanza)
    } else {
      // fallo: NO avanzamos idx (retrocedemos a inicio de línea)
      newOffset = lineStart;
      break;
    }
  }
  f.close();

  // Volver a medir tamaño al final por seguridad (archivo pudo crecer)
  File fsize1 = SD.open(csvPath, FILE_READ);
  uint32_t size1 = 0;
  if (fsize1) { size1 = (uint32_t)fsize1.size(); fsize1.close(); }

  if (newOffset != offset) {
    if (writeIdxAtomic(idxPath, newOffset)) {
      logEvento("REINTENTO_OK", "Avance idx a " + String(newOffset) +
                                " (" + String(enviados) + " enviados, " +
                                String(saltados) + " saltados)");
    }
    logEvento("REINTENTO", "Archivo " + csvPath +
                           " enviados=" + String(enviados) +
                           " saltados=" + String(saltados));
  } else {
    // Distinguir causas con precisión
    if (!WiFi.isConnected()) {
      logEvento("REINTENTO_SKIP_WIFI", "WiFi cayó durante el reenvío en " + csvPath);
      return;
    }
    if (newOffset >= size1) {
      logEvento("REINTENTO_EOF", "Sin pendientes en " + csvPath +
                                 " (idx=" + String(newOffset) +
                                 ", size=" + String(size1) + ")");
      (void)archiveBackupCsv(csvPath);
      return;
    }
    // Si llegamos aquí, hubo fallo HTTP en la primera línea pendiente
    logEvento("REINTENTO_HOLD", "Fallo envío; se conservará posición en " + csvPath);
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

void reenviarDatosDesdeBackup() {
  static unsigned long lastScan = 0;
  if (millis() - lastScan < SCAN_BACKUPS_EVERY_MS) return;   // antirrebote del escaneo
  lastScan = millis();

  if (!WiFi.isConnected()) {
    // Un solo log por escaneo cuando no hay WiFi (evita ruido)
    logEvento("REINTENTO_WAIT", "Sin WiFi, difiriendo reenvio/escaneo backups");
    return;
  }

  logEvento("REINTENTO_INFO", "Procesando backups pendientes (WiFi OK)");

  File root = SD.open("/");
  if (!root) {
    logEvento("SD_ERR", "No se pudo abrir root de SD");
    return;
  }

  uint16_t candidatos = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    String name = entry.name();
    entry.close();

    // Normaliza a base (sin directorio)
    String base = name;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(slash + 1);

    // Filtra sólo backups válidos (NO loguear los que se descartan)
    if (!esBackupCsvValido(base)) continue;

    candidatos++;
    String path = ensureRootSlash(base);
    procesarBackupConIdx(path);
  }
  root.close();

  // Resumen (menos ruido que un log por archivo encontrado)
  logEvento("REINTENTO_SUMMARY", "Backups candidatos: " + String(candidatos));
}
