// main.cpp - FSM robusta con DS3231, reintentos, backup SD, validación de envíos y watchdog WiFi
#include <Arduino.h>
#include <SD.h>
#include <sys/time.h>
#include <time.h>
#include <WiFi.h>
#include "esp_system.h"

#include "wifi_mgr.h"
#include "ds3231_time.h"
#include "api.h"
#include "ntp.h"
#include "sdlog.h"         // << logger anti-reentrancia
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
static inline void kv_caudal(char* kv, size_t n, float v, unsigned long long ts) {
  snprintf(kv, n, "m=caudal;s=YF-S201;v=%.2f;ts=%llu", v, (unsigned long long)ts);
}
static inline void kv_temp(char* kv, size_t n, float v, unsigned long long ts) {
  snprintf(kv, n, "m=temperatura;s=MAX6675;v=%.2f;ts=%llu", v, (unsigned long long)ts);
}
static inline void kv_volt(char* kv, size_t n, float v, unsigned long long ts) {
  snprintf(kv, n, "m=voltaje;s=ZMPT101B;v=%.2f;ts=%llu", v, (unsigned long long)ts);
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
const int SEG_INICIO_CAUDAL = 0;
const int SEG_FIN_CAUDAL    = 29;
const int SEG_LEER_TEMPERATURA = 35;
const int SEG_LEER_VOLTAJE     = 40;

// Latches por minuto
static uint32_t windowMinute = UINT32_MAX;
static bool ventanaCaudalAbierta = false;
static uint32_t lastEpochMinuteTemp = UINT32_MAX;
static uint32_t lastEpochMinuteVolt = UINT32_MAX;

// Timestamps inválidos (protección)
static const unsigned long long TS_INVALIDO_1 = 0ULL;
static const unsigned long long TS_INVALIDO_2 = 943920000000000ULL;

// Resincronización NTP → RTC cada 6 h
const unsigned long SYNC_PERIOD_MS = 6UL * 60UL * 60UL * 1000UL;
static unsigned long lastSyncMs = 0;

// Throttling de logs
static unsigned long lastNoWifiLogMs = 0;
static const unsigned long NO_WIFI_LOG_EVERY_MS = 2000;
static bool wasWifiReady = false;
static volatile bool kickReintentoBackups = false;
static unsigned long lastRetryScanMs = 0;
static const unsigned long MIN_RETRY_SCAN_GAP_MS = 3000;
static unsigned long lastWifiLostLogMs  = 0;
static const unsigned long WIFI_THROTTLE_MS = 10000;

// Heartbeat
static uint32_t lastHeartbeatMs = 0;
static const uint32_t HEARTBEAT_MS = 60000;

// Quiet boot (reduce ruido primeros 30s)
static uint32_t bootStartMs = 0;
// Desactivado para ver todo por serie como solicitaste:
static bool bootQuiet = false;
static inline bool quietBoot() {
  if (bootQuiet && millis() - bootStartMs > 30000) bootQuiet = false;
  return bootQuiet;
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(F("== Boot IoT ESP32 + DS3231 =="));

  // Boot reason
  {
    esp_reset_reason_t r = esp_reset_reason();
    char kv[48]; snprintf(kv, sizeof(kv), "reason=%d", (int)r);
    LOGI("BOOT_REASON", "Reset detectado", kv);
  }

  // WiFi
  wifiSetup(WIFI_SSID, WIFI_PASS);

  // RTC
  initDS3231(21, 22);

  // Hora de sistema lo antes posible (evita 1970 en logs)
  bool horaLista = false;
  if (!rtcIsPresent() || !rtcIsTimeValid()) {
    LOGW("RTC_LOST_POWER", "RTC indica perdida de energia", "");
    if (wifiReady() && sincronizarNTP(3, 1500)) {
      uint32_t u = (uint32_t)getTimestamp();
      setRTCFromUnix(u);
      setSystemClock(u);
      horaLista = true;
    } else {
      setSystemClock(946684800UL); // 2000-01-01 para logs legibles
    }
  } else {
    uint32_t u = (uint32_t)getTimestamp();
    setSystemClock(u);
    horaLista = true;
  }

  // SD
  inicializarSD();
  sdDisponible = (SD.cardType() != CARD_NONE);

  // Contexto estático (MAC + versión)
  uint8_t macRaw[6]; WiFi.macAddress(macRaw);
  char macStr[13];
  snprintf(macStr, sizeof(macStr), "%02x%02x%02x%02x%02x%02x",
           macRaw[0], macRaw[1], macRaw[2], macRaw[3], macRaw[4], macRaw[5]);
  logSetStaticContext(String(macStr), "dev");
  logSetState(estadoToStr(estadoActual));
  logSetHeapFree(ESP.getFreeHeap());
  logSetWifiRssi(wifiReady()?WiFi.RSSI():-127);

  // Sensores (inicializar tras fijar hora del sistema)
  inicializarSensorCaudal();
  inicializarSensorTermocupla();
  inicializarSensorVoltaje();

  // NTP inicial si hora no lista
  if (!horaLista && wifiReady()) {
    if (sincronizarNTP(5,2000)) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      setRTCFromUnix(unixNtp);
      setSystemClock(unixNtp);
      char kv[32]; snprintf(kv, sizeof(kv), "unix=%lu", (unsigned long)unixNtp);
      LOGI("RTC_SET_OK", "DS3231 ajustado desde NTP", kv);
      horaLista = true;
    } else {
      LOGW("NTP_ERR", "Fallo sincronizacion NTP inicial", "");
    }
  } else if (horaLista) {
    LOGI("RTC_OK", "DS3231 presente y hora valida", "");
  } else {
    LOGI("WIFI_WAIT", "A la espera de WiFi para NTP inicial", "");
  }
  lastSyncMs = millis();

  // Estado inicial
  estadoActual = sdDisponible ? IDLE : ERROR_RECUPERABLE;
  logSetState(estadoToStr(estadoActual));

  {
    char kv[16];
    snprintf(kv, sizeof(kv), "sd=%s", sdDisponible ? "OK" : "FAIL");
    LOGI("BOOT", "Inicio del dispositivo", kv);
  }

  bootStartMs = millis();
}

