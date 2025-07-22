#ifndef SENSORES_CAUDALIMETRO_YF_S201_H
#define SENSORES_CAUDALIMETRO_YF_S201_H

#include <Arduino.h>

void inicializarSensorCaudal();
void actualizarCaudal();
float obtenerCaudalLPM();
void comenzarLecturaCaudal();
void detenerLecturaCaudal();

#endif
