#include "sensores_TERMOCUPLA_MAX6675.h"
#include <Arduino.h>
#include "sdlog.h"

#define SIMULACION_TERMOCOUPLE 1
float temperaturaC = 0.0;

void inicializarSensorTermocupla() {
  Serial.println("Sensor MAX6675 inicializado (modo simulacion)");
  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", SIMULACION_TERMOCOUPLE ? 1 : 0);
  logEventoM("MAX6675", "MOD_UP", kv);
}

void actualizarTermocupla() {
#if SIMULACION_TERMOCOUPLE
  temperaturaC = random(2500, 3500) / 100.0;
  Serial.printf("[SIM] Temp simulada: %.2f °C\n", temperaturaC);
#else
  // Código para lectura real aquí
#endif
}

float obtenerTemperatura() {
  return temperaturaC;
}
