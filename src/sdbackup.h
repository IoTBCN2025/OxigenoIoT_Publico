#ifndef SDBACKUP_H
#define SDBACKUP_H

#include <Arduino.h>

void guardarEnBackupSD(const String& measurement, const String& sensor, float valor, unsigned long long timestamp, const String& source);
void testBackup();  // función de prueba opcional

#endif
