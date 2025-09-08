#ifndef SDBACKUP_H
#define SDBACKUP_H

#include <Arduino.h>

/**
 * Genera el nombre del archivo de backup diario basado en RTC DS3231.
 * Si el RTC no es válido, retorna "/backup_unsync.csv" (evita 1970).
 */
String generarNombreArchivoBackup();

/**
 * Guarda un registro en el CSV de backup con estado PENDIENTE y
 * añade el archivo a /pendientes.idx si aún no está listado.
 *
 * @param measurement   "caudal" | "temperatura" | "voltaje" | ...
 * @param sensor        "YF-S201" | "MAX6675" | "ZMPT101B" | ...
 * @param valor         valor medido
 * @param timestamp     ts en microsegundos (uint64)
 * @param source        "backup" (u otro)
 * @param reason        OPCIONAL: razón de respaldo para el log forense
 *                      ("wifi_down", "api_fail", etc). Por defecto "unspecified".
 */
void guardarEnBackupSD(const String& measurement,
                       const String& sensor,
                       float valor,
                       unsigned long long timestamp,
                       const String& source,
                       const char* reason = "unspecified");

// Utilidad de prueba (opcional)
void testBackup();

#endif
