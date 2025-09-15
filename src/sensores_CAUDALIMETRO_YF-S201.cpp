#include "sensores_CAUDALIMETRO_YF-S201.h"
#include "config.h"
#include "sdlog.h"

volatile unsigned long pulsos = 0;
float caudalLPM = 0.0;

void IRAM_ATTR contarPulso() {
  pulsos++;
}

void inicializarSensorCaudal() {
  if (config.caudal.mode == Mode::REAL) {
    pinMode(config.caudal.pin1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(config.caudal.pin1), contarPulso, RISING);
    Serial.println("Sensor de caudal YF-S201 inicializado (modo real)");
  } else {
    Serial.println("Sensor de caudal en modo SIMULACIÓN");
  }

  char kv[24];
  snprintf(kv, sizeof(kv), "sim=%d", config.caudal.mode == Mode::SIMULATION ? 1 : 0);
  logEventoM("YF-S201", "MOD_UP", kv);
}

void actualizarCaudal() {
  if (config.caudal.mode == Mode::SIMULATION) {
    caudalLPM = random(200, 800) / 100.0;
    Serial.printf("[SIM] Caudal simulado: %.2f L/min\n", caudalLPM);
  } else {
    noInterrupts();
    unsigned long pulsosLeidos = pulsos;
    pulsos = 0;
    interrupts();
    caudalLPM = pulsosLeidos / 7.5;
    Serial.printf("Caudal leído: %.2f L/min (%lu pulsos)\n", caudalLPM, pulsosLeidos);
  }
}

void comenzarLecturaCaudal() {
  if (config.caudal.mode == Mode::REAL) {
    pulsos = 0;
    attachInterrupt(digitalPinToInterrupt(config.caudal.pin1), contarPulso, RISING);
    Serial.println("Sensor caudal habilitado (modo real)");
  } else {
    Serial.println("[SIM] Inicio lectura continua del caudalímetro");
  }
}

void detenerLecturaCaudal() {
  if (config.caudal.mode == Mode::REAL) {
    detachInterrupt(digitalPinToInterrupt(config.caudal.pin1));
    Serial.println("Sensor caudal deshabilitado (modo real)");
  } else {
    Serial.println("[SIM] Fin lectura continua del caudalímetro");
  }
}

float obtenerCaudalLPM() {
  return caudalLPM;
}
