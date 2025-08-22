#ifndef REENVIARBACKUPSD_H
#define REENVIARBACKUPSD_H

#include <Arduino.h>

// Métricas de un ciclo de reenvío
struct ReenvioStats {
  uint32_t archivos_visitados = 0;
  uint32_t archivos_cerrados  = 0;  // ya sin PENDIENTE (archivados)
  uint32_t lineas_procesadas  = 0;
  uint32_t ok                 = 0;
  uint32_t err                = 0;
};

// Versión extendida con control de lote y retorno de métricas
ReenvioStats reenviarDatosDesdeBackup_ex(uint16_t max_lineas_por_ciclo = 10);

// Compatibilidad: firma original (no retorna métricas)
void reenviarDatosDesdeBackup();

#endif
