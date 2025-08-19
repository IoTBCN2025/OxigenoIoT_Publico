#ifndef SDLOG_H
#define SDLOG_H

#include <Arduino.h>

// Monta SD (CS=5 por defecto), prepara archivo del día, vuelca buffer RAM si existía.
// Idempotente: puedes llamarla varias veces.
void inicializarSD();

// Registra un evento. Si la SD no está disponible, se almacena en RAM y por Serial.
// Formato CSV: timestamp_iso,event,detalle
void logEvento(const String& codigo, const String& mensaje);

// Fuerza el volcado de los logs pendientes en RAM al archivo actual si la SD está lista.
// Reintenta montar SD si no está lista.
void reintentarLogsPendientes();

#endif
