# Oxígeno IoT – ESP32 + FSM (Agricultura)

[![CI](https://github.com/IoTBCN2025/OxigenoIoT_Publico/actions/workflows/build.yml/badge.svg)](https://github.com/IoTBCN2025/OxigenoIoT_Publico/actions)

Sistema IoT robusto para entornos rurales basado en **ESP32‑WROOM‑32** con **arquitectura FSM**, sensores agrícolas (caudal YF‑S201, termocupla MAX6675, voltaje ZMPT101B), **RTC DS3231** (µs), **respaldo en SD** y **reenvío incremental** hacia una **API PHP** que escribe en **InfluxDB v2**. Incluye **trazabilidad completa** por LOG y **CI/CD** con GitHub Actions.

> Objetivo: **no perder datos** y **mantener trazabilidad** aun con WiFi inestable o caídas eléctricas.

---

## Características clave

* ⏱️ **Timestamp en microsegundos** (DS3231 + `esp_timer_get_time()` para delta estable en ESP32).
* 🧭 **Validación de tiempo** (descarta 0/1970; `rtc.lostPower()` bloquea envíos hasta sincronizar).
* 📡 **Envío HTTP GET** a API intermedia (firma con `api_key`) → InfluxDB (Line Protocol).
* 💾 **Respaldo en SD** cuando la API falla y **reenvío por lotes** (`status=PENDIENTE→OK`, `ts_envio`).
* 🧠 **FSM** con ventanas por sensor (0–29s caudal, 35s temp, 40s voltaje); reintentos **no bloqueantes**.
* 🧰 **Logs CSV** con niveles (INFO/DEBUG/WARN/ERROR) y **rate‑limiting** anti‑spam.
* 🔄 **CI/CD**: build con PlatformIO + artefactos por commit + **Release automático** al taggear `v*`.
* 🛟 **Watchdog** de tareas y reconexión WiFi con backoff. (LTE opcional como fallback).

---

## Arquitectura (alto nivel)

```
[Sensores] ──> [FSM] ──> [API PHP] ──> [InfluxDB] ──> [Grafana]
    │           │          ↑              │              
    └──> [SD Backup] <─────┘         [n8n/IA reglas]

[RTC DS3231 + NTP] -> [Timestamp µs] -> usado en todos los módulos
[WiFi/LTE fallback] -> [Watchdog & Health]
```

---

## Hardware

* **MCU**: ESP32‑WROOM‑32 (DevKit v1 o similar).
* **RTC**: DS3231 (I²C @ 400 kHz si el cableado lo permite).
* **SD**: lector micro‑SD por SPI.
* **Sensores**:

  * Caudalímetro **YF‑S201** (entrada de pulsos; ventana continua 0–29s/min).
  * **MAX6675** (termocupla K) – lectura en el segundo 35.
  * **ZMPT101B** (voltaje AC) – lectura en el segundo 40.
* **Opcional**: módem LTE tipo **Huawei E8372** para contingencia.

> Consulta `docs/FSM.md` para la temporización por ventanas.

---

## Estructura del repositorio

```
OxigenoIoT/
├─ .github/workflows/build.yml      # CI (build + artefacto + release on tag)
├─ docs/
│  ├─ Estructura_Proyecto_IoT.md    # guía de arquitectura y módulos
│  ├─ FSM.md                        # máquina de estados y transiciones
│  ├─ LOG.md                        # diseño de logs CSV y ejemplos
│  └─ CI_CD.md                      # pipeline de Actions y buenas prácticas
├─ include/
├─ lib/
├─ src/
└─ platformio.ini
```

* Documentación ampliada:

  * 📄 **Estructura**: [`docs/Estructura_Proyecto_IoT.md`](./docs/Estructura_Proyecto_IoT.md)
  * ⚙️ **FSM**: [`docs/FSM.md`](./docs/FSM.md)
  * 🧾 **Logs**: [`docs/LOG.md`](./docs/LOG.md)
  * 🚀 **CI/CD**: [`docs/CI_CD.md`](./docs/CI_CD.md)

---

## Puesta en marcha

### Requisitos

* **VS Code + PlatformIO**
* Python 3.11+

### Configura tus secretos (no commitear)

Crea `include/secrets.h` y añádelo a `.gitignore`:

```cpp
#pragma once

// WiFi
static const char* WIFI_SSID = "MiSSID";
static const char* WIFI_PASS = "MiClaveFuerte";

// API
static const char* API_HOST   = "iotbcn.com";
static const int   API_PORT   = 80;
static const char* API_PATH   = "/IoT/api.php";
static const char* API_KEY    = "XXXXXXXXXXXXXXXX";
```

### Compilación / carga / monitor

```bash
pio run -e esp32dev
pio run -e esp32dev -t upload
pio run -e esp32dev -t monitor
```

### `platformio.ini` sugerido

```ini
[env:esp32dev]
platform = espressif32@^6
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = time, colorize
build_flags = -DCORE_DEBUG_LEVEL=3
```

---

## Flujo de datos → API (GET)

Ejemplo de petición:

```
/IoT/api.php?api_key=XXXXX&measurement=caudal&sensor=YF-S201&valor=27.50&ts=1752611394058000&mac=34b7da60c44c&source=LIVE
```

* **Políticas**: no enviar si `ts` inválido; timeout corto (p.e., 5 s); cerrar `WiFiClient` antes de tocar la SD.

---

## Logs y trazabilidad

Formato CSV recomendado (`eventlog_YYYY.MM.DD.csv`):

```
fecha,ts_us,nivel,codigo,detalle,contexto
2025-08-20,1750000123456789,INFO,BOOT,Inicio del sistema,version=v1.2.0
2025-08-20,1750000128456000,WARN,API_ERR,Timeout API,endpoint=/IoT/api.php&dur_ms=5000
2025-08-20,1750000130456000,INFO,BACKUP_OK,Registro almacenado,archivo=backup_20250820.csv
```

Códigos típicos: `BOOT`, `API_OK`, `API_ERR`, `TS_INVALID`, `BACKUP_OK`, `RETRY_SD`, `WIFI_WDT`.

> Detalles y anti‑spam en [`docs/LOG.md`](./docs/LOG.md).

---

## CI/CD

* Workflow: `.github/workflows/build.yml`
* En cada push/PR: build de **PlatformIO** y artefactos.
* Al taggear `v*`: **Release** con binarios (`.bin`, `.elf`, `.map`).

Más info en [`docs/CI_CD.md`](./docs/CI_CD.md).

---

## Resolución de problemas

* **TS = 0/1970**: se bloquean envíos; sincroniza DS3231 (NTP o semilla) y revisa batería del RTC.
* **SD no abre**: cierra `WiFiClient`, reinicia bus SPI, reintenta en frío; aplica **safe‑write**.
* **API timeout/5xx**: guarda en SD, activa reenvío en lotes (10) con rate‑limit de `API_ERR`.
* **Logs ruidosos**: verifica rate‑limit por código y niveles adecuados.

---

## IA / Automatización

* Reglas con **n8n** para alarmas (deriva de caudal, sobre‑temperatura, picos de voltaje).
* Detección de anomalías (IA) sobre series en InfluxDB/Grafana (umbral dinámico, z‑score, STL).

---

## Roadmap

* `esp_timer_get_time()` en `ds3231_time.cpp` para evitar rollover de `micros()`.
* `.meta` por archivo y `pendientes.idx` con limpieza automática.
* Módulo `lte_mgr` (fallback 4G) y health‑check.
* `pio check`/`cppcheck` en CI como job opcional.
* OTA seguro con particiones duales.

---

## Licencia

MIT (propuesto). Añade `LICENSE` en la raíz.

---

## Contribuir

* Realiza PRs con CI en verde.
* Incluye tests o logs de campo si aplica.
* Actualiza documentación pertinente (`docs/`).
