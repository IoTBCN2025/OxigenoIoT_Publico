#include "sensores_TERMOCUPLA_MAX6675.h"
#include <Arduino.h>

#define SIMULACION_TERMOCOUPLE      1
#define SIM_PRINT_THROTTLE_MS       1000UL   // como mucho 1 línea/seg

static float temperaturaC = 0.0f;

void inicializarSensorTermocupla() {
#if SIMULACION_TERMOCOUPLE
  Serial.println(F("Sensor MAX6675 inicializado (modo simulacion)"));
#else
  // init real si aplica
#endif
}

void actualizarTermocupla() {
#if SIMULACION_TERMOCOUPLE
  // Simulación: 25.00 .. 35.00 °C
  temperaturaC = random(2500, 3500) / 100.0f;

  static unsigned long lastSimPrint = 0;
  const unsigned long now = millis();
  if (now - lastSimPrint >= SIM_PRINT_THROTTLE_MS) {
    char tstr[16];
    dtostrf(temperaturaC, 0, 2, tstr);
    Serial.print(F("[SIM] Temp simulada: "));
    Serial.print(tstr);
    Serial.println(F(" °C"));
    lastSimPrint = now;
    yield();
  }
#else
  // Lectura real MAX6675 aquí
  // temperaturaC = ...
#endif
}

float obtenerTemperatura() {
  return temperaturaC;
}
