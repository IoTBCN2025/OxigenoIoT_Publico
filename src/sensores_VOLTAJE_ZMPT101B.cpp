#include "sensores_VOLTAJE_ZMPT101B.h"
#include "config.h"
#include "sdlog.h"

float voltajeAC = 0.0;

void inicializarSensorVoltaje() {
  if (config.voltaje.mode == Mode::SIMULATION) {
    Serial.println("Sensor ZMPT101B inicializado (modo simulacion)");
  } else {
    Serial.println("Sensor ZMPT101B inicializado (modo REAL - no implementado aún)");
  }

  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", config.voltaje.mode == Mode::SIMULATION ? 1 : 0);
  logEventoM("ZMPT101B", "MOD_UP", kv);
}

void actualizarVoltaje() {
  if (config.voltaje.mode == Mode::SIMULATION) {
    voltajeAC = random(2100, 2500) / 100.0;
    Serial.printf("[SIM] Voltaje simulado: %.2f V\n", voltajeAC);
  } else {
    // TODO: implementar lectura analógica
  }
}

float obtenerVoltajeAC() {
  return voltajeAC;
}
