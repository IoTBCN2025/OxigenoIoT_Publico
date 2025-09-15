#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

enum class Mode { SIMULATION, REAL };

// === Configuración individual de cada sensor ===
struct SensorConfig {
    Mode mode;       // SIMULATION o REAL
    int pin1;        // Uso depende del sensor (ej. señal o CS)
    int pin2;        // (opcional) ej. SCK
    int pin3;        // (opcional) ej. MISO
    int pin4;        // (opcional)
};

// === Configuración de red WiFi ===
struct NetworkConfig {
    const char* ssid;
    const char* password;
};

// === Configuración de API (HTTP → InfluxDB) ===
struct ApiConfig {
    String endpoint;
    String key;
};

// === Configuración de NTP (hora por red) ===
struct NtpConfig {
    String servidor;
    long gmtOffset;   // en segundos
    int dstOffset;    // daylight saving offset (ej. 3600)
};

// === Pines globales del sistema ===
struct PinConfig {
    int SDA;    // I2C SDA (RTC)
    int SCL;    // I2C SCL (RTC)
    int SD_CS;  // SPI Chip Select (SD)
    int SCK;    // SPI Clock
    int MISO;   // SPI Master In
    int MOSI;   // SPI Master Out
};

// === Ventanas temporales para la FSM (segundo del minuto) ===
struct TimingConfig {
    int window_caudal;     // Duración de ventana de lectura caudal
    int window_temp;       // Segundo para lectura temperatura
    int window_voltage;    // Segundo para lectura voltaje
};

// === Configuración completa del sistema ===
struct Config {
    SensorConfig caudal;
    SensorConfig termocupla;
    SensorConfig voltaje;
    NetworkConfig network;
    ApiConfig api;
    NtpConfig ntp;
    PinConfig pins;
    TimingConfig timing;
};

// Variable global y función para cargar configuración por defecto
extern Config config;
Config loadDefaultConfig();

#endif