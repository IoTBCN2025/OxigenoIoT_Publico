# Oxígeno IoT – ESP32 + FSM Arquitectura

---

📎 Repositorio oficial: https://github.com/IoTBCN2025/OxigenoIoT_Publico  
📅 Publicado: 2025-09-19  
🔖 Tag: `v1.0.0`  
👤 Autor: José Alberto Monje Ruiz


[![CI](https://github.com/IoTBCN2025/OxigenoIoT_Publico/actions/workflows/build.yml/badge.svg)](https://github.com/IoTBCN2025/OxigenoIoT_Publico/actions)

Sistema IoT robusto para entornos rurales basado en **ESP32‑WROOM‑32** con **arquitectura FSM** modular, sensores agrícolas (caudal YF‑S201, termocupla MAX6675, voltaje ZMPT101B), **RTC DS3231** (µs), **respaldo en SD** y **reintento inteligente** hacia una **API PHP** que escribe en **InfluxDB v2**.  
Incluye **trazabilidad completa** vía logs estructurados, configuración centralizada y **CI/CD** con GitHub Actions.

> 🎯 Objetivo: **no perder datos** y **mantener trazabilidad** incluso con WiFi inestable o caídas eléctricas.

---

## 🆕 Versión estable

📦 `v1.0.0` (2025-09-19): versión inicial estable  
🔖 Tag sincronizado a [OxigenoIoT_Publico](https://github.com/IoTBCN2025/OxigenoIoT_Publico/releases/tag/v1.0.0)

Incluye:
- FSM modular robusta
- Sensores en modo real/simulado (YF-S201, MAX6675, ZMPT101B)
- Respaldo automático y reintento desde SD
- Logging CSV con contexto por módulo y evento
- Centralización de configuración en `config.{h,cpp}`

---

## 🚀 Características destacadas

* ⏱️ Timestamp en microsegundos (RTC DS3231 + fallback con `esp_timer_get_time()`).
* 🧠 FSM no bloqueante por ventanas temporales:
  - 0–29 s: caudal
  - 35 s: temperatura
  - 40 s: voltaje
* 📉 Backup en SD ante fallo de red con reintento por lote y control por `.meta`/`.idx`.
* 📡 Envío HTTP GET firmado (`api_key`) a API intermedia que reenvía a InfluxDB.
* 📁 Logs enriquecidos en CSV: `ts_iso,ts_us,level,module,code,fsm,context`.
* 🔐 Validación de estado WiFi, timestamp, RTC, SD en boot y operación.
* 🛠️ CI/CD GitHub Actions (build + sync mirror).
* ⚙️ Configuración centralizada: pines, modos (real/simulado), claves.

---

## 🧩 Arquitectura general

```
[Sensores] ──> [FSM] ──> [API PHP] ──> [InfluxDB] ──> [Grafana]
    │           │          ↑              │
    └──> [SD Backup] <─────┘         [n8n / IA reglas]

[RTC DS3231] + [NTP] → timestamp µs confiable para todos los módulos
[WiFi] ↔ watchdog y fallback
```

---

## 🔌 Hardware utilizado

* MCU: ESP32-WROOM-32 (DevKit v1)
* RTC: DS3231 (I²C @ 400 kHz)
* SD: lector micro-SD por SPI (CS=5, SCK=18, MISO=19, MOSI=23)
* Sensores:
  * 💧 YF-S201 (caudalímetro) – modo real o simulado
  * 🌡 MAX6675 (termocupla tipo K) – modo HSPI
  * ⚡ ZMPT101B (voltaje AC) – analógico
* Opcional: módem LTE Huawei E8372 como backup

---

## 📂 Estructura del proyecto

```
OxigenoIoT/
├─ src/
│  ├─ main.cpp                       # FSM principal por ventana de sensores
│  ├─ config.{h,cpp}                # Configuración centralizada
│  ├─ api.cpp                       # Envío a API PHP
│  ├─ wifi_mgr.cpp                  # Conexión WiFi y watchdog
│  ├─ ntp.cpp / ds3231_time.cpp    # Sincronización y timestamp µs
│  ├─ sdlog.cpp                     # Registro de eventos y errores
│  ├─ sdbackup.cpp                  # Backup en SD con formato CSV
│  ├─ reenviarBackupSD.cpp         # Reintento desde archivos SD
│  └─ sensores_*.cpp                # Módulos: YF-S201, MAX6675, ZMPT101B
├─ docs/
│  ├─ Main_cpp.md                   # FSM y lógica principal
│  ├─ LOG.md                        # Logs estructurados
│  ├─ CI_CD.md                      # Flujo de despliegue
│  ├─ Caudalimetro_YF-S201.md      # Sensor de caudal
│  ├─ Termocupla_MAX6675.md        # Sensor de temperatura
│  ├─ Voltimetro_ZMPT101B.md       # Sensor de voltaje
│  └─ Infraestructura_Tiempo_WiFi.md
├─ .github/workflows/
│  ├─ build.yml                     # CI PlatformIO
│  └─ sync-public.yml              # Sync repositorio público
└─ platformio.ini
```

---

## ⚙️ Configuración WiFi y API (`secrets.h`)

```cpp
#pragma once
static const char* WIFI_SSID = "MiRed";
static const char* WIFI_PASS = "MiClave";

static const char* API_HOST = "iotbcn.com";
static const int   API_PORT = 80;
static const char* API_PATH = "/IoT/api.php";
static const char* API_KEY  = "XXXXXXXXXXXXXXX";
```

> Asegúrate de incluir `include/secrets.h` en `.gitignore`.

---

## 🧪 Comandos PlatformIO

```bash
pio run -e esp32dev             # Compilar
pio run -e esp32dev -t upload   # Subir firmware
pio run -e esp32dev -t monitor  # Ver salida por serie
```

---

## 📤 Flujo de datos a la API

Ejemplo:

```
/IoT/api.php?api_key=XXXXX&measurement=caudal&sensor=YF-S201&valor=27.50&ts=1752611394058000&mac=34b7da60c44c&source=LIVE
```

- Si falla: el dato se guarda como `PENDIENTE` en la SD.
- Al reenviar: se marca como `ENVIADO`, se agrega `ts_envio` y se calcula latencia.

---

## 📝 Formato de logs

Archivo: `eventlog_YYYY.MM.DD.csv`

```csv
ts_iso,ts_us,level,module,code,fsm,context
2025-09-15 10:01:22,1752611394058000,INFO,BOOT,MOD_UP,,wifi=OK
2025-09-15 10:01:33,1752611396050000,WARN,API,API_ERR,,status=timeout
2025-09-15 10:01:35,1752611398000000,INFO,SD_BACKUP,BACKUP_OK,,archivo=backup_20250915.csv
```

> Más detalles en [`docs/LOG.md`](./docs/LOG.md)

---

## 🚀 CI/CD y sincronización automática

- `build.yml`: compila en cada push y genera artefactos (`.bin`, `.elf`, `.map`)
- `sync-public.yml`: sincroniza automáticamente el repositorio privado con el público (`IoTBCN2025/OxigenoIoT_Publico`)
- Al crear un tag `v*`, se publica automáticamente un release en GitHub

---

## 🧠 Automatización / IA

- `n8n` para alertas automáticas (falta de datos, anomalías)
- Visualización en Grafana
- IA para detección de fallos usando `z-score`, STL o reglas dinámicas

---

## 🧪 Troubleshooting

| Problema                        | Diagnóstico                                                  |
|---------------------------------|--------------------------------------------------------------|
| ❌ Timestamp 0 o 1970           | RTC desincronizado o batería agotada                         |
| ❌ Falla al escribir en SD      | Verifica cerrar WiFi antes de SD, reinicializar SPI          |
| ❌ Logs excesivos               | Verifica nivel de log y evita repetición (coalescing/log rate)|

---

## 📄 Licencia

MIT (pendiente de inclusión como `LICENSE` en la raíz)

---

## 🤝 Contribuir

- PRs con CI en verde ✅
- Añadir logs de pruebas reales o fotos de instalación
- Mantener consistencia en FSM y documentación en `docs/`