// ================== LOOP ==================
void loop() {
  wifiLoop();

  if (millis() - lastHeartbeatMs > HEARTBEAT_MS) {
    logSetWifiRssi(wifiReady()?WiFi.RSSI():-127);
    logSetHeapFree(ESP.getFreeHeap());
    if (!quietBoot()) LOGD("HEARTBEAT", "Pulso de vida", "");
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
    if (millis() - lastWifiLostLogMs > WIFI_THROTTLE_MS) {
      LOGW("WIFI_DOWN", "WiFi desconectado", "");
      lastWifiLostLogMs = millis();
    }
  }
  wasWifiReady = nowReady;

  // Resincronización periódica NTP → RTC
  if ((millis() - lastSyncMs) > SYNC_PERIOD_MS && nowReady) {
    bool ntp_ok = sincronizarNTP(2, 1500);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      keepRTCInSyncWithNTP(true, unixNtp);
      char kv[32]; snprintf(kv, sizeof(kv), "unix=%lu", (unsigned long)unixNtp);
      LOGI("RTC_RESYNC", "RTC corregido contra NTP (periodico)", kv);
    } else {
      LOGW("NTP_WARN", "No se pudo resincronizar (periodico)", "");
    }
    lastSyncMs = millis();
  }

  // ===== Tiempo actual (con fallback a reloj de sistema o millis) =====
  uint32_t unixS;
  if (rtcIsPresent() && rtcIsTimeValid()) {
    unixS = getUnixSeconds();                    // DS3231
  } else {
    time_t sys = time(nullptr);                  // reloj de sistema
    unixS = (sys > 0) ? (uint32_t)sys : 0;
  }

  int segundo = -1;
  uint32_t epochMinute = 0;

  if (unixS >= 946684800UL) {                    // >= 2000-01-01 → plausible
    segundo = (int)(unixS % 60);
    epochMinute = unixS / 60;
  } else {
    // Fallback: no bloquear FSM si no hay epoch plausible
    uint32_t s_from_ms = (millis() / 1000UL);
    segundo = (int)(s_from_ms % 60UL);
    epochMinute = s_from_ms / 60UL;
  }

  static unsigned long long timestamp = 0ULL;

  // Log de transición FSM
  if (estadoActual != estadoAnterior) {
    char kv[96];
    snprintf(kv, sizeof(kv), "from=%s;to=%s", estadoToStr(estadoAnterior), estadoToStr(estadoActual));
    LOGD("FSM_TRANSITION", "Cambio de estado", kv);
    estadoAnterior = estadoActual;
    logSetState(estadoToStr(estadoActual));
  }

  // FSM
  switch (estadoActual) {
    case IDLE: {
      if (!ventanaCaudalAbierta && segundo == SEG_INICIO_CAUDAL && epochMinute != windowMinute) {
        windowMinute = epochMinute;
        ventanaCaudalAbierta = true;
        LOGD("FLOW_WINDOW_START", "Ventana caudal abierta", "seg=0");
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
          char kv[32]; snprintf(kv, sizeof(kv), "ts=%llu", (unsigned long long)timestamp);
          LOGW("TS_INVALID", "Timestamp invalido; no se envia caudal", kv);
        } else {
          actualizarCaudal();
          float caudal = obtenerCaudalLPM();

          if (nowReady) {
            bool ok = enviarDatoAPI("caudal", "YF-S201", caudal, timestamp, "wifi");
            if (!ok) {
              guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup", "api_fail");
            } else {
              char kv2[96]; kv_caudal(kv2, sizeof(kv2), caudal, timestamp);
              LOGI("API_OK", "Caudal enviado", kv2);
            }
          } else {
            guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup", "wifi_down");
          }
        }
        ultimoEnvioCaudal = millis();
      }

      if (segundo == (SEG_FIN_CAUDAL + 1) && ventanaCaudalAbierta) {
        detenerLecturaCaudal();
        ventanaCaudalAbierta = false;
        LOGD("FLOW_WINDOW_END", "Ventana caudal cerrada", "");
        estadoActual = IDLE;
      }
      break;
    }

    case LECTURA_TEMPERATURA: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        char kv[32]; snprintf(kv, sizeof(kv), "ts=%llu", (unsigned long long)timestamp);
        LOGW("TS_INVALID", "Timestamp invalido; no se envia temperatura", kv);
      } else {
        actualizarTermocupla();
        float temp = obtenerTemperatura();
        if (nowReady) {
          bool ok = enviarDatoAPI("temperatura", "MAX6675", temp, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup", "api_fail");
          } else {
            char kv2[96]; kv_temp(kv2, sizeof(kv2), temp, timestamp);
            LOGI("API_OK", "Temp enviada", kv2);
          }
        } else {
          guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup", "wifi_down");
        }
      }
      estadoActual = IDLE;
      break;
    }

    case LECTURA_VOLTAJE: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        char kv[32]; snprintf(kv, sizeof(kv), "ts=%llu", (unsigned long long)timestamp);
        LOGW("TS_INVALID", "Timestamp invalido; no se envia voltaje", kv);
      } else {
        actualizarVoltaje();
        float volt = obtenerVoltajeAC();
        if (nowReady) {
          bool ok = enviarDatoAPI("voltaje", "ZMPT101B", volt, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup", "api_fail");
          } else {
            char kv2[96]; kv_volt(kv2, sizeof(kv2), volt, timestamp);
            LOGI("API_OK", "Voltaje enviado", kv2);
          }
        } else {
          guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup", "wifi_down");
        }
      }
      estadoActual = nowReady ? REINTENTO_BACKUP : IDLE;
      break;
    }

    case REINTENTO_BACKUP: {
      if (sdDisponible && nowReady) {
        static unsigned long lastRetryLogMs = 0;
        if (!quietBoot() && millis() - lastRetryLogMs > 10000) {
          LOGI("REINTENTO_INFO", "Procesando backups pendientes", "");
          lastRetryLogMs = millis();
        }
        reenviarDatosDesdeBackup();
        yield();
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
    if (name.indexOf("1970") >= 0) continue;

    String path = rootSlash(name);
    File csv = SD.open(path, FILE_READ);
    if (!csv) continue;
    uint32_t size = csv.size();
    csv.close();

    uint32_t off = 0;
    if (!leerIdxLocal(path + ".idx", off)) { pendiente = true; break; }
    if (off < size)                 { pendiente = true; break; }
  }
  root.close();
  return pendiente;
}
