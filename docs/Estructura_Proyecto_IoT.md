# Estructura del Proyecto IoT (ESP32‑WROOM‑32 + FSM) — v1.4.2

> **Propósito:** Guía clara para mantener y escalar el proyecto con arquitectura FSM, sensores agrícolas (YF‑S201, MAX6675, ZMPT101B), RTC DS3231, respaldo en SD, envío a API (InfluxDB vía proxy HTTP) y trazabilidad completa por LOG. Diseñado para **PlatformIO + GitHub (CI/CD)** y operación autónoma en entorno rural (intermitencias de red/energía).

---

## 1) Visión general

**Plataforma:** ESP32‑WROOM‑32 (Arduino framework, PlatformIO).  
**Arquitectura:** FSM (Finite State Machine) con módulos desacoplados y logs estructurados.  
**Tiempo:** DS3231 como fuente primaria; NTP como respaldo; timestamps en **microsegundos**.  
**Datos:** Envío vía HTTP GET a API PHP intermedia → InfluxDB v2. Visualización en Grafana.  
**Resiliencia:** Respaldo en SD + reintentos incrementales + watchdog WiFi.  
**Trazabilidad:** `eventlog_YYYY.MM.DD.csv` (eventos) + `backup_YYYYMMDD.csv` (datos), `status=PENDIENTE/ENVIADO` y `ts_envio`.

### Diagrama (alto nivel)

```
[Sensores] ──> [FSM] ──> [API PHP] ──> [InfluxDB] ──> [Grafana]
    │           │          ↑              │              
    └──> [SD Backup] <─────┘         [n8n/IA reglas]
                      
[RTC DS3231 + NTP] -> [Timestamp µs] -> usado en todos los módulos
[WiFi/LTE fallback] -> [Watchdog & Health]
```

---

## 2) Árbol de carpetas (estado objetivo)

```
OxigenoIoT/
├─ .github/
│  └─ workflows/
│     ├─ build.yml                # CI: build + artefactos + release on tag
│     └─ sync-public.yml          # (opcional) espejo a repo público
├─ docs/
│  ├─ Estructura_Proyecto_IoT.md  # este documento
│  ├─ FSM.md                      # diagrama/tabla de estados y eventos
│  ├─ LOG.md                      # niveles, formato, ejemplos y análisis
│  ├─ Caudalimetro_YF-S201.md     # sensor caudal (pulsos → L/min)
│  ├─ Termocupla_MAX6675.md       # sensor temperatura (HSPI)
│  └─ Voltimetro_ZMPT101B.md      # sensor voltaje AC (ADC)
├─ include/                       # headers públicos
│  ├─ config.h
│  ├─ secrets.h                   # credenciales (NO commitear)
│  ├─ wifi_mgr.h
│  ├─ ds3231_time.h
│  ├─ ntp.h
│  ├─ api.h
│  ├─ sdlog.h
│  ├─ sdbackup.h
│  ├─ reenviarBackupSD.h
│  ├─ spi_temp.h
│  ├─ sensores_CAUDALIMETRO_YF-S201.h
│  ├─ sensores_TERMOCUPLA_MAX6675.h
│  └─ sensores_VOLTAJE_ZMPT101B.h
├─ src/
│  ├─ main.cpp                    # orquestación FSM
│  ├─ config.cpp                  # parámetros centralizados (pines, modos, NTP, API)
│  ├─ wifi_mgr.cpp
│  ├─ ds3231_time.cpp             # timestamp µs, sync, plausibilidad
│  ├─ ntp.cpp                     # sync NTP (respaldo/ajuste RTC)
│  ├─ api.cpp                     # envío HTTP → API PHP (Influx Line Protocol)
│  ├─ sdlog.cpp                   # eventos del sistema (INFO/WARN/ERROR/DEBUG)
│  ├─ sdbackup.cpp                # persistencia de datos pendientes
│  ├─ reenviarBackupSD.cpp        # reintentos incrementales por lotes + safe-write
│  ├─ spi_temp.cpp                # gestor HSPI exclusivo para MAX6675
│  ├─ sensores_CAUDALIMETRO_YF-S201.cpp
│  ├─ sensores_TERMOCUPLA_MAX6675.cpp
│  └─ sensores_VOLTAJE_ZMPT101B.cpp
├─ test/                          # pruebas unitarias/integración (si se usan)
├─ platformio.ini
├─ CHANGELOG.md
└─ README.md
```

> **Nota**: Añade `include/secrets.h` a `.gitignore`. Si usas repositorio público, evita exponer claves/tokens.

---

## 3) Módulos y responsabilidades

