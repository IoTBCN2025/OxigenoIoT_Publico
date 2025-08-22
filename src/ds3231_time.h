#pragma once
#include <Arduino.h>

// Inicializa I2C y el DS3231. Devuelve true si el RTC responde.
bool initDS3231(int sda = 21, int scl = 22);

// ¿El RTC está presente en el bus y responde?
bool rtcIsPresent();

// ¿La hora del RTC es válida (>= 2020-01-01) y no es 1970?
bool rtcIsTimeValid();

// Ajusta el RTC con un timestamp UNIX (seg).
bool setRTCFromUnix(uint32_t unixSeconds);

// Devuelve UNIX time en segundos desde DS3231 si válido; si no, 0.
uint32_t getUnixSeconds();

// Devuelve timestamp en microsegundos con base RTC + microsegundos locales.
// Si no hay RTC válido, devuelve 0.
unsigned long long getTimestampMicros();

// Sincroniza RTC con NTP si NTP está OK (idempotente).
void keepRTCInSyncWithNTP(bool ntpOk, uint32_t ntpUnixSeconds);