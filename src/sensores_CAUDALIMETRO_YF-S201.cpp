#include "sensores_CAUDALIMETRO_YF-S201.h"
#include <Arduino.h>

#define PIN_CAUDAL 27
#define FACTOR_CAUDAL 7.5
#define SIMULACION_CAUDAL 1

volatile unsigned long pulsos = 0;
float caudalLPM = 0.0;

#if !SIMULACION_CAUDAL
void IRAM_ATTR contarPulso() {
  pulsos++;
}
#endif

void inicializarSensorCaudal() {
#if !SIMULACION_CAUDAL
  pinMode(PIN_CAUDAL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL), contarPulso, RISING);
  Serial.println("Sensor de caudal YF-S201 inicializado (modo real)");
#else
  Serial.println("Sensor de caudal en modo SIMULACIÓN");
#endif
}

void actualizarCaudal() {
#if SIMULACION_CAUDAL
  caudalLPM = random(200, 800) / 100.0;
  Serial.printf("[SIM] Caudal simulado: %.2f L/min\n", caudalLPM);
#else
  noInterrupts();
  unsigned long pulsosLeidos = pulsos;
  pulsos = 0;
  interrupts();
  caudalLPM = pulsosLeidos / FACTOR_CAUDAL;
  Serial.printf("Caudal leído: %.2f L/min (%lu pulsos)\n", caudalLPM, pulsosLeidos);
#endif
}

void comenzarLecturaCaudal() {
#if SIMULACION_CAUDAL
  Serial.println("[SIM] Inicio lectura continua del caudalímetro");
#else
  pulsos = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL), contarPulso, RISING);
  Serial.println("Sensor caudal habilitado (modo real)");
#endif
}

void detenerLecturaCaudal() {
#if SIMULACION_CAUDAL
  Serial.println("[SIM] Fin lectura continua del caudalímetro");
#else
  detachInterrupt(digitalPinToInterrupt(PIN_CAUDAL));
  Serial.println("Sensor caudal deshabilitado (modo real)");
#endif
}

float obtenerCaudalLPM() {
  return caudalLPM;
}
