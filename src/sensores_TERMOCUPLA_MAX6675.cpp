// sensores_TERMOCUPLA_MAX6675.cpp - lectura real por HSPI validada

#include "sensores_TERMOCUPLA_MAX6675.h"
#include "config.h"
#include "sdlog.h"
#include <SPI.h>
#include "spi_temp.h"

float temperaturaC = 0.0;

void inicializarSensorTermocupla() {
  if (config.termocupla.mode == Mode::SIMULATION) {
    Serial.println("Sensor MAX6675 inicializado (modo simulación)");
  } else {
    // Asegurar que HSPI esté inicializado correctamente (solo una vez)
    iniciarSPITermocupla();

    Serial.println("Sensor MAX6675 inicializado (modo REAL - HSPI)");
  }

  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", config.termocupla.mode == Mode::SIMULATION ? 1 : 0);
  logEventoM("MAX6675", "MOD_UP", kv);
}

void actualizarTermocupla() {
  if (config.termocupla.mode == Mode::SIMULATION) {
    temperaturaC = random(2500, 3500) / 100.0;
    Serial.printf("[SIM] Temp simulada: %.2f °C\n", temperaturaC);
    return;
  }

  // === Modo REAL ===
  uint16_t raw = 0;

  digitalWrite(config.termocupla.pin2, LOW);  // CS LOW
  delayMicroseconds(10);

  raw = spiTempSensor.transfer16(0x0000);     // Leer 16 bits

  digitalWrite(config.termocupla.pin2, HIGH); // CS HIGH

  if (raw == 0x0000) {
    temperaturaC = -127.0;
    Serial.println("MAX6675 sin respuesta (raw = 0x0000)");
    logEventoM("MAX6675", "READ_ERR", "raw=0x0000");
    return;
  }

  if (raw & 0x0004) {
    temperaturaC = -127.0;
    Serial.println("MAX6675 desconectado o error (bit D2 activo)");
    logEventoM("MAX6675", "READ_ERR", "desconectado");
    return;
  }

  temperaturaC = ((raw >> 3) & 0x0FFF) * 0.25;
  Serial.printf("Temp real: %.2f °C (raw=0x%04X)\n", temperaturaC, raw);
}

float obtenerTemperatura() {
  return temperaturaC;
}
