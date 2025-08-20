// main.cpp - FSM robusta con DS3231, reintentos, backup SD, validación de envíos y watchdog WiFi

#include <Arduino.h>
#include <SD.h>
#include <sys/time.h>   // settimeofday()

// === WIFI (watchdog + eventos) ===
#include <WiFi.h>
#include "wifi_mgr.h"      // wifiSetup(ssid, pass), wifiLoop(), wifiReady()

// === TIEMPO (RTC DS3231) ===
#include "ds3231_time.h"

// === MÓDULOS EXISTENTES ===
#include "api.h"
#include "ntp.h"
#include "sdlog.h"          // inicializarSD(), logEvento(), reintentarLogsPendientes()
#include "sdbackup.h"
#include "reenviarBackupSD.h"
#include "sensores_CAUDALIMETRO_YF-S201.h"
#include "sensores_TERMOCUPLA_MAX6675.h"
#include "sensores_VOLTAJE_ZMPT101B.h"

// Credenciales WiFi
static const char* WIFI_SSID = "Jose Monje Ruiz";
static const char* WIFI_PASS = "QWERTYUI2022";

// Prototipos
void comenzarLecturaCaudal();
void detenerLecturaCaudal();
bool hayBackupsPendientes();

// ================== Helpers hora sistema ==================
static inline bool isValidEpoch(uint32_t s) { return s >= 946684800UL; } // >= 2000-01-01
static inline void setSystemClock(uint32_t unixSec) {
  timeval tv; tv.tv_sec = (time_t)unixSec; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

// ================== FSM ==================
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

// ================== FLAGS / TIMERS ==================
bool sdDisponible = false;
unsigned long ultimoEnvioCaudal = 0;
const unsigned long INTERVALO_ENVIO_CAUDAL = 1000; // ms

// Parámetros de temporización (segundo del minuto)
const int SEG_INICIO_CAUDAL = 0;
const int SEG_FIN_CAUDAL    = 29;
const int SEG_LEER_TEMPERATURA = 35;
const int SEG_LEER_VOLTAJE     = 40;

// Constantes de timestamp inválido (protección)
static const unsigned long long TS_INVALIDO_1 = 0ULL;
static const unsigned long long TS_INVALIDO_2 = 943920000000000ULL; // centinela heredado

// Resincronización NTP → RTC cada 6 h
const unsigned long SYNC_PERIOD_MS = 6UL * 60UL * 60UL * 1000UL;
static unsigned long lastSyncMs = 0;

// Latches por minuto (evitan doble disparo)
static uint32_t lastEpochMinuteTemp = UINT32_MAX;
static uint32_t lastEpochMinuteVolt = UINT32_MAX;

// Throttling de logs cuando NO hay WiFi
static unsigned long lastNoWifiLogMs = 0;
static const unsigned long NO_WIFI_LOG_EVERY_MS = 2000;

// Impulso de reintento tras reconectar WiFi
static bool wasWifiReady = false;
static volatile bool kickReintentoBackups = false;
static unsigned long lastRetryScanMs = 0;
static const unsigned long MIN_RETRY_SCAN_GAP_MS = 3000;  // evita entrar en bucle

// Latch para bootstrap NTP si el RTC era inválido al arranque
static bool ntpBootstrapPending = false;

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(F("== Boot IoT ESP32 + DS3231 =="));

  // Watchdog WiFi + eventos
  wifiSetup(WIFI_SSID, WIFI_PASS);

  // 1) Inicializa RTC
  initDS3231(21, 22);

  // Si el RTC es válido, súbelo al reloj del sistema para evitar 1970
  if (rtcIsPresent() && rtcIsTimeValid()) {
    uint32_t rtcSec = getUnixSeconds();
    if (isValidEpoch(rtcSec)) setSystemClock(rtcSec);
  }

  // 2) SD/FS
  inicializarSD();
  sdDisponible = (SD.cardType() != CARD_NONE);

  // 3) Log estado RTC
  if (!rtcIsPresent()) {
    logEvento("RTC_ERR", "DS3231 no responde en I2C");
  } else if (!rtcIsTimeValid()) {
    logEvento("RTC_TIME_INVALID", "Hora RTC invalida; se intentara ajustar con NTP");
  } else {
    logEvento("RTC_OK", "DS3231 presente y hora valida");
  }

  // 4) Sensores
  inicializarSensorCaudal();
  inicializarSensorTermocupla();
  inicializarSensorVoltaje();

  // 5) NTP inicial (si hay WiFi)
  if (wifiReady()) {
    bool ntp_ok = sincronizarNTP(5, 2000);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp(); // segundos (según tu impl.)
      if (setRTCFromUnix(unixNtp)) {
        logEvento("RTC_SET_OK", "DS3231 ajustado desde NTP");
      } else {
        logEvento("RTC_SET_ERR", "Fallo al ajustar DS3231 con NTP");
      }
      setSystemClock(unixNtp); // <-- evita 1970 en logs
    } else {
      logEvento("NTP_ERR", "Fallo sincronizacion NTP inicial");
    }
  } else {
    logEvento("WIFI_WAIT", "A la espera de WiFi para NTP inicial");
  }
  lastSyncMs = millis();

  // Si el RTC estaba inválido al boot, dejamos pendiente el bootstrap NTP
  ntpBootstrapPending = !rtcIsTimeValid();

  // 6) Estado inicial
  estadoActual = sdDisponible ? IDLE : ERROR_RECUPERABLE;

  logEvento("BOOT", "Inicio del dispositivo");
}

