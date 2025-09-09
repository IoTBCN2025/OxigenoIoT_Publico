#ifndef SDLOG_H
#define SDLOG_H

#include <Arduino.h>

// Monta SD (CS=5 por defecto), prepara archivo del día, vuelca buffer RAM si existía.
// Idempotente: puedes llamarla varias veces.
void inicializarSD();

// NUEVO: logger con módulo explícito (queda en el campo 'mod' del CSV)
void logEventoM(const String& mod, const String& code, const String& kv);

// LEGACY: compatibilidad con código antiguo -> deja mod="LEG"
inline void logEvento(const String& code, const String& kv) {
  logEventoM("LEG", code, kv);
}

// Fuerza el volcado de los logs pendientes en RAM al archivo actual si la SD está lista.
// Reintenta montar SD si no está lista.
void reintentarLogsPendientes();

#endif
