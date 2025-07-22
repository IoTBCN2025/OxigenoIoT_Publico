
// main.cpp - FSM robusta con reintentos, backup SD y validación de envíos
#include <Arduino.h>
#include <SD.h>
#include "api.h"
#include "ntp.h"
#include "sdlog.h"
#include "sdbackup.h"
#include "reenviarBackupSD.h"
#include "sensores_CAUDALIMETRO_YF-S201.h"
#include "sensores_TERMOCUPLA_MAX6675.h"
#include "sensores_VOLTAJE_ZMPT101B.h"

// Prototipos
void comenzarLecturaCaudal();
void detenerLecturaCaudal();
bool hayBackupsPendientes();

enum Estado {
  INICIALIZACION,
  LECTURA_CONTINUA_CAUDAL,
  LECTURA_TEMPERATURA,
  LECTURA_VOLTAJE,
  REINTENTO_BACKUP,
  IDLE,
  ERROR_RECUPERABLE
};

Estado estadoActual = INICIALIZACION;
Estado estadoAnterior = INICIALIZACION;

bool sdDisponible = false;
unsigned long ultimoEnvioCaudal = 0;
const unsigned long INTERVALO_ENVIO_CAUDAL = 1000;

// Parámetros de temporización
const int SEG_INICIO_CAUDAL = 0;
const int SEG_FIN_CAUDAL = 29;
const int SEG_LEER_TEMPERATURA = 35;
const int SEG_LEER_VOLTAJE = 40;

void setup() {
  Serial.begin(115200);
  inicializarSensorCaudal();
  inicializarSensorTermocupla();
  inicializarSensorVoltaje();

  if (!sincronizarNTP(5, 2000)) {
    logEvento("NTP_ERR", "Fallo sincronización NTP");
  }

  inicializarSD();
  sdDisponible = SD.begin(5);
  if (!sdDisponible) {
    logEvento("SD_ERR", "No se pudo inicializar SD");
    estadoActual = ERROR_RECUPERABLE;
  } else {
    estadoActual = IDLE;
  }
}

void loop() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  int segundo = t->tm_sec;
  static unsigned long long timestamp = 0;

  if (estadoActual != estadoAnterior) {
    Serial.print("Estado actual: ");
    Serial.println(estadoActual);
    estadoAnterior = estadoActual;
  }

  switch (estadoActual) {
    case IDLE:
      if (segundo >= SEG_INICIO_CAUDAL && segundo <= SEG_FIN_CAUDAL) {
        comenzarLecturaCaudal();
        estadoActual = LECTURA_CONTINUA_CAUDAL;
      } else if (segundo == SEG_LEER_TEMPERATURA) {
        estadoActual = LECTURA_TEMPERATURA;
      } else if (segundo == SEG_LEER_VOLTAJE) {
        estadoActual = LECTURA_VOLTAJE;
      } else if (hayBackupsPendientes()) {
        estadoActual = REINTENTO_BACKUP;
      }
      break;

    case LECTURA_CONTINUA_CAUDAL: {
      if (millis() - ultimoEnvioCaudal >= INTERVALO_ENVIO_CAUDAL) {
        timestamp = getTimestamp() * 1000000ULL;
        actualizarCaudal();
        float caudal = obtenerCaudalLPM();
        bool ok = enviarDatoAPI("caudal", "YF-S201", caudal, timestamp, "wifi");
        if (!ok) {
          guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
          logEvento("RESPALDO", "Caudal no enviado, respaldado en SD");
        } else {
          logEvento("API_OK", "Caudal enviado: " + String(caudal));
        }
        ultimoEnvioCaudal = millis();
      }
      if (segundo > SEG_FIN_CAUDAL) {
        detenerLecturaCaudal();
        estadoActual = IDLE;
      }
      break;
    }

    case LECTURA_TEMPERATURA: {
      timestamp = getTimestamp() * 1000000ULL;
      actualizarTermocupla();
      float temp = obtenerTemperatura();
      bool ok = enviarDatoAPI("temperatura", "MAX6675", temp, timestamp, "wifi");
      if (!ok) {
        guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
        logEvento("RESPALDO", "Temp no enviada, respaldada en SD");
      } else {
        logEvento("API_OK", "Temp enviada: " + String(temp));
      }
      estadoActual = IDLE;
      break;
    }

    case LECTURA_VOLTAJE: {
      timestamp = getTimestamp() * 1000000ULL;
      actualizarVoltaje();
      float volt = obtenerVoltajeAC();
      bool ok = enviarDatoAPI("voltaje", "ZMPT101B", volt, timestamp, "wifi");
      if (!ok) {
        guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
        logEvento("RESPALDO", "Voltaje no enviado, respaldado en SD");
      } else {
        logEvento("API_OK", "Voltaje enviado: " + String(volt));
      }
      estadoActual = REINTENTO_BACKUP;
      break;
    }

    case REINTENTO_BACKUP: {
      if (sdDisponible) {
        reenviarDatosDesdeBackup();
      }
      estadoActual = IDLE;
      break;
    }

    case ERROR_RECUPERABLE: {
      delay(1000);
      sdDisponible = SD.begin(5);
      if (sdDisponible) {
        inicializarSD();
        estadoActual = IDLE;
      }
      break;
    }

    default:
      estadoActual = IDLE;
      break;
  }
}

bool hayBackupsPendientes() {
  File root = SD.open("/");
  while (true) {
    File f = root.openNextFile();
    if (!f) break;
    String nombre = f.name();
    if (nombre.startsWith("backup_") && nombre.endsWith(".csv")) {
      logEvento("DEBUG", "Backup encontrado: " + nombre);
      f.close();
      root.close();
      return true;
    }
    f.close();
  }
  root.close();
  return false;
}
