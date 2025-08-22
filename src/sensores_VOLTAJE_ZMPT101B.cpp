#include "sensores_VOLTAJE_ZMPT101B.h"
#include <Arduino.h>

#define SIMULACION_VOLTAJE          1
#define SIM_PRINT_THROTTLE_MS       1000UL   // como mucho 1 línea/seg

static float voltajeAC = 0.0f;

void inicializarSensorVoltaje() {
#if SIMULACION_VOLTAJE
  Serial.println(F("Sensor ZMPT101B inicializado (modo simulacion)"));
#else
  // init real si aplica
#endif
}

void actualizarVoltaje() {
#if SIMULACION_VOLTAJE
  // Simulación: 21.00 .. 25.00 V
  voltajeAC = random(2100, 2500) / 100.0f;

  static unsigned long lastSimPrint = 0;
  const unsigned long now = millis();
  if (now - lastSimPrint >= SIM_PRINT_THROTTLE_MS) {
    char vstr[16];
    dtostrf(voltajeAC, 0, 2, vstr);
    Serial.print(F("[SIM] Voltaje simulado: "));
    Serial.print(vstr);
    Serial.println(F(" V"));
    lastSimPrint = now;
    yield();
  }
#else
  // Lectura real ZMPT101B aquí
  // voltajeAC = ...
#endif
}

float obtenerVoltajeAC() {
  return voltajeAC;
}
