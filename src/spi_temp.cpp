// === Archivo: spi_temp.cpp ===

#include "spi_temp.h"
#include "config.h"

// HSPI para MAX6675
SPIClass spiTempSensor(HSPI);

void iniciarSPITermocupla() {
  static bool spiIniciado = false;
  if (spiIniciado) return;

  spiTempSensor.begin(
    config.termocupla.pin3, // SCK
    config.termocupla.pin4, // MISO (SO)
    -1,                     // MOSI no se usa
    config.termocupla.pin2  // CS como SS
  );

  pinMode(config.termocupla.pin2, OUTPUT);
  digitalWrite(config.termocupla.pin2, HIGH);

  spiIniciado = true;
}
