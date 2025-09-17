// spi_temp.h
#ifndef SPI_TEMP_H
#define SPI_TEMP_H

#include <SPI.h>

extern SPIClass spiTempSensor;  // Declaración, NO definición

void iniciarSPITermocupla();

#endif
