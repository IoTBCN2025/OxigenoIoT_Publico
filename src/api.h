#ifndef API_H
#define API_H

#include <Arduino.h>

bool enviarDatoAPI(const String& measurement, const String& sensor, float valor, unsigned long long timestamp, const String& source);

#endif
