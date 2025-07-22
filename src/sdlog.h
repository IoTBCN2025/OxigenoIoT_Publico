#ifndef SDLOG_H
#define SDLOG_H

#include <Arduino.h>

void inicializarSD();
void logEvento(const String& codigo, const String& mensaje);
void reintentarLogsPendientes();

#endif
