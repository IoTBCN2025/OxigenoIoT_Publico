#include "sdlog.h"
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include "ntp.h"

File logFile;
String logFileName;

void inicializarSD() {
  if (!SD.begin(5)) {
    Serial.println("SD no detectada");
    return;
  }
  Serial.println("SD inicializada correctamente");

  // Obtener fecha actual del timestamp
  time_t t = time(nullptr);
  struct tm* now = localtime(&t);

  char nombre[32];
  snprintf(nombre, sizeof(nombre), "/eventlog_%04d.%02d.%02d.csv", now->tm_year + 1900, now->tm_mon + 1, now->tm_mday);
  logFileName = String(nombre);

  if (!SD.exists(logFileName)) {
    logFile = SD.open(logFileName, FILE_WRITE);
    if (logFile) {
      logFile.println("timestamp,event,detalle");
      logFile.close();
    }
  }
}

void logEvento(const String& evento, const String& detalle) {
  unsigned long long ts = getTimestamp() * 1000000ULL;
  time_t t = time(nullptr);
  struct tm* now = localtime(&t);

  char fecha[32];
  snprintf(fecha, sizeof(fecha), "%04d-%02d-%02d %02d:%02d:%02d",
           now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
           now->tm_hour, now->tm_min, now->tm_sec);

  String linea = String(fecha) + "," + evento + "," + detalle;

  File logFile = SD.open(logFileName, FILE_APPEND);
  if (logFile) {
    logFile.println(linea);
    logFile.close();
  }

  Serial.println("Evento logueado: " + linea);
}
