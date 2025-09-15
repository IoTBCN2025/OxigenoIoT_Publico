#include "sensores_TERMOCUPLA_MAX6675.h"
#include "config.h"
#include "sdlog.h"

float temperaturaC = 0.0;

void inicializarSensorTermocupla() {
  if (config.termocupla.mode == Mode::SIMULATION) {
    Serial.println("Sensor MAX6675 inicializado (modo simulacion)");
  } else {
    Serial.println("Sensor MAX6675 inicializado (modo REAL - sin implementar aún)");
  }
  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", config.termocupla.mode == Mode::SIMULATION ? 1 : 0);
  logEventoM("MAX6675", "MOD_UP", kv);
}

void actualizarTermocupla() {
  if (config.termocupla.mode == Mode::SIMULATION) {
    temperaturaC = random(2500, 3500) / 100.0;
    Serial.printf("[SIM] Temp simulada: %.2f °C\n", temperaturaC);
  } else {
    // TODO: implementar lectura real SPI
  }
}

float obtenerTemperatura() {
  return temperaturaC;
}
