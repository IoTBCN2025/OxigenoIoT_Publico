
# Notas Importantes
- Fecha: 18/07/2025
- Autor: Jose Monje Ruiz
- Descripción: Este archivo contiene observaciones críticas sobre el proyecto y parte del funcionamiento clave para entender la logica del desarollo

# Análisis del Proyecto IoT - ESP32-WROOM-32 con FSM Modular

Este proyecto implementa un sistema IoT robusto y resiliente basado en **ESP32-WROOM-32**, enfocado en entornos rurales o desconectados. 
Utiliza una arquitectura **FSM (Finite State Machine)** con lógica modular distribuida por archivo, permitiendo trazabilidad, respaldo de datos y reintentos automáticos en caso de fallo.

## 📂 Estructura del Proyecto

```bash
src/
├── main.cpp                            # FSM general y reintento desde backup
├── api.cpp                             # Envío de datos HTTP GET a API PHP
├── ntp.cpp                             # Sincronización NTP + timestamp
├── sdlog.cpp                           # Logging de eventos (eventlog_YYYY.MM.DD.csv)
├── sdbackup.cpp                        # Backup de datos fallidos en CSV
├── reenviarBackupSD.cpp                # Reintento automático desde archivos de respaldo
├── sensores_CAUDALIMETRO_YF-S201.cpp   # Lectura del Caudalimetro
├── sensores_TERMOCUPLA_MAX6675.cpp     # Lectura del Termocupla
└── sensores_VOLTAJE_ZMPT101B.cpp       # Lectura del Voltaje
