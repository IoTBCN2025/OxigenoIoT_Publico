#include "ds3231_time.h"
#include "sdlog.h"
#include <Wire.h>
#include <RTClib.h>

static RTC_DS3231 rtc;
static bool rtc_ok = false;
static bool rtc_needs_set = false;

static uint32_t last_unix_sec = 0;
static unsigned long last_micros_snap = 0;

static bool plausibleUnix(uint32_t t) {
  // >= 2020-01-01 y < 2100-01-01
  return (t >= 1577836800UL) && (t < 4102444800UL);
}

bool initDS3231(int sda, int scl) {
  Wire.begin(sda, scl);
  delay(10);
  rtc_ok = rtc.begin();

  if (rtc_ok) {
    if (rtc.lostPower()) {
      rtc_needs_set = true;
      LOGW("RTC_LOST_POWER", "RTC indica perdida de energia", "");
    } else {
      rtc_needs_set = false;
      LOGD("RTC_PRESENT", "DS3231 detectado", "");
    }
  } else {
    LOGE("RTC_ERR", "DS3231 no responde en I2C", "");
  }

  last_unix_sec = 0;
  last_micros_snap = micros();
  return rtc_ok;
}

bool rtcIsPresent() { return rtc_ok; }

bool rtcIsTimeValid() {
  if (!rtc_ok) return false;
  if (rtc_needs_set) return false;
  DateTime now = rtc.now();
  return plausibleUnix(now.unixtime());
}

bool setRTCFromUnix(uint32_t unixSeconds) {
  if (!rtc_ok) return false;
  if (!plausibleUnix(unixSeconds)) {
    LOGW("RTC_SET_INVALID", "Intento de set con UNIX no plausible",
         String("unix=") + String(unixSeconds));
    return false;
  }

  uint32_t prev = 0;
  if (rtcIsTimeValid()) {
    prev = rtc.now().unixtime();
  }

  rtc.adjust(DateTime(unixSeconds));
  rtc_needs_set = false;
  last_unix_sec = unixSeconds;
  last_micros_snap = micros();

  if (prev != 0) {
    int32_t delta = (int32_t)unixSeconds - (int32_t)prev;
    LOGI("RTC_SET_OK", "RTC actualizado desde UNIX",
         String("unix=") + String(unixSeconds) +
         ";delta_s=" + String(delta));
  } else {
    LOGI("RTC_SET_OK", "RTC establecido (sin referencia previa)",
         String("unix=") + String(unixSeconds));
  }
  return true;
}

uint32_t getUnixSeconds() {
  if (!rtc_ok || rtc_needs_set) return 0;
  DateTime now = rtc.now();
  uint32_t s = now.unixtime();
  return plausibleUnix(s) ? s : 0;
}

unsigned long long getTimestampMicros() {
  if (!rtc_ok || rtc_needs_set) return 0ULL;

  uint32_t secs = getUnixSeconds();
  if (!secs) return 0ULL;

  unsigned long m = micros();
  if (secs != last_unix_sec) {
    last_unix_sec = secs;
    last_micros_snap = m;
  }
  unsigned long delta_us = (unsigned long)(m - last_micros_snap);
  if (delta_us >= 1000000UL) delta_us = 999999UL;

  return (unsigned long long)secs * 1000000ULL + (unsigned long long)delta_us;
}

void keepRTCInSyncWithNTP(bool ntpOk, uint32_t ntpUnixSeconds) {
  if (!rtc_ok) return;
  if (!ntpOk) return;
  if (!plausibleUnix(ntpUnixSeconds)) {
    LOGW("RTC_RESYNC_SKIP", "NTP UNIX no plausible", String("unix=") + String(ntpUnixSeconds));
    return;
  }

  uint32_t rtcS = getUnixSeconds();
  if (!rtcS) {
    // Si RTC invalido pero NTP OK, fijamos directamente
    if (setRTCFromUnix(ntpUnixSeconds)) {
      LOGI("RTC_RESYNC", "RTC fijado (estaba invalido)",
           String("rtc=") + String(rtcS) + ";ntp=" + String(ntpUnixSeconds));
    }
    return;
  }

  int32_t offset = (int32_t)ntpUnixSeconds - (int32_t)rtcS;  // NTP - RTC (s)
  // Tolerancia ±2 s (tu lógica previa); ajusta si sales de banda
  if (offset > 2 || offset < -2) {
    bool ok = setRTCFromUnix(ntpUnixSeconds);
    if (ok) {
      LOGI("RTC_RESYNC", "Ajuste contra NTP",
           String("offset_s=") + String(offset) +
           ";rtc=" + String(rtcS) +
           ";ntp=" + String(ntpUnixSeconds));
    } else {
      LOGW("RTC_SET_ERR", "Fallo al ajustar RTC en resincronizacion",
           String("rtc=") + String(rtcS) +
           ";ntp=" + String(ntpUnixSeconds));
    }
  } else {
    LOGD("RTC_INSYNC", "RTC dentro de tolerancia",
         String("offset_s=") + String(offset));
  }
}
