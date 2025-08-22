#ifndef SDBACKUP_H
#define SDBACKUP_H

#include <Arduino.h>

// Genera el nombre del archivo de backup diario basado en RTC DS3231.
// Si el RTC no es válido, retorna "/backup_unsync.csv" (evita 1970).
String generarNombreArchivoBackup();

void guardarEnBackupSD(const String& measurement,
                       const String& sensor,
                       float valor,
                       unsigned long long timestamp,
                       const String& source);

// Utilidad de prueba (opcional)
void testBackup();

#endif
