#include "config.h"

Config loadDefaultConfig() {
    return Config{
        // === Sensor de caudal YF-S201 ===
        .caudal = {
            Mode::REAL,       // Modo de operación: REAL o SIMULATION
            27,               // pin1: D27 = señal de pulsos (YF-S201)
            0, 0, 0           // No se usan otros pines
        },

        // === Termocupla MAX6675 ===
        .termocupla = {
            Mode::REAL,        // Cambiado a modo REAL
            0,                // pin1: no usado
            15,               // pin2: CS
            14,               // pin3: SCK
            12                // pin4: SO (MISO)
        },

        // === Sensor de voltaje ZMPT101B ===
        .voltaje = {
            Mode::REAL, // Modo de operación: REAL o SIMULATION
            32,               // pin1: señal analógica
            0, 0, 0
        },

        // === Red WiFi ===
        .network = {
            "Jose Monje Ruiz",   // SSID
            "QWERTYUI2022"       // Password
        },

        // === API Intermedia (HTTP → InfluxDB) ===
        .api = {
            "http://iotbcn.com/IoT/api.php",    // Endpoint API
            "123456789ABCDEF"                   // API Key
        },

        // === NTP (hora global) ===
        .ntp = {
            "pool.ntp.org",     // Servidor NTP
            3600 * 2,           // GMT+2 → 7200 segundos
            0                   // Sin horario de verano
        },

        // === Pines globales del sistema (I2C + SD SPI) ===
        .pins = {
            21,  // SDA (RTC DS3231)
            22,  // SCL (RTC DS3231)
             5,  // SD_CS   (Chip Select tarjeta SD)
            18,  // SCK     (SPI Clock)
            19,  // MISO    (SPI Master In)
            23   // MOSI    (SPI Master Out)
        },

        // === Tiempo por ventana de ejecución (segundos del minuto) ===
        .timing = {
            29,  // caudal: segundos 0..29 (tiempo de medición)
            35,  // termocupla: leer en el segundo 35
            40   // voltaje: leer en el segundo 40
        }
    };
}