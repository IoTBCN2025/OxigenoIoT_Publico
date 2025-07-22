// sdbackup.cpp
#include "sdbackup.h"
#include "sdlog.h"
#include <SD.h>
#include <SPI.h>
#include <time.h>

#define SD_CS   5
#define SCK     18
#define MISO    19
#define MOSI    23

String generarNombreArchivoBackup() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char nombre[32];
  strftime(nombre, sizeof(nombre), "/backup_%Y%m%d.csv", t);
  return String(nombre);
}

void guardarEnBackupSD(const String& measurement, const String& sensor, float valor, unsigned long long timestamp, const String& source) {
  String nombreArchivo = generarNombreArchivoBackup();

  if (!SD.exists(nombreArchivo)) {
    File f = SD.open(nombreArchivo, FILE_WRITE);
    if (f) {
      f.println("timestamp,measurement,sensor,valor,source,status,ts_envio");
      f.close();
    } else {
      logEvento("SD_ERR", "No se pudo crear archivo de backup");
      return;
    }
  }

  File f = SD.open(nombreArchivo, FILE_APPEND);
  if (f) {
    String fila = String(timestamp) + "," + measurement + "," + sensor + "," + String(valor, 2) + "," + source + ",PENDIENTE,";
    f.println(fila);
    f.close();
    logEvento("BACKUP_OK", "Dato guardado en backup SD: " + sensor + "=" + String(valor, 2) + " (" + source + ")");
  } else {
    logEvento("SD_ERR", "Fallo al abrir archivo para backup");
  }
}

void testBackup() {
  unsigned long long ts = time(nullptr) * 1000000ULL;
  guardarEnBackupSD("agua", "YF-S201", 3.14, ts, "backup");
}
