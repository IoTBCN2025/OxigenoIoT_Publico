# 📦 CHANGELOG

Todas las versiones publicadas del proyecto **OxigenoIoT**, ordenadas cronológicamente (semver).

---
## [v1.3.2] - 2025-09-12

### Added
- Nueva función `logEventoM(mod, code, kv)` para trazabilidad estructurada en CSV (`mod`, `code`, `fsm`, `kv`).
- Migración completa de logs de sensores, WiFi, NTP, RTC, API y SD a `logEventoM()`.
- Estructura de log CSV actualizada: `ts_iso,ts_us,level,mod,code,fsm,kv` con niveles INFO/WARN/ERROR/DEBUG.
- Logging coalescente con throttle inteligente para evitar repetición de eventos.
- Archivos `.csv` de backup con marcas `ENVIADO`, `ts_envio` y trazabilidad mejorada.
- Función `reintentarLogsPendientes()` integrada para volcado forzado del buffer RAM a SD.

### Changed
- `sdlog.cpp`: mejora robusta anti-fragmentación con cola circular y safe-write.
- `sdbackup.cpp`: marca `MOD_UP` y `MOD_FAIL` para SD_BACKUP, mejor gestión de errores.
- `main.cpp`: todos los estados FSM ahora registran evento estructurado `FSM_STATE`.
- Mejoras visuales en el Serial Monitor con log más claro y ordenado.
- Niveles de log reemplazan letras por etiquetas completas: INFO, WARN, ERROR, DEBUG.

### Fixed
- Corrección de fallos de compilación por múltiples definiciones de `logEvento`.
- Corrección en fallback de timestamp y validación de RTC.
- Validación de archivos `backup_1970*.csv` para evitar falsos positivos.

## [v1.3.2] - 2025-09-09

### ✨ Mejora principal
- Refactorización avanzada del sistema de trazabilidad en `sdlog.cpp` y `sdlog.h`.
  - Introducción de la función `logEventoM()` con estructura uniforme de log.
  - Inclusión de niveles `INFO`, `WARN`, `ERROR`, `DEBUG`.
  - Identificación precisa del módulo (`mod`) generador del evento (ej. `RTC`, `WiFi`, `API`, etc.).
  - Prevención de logs repetitivos.
  - Soporte para rotación diaria de archivos CSV.
  - Reducción de fragmentación al escribir en SD.

### 📂 Archivos afectados
- `src/sdlog.cpp`
- `src/sdlog.h`

### 🔗 Tag asociado
- [v1.3.2](https://github.com/IoTBCN2025/OxigenoIoT_Publico/releases/tag/v1.3.2)

---

## [v1.3.1] - 2025-08-20

### 🔁 Integración continua
- Se añade `sync-public.yml` para sincronizar automáticamente `tags` y ramas al repositorio público (`OxigenoIoT_Publico`) usando GitHub Actions.
- Uso del token secreto `MIRROR_TOKEN` en `secrets` del repositorio privado.

### 🔗 Tag asociado
- [v1.3.1](https://github.com/IoTBCN2025/OxigenoIoT_Publico/releases/tag/v1.3.1)

---

## [v1.3.0] - 2025-07-22

### 🧠 Arquitectura FSM robusta
- Se implementa FSM completa: `INIT`, `LECTURA`, `REINTENTO`, `ERROR`, etc.
- Soporte para sensores: YF-S201 (caudal), MAX6675 (temperatura), ZMPT101B (voltaje).
- Sincronización horaria vía DS3231 (RTC) y fallback NTP.
- Watchdog WiFi y reconexión automática.
- Envío de datos a API intermedia con respaldo en tarjeta SD y reintentos.

### 📂 Módulos nuevos
- `api.cpp`, `ntp.cpp`, `ds3231_time.cpp`, `sdlog.cpp`, `sdbackup.cpp`, `reenviarBackupSD.cpp`
- Sensores: `sensores_CAUDALIMETRO_YF-S201.cpp`, `sensores_TERMOCUPLA_MAX6675.cpp`, `sensores_VOLTAJE_ZMPT101B.cpp`

### 📂 Logs
- Formato CSV estructurado para eventos y reintentos.

---

## [v1.2.1] - 2025-06-30

### 🛜 Soporte GPRS (fallback)
- Envío de datos desde Arduino UNO R4 por WiFi o, en caso de fallo, por GPRS (SIM800/SIM900).
- Lógica para failover automático si no hay WiFi.
- Nuevo script PHP intermedio (`registro.php`) con soporte GET/POST.

---

## [v1.2.0] - 2025-06-15

### 📡 Conexión directa Arduino → InfluxDB v2
- Envío de datos desde Arduino UNO R4 WiFi vía HTTP a InfluxDB usando Line Protocol.
- Visualización en Grafana.
- Integración con n8n para automatización.

---

## [v1.0.0] - 2025-05-26

### 🧪 Versión inicial
- Primer sketch funcional de envío desde Arduino a InfluxDB vía PHP.
- Pruebas básicas con sensores: YF-S201, MAX6675.
- Setup inicial con Debian 12, Nginx, InfluxDB 2.x, Grafana.
- API intermedia PHP para traducir datos hacia InfluxDB.

---

