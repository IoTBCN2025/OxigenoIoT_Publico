// sensores_VOLTAJE_ZMPT101B.cpp - lectura real desde ADC (ZMPT101B)

#include "sensores_VOLTAJE_ZMPT101B.h"
#include "config.h"
#include "sdlog.h"

float voltajeAC = 0.0;

void inicializarSensorVoltaje() {
  if (config.voltaje.mode == Mode::SIMULATION) {
    Serial.println("Sensor ZMPT101B inicializado (modo simulacion)");
  } else {
    Serial.println("Sensor ZMPT101B inicializado (modo REAL - entrada analógica)");
  }

  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", config.voltaje.mode == Mode::SIMULATION ? 1 : 0);
  logEventoM("ZMPT101B", "MOD_UP", kv);
}

void actualizarVoltaje() {
  if (config.voltaje.mode == Mode::SIMULATION) {
    voltajeAC = random(2100, 2500) / 100.0;
    Serial.printf("[SIM] Voltaje simulado: %.2f V\n", voltajeAC);
    return;
  }

  // === Modo REAL ===
  int maxValor = 0;
  int minValor = 4095;

  // Medir durante ~100 ms (500 muestras cada 200 us)
  for (int i = 0; i < 500; i++) {
    int lectura = analogRead(config.voltaje.pin1);
    if (lectura > maxValor) maxValor = lectura;
    if (lectura < minValor) minValor = lectura;
    delayMicroseconds(200);
  }

  int picoPico = maxValor - minValor;

  // Factor calibrado: si 1142 p-p ≈ 230 V => factor ≈ 0.2014
  const float FACTOR_CALIBRADO = 0.2014;
  float voltajeEstimado = picoPico * FACTOR_CALIBRADO;

  voltajeAC = voltajeEstimado;  // Valor final para obtenerVoltajeAC()

  Serial.printf("Voltaje estimado: %.2f V (pico a pico: %d)\n", voltajeAC, picoPico);

  // Log de medición real
  char kv[64];
  snprintf(kv, sizeof(kv), "v=%.2f;pico=%d", voltajeAC, picoPico);
  logEventoM("ZMPT101B", "READ_OK", kv);
}

float obtenerVoltajeAC() {
  return voltajeAC;
}
