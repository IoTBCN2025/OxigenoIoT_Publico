#include "sdlog.h"
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <time.h>
#include <vector>

#include "ntp.h"             // getTimestamp() en segundos (fallback)
#include "ds3231_time.h"     // getUnixSeconds(), getTimestampMicros()

#ifndef SD_CS_PIN
#define SD_CS_PIN 5
#endif

// Estado interno
static bool g_sd_ready = false;
static String g_logFileName;
static int g_currentY = 0, g_currentM = 0, g_currentD = 0;
static std::vector<String> g_bufferRAM;

// ------------------------ helpers ------------------------

static void formatearFechaYNombre(time_t t, String& outName, int& y, int& m, int& d) {
  struct tm* now = localtime(&t);
  if (!now) { // fallback muy defensivo
    outName = "/eventlog_unknown.csv";
    y = m = d = 0;
    return;
  }
  y = now->tm_year + 1900; m = now->tm_mon + 1; d = now->tm_mday;
  char nombre[40];
  snprintf(nombre, sizeof(nombre), "/eventlog_%04d.%02d.%02d.csv", y, m, d);
  outName = String(nombre);
}

static void asegurarCabeceraCSV(const String& path) {
  if (!SD.exists(path)) {
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.println("timestamp,event,detalle,ts_us");
      f.close();
    }
  }
}

static time_t unix_to_time_t(uint32_t unixS) {
  // En ESP32 time_t ya es UNIX epoch; conversión directa.
  return (time_t)unixS;
}

static uint32_t unix_actual_segundos() {
  // Prioriza DS3231 → si inválido, usa NTP → si no, time(nullptr).
  uint32_t s = getUnixSeconds();           // DS3231
  if (s == 0) {
    time_t ntp_s = getTimestamp();         // NTP (tu función existente)
    if (ntp_s > 0) s = (uint32_t)ntp_s;
  }
  if (s == 0) {
    time_t t = time(nullptr);              // último fallback
    if (t > 0) s = (uint32_t)t;
  }
  return s;
}

static unsigned long long timestamp_us_actual() {
  // Prioriza DS3231 µs → si inválido, NTP*1e6 → si no, millis() (último recurso).
  unsigned long long ts = getTimestampMicros(); // DS3231 µs
  if (ts != 0ULL && ts != 943920000000000ULL) return ts;

  time_t ntp_s = getTimestamp();
  if (ntp_s > 0) return (unsigned long long)ntp_s * 1000000ULL;

  return (unsigned long long)millis() * 1000ULL; // degradado
}

static bool montarSD() {
  if (g_sd_ready) return true;
  g_sd_ready = SD.begin(SD_CS_PIN);
  return g_sd_ready;
}

static void prepararArchivoDelDia() {
  // Determina fecha del día y archivo correspondiente
  uint32_t s = unix_actual_segundos();
  if (s == 0) {
    // Sin fecha fiable: usa un nombre genérico (evita fallo de open)
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
  if (s == 0) return; // sin fecha fiable, no rotamos
  int y, m, d;
  String nuevo;
  time_t t = unix_to_time_t(s);
  struct tm* now = localtime(&t);
  if (!now) return;
  y = now->tm_year + 1900; m = now->tm_mon + 1; d = now->tm_mday;
  if (y != g_currentY || m != g_currentM || d != g_currentD) {
    // día cambió → nuevo archivo
    formatearFechaYNombre(t, nuevo, g_currentY, g_currentM, g_currentD);
    g_logFileName = nuevo;
    asegurarCabeceraCSV(g_logFileName);
  }
}

static void volcarBufferRAM() {
  if (!g_sd_ready || g_bufferRAM.empty()) return;
  File f = SD.open(g_logFileName, FILE_APPEND);
  if (!f) return; // si falla, lo dejamos en RAM para próximo intento
  for (auto &ln : g_bufferRAM) f.println(ln);
  f.close();
  g_bufferRAM.clear();
}

// ------------------------ API pública ------------------------

void inicializarSD() {
  if (!montarSD()) {
    Serial.println("SD no detectada (se loguea en RAM y Serial hasta que esté lista)");
    return;
  }
  Serial.println("SD inicializada correctamente");
  prepararArchivoDelDia();
  volcarBufferRAM(); // si había pendientes en RAM, intenta volcarlos
}

void logEvento(const String& evento, const String& detalle) {
  // 1) Timestamp ISO para la primera columna
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

  // 2) Construye línea CSV (si quieres incluir µs, añádelo al detalle)
  //    Ej.: detalle + " ts_us=XXXXXX"
  unsigned long long ts_us = timestamp_us_actual();
  String detalle_ext = detalle;
  detalle_ext += " ts_us=" + String((uint32_t)(ts_us % 1000000ULL)); // parte µs visible (opcional)

  // Propuesta (4 columnas; mantén compatibilidad con cabecera):
  // Asegura que la cabecera también tenga la cuarta columna una sola vez.
  String linea = String(fecha) + "," + evento + "," + detalle_ext + "," + String(ts_us);

  // 3) Si SD no está lista, guarda en RAM y muestra por Serial
  if (!g_sd_ready) {
    g_bufferRAM.push_back(linea);
    Serial.println("Evento (RAM): " + linea);
    return;
  }

  // 4) Rotación diaria si cambió de día
  rotarSiCambioDeDia();

  // 5) Escritura segura + volcado de RAM si es posible
  File f = SD.open(g_logFileName, FILE_APPEND);
  if (!f) {
    // Si no se pudo abrir: reintentar montar y bufferizar
    g_sd_ready = false; // fuerza re-montaje en reintentarLogsPendientes()
    g_bufferRAM.push_back(linea);
    Serial.println("LOG(FSERR): " + linea + " (guardado en RAM para reintento)");
    return;
  }

  f.println(linea);
  f.close();

  // Intenta volcar cualquier pendiente de RAM
  volcarBufferRAM();

  Serial.println("Evento logueado: " + linea);
}

void reintentarLogsPendientes() {
  // 1) Si la SD no está montada, intenta montarla usando el pin configurado
  if (!g_sd_ready) {
    if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD no disponible en reintento");
      return;
    }
    g_sd_ready = true;
    Serial.println("SD montada en reintento");
  }

  // 2) Si no hay nombre de archivo resuelto aún, prepáralo
  if (g_logFileName.length() == 0) {
    prepararArchivoDelDia();
  }

  // 3) Asegura cabecera del archivo actual
  asegurarCabeceraCSV(g_logFileName);

  // 4) Rotación diaria si cambió de día
  rotarSiCambioDeDia();

  // 5) Toca el archivo y vuelca buffer RAM si existe
  File f = SD.open(g_logFileName, FILE_APPEND);
  if (!f) {
    // Si falla abrir, marcamos como no montada para reintentar en el próximo ciclo
    g_sd_ready = false;
    Serial.println("reintentarLogsPendientes(): fallo al abrir archivo; se reintentará");
    return;
  }
  f.close();

  // 6) Volcar buffer RAM → SD (si había)
  volcarBufferRAM();

  Serial.println("reintentarLogsPendientes(): SD lista y archivo preparado: " + g_logFileName);
}

