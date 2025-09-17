#include <Arduino.h>
#include <SD.h>
#include "config.h"
Config config = loadDefaultConfig();

#include <WiFi.h>
#include "wifi_mgr.h"
#include "ds3231_time.h"
#include "api.h"
#include "ntp.h"
#include "sdlog.h"
#include "sdbackup.h"
#include "reenviarBackupSD.h"
#include "sensores_CAUDALIMETRO_YF-S201.h"
#include "sensores_TERMOCUPLA_MAX6675.h"
#include "sensores_VOLTAJE_ZMPT101B.h"
#include "spi_temp.h"  // para inicializar HSPI exclusivo

#ifndef FW_VERSION
#define FW_VERSION "1.4.1"
#endif
#ifndef FW_BUILD
#define FW_BUILD __DATE__ " " __TIME__
#endif

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

static const unsigned long long TS_INVALIDO_1 = 0ULL;
static const unsigned long long TS_INVALIDO_2 = 943920000000000ULL;

const unsigned long SYNC_PERIOD_MS = 6UL * 60UL * 60UL * 1000UL;
static unsigned long lastSyncMs = 0;

static uint32_t lastEpochMinuteTemp = UINT32_MAX;
static uint32_t lastEpochMinuteVolt = UINT32_MAX;

static unsigned long lastNoWifiLogMs = 0;
static const unsigned long NO_WIFI_LOG_EVERY_MS = 2000;

static bool wasWifiReady = false;
static volatile bool kickReintentoBackups = false;
static unsigned long lastRetryScanMs = 0;
static const unsigned long MIN_RETRY_SCAN_GAP_MS = 3000;

static unsigned long lastInvalidRtcSyncTryMs = 0;
static const unsigned long INVALID_RTC_SYNC_RETRY_MS = 10000;

static inline unsigned long long ts_fallback_micros() {
  return (unsigned long long)millis() * 1000ULL;
}

static uint8_t g_upCount = 0;
static uint8_t g_failCount = 0;

void setup() {
  const uint32_t t0 = millis();
  Serial.begin(115200);
  delay(50);
  Serial.println(F("== Boot IoT ESP32 + DS3231 =="));

  char kv[160];
  snprintf(kv, sizeof(kv), "fw=%s;build=%s;heap=%luB", FW_VERSION, FW_BUILD, (unsigned long)ESP.getFreeHeap());
  logEventoM("SYS", "BOOT_INFO", kv);

  wifiSetup(config.network.ssid, config.network.password);
  initDS3231(config.pins.SDA, config.pins.SCL);

  inicializarSD();
  sdDisponible = (SD.cardType() != CARD_NONE);
  if (sdDisponible) { logEventoM("SD", "MOD_UP",   "logger=v2"); g_upCount++; }
  else              { logEventoM("SD", "MOD_FAIL", "err=not_detected"); g_failCount++; }

  if (!rtcIsPresent()) {
    logEventoM("RTC", "RTC_ERR", "no_i2c");
    logEventoM("RTC", "MOD_FAIL", "err=no_i2c");
    g_failCount++;
  } else if (!rtcIsTimeValid()) {
    logEventoM("RTC", "RTC_TIME_INVALID", "will_set_ntp");
    logEventoM("RTC", "MOD_FAIL", "err=time_invalid");
    g_failCount++;
  } else {
    logEventoM("RTC", "RTC_OK", "valid=1");
    logEventoM("RTC", "MOD_UP", "source=RTC_only");
    g_upCount++;
  }

  // === Inicialización de módulos ===
  inicializarSensorCaudal();
  iniciarSPITermocupla(); // Inicializa HSPI dedicado
  inicializarSensorTermocupla();
  inicializarSensorVoltaje();

  if (wifiReady()) {
    if (sincronizarNTP(5, 2000)) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      if (setRTCFromUnix(unixNtp)) {
        logEventoM("NTP", "RTC_SET_OK", "phase=setup");
        logEventoM("NTP", "MOD_UP", "phase=setup");
        g_upCount++;
      } else {
        logEventoM("NTP", "RTC_SET_ERR", "phase=setup");
        logEventoM("NTP", "MOD_FAIL", "err=set_rtc");
        g_failCount++;
      }
    } else {
      logEventoM("NTP", "NTP_ERR", "phase=setup");
      logEventoM("NTP", "MOD_FAIL", "err=ntp_sync");
      g_failCount++;
    }
  } else {
    logEventoM("WIFI", "WIFI_WAIT", "ntp_initial");
    logEventoM("WIFI", "MOD_FAIL", "err=no_ip_boot");
    g_failCount++;
  }

  lastSyncMs = millis();
  estadoActual = sdDisponible ? IDLE : ERROR_RECUPERABLE;

  snprintf(kv, sizeof(kv), "up=%u;fail=%u;elapsed_ms=%lu", (unsigned)g_upCount, (unsigned)g_failCount, (unsigned long)(millis() - t0));
  logEventoM("SYS", "STARTUP_SUMMARY", kv);
  logEventoM("SYS", "BOOT", "device_start");
}

