#include "sensores_VOLTAJE_ZMPT101B.h"
#include <Arduino.h>
#include "sdlog.h"

#define SIMULACION_VOLTAJE 1
float voltajeAC = 0.0;

void inicializarSensorVoltaje() {
  Serial.println("Sensor ZMPT101B inicializado (modo simulacion)");
  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", SIMULACION_VOLTAJE ? 1 : 0);
  logEventoM("ZMPT101B", "MOD_UP", kv);
}

void actualizarVoltaje() {
#if SIMULACION_VOLTAJE
  voltajeAC = random(2100, 2500) / 100.0;
  Serial.printf("[SIM] Voltaje simulado: %.2f V\n", voltajeAC);
#else
  // Código para lectura real aquí
#endif
}

float obtenerVoltajeAC() {
  return voltajeAC;
}