// ================== LOOP ==================
void loop() {
  // Watchdog WiFi (no bloqueante)
  wifiLoop();

  // Flanco de subida: WiFi pasó de no listo -> listo
  bool nowReady = wifiReady();
  if (nowReady && !wasWifiReady) {
    // Evita tormenta de reintentos con un pequeño gap
    if (millis() - lastRetryScanMs > MIN_RETRY_SCAN_GAP_MS) {
      kickReintentoBackups = true;
      lastRetryScanMs = millis();
      logEvento("WIFI_UP", "WiFi restablecido; se programan reintentos de backup");
    }
  }
  wasWifiReady = nowReady;

  // Bootstrap NTP si el RTC era inválido al arranque y ya hay WiFi
  if (ntpBootstrapPending && nowReady) {
    if (sincronizarNTP(3, 1500)) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      setRTCFromUnix(unixNtp);
      setSystemClock(unixNtp);
      logEvento("RTC_RESYNC", "RTC y system clock alineados (bootstrap)");
      ntpBootstrapPending = false;
    }
  }

  // Resincronización periódica NTP → RTC (cada 6h), solo si hay WiFi
  if ((millis() - lastSyncMs) > SYNC_PERIOD_MS && nowReady) {
    bool ntp_ok = sincronizarNTP(2, 1500);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      keepRTCInSyncWithNTP(true, unixNtp);
      setSystemClock(unixNtp); // mantener system clock alineado
      logEvento("RTC_RESYNC", "RTC corregido contra NTP (periodico)");
    } else {
      logEvento("NTP_WARN", "No se pudo resincronizar (periodico)");
    }
    lastSyncMs = millis();
  }

  // Segundo del minuto desde DS3231
  uint32_t unixS = getUnixSeconds();
  int segundo = (unixS > 0) ? (int)(unixS % 60) : -1;
  uint32_t epochMinute = (unixS > 0) ? (unixS / 60) : 0;
  static unsigned long long timestamp = 0ULL;

  if (estadoActual != estadoAnterior) {
    Serial.print(F("Estado actual: "));
    Serial.println(estadoActual);
    estadoAnterior = estadoActual;
  }

  switch (estadoActual) {
    case IDLE: {
      if (segundo >= SEG_INICIO_CAUDAL && segundo <= SEG_FIN_CAUDAL) {
        comenzarLecturaCaudal();
        estadoActual = LECTURA_CONTINUA_CAUDAL;
      } else if (segundo == SEG_LEER_TEMPERATURA && epochMinute != lastEpochMinuteTemp) {
        lastEpochMinuteTemp = epochMinute;
        estadoActual = LECTURA_TEMPERATURA;
      } else if (segundo == SEG_LEER_VOLTAJE && epochMinute != lastEpochMinuteVolt) {
        lastEpochMinuteVolt = epochMinute;
        estadoActual = LECTURA_VOLTAJE;
      } else if (kickReintentoBackups && nowReady && sdDisponible) {
        // Impulso de reintento tras reconectar
        kickReintentoBackups = false;
        estadoActual = REINTENTO_BACKUP;
      } else if (nowReady && hayBackupsPendientes()) {
        // Camino “normal” por detección rápida
        estadoActual = REINTENTO_BACKUP;
      } else if (nowReady && (millis() - lastRetryScanMs > 15000)) {
        // Plan B: cada 15 s, aunque hayBackupsPendientes() no detecte, pegamos un vistazo
        lastRetryScanMs = millis();
        estadoActual = REINTENTO_BACKUP;
      }
      break;
    }

    case LECTURA_CONTINUA_CAUDAL: {
      if (millis() - ultimoEnvioCaudal >= INTERVALO_ENVIO_CAUDAL) {
        timestamp = getTimestampMicros();
        if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
          logEvento("TS_INVALID", "Timestamp invalido; no se envia caudal");
        } else {
          actualizarCaudal();
          float caudal = obtenerCaudalLPM();

          if (nowReady) {
            bool ok = enviarDatoAPI("caudal", "YF-S201", caudal, timestamp, "wifi");
            if (!ok) {
              guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
              logEvento("RESPALDO", "Caudal no enviado, respaldado en SD");
            } else {
              logEvento("API_OK", "Caudal enviado: " + String(caudal));
            }
          } else {
            guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
            logEvento("RESPALDO", "Sin WiFi, caudal respaldado en SD");
          }
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
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        logEvento("TS_INVALID", "Timestamp invalido; no se envia temperatura");
      } else {
        actualizarTermocupla();
        float temp = obtenerTemperatura();
        if (nowReady) {
          bool ok = enviarDatoAPI("temperatura", "MAX6675", temp, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
            logEvento("RESPALDO", "Temp no enviada, respaldada en SD");
          } else {
            logEvento("API_OK", "Temp enviada: " + String(temp));
          }
        } else {
          guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
          logEvento("RESPALDO", "Sin WiFi, temp respaldada en SD");
        }
      }
      estadoActual = IDLE;
      break;
    }

    case LECTURA_VOLTAJE: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        logEvento("TS_INVALID", "Timestamp invalido; no se envia voltaje");
      } else {
        actualizarVoltaje();
        float volt = obtenerVoltajeAC();
        if (nowReady) {
          bool ok = enviarDatoAPI("voltaje", "ZMPT101B", volt, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
            logEvento("RESPALDO", "Voltaje no enviado, respaldado en SD");
          } else {
            logEvento("API_OK", "Voltaje enviado: " + String(volt));
          }
        } else {
          guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
          logEvento("RESPALDO", "Sin WiFi, voltaje respaldado en SD");
        }
      }
      // tras voltaje, si hay WiFi, intenta limpiar pendientes
      estadoActual = nowReady ? REINTENTO_BACKUP : IDLE;
      break;
    }

    case REINTENTO_BACKUP: {
      if (sdDisponible && nowReady) {
        static unsigned long lastRetryLogMs = 0;
        if (millis() - lastRetryLogMs > 10000) {
          logEvento("REINTENTO_INFO", "Procesando backups pendientes");
          lastRetryLogMs = millis();
        }
        // Llama SIEMPRE: internamente tiene throttle y saldrá rápido si no hay trabajo
        reenviarDatosDesdeBackup();
      } else if (!nowReady) {
        if (millis() - lastNoWifiLogMs > NO_WIFI_LOG_EVERY_MS) {
          logEvento("REINTENTO_WAIT", "Sin WiFi, se difiere reenvio de backups");
          lastNoWifiLogMs = millis();
        }
      }
      estadoActual = IDLE;
      break;
    }

    case ERROR_RECUPERABLE: {
      delay(1000);
      inicializarSD();
      sdDisponible = (SD.cardType() != CARD_NONE);
      if (sdDisponible) {
        logEvento("SD_OK", "SD re-inicializada tras error");
        reintentarLogsPendientes();
        estadoActual = IDLE;
      }
      break;
    }

    default:
      estadoActual = IDLE;
      break;
  }
}