// ================== LOOP ==================
void loop() {
  // Watchdog WiFi (no bloqueante)
  wifiLoop();

  // Flanco de subida WiFi
  bool nowReady = wifiReady();
  if (nowReady && !wasWifiReady) {
    if (millis() - lastRetryScanMs > MIN_RETRY_SCAN_GAP_MS) {
      kickReintentoBackups = true;
      lastRetryScanMs = millis();
      logEventoM("WIFI", "WIFI_UP", "reintentos_backup=1");
    }

    // Sincronizar NTP al tener WiFi
    bool ntp_ok = sincronizarNTP(4, 800);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      if (setRTCFromUnix(unixNtp)) {
        logEventoM("NTP", "RTC_SET_OK", "phase=wifi_up");
        logEventoM("NTP", "MOD_UP", "phase=wifi_up");
      } else {
        logEventoM("NTP", "RTC_SET_ERR", "phase=wifi_up");
        logEventoM("NTP", "MOD_FAIL", "err=set_rtc_wifiup");
      }
    } else {
      logEventoM("NTP", "NTP_ERR", "phase=wifi_up");
      logEventoM("NTP", "MOD_FAIL", "err=ntp_sync_wifiup");
    }
  }
  wasWifiReady = nowReady;

  // Retry NTP si RTC inválido
  if (nowReady && !rtcIsTimeValid() && (millis() - lastInvalidRtcSyncTryMs > INVALID_RTC_SYNC_RETRY_MS)) {
    lastInvalidRtcSyncTryMs = millis();
    bool ntp_ok = sincronizarNTP(3, 800);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      if (setRTCFromUnix(unixNtp)) {
        logEventoM("NTP", "RTC_SET_OK", "phase=retry_invalid");
        logEventoM("NTP", "MOD_UP", "phase=retry_invalid");
      } else {
        logEventoM("NTP", "RTC_SET_ERR", "phase=retry_invalid");
        logEventoM("NTP", "MOD_FAIL", "err=set_rtc_retry");
      }
    } else {
      logEventoM("NTP", "NTP_WARN", "retry_with_invalid_rtc");
    }
  }

  // Resincronización periódica
  if ((millis() - lastSyncMs) > SYNC_PERIOD_MS && nowReady) {
    bool ntp_ok = sincronizarNTP(2, 1500);
    if (ntp_ok) {
      uint32_t unixNtp = (uint32_t)getTimestamp();
      keepRTCInSyncWithNTP(true, unixNtp);
      logEventoM("NTP", "RTC_RESYNC", "periodic");
    } else {
      logEventoM("NTP", "NTP_WARN", "periodic_resync_failed");
    }
    lastSyncMs = millis();
  }

  // Segundo del minuto (con fallback si RTC inválido)
  uint32_t unixS = getUnixSeconds();
  int segundo = (unixS > 0) ? (int)(unixS % 60) : (int)((millis() / 1000UL) % 60);
  uint32_t epochMinute = (unixS > 0) ? (unixS / 60) : (uint32_t)((millis() / 1000UL) / 60UL);
  static unsigned long long timestamp = 0ULL;

  if (estadoActual != estadoAnterior) {
    char kv[24]; snprintf(kv, sizeof(kv), "state=%d", (int)estadoActual);
    logEventoM("FSM", "FSM_STATE", kv);
    estadoAnterior = estadoActual;
  }

  switch (estadoActual) {
    case IDLE: {
      if (segundo >= 0 && segundo <= config.timing.window_caudal) {
        comenzarLecturaCaudal();
        estadoActual = LECTURA_CONTINUA_CAUDAL;

      } else if (segundo == config.timing.window_temp && epochMinute != lastEpochMinuteTemp) {
        lastEpochMinuteTemp = epochMinute;
        estadoActual = LECTURA_TEMPERATURA;

      } else if (segundo == config.timing.window_voltage && epochMinute != lastEpochMinuteVolt) {
        lastEpochMinuteVolt = epochMinute;
        estadoActual = LECTURA_VOLTAJE;

      } else if (kickReintentoBackups && nowReady && sdDisponible) {
        kickReintentoBackups = false;
        lastRetryScanMs = millis();
        estadoActual = REINTENTO_BACKUP;

      } else if (nowReady && hayBackupsPendientes()) {
        lastRetryScanMs = millis();
        estadoActual = REINTENTO_BACKUP;

      } else if (nowReady && (millis() - lastRetryScanMs > 30000)) {
        lastRetryScanMs = millis();
        estadoActual = REINTENTO_BACKUP;
      }
      break;
    }

    case LECTURA_CONTINUA_CAUDAL: {
      if (millis() - ultimoEnvioCaudal >= INTERVALO_ENVIO_CAUDAL) {
        timestamp = getTimestampMicros();

        if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
          unsigned long long ts_fb = ts_fallback_micros();
          actualizarCaudal();
          float caudal = obtenerCaudalLPM();
          guardarEnBackupSD("caudal", "YF-S201", caudal, ts_fb, "backup");
          logEventoM("SD_BACKUP", "TS_INVALID_BACKUP", "sensor=YF-S201");
        } else {
          actualizarCaudal();
          float caudal = obtenerCaudalLPM();

          if (nowReady) {
            bool ok = enviarDatoAPI("caudal", "YF-S201", caudal, timestamp, "wifi");
            if (!ok) {
              guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
              logEventoM("SD_BACKUP", "RESPALDO", "reason=api_fail;sensor=YF-S201");
            } else {
              logEventoM("API", "API_OK", String("sensor=YF-S201;valor=" + String(caudal)).c_str());
            }
          } else {
            guardarEnBackupSD("caudal", "YF-S201", caudal, timestamp, "backup");
            logEventoM("SD_BACKUP", "RESPALDO", "reason=no_wifi;sensor=YF-S201");
          }
        }
        ultimoEnvioCaudal = millis();
      }
      if (segundo > config.timing.window_caudal) {
        detenerLecturaCaudal();
        estadoActual = IDLE;
      }
      break;
    }

    case LECTURA_TEMPERATURA: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        unsigned long long ts_fb = ts_fallback_micros();
        actualizarTermocupla();
        float temp = obtenerTemperatura();
        guardarEnBackupSD("temperatura", "MAX6675", temp, ts_fb, "backup");
        logEventoM("SD_BACKUP", "TS_INVALID_BACKUP", "sensor=MAX6675");
      } else {
        actualizarTermocupla();
        float temp = obtenerTemperatura();
        if (nowReady) {
          bool ok = enviarDatoAPI("temperatura", "MAX6675", temp, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
            logEventoM("SD_BACKUP", "RESPALDO", "reason=api_fail;sensor=MAX6675");
          } else {
            logEventoM("API", "API_OK", String("sensor=MAX6675;valor=" + String(temp)).c_str());
          }
        } else {
          guardarEnBackupSD("temperatura", "MAX6675", temp, timestamp, "backup");
          logEventoM("SD_BACKUP", "RESPALDO", "reason=no_wifi;sensor=MAX6675");
        }
      }
      estadoActual = IDLE;
      break;
    }

    case LECTURA_VOLTAJE: {
      timestamp = getTimestampMicros();
      if (timestamp == TS_INVALIDO_1 || timestamp == TS_INVALIDO_2) {
        unsigned long long ts_fb = ts_fallback_micros();
        actualizarVoltaje();
        float volt = obtenerVoltajeAC();
        guardarEnBackupSD("voltaje", "ZMPT101B", volt, ts_fb, "backup");
        logEventoM("SD_BACKUP", "TS_INVALID_BACKUP", "sensor=ZMPT101B");
      } else {
        actualizarVoltaje();
        float volt = obtenerVoltajeAC();
        if (nowReady) {
          bool ok = enviarDatoAPI("voltaje", "ZMPT101B", volt, timestamp, "wifi");
          if (!ok) {
            guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
            logEventoM("SD_BACKUP", "RESPALDO", "reason=api_fail;sensor=ZMPT101B");
          } else {
            logEventoM("API", "API_OK", String("sensor=ZMPT101B;valor=" + String(volt)).c_str());
          }
        } else {
          guardarEnBackupSD("voltaje", "ZMPT101B", volt, timestamp, "backup");
          logEventoM("SD_BACKUP", "RESPALDO", "reason=no_wifi;sensor=ZMPT101B");
        }
      }
      estadoActual = nowReady ? REINTENTO_BACKUP : IDLE;
      break;
    }

    case REINTENTO_BACKUP: {
      if (sdDisponible && nowReady) {
        static unsigned long lastRetryLogMs = 0;
        if (millis() - lastRetryLogMs > 10000) {
          logEventoM("SD_BACKUP", "REINTENTO_INFO", "scan=1");
          lastRetryLogMs = millis();
        }
        reenviarDatosDesdeBackup();
      } else if (!nowReady) {
        if (millis() - lastNoWifiLogMs > NO_WIFI_LOG_EVERY_MS) {
          logEventoM("SD_BACKUP", "REINTENTO_WAIT", "no_wifi");
          lastNoWifiLogMs = millis();
        }
      }
      lastRetryScanMs = millis();
      estadoActual = IDLE;
      break;
    }

    case ERROR_RECUPERABLE: {
      delay(1000);
      inicializarSD();
      sdDisponible = (SD.cardType() != CARD_NONE);
      if (sdDisponible) {
        logEventoM("SD", "SD_OK", "reinit_after_error");
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