- **`main.cpp`**: FSM central. Coordina ventanas de lectura/envío, reintentos y estados de error recuperable. Gestiona sincronización NTP periódica, fallback de timestamp y transición a `REINTENTO_BACKUP` sin bloquear el loop.
- **`config.{h,cpp}`**: configuración centralizada (pines, modos `REAL/SIMULATION`, NTP, API, timing por sensor). Facilita escalabilidad y conmutación de hardware/simulación sin tocar lógica.
- **`wifi_mgr.*`**: conexión WiFi estable con watchdog (reintentos, backoff, métricas de uptime, RSSI, MAC). Emite `WIFI_UP/WIFI_WAIT/MOD_FAIL`.
- **`ds3231_time.*`**: inicializa I2C, valida `rtcIsPresent()` y `rtcIsTimeValid()`, obtiene **timestamp en µs** con fallback a `millis()` si es necesario.
- **`ntp.*`**: sincroniza DS3231 si hay WiFi; resincroniza cada 6 h; backoff específico si el RTC es inválido.
- **`api.*`**: construye query GET (`api_key`, `measurement`, `sensor`, `valor`, `ts`, `mac`, `source`). Timeouts cortos y cierre del cliente antes de usar SD.
- **`sdlog.*`**: logging de eventos del sistema en CSV con niveles (INFO/WARN/ERROR/DEBUG) y coalescencia para evitar spam.
- **`sdbackup.*`**: guarda registros `PENDIENTE` en `backup_YYYYMMDD.csv`. Maneja `.idx` y `.meta` básicos.
- **`reenviarBackupSD.*`**: procesa en lotes (p.e. 10) y por archivo, marca `ENVIADO` y añade `ts_envio`. Estrategia **safe‑write**/archivo temporal o `seek()` controlado.
- **`spi_temp.*`**: encapsula HSPI para el MAX6675 (evita colisiones SPI con SD).
- **Sensores** (desacoplados y parametrizables):
  - **YF‑S201 (caudalímetro)**: pulsos → L/min (ventana **seg 0‑29**). Interrupción en GPIO27.  
  - **MAX6675 (termocupla K)**: lectura HSPI (CS=15, SCK=14, SO=12) **seg 35**.  
  - **ZMPT101B (voltaje AC)**: muestreo ADC (GPIO32) **seg 40** con 500 muestras/100 ms y conversión calibrada.

---

## 4) FSM (resumen)

**Estados:** `INICIALIZACION`, `LECTURA_CONTINUA_CAUDAL`, `LECTURA_TEMPERATURA`, `LECTURA_VOLTAJE`, `REINTENTO_BACKUP`, `IDLE`, `ERROR_RECUPERABLE`.

**Eventos de disparo** (ejemplos): `WIFI_UP`, `WIFI_DOWN`, `TS_INVALID`, `SD_OK`, `SD_FAIL`, `API_OK`, `API_ERR`, `BACKUP_OK`…

**Reglas clave:**
- No enviar si `timestamp` inválido (`0` o `943920000000000`); usar **fallback µs** desde `millis()` y respaldar en SD.
- Cerrar `WiFiClient` antes de tocar SD para evitar colisiones.
- Reintentar SD en frío si falla init (`ERROR_RECUPERABLE`) y retornar a `IDLE` cuando esté lista.
- Limitar la verbosidad de logs (`rate-limit` en reintentos).

---

## 5) Trazabilidad (LOG)

**Archivo:** `eventlog_YYYY.MM.DD.csv`  
**Formato:** `ts_iso,ts_us,level,mod,code,fsm,kv` (ver `docs/LOG.md`).

Buenas prácticas:
- Registrar **transiciones FSM** (`FSM_STATE`) con el identificador del estado.
- `MOD_UP/MOD_FAIL` en el arranque de cada módulo (SD/RTC/WiFi/NTP/API/Sensores).
- Consolidar eventos repetitivos (coalescing) para evitar ruido.
- Marcar `API_OK`/`RESPALDO` por cada intento de envío/backup.

---

## 6) Respaldo SD y reintentos

**Archivos:**  
- `backup_YYYYMMDD.csv` → registros `PENDIENTE/ENVIADO`.  
- `backup_19700101.csv` → no se elimina; se **ignora** salvo que contenga pendientes (TS inválido).  
- `*.meta` → metadatos por archivo (opcional: progreso, offsets, reintentos).  
- `pendientes.idx` → índice de archivos con pendientes.

**Formato CSV (backup):**
```
timestamp,measurement,sensor,valor,source,status,ts_envio
1757431090033513,caudal,YF-S201,6.85,backup,PENDIENTE,
1757431090033513,caudal,YF-S201,6.85,backup,ENVIADO,1757431124019235
```

**Estrategia de escritura segura:**
- Edición en archivo temporal + `rename()`; o mantener handler + `seek()` preciso solo a la línea modificada.
- Procesar **lotes pequeños** (p.e. 10) por ciclo para no bloquear el loop.
- Prioridad FIFO/LIFO según política definida (ver `docs/FSM.md`).

---

## 7) Timestamps (µs) y sincronización

- Fuente primaria: **DS3231** (I²C 400 kHz recomendado si el cableado lo permite).  
- Plausibilidad mínima: año ≥ 2020. Si `rtcIsTimeValid()` es falso, usar fallback µs (`millis()*1000`) y disparar resync NTP.  
- Resincronización NTP: al **WIFI_UP** y de forma **periódica cada 6 h**.  
- Constantes de invalidación comunes: `0`, `943920000000000`.  
- **Recomendación** interna: para deltas en µs usar `esp_timer_get_time()` (64‑bit) y evitar rollover de `micros()` (~71 min).