// ================== DETECCIÓN DE BACKUPS ==================
static bool leerIdxLocal(const String& idxPath, uint32_t& off) {
  if (!SD.exists(idxPath)) return false;
  File f = SD.open(idxPath, FILE_READ);
  if (!f) return false;
  String s = f.readStringUntil('\n'); f.close();
  s.trim();
  if (!s.length()) return false;
  off = (uint32_t)strtoul(s.c_str(), nullptr, 10);
  return true;
}

static String rootSlash(const String& p) {
  return (p.length() && p[0]=='/') ? p : ("/" + p);
}

bool hayBackupsPendientes() {
  if (!sdDisponible) return false;

  File root = SD.open("/");
  if (!root) return false;

  bool pendiente = false;
  while (true) {
    File e = root.openNextFile();
    if (!e) break;
    String name = e.name(); e.close();

    if (!name.startsWith("backup_") || !name.endsWith(".csv")) continue;
    if (name.indexOf("1970") >= 0) continue;                 // evita centinelas

    String path = rootSlash(name);
    File csv = SD.open(path, FILE_READ);
    if (!csv) continue;
    uint32_t size = csv.size();
    csv.close();

    uint32_t off = 0;
    if (!leerIdxLocal(path + ".idx", off)) { pendiente = true; break; } // sin idx: hay trabajo
    if (off < size)                 { pendiente = true; break; }        // idx por detrás: hay trabajo
  }
  root.close();
  return pendiente;
}
