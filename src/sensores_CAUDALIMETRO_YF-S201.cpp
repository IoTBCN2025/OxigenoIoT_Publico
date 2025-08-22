#include "sensores_CAUDALIMETRO_YF-S201.h"
#include <Arduino.h>

#define PIN_CAUDAL           27
#define FACTOR_CAUDAL        7.5f
#define SIMULACION_CAUDAL    1          // 1 = SIM, 0 = HW real
#define SIM_PRINT_THROTTLE_MS 1000UL    // imprimir como mucho 1 vez/seg

volatile unsigned long pulsos = 0;
static float caudalLPM = 0.0f;

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
  // Simulación: 2.00 .. 8.00 L/min (random)
  caudalLPM = random(200, 800) / 100.0f;

  // Throttle de consola a 1 Hz y sin %f
  static unsigned long lastSimPrint = 0;
  const unsigned long now = millis();
  if (now - lastSimPrint >= SIM_PRINT_THROTTLE_MS) {
    char vstr[16];
    dtostrf(caudalLPM, 0, 2, vstr);  // 2 decimales, sin %f en printf
    Serial.print(F("[SIM] Caudal simulado: "));
    Serial.print(vstr);
    Serial.println(F(" L/min"));
    lastSimPrint = now;
    yield(); // cede CPU al RTOS/WiFi
  }
#else
  // Lectura modo real: lee y resetea contador atómicamente
  noInterrupts();
  unsigned long pulsosLeidos = pulsos;
  pulsos = 0;
  interrupts();

  // YF-S201 típico: 450 pulsos/L → FACTOR_CAUDAL ajusta a L/min
  caudalLPM = (float)pulsosLeidos / FACTOR_CAUDAL;

  // (Opcional) Throttle de consola también en real
  static unsigned long lastRealPrint = 0;
  const unsigned long now = millis();
  if (now - lastRealPrint >= SIM_PRINT_THROTTLE_MS) {
    char vstr[16];
    dtostrf(caudalLPM, 0, 2, vstr);
    Serial.print(F("Caudal leído: "));
    Serial.print(vstr);
    Serial.print(F(" L/min ("));
    Serial.print(pulsosLeidos);
    Serial.println(F(" pulsos)"));
    lastRealPrint = now;
    yield();
  }
#endif
}

void comenzarLecturaCaudal() {
#if SIMULACION_CAUDAL
  Serial.println(F("[SIM] Inicio lectura continua del caudalímetro"));
#else
  pulsos = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL), contarPulso, RISING);
  Serial.println(F("Sensor caudal habilitado (modo real)"));
#endif
}

void detenerLecturaCaudal() {
#if SIMULACION_CAUDAL
  Serial.println(F("[SIM] Fin lectura continua del caudalímetro"));
#else
  detachInterrupt(digitalPinToInterrupt(PIN_CAUDAL));
  Serial.println(F("Sensor caudal deshabilitado (modo real)"));
#endif
}

float obtenerCaudalLPM() {
  return caudalLPM;
}
