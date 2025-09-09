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
  return (t >= 1577836800UL) && (t < 4102444800UL);
}

bool initDS3231(int sda, int scl) {
  Wire.begin(sda, scl);
  delay(10);
  rtc_ok = rtc.begin();

  if (!rtc_ok) {
    logEventoM("RTC", "MOD_FAIL", "i2c=0x68;err=no_response");
    return false;
  }

  if (rtc.lostPower()) {
    rtc_needs_set = true;
    logEventoM("RTC", "MOD_UP", "i2c=0x68;need_set=1");
  } else {
    rtc_needs_set = false;
    logEventoM("RTC", "MOD_UP", "i2c=0x68;need_set=0");
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
  if (!rtc_ok) {
    logEventoM("RTC", "MOD_FAIL", "op=set;err=not_present");
    return false;
  }
  if (!plausibleUnix(unixSeconds)) {
    logEventoM("RTC", "MOD_FAIL", "op=set;err=ts_implausible");
    return false;
  }
  rtc.adjust(DateTime(unixSeconds));
  rtc_needs_set = false;
  last_unix_sec = unixSeconds;
  last_micros_snap = micros();
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
    logEventoM("RTC", "MOD_WARN", "op=sync;err=ntp_ts_implausible");
    return;
  }

  uint32_t rtcS = getUnixSeconds();
  if (!rtcS || (rtcS > ntpUnixSeconds + 2) || (ntpUnixSeconds > rtcS + 2)) {
    setRTCFromUnix(ntpUnixSeconds);
  }
}
