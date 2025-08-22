// main.cpp - FSM robusta con DS3231, reintentos, backup SD, validación de envíos y watchdog WiFi
#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#include "wifi_mgr.h"      // wifiSetup(ssid, pass), wifiLoop(), wifiReady()
#include "ds3231_time.h"   // RTC DS3231
#include "api.h"
#include "ntp.h"
#include "sdlog.h"         // logger forense
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

static const char* estadoToStr(Estado e) {
  switch (e) {
    case INICIALIZACION:         return "INICIALIZACION";
    case LECTURA_CONTINUA_CAUDAL:return "LECTURA_CONTINUA_CAUDAL";
    case LECTURA_TEMPERATURA:    return "LECTURA_TEMPERATURA";
    case LECTURA_VOLTAJE:        return "LECTURA_VOLTAJE";
    case REINTENTO_BACKUP:       return "REINTENTO_BACKUP";
    case IDLE:                   return "IDLE";
    case ERROR_RECUPERABLE:      return "ERROR_RECUPERABLE";
    default:                     return "UNKNOWN";
  }
}

Estado estadoActual = INICIALIZACION;
Estado estadoAnterior = INICIALIZACION;

// ================== FLAGS / TIMERS ==================
bool sdDisponible = false;
unsigned long ultimoEnvioCaudal = 0;
const unsigned long INTERVALO_ENVIO_CAUDAL = 1000; // ms

// Ventanas temporales
const int SEG_INICIO_CAUDAL = 0;
const int SEG_FIN_CAUDAL    = 29;
const int SEG_LEER_TEMPERATURA = 35;
const int SEG_LEER_VOLTAJE     = 40;

// Timestamps inválidos (protección)
static const unsigned long long TS_INVALIDO_1 = 0ULL;
static const unsigned long long TS_INVALIDO_2 = 943920000000000ULL; // centinela heredado

// Resincronización NTP → RTC cada 6 h
const unsigned long SYNC_PERIOD_MS = 6UL * 60UL * 60UL * 1000UL;
static unsigned long lastSyncMs = 0;

// Latches por minuto
static uint32_t lastEpochMinuteTemp = UINT32_MAX;
static uint32_t lastEpochMinuteVolt = UINT32_MAX;

// Throttling de logs cuando NO hay WiFi
static unsigned long lastNoWifiLogMs = 0;
static const unsigned long NO_WIFI_LOG_EVERY_MS = 2000;

// Reintento tras reconectar WiFi
static bool wasWifiReady = false;
static volatile bool kickReintentoBackups = false;
static unsigned long lastRetryScanMs = 0;
static const unsigned long MIN_RETRY_SCAN_GAP_MS = 3000;  // evita bucle

// Heartbeat
static uint32_t lastHeartbeatMs = 0;
static const uint32_t HEARTBEAT_MS = 60000;

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(F("== Boot IoT ESP32 + DS3231 =="));

  // WiFi
  wifiSetup(WIFI_SSID, WIFI_PASS);

  // RTC
  initDS3231(21, 22);

  // SD/FS
  inicializarSD();
  sdDisponible = (SD.cardType() != CARD_NONE);

  // Contexto estático (MAC + FW_VER)
  uint8_t macRaw[6]; WiFi.macAddress(macRaw);
  char macStr[13];
  snprintf(macStr, sizeof(macStr), "%02x%02x%02x%02x%02x%02x",
           macRaw[0], macRaw[1], macRaw[2], macRaw[3], macRaw[4], macRaw[5]);


  logSetStaticContext(String(macStr), "dev");
  logSetState(estadoToStr(estadoActual));
  logSetHeapFree(ESP.getFreeHeap());
  logSetWifiRssi(wifiReady()?WiFi.RSSI():-127);

  // Estado RTC
  if (!rtcIsPresent()) {
    LOGE("RTC_ERR", "DS3231 no responde en I2C", "");
  } else if (!rtcIsTimeValid()) {
    LOGW("RTC_TIME_INVALID", "Hora RTC invalida; se intentara ajustar con NTP", "");
  } else {
    LOGI("RTC_OK", "DS3231 presente y hora valida", "");
  }

  // Sensores
  inicializarSensorCaudal();
  inicializarSensorTermocupla();
  inicializarSensorVoltaje();

  // NTP inicial
  if (wifiReady()) {
    bool ntp_ok = sincronizarNTP(5, 2000);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp(); // segundos
      if (setRTCFromUnix(unixNtp)) {
        LOGI("RTC_SET_OK", "DS3231 ajustado desde NTP", String("unix=")+String(unixNtp));
      } else {
        LOGE("RTC_SET_ERR", "Fallo al ajustar DS3231 con NTP", "");
      }
    } else {
      LOGW("NTP_ERR", "Fallo sincronizacion NTP inicial", "");
    }
  } else {
    LOGI("WIFI_WAIT", "A la espera de WiFi para NTP inicial", "");
  }
  lastSyncMs = millis();

  // Estado inicial
  estadoActual = sdDisponible ? IDLE : ERROR_RECUPERABLE;
  logSetState(estadoToStr(estadoActual));

  LOGI("BOOT", "Inicio del dispositivo",
       String("sd=") + (sdDisponible?"OK":"FAIL"));
}

