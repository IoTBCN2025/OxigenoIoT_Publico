# 📦 Changelog

## [v1.0.0] - 2025-09-19

### 🚀 Primera versión estable del firmware IoT ESP32

#### 🧠 Arquitectura
- Implementación completa de FSM (Finite State Machine) robusta y escalable.
- Modularización por responsabilidad: sensores, tiempo, WiFi, SD, API, etc.
- Configuración centralizada en `config.h` y `config.cpp`.

#### 🔌 Conectividad
- Gestión automática de WiFi con watchdog y validación.
- Sincronización horaria con RTC DS3231 y fallback a NTP.
- Sistema tolerante a fallos de red, corte de energía o RTC inválido.

#### 📦 Respaldo y reintento
- Backup inteligente en tarjeta SD ante fallos de red o API.
- Reintento automático desde SD cuando se restablece conectividad.
- Archivos `.csv` por fecha con estado `PENDIENTE` / `ENVIADO` y `ts_envio`.

#### 📊 Sensores integrados
- YF-S201 (caudalímetro) — lectura en modo real o simulado.
- MAX6675 (termocupla tipo K) — lectura vía HSPI con pines configurables.
- ZMPT101B (voltaje) — lectura analógica calibrada.

#### 🕒 Tiempo
- Timestamps en microsegundos válidos para InfluxDB.
- Validación de RTC en arranque y fallback a `esp_timer_get_time()`.

#### 🧾 Logs estructurados
- Logging en CSV: `ts_iso,ts_us,level,mod,code,fsm,kv`.
- Trazabilidad completa con módulos identificados (ej: `API`, `SD_BACKUP`, `YF-S201`).
- Logs diarios rotativos en `/eventlog_YYYY.MM.DD.csv`.

#### 🧰 Proyecto y estructura
- Modularizado por archivos: sensores, reintento, backup, logs, envío, tiempo.
- Integración con PlatformIO y documentación en `docs/`.
- Preparado para CI/CD: compilación automática, releases por tags y mirror público.

#### 🔁 Sincronización y CI/CD
- GitHub Actions:
  - `build.yml`: compila y verifica en cada commit.
  - `sync-public.yml`: sincroniza ramas y tags del repositorio privado al público.
- Publicación automática de releases al crear un tag `v*`.

---