---

## 8) Envío a API (HTTP GET → InfluxDB)

**Parámetros:** `api_key`, `measurement`, `sensor`, `valor`, `ts`, `mac`, `source`.  
**Ejemplo:**  
```
/IoT/api.php?api_key=XXXXX&measurement=caudal&sensor=YF-S201&valor=27.50&ts=1752611394058000&mac=34b7da60c44c&source=LIVE
```

**Políticas:**
- Timeout corto (p.e. 5 s).  
- Cerrar WiFiClient antes de operar SD.  
- No invocar API si no hay IP válida o TS inválido.  
- Log de `API_OK` / `API_ERR` con detalles (códigos HTTP, latencia).

---

## 9) Métricas de salud

- Reconexiones WiFi (conteo y última causa).  
- Fallos API por tipo (timeout, HTTP 5xx).  
- Latencia de pipeline (`ts_envio - ts`).  
- Pendientes en SD y tasa de vaciado.  
- Estado de módulos en arranque (`MOD_UP/MOD_FAIL`) y memoria libre (`heap`).

---

## 10) CI/CD (GitHub Actions)

**Workflow `build.yml`** (sugerido):
- Cache de PlatformIO/PIP.
- Compilar entorno `esp32dev` (y variantes si aplica).
- Publicar artefactos (`firmware.bin/.elf/.map`).

**Release on tag `v*`:**
- Crear Release y adjuntar binarios.
- Actualizar `CHANGELOG.md` automáticamente (opcional).

**SemVer:** `vMAJOR.MINOR.PATCH` (ej., `v1.4.2`).

---

## 11) Configuración y secretos

- `platformio.ini`: fijar plataforma (`espressif32@^6`) para builds reproducibles.
- **Secretos:** no commitear credenciales; usar `include/secrets.h` (excluido en `.gitignore`) o variables de entorno.
- **config centralizado:** `config.{h,cpp}` expone pines, credenciales, modos y ventanas temporales.

---

## 12) Convenciones de código

- C++17, `-Wall -Wextra`.
- Prototipos adelantados; llaves `{}` en `case` para variables locales.
- Evitar `String` en paths críticos; preferir `snprintf` y buffers fijos.
- Macros de log por nivel (`LOGI/LOGW/LOGE/LOGD`) o `logEventoM(mod, code, kv)` uniformes.
- ISR marcadas con `IRAM_ATTR` (ej. contador de pulsos YF‑S201).
- Manejo cuidadoso de SPI/HSPI para evitar colisiones (SD vs MAX6675).

---

## 13) Roadmap corto

- Migrar del uso de `micros()` a `esp_timer_get_time()` para obtener µs robustos.
- Completar `.meta` y `pendientes.idx` con limpieza automática y resumen de estado.
- Añadir `lte_mgr` (E8372) como fallback WAN y health‑check periódico.
- Añadir `pio check` / `cppcheck` en CI como job opcional.
- `docs/FSM.md`: añadir diagrama de estados/transiciones actualizado.

---

## 14) Troubleshooting

- **TS = 0 o 1970:** bloquear envíos, registrar `TS_INVALID`, activar `SYNC_TIEMPO` (NTP) y respaldar a SD con fallback µs.  
- **SD “busy/cannot open”:** cerrar WiFi, resetear bus SPI, reintento escalonado y **safe‑write**.  
- **API 5xx/timeout:** guardar en backup, aplicar rate‑limit a `API_ERR`, agendar `REINTENTO_BACKUP`.  
- **Logs ruidosos:** consolidar por código + ventana de supresión.  
- **HSPI/SPI colisión:** asegurar CS únicos, inicialización de `spi_temp` aislada y `noInterrupts()` en secciones críticas.  
- **ADC ruido (ZMPT101B):** GND común, cables cortos, promedio por ventana y calibración de factor (50/60 Hz).

---

## 15) Anexos (pines y parámetros actuales)

**Sensores (modo REAL):**
- **YF‑S201**: GPIO27 (pulsos, `RISING`). Conversión: `L/min = pulsos / 7.5`.
- **MAX6675** (HSPI): CS=15, SCK=14, SO(MISO)=12.  
- **ZMPT101B**: GPIO32 (ADC). Muestreo 500 lecturas/100 ms. Factor típico: `V = p2p * 0.2014` (calibrable).

**SD (VSPI por defecto):** CS=5, SCK=18, MISO=19, MOSI=23.  
**RTC (I2C):** SDA=21, SCL=22.  
**NTP:** `pool.ntp.org`, GMT+2 (7200 s), sin DST (ajustable).  
**API:** `http://iotbcn.com/IoT/api.php` con `api_key` (ver `secrets.h`).

---

> **Nota final:** Mantener la trazabilidad completa es prioridad. Cada transición de estado, error recuperable y acción de reintento debe quedar en `eventlog_*.csv` para diagnóstico posterior (Grafana/Excel/ETL). Integrar reglas de IA/n8n para alertas proactivas ante anomalías (deriva de caudal, sobre‑temperatura, picos de voltaje).