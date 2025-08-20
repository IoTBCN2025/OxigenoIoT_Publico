# Changelog

Todos los cambios notables de este proyecto se documentarán en este archivo.

El formato sigue **Keep a Changelog** y este proyecto adhiere a **Semantic Versioning (SemVer)**.

* Keep a Changelog: [https://keepachangelog.com/es-ES/1.1.0/](https://keepachangelog.com/es-ES/1.1.0/)
* SemVer: [https://semver.org/lang/es/](https://semver.org/lang/es/)

---

## [Unreleased]

### Added

* Documentación en `docs/`: Estructura, FSM, LOG y CI/CD.
* Workflow de CI/CD (`.github/workflows/build.yml`) con caché, artefactos y Release automático por tag `v*`.
* `README.md` con badge del pipeline, guía de uso y referencias.

### Changed

* (Propuesto) Cálculo de timestamp en microsegundos usando `esp_timer_get_time()` en ESP32 para evitar el rollover de `micros()`.

### Fixed

* (Propuesto) Rate‑limiting en logs (`API_ERR`, `RETRY_SD`) para evitar ruido.
* (Propuesto) Safe‑write al actualizar registros `PENDIENTE→OK` en backups SD.

---

## [1.2.0] - 2025-07-22

### Added

* Integración de **RTC DS3231** con validación de plausibilidad (año ≥ 2020) y sincronización vía NTP opcional.
* Arquitectura **FSM** con ventanas por sensor (caudal 0–29s, temp 35s, voltaje 40s).
* Respaldo en **tarjeta SD** y **reenvío incremental** con `status` y `ts_envio`.

### Changed

* Envío a API (HTTP GET) incluyendo `ts` en **microsegundos** para compatibilidad con InfluxDB.

---

## [1.0.0] - 2025-07-22

### Added

* Publicación inicial del proyecto en PlatformIO (ESP32‑WROOM‑32) con estructura base y módulos principales.

---

[Unreleased]: https://github.com/IoTBCN2025/OxigenoIoT_Publico/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/IoTBCN2025/OxigenoIoT_Publico/releases/tag/v1.2.0
[1.0.0]: https://github.com/IoTBCN2025/OxigenoIoT_Publico/releases/tag/v1.0.0
