# Oxígeno IoT – ESP32 + FSM (Agricultura)

[![CI](https://github.com/IoTBCN2025/OxigenoIoT_Publico/actions/workflows/build.yml/badge.svg)](https://github.com/IoTBCN2025/OxigenoIoT_Publico/actions)

Sistema IoT robusto para entornos rurales basado en **ESP32‑WROOM‑32** con **arquitectura FSM**, sensores agrícolas (caudal YF‑S201, termocupla MAX6675, voltaje ZMPT101B), **RTC DS3231** (µs), **respaldo en SD** y **reenvío incremental** hacia una **API PHP** que escribe en **InfluxDB v2**. Incluye **trazabilidad completa** por LOG, documentación modular y **CI/CD** con GitHub Actions.

> Objetivo: **no perder datos** y **mantener trazabilidad** aun con WiFi inestable o caídas eléctricas.

---

## 🆕 Última versión

📦 `v1.4.0`: incluye mejoras en `main.cpp`, trazabilidad modular con `logEventoM(...)` y nueva documentación técnica [`docs/Main_cpp.md`](./docs/Main_cpp.md).  
🎯 Tag sincronizado a [OxigenoIoT_Publico](https://github.com/IoTBCN2025/OxigenoIoT_Publico/tags).

---

## Características clave

* ⏱️ **Timestamp en microsegundos** (DS3231 + `esp_timer_get_time()` como fallback).
* 📉 **Respaldo en SD** y **reintento inteligente** con `status` y `ts_envio` (latencia).
* 📡 **Envío HTTP GET** firmado (`api_key`) hacia API → InfluxDB (Line Protocol).
* 🧠 **FSM por ventanas**: 0–29 s (caudal), 35 s (temp), 40 s (voltaje); no bloqueante.
* 🧾 **Logs CSV enriquecidos**: nivel (INFO/WARN/ERROR/DEBUG), módulo, código y contexto.
* 🛠️ **CI/CD**: PlatformIO, artefactos por commit y release automático con `v*`.
* 🔁 **Sincronización privada ↔ pública** automática de ramas y tags (`sync-public.yml`).

---

## Arquitectura general

```
[Sensores] ──> [FSM] ──> [API PHP] ──> [InfluxDB] ──> [Grafana]
    │           │          ↑              │
    └──> [SD Backup] <─────┘         [n8n / IA reglas]

[RTC DS3231] + [NTP] → Timestamp µs para todos los módulos
[WiFi] ↔ [Watchdog / Fallback LTE]
```

---

## Hardware utilizado

* **MCU**: ESP32-WROOM-32 (DevKit v1)
* **RTC**: DS3231 (I²C @ 400 kHz)
* **SD**: lector micro-SD por SPI (CS=5)
* **Sensores**:
  * 💧 YF-S201 (caudalímetro)
  * 🌡 MAX6675 (termocupla tipo K)
  * ⚡ ZMPT101B (voltaje AC)
* **Opcional**: módem LTE Huawei E8372 (modo fallback)

---

## Estructura del proyecto

```
OxigenoIoT/
├─ src/
│  ├─ main.cpp                  # FSM principal por ventana de sensores
│  └─ sensores_*.cpp            # Caudal, temperatura, voltaje
├─ docs/
│  ├─ Main_cpp.md               # Documentación técnica de main.cpp y FSM
│  ├─ FSM.md                    # Estados, transiciones y reintentos
│  ├─ LOG.md                    # Trazabilidad, formatos, niveles
│  └─ CI_CD.md                  # Flujo CI/CD y releases
├─ .github/workflows/
│  ├─ build.yml                 # CI PlatformIO + releases
│  └─ sync-public.yml           # Mirror a repo público con tags
└─ platformio.ini
```

---

## Ejecución del firmware

### 1. Configura `secrets.h`

```cpp
#pragma once
static const char* WIFI_SSID = "MiRed";
static const char* WIFI_PASS = "MiClave";

static const char* API_HOST = "iotbcn.com";
static const int   API_PORT = 80;
static const char* API_PATH = "/IoT/api.php";
static const char* API_KEY  = "XXXXXXXXXXXXXXX";
```

Asegúrate de agregar `include/secrets.h` al `.gitignore`.

---

### 2. Comandos PlatformIO

```bash
pio run -e esp32dev            # Compilar
pio run -e esp32dev -t upload  # Subir firmware
pio run -e esp32dev -t monitor # Monitor serie
```

---

### 3. `platformio.ini` sugerido

```ini
[env:esp32dev]
platform = espressif32@^6
board = esp32dev
framework = arduino
monitor_speed = 115200
build_flags = -DCORE_DEBUG_LEVEL=3
monitor_filters = time, colorize
```

---

## Flujo de datos a la API

Ejemplo de envío (HTTP GET):

```
/IoT/api.php?api_key=XXXXX&measurement=caudal&sensor=YF-S201&valor=27.50&ts=1752611394058000&mac=34b7da60c44c&source=LIVE
```

* Si la API responde con error o timeout: el dato se guarda en SD como `status=PENDIENTE`.
* Al reenviar: se marca como `ENVIADO`, se registra `ts_envio` y se guarda la latencia.

---

## Formato de logs

Archivo: `eventlog_YYYY.MM.DD.csv`

```csv
ts_iso,ts_us,level,module,code,fsm,context
2025-09-15 10:01:22,1752611394058000,INFO,BOOT,MOD_UP,,wifi=OK
2025-09-15 10:01:33,1752611396050000,WARN,API,API_ERR,,status=timeout
2025-09-15 10:01:35,1752611398000000,INFO,SD_BACKUP,BACKUP_OK,,archivo=backup_20250915.csv
```

> Ver más en [`docs/LOG.md`](./docs/LOG.md)

---

## CI/CD y sincronización pública

- GitHub Actions:
  - `build.yml`: compila, genera artefactos.
  - `sync-public.yml`: sincroniza ramas y tags al mirror público.
- Al crear tag `v*`: se genera un **release automático** con binarios `.bin`, `.elf`, `.map`.

---

## Troubleshooting

- ❌ **TS inválido (0 o 1970)** → RTC mal sincronizado, revisar batería.
- ❌ **SD falla** → cerrar WiFi, reiniciar SPI, aplicar safe-write.
- ❌ **Logs ruidosos** → usar niveles adecuados y rate-limiting por código.

---

## Automatización / IA

- 🔁 n8n para alertas automáticas (ej. temperatura alta, pérdida de caudal).
- 🤖 Análisis en Grafana/Influx con IA:
  - Anomalías: `z-score`, STL, umbrales dinámicos.
  - Eventos: visualización temporal por sensor/log.

---

## Licencia

MIT (propuesta). Añade un archivo `LICENSE` en la raíz.

---

## Contribuir

- Haz PRs con CI en verde ✅
- Añade logs o ejemplos de pruebas de campo.
- Actualiza la documentación en `docs/`.