// ================== LOOP ==================
void loop() {
  // Watchdog WiFi
  wifiLoop();

  // Heartbeat cada 60 s
  if (millis() - lastHeartbeatMs > HEARTBEAT_MS) {
    logSetWifiRssi(wifiReady()?WiFi.RSSI():-127);
    logSetHeapFree(ESP.getFreeHeap());
    LOGD("HEARTBEAT", "Pulso de vida", "");
    lastHeartbeatMs = millis();
  }

  // Flancos WiFi
  bool nowReady = wifiReady();
  if (nowReady && !wasWifiReady) {
    if (millis() - lastRetryScanMs > MIN_RETRY_SCAN_GAP_MS) {
      kickReintentoBackups = true;
      lastRetryScanMs = millis();
      LOGI("WIFI_UP", "WiFi restablecido; se programan reintentos de backup", "");
    }
  } else if (!nowReady && wasWifiReady) {
    LOGW("WIFI_DOWN", "WiFi desconectado", "");
  }
  wasWifiReady = nowReady;

  // Resincronización periódica NTP → RTC
  if ((millis() - lastSyncMs) > SYNC_PERIOD_MS && nowReady) {
    bool ntp_ok = sincronizarNTP(2, 1500);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      keepRTCInSyncWithNTP(true, unixNtp);
      LOGI("RTC_RESYNC", "RTC corregido contra NTP (periodico)", String("unix=")+String(unixNtp));
    } else {
      LOGW("NTP_WARN", "No se pudo resincronizar (periodico)", "");
    }
    lastSyncMs = millis();
  }

  // Tiempo actual
  uint32_t unixS = getUnixSeconds();
  int segundo = (unixS > 0) ? (int)(unixS % 60) : -1;
  uint32_t epochMinute = (unixS > 0) ? (unixS / 60) : 0;
  static unsigned long long timestamp = 0ULL;

  // Log de transición FSM
  if (estadoActual != estadoAnterior) {
    LOGD("FSM_TRANSITION", "Cambio de estado",
         String("from=") + estadoToStr(estadoAnterior) + ";to=" + estadoToStr(estadoActual));
    estadoAnterior = estadoActual;
    logSetState(estadoToStr(estadoActual));
  }

  // FSM
  switch (estadoActual) {
    case IDLE: {
      if (segundo >= SEG_INICIO_CAUDAL && segundo <= SEG_FIN_CAUDAL) {
        LOGD("FLOW_WINDOW_START", "Ventana caudal abierta", String("seg=")+String(segundo));
        comenzarLecturaCaudal();
        estadoActual = LECTURA_CONTINUA_CAUDAL;
      } else if (segundo == SEG_LEER_TEMPERATURA && epochMinute != lastEpochMinuteTemp) {
        lastEpochMinuteTemp = epochMinute;
        estadoActual = LECTURA_TEMPERATURA;
      } else if (segundo == SEG_LEER_VOLTAJE && epochMinute != lastEpochMinuteVolt) {
        lastEpochMinuteVolt = epochMinute;
        estadoActual = LECTURA_VOLTAJE;
      } else if (kickReintentoBackups && nowReady && sdDisponible) {
        kickReintentoBackups = false;
        estadoActual = REINTENTO_BACKUP;
      } else if (nowReady && hayBackupsPendientes()) {
        estadoActual = REINTENTO_BACKUP;
      } else if (nowReady && (millis() - lastRetryScanMs > 15000)) {
        lastRetryScanMs = millis();
        estadoActual = REINTENTO_BACKUP;
      }
      break;
    }

    case LECTURA_CONTINUA_CAUDAL: {
      if (millis() - ultimoEnvioCaudal >= INTERVALO_ENVIO_CAUDAL) {
        timestamp = getTimestampMicros();
        if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
          LOGW("TS_INVALID", "Timestamp invalido; no se envia caudal", String("ts=")+String(timestamp));
        } else {
          actualizarCaudal();
          float caudal = obtenerCaudalLPM();

          if (nowReady) {
            bool ok = enviarDatoAPI("caudal", "YF-S201", caudal, timestamp, "wifi");
            if (!ok) {
              guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
              LOGW("BACKUP_STORE", "Caudal no enviado, respaldado en SD",
                   String("m=caudal;s=YF-S201;v=")+String(caudal,2)+";ts="+String(timestamp));
            } else {
              LOGI("API_OK", "Caudal enviado",
                   String("m=caudal;s=YF-S201;v=")+String(caudal,2)+";ts="+String(timestamp));
            }
          } else {
            guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
            LOGW("BACKUP_STORE", "Sin WiFi, caudal respaldado en SD",
                 String("m=caudal;s=YF-S201;v=")+String(caudal,2)+";ts="+String(timestamp));
          }
        }
        ultimoEnvioCaudal = millis();
      }
      if (segundo > SEG_FIN_CAUDAL) {
        detenerLecturaCaudal();
        LOGD("FLOW_WINDOW_END", "Ventana caudal cerrada", "");
        estadoActual = IDLE;
      }
      break;
    }

    case LECTURA_TEMPERATURA: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        LOGW("TS_INVALID", "Timestamp invalido; no se envia temperatura", String("ts=")+String(timestamp));
      } else {
        actualizarTermocupla();
        float temp = obtenerTemperatura();
        if (nowReady) {
          bool ok = enviarDatoAPI("temperatura", "MAX6675", temp, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
            LOGW("BACKUP_STORE", "Temp no enviada, respaldada en SD",
                 String("m=temperatura;s=MAX6675;v=")+String(temp,2)+";ts="+String(timestamp));
          } else {
            LOGI("API_OK", "Temp enviada",
                 String("m=temperatura;s=MAX6675;v=")+String(temp,2)+";ts="+String(timestamp));
          }
        } else {
          guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
          LOGW("BACKUP_STORE", "Sin WiFi, temp respaldada en SD",
               String("m=temperatura;s=MAX6675;v=")+String(temp,2)+";ts="+String(timestamp));
        }
      }
      estadoActual = IDLE;
      break;
    }

    case LECTURA_VOLTAJE: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        LOGW("TS_INVALID", "Timestamp invalido; no se envia voltaje", String("ts=")+String(timestamp));
      } else {
        actualizarVoltaje();
        float volt = obtenerVoltajeAC();
        if (nowReady) {
          bool ok = enviarDatoAPI("voltaje", "ZMPT101B", volt, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
            LOGW("BACKUP_STORE", "Voltaje no enviado, respaldado en SD",
                 String("m=voltaje;s=ZMPT101B;v=")+String(volt,2)+";ts="+String(timestamp));
          } else {
            LOGI("API_OK", "Voltaje enviado",
                 String("m=voltaje;s=ZMPT101B;v=")+String(volt,2)+";ts="+String(timestamp));
          }
        } else {
          guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
          LOGW("BACKUP_STORE", "Sin WiFi, voltaje respaldado en SD",
               String("m=voltaje;s=ZMPT101B;v=")+String(volt,2)+";ts="+String(timestamp));
        }
      }
      estadoActual = nowReady ? REINTENTO_BACKUP : IDLE;
      break;
    }

    case REINTENTO_BACKUP: {
      if (sdDisponible && nowReady) {
        static unsigned long lastRetryLogMs = 0;
        if (millis() - lastRetryLogMs > 10000) {
          LOGI("REINTENTO_INFO", "Procesando backups pendientes", "");
          lastRetryLogMs = millis();
        }
        LOGI("BACKUP_RETRY", "Intento de reenvío desde SD", "");
        reenviarDatosDesdeBackup();
      } else if (!nowReady) {
        if (millis() - lastNoWifiLogMs > NO_WIFI_LOG_EVERY_MS) {
          LOGI("REINTENTO_WAIT", "Sin WiFi, se difiere reenvio de backups", "");
          lastNoWifiLogMs = millis();
        }
      }
      estadoActual = IDLE;
      break;
    }

    case ERROR_RECUPERABLE: {
      delay(1000);
      LOGW("SD_REINIT", "Reintentando SD", "");
      inicializarSD();
      sdDisponible = (SD.cardType() != CARD_NONE);
      if (sdDisponible) {
        LOGI("SD_OK", "SD re-inicializada tras error", "");
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