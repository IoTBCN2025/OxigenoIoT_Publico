#pragma once
#include <Arduino.h>

void wifiSetup(const char* ssid, const char* pass);
void wifiLoop();                 // watchdog no-bloqueante; llamarlo en loop()
bool wifiReady();                // true cuando hay IP válida (tras estabilización)
const char* wifiStatusStr();     // opcional, para logs

// (opcional) obtener último RSSI conocido; -127 si no disponible
int wifiLastRssi();
