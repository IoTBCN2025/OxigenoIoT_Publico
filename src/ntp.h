#ifndef NTP_H
#define NTP_H

#include <Arduino.h>

bool sincronizarNTP(uint8_t intentosMax = 3, uint16_t tiempoEntreIntentos = 5000);
time_t getTimestamp();

#endif
