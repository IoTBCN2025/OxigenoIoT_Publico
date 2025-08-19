#pragma once
#include <Arduino.h>

void wifiSetup(const char* ssid, const char* pass);
void wifiLoop();                 // watchdog no-bloqueante; llamarlo en loop()
bool wifiReady();                // true cuando hay IP válida
const char* wifiStatusStr();     // opcional, para logs
