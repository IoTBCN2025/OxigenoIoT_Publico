# Estructura del Proyecto IoT (ESP32‑WROOM‑32 + FSM)

> **Propósito:** Dejar una guía clara para mantener y escalar el proyecto con arquitectura FSM, sensores agrícolas (YF‑S201, MAX6675, ZMPT101B), RTC DS3231, respaldo en SD, envío a API (InfluxDB vía proxy) y trazabilidad completa por LOG. Pensado para PlatformIO + GitHub (CI/CD) y operación autónoma en entorno rural.

---

## 1) Visión general

**Plataforma:** ESP32‑WROOM‑32 (Arduino framework, PlatformIO).
**Arquitectura:** FSM (Finite State Machine) con módulos desacoplados.
**Tiempo:** DS3231 como fuente primaria; NTP como respaldo; timestamps en **microsegundos**.
**Datos:** Envío vía HTTP GET a API PHP intermedia → InfluxDB v2.
**Resiliencia:** Respaldo en SD + reintentos incrementales + watchdog WiFi.
**Trazabilidad:** `eventlog_YYYY.MM.DD.csv` + estados por registro (`PENDIENTE/OK`) y `ts_envio`.

### Diagrama (alto nivel)

```
[Sensores] ──> [FSM] ──> [API PHP] ──> [InfluxDB] ──> [Grafana]
    │           │          ↑              │              
    └──> [SD Backup] <─────┘         [n8n/IA reglas]
                      
[RTC DS3231 + NTP] -> [Timestamp µs] -> usado en todos los módulos
[WiFi/LTE fallback] -> [Watchdog & Health]
```

---

## 2) Árbol de carpetas (sugerido/objetivo)

```
OxigenoIoT/
├─ .github/
│  └─ workflows/
│     └─ build.yml                # CI: build + artefactos + release on tag
├─ docs/
│  ├─ Estructura_Proyecto_IoT.md  # este documento
│  ├─ FSM.md                      # diagrama/tabla de estados y eventos
│  ├─ LOG.md                      # niveles, formato, ejemplos y análisis
│  ├─ API.md                      # contrato API PHP (param/ejemplos)
│  └─ CI_CD.md                    # detalles de CI/CD y versionado
├─ include/                       # headers públicos
│  ├─ wifi_mgr.h
│  ├─ ds3231_time.h
│  ├─ ntp.h
│  ├─ api.h
│  ├─ sdlog.h
│  ├─ sdbackup.h
│  ├─ reenviarBackupSD.h
│  ├─ sensores_CAUDALIMETRO_YF-S201.h
│  ├─ sensores_TERMOCUPLA_MAX6675.h
│  └─ sensores_VOLTAJE_ZMPT101B.h
├─ lib/                           # librerías locales (si aplica)
├─ src/
│  ├─ main.cpp                    # orquestación FSM
│  ├─ wifi_mgr.cpp
│  ├─ ds3231_time.cpp             # timestamp µs, sync, plausibilidad
│  ├─ ntp.cpp                     # sync NTP (opcional/backup)
│  ├─ api.cpp                     # envío HTTP → API PHP (Influx Line Protocol)
│  ├─ sdlog.cpp                   # eventos del sistema (INFO/DEBUG/WARN/ERROR)
│  ├─ sdbackup.cpp                # persistencia de datos pendientes
│  ├─ reenviarBackupSD.cpp        # reintentos incrementales por lotes
│  ├─ sensores_CAUDALIMETRO_YF-S201.cpp
│  ├─ sensores_TERMOCUPLA_MAX6675.cpp
│  └─ sensores_VOLTAJE_ZMPT101B.cpp
├─ test/                          # pruebas unitarias/integración (si se usan)
├─ platformio.ini
└─ README.md
```

---

## 3) Módulos y responsabilidades

* **`main.cpp`**: FSM central. Coordina ventanas de lectura/envío, reintentos y estados de error recuperable.
* **`wifi_mgr.*`**: conexión WiFi estable con watchdog (reintentos, backoff, métricas de uptime, RSSI, MAC).
* **`ds3231_time.*`**: inicializa I2C, valida `rtc.lostPower()`, plausibilidad (≥2020), obtiene **timestamp en µs**.

  * **Recomendación**: en ESP32 usar `esp_timer_get_time()` (64‑bit) para delta en µs y evitar rollover de `micros()`.
* **`ntp.*`**: sincroniza DS3231 si hay WiFi; evita corregir a cada lectura (histéresis p.e. > 2 s y contador de confirmación).
* **`api.*`**: construye query GET: `api_key`, `measurement`, `sensor`, `valor`, `ts`, `mac`, `source`. Maneja timeouts cortos y cierra cliente antes de SD.
* **`sdlog.*`**: logging de eventos del sistema en CSV con niveles (INFO/DEBUG/WARN/ERROR). Rate‑limit para evitar spam.
* **`sdbackup.*`**: guarda registros `PENDIENTE` en `backup_YYYYMMDD.csv` (o `bad_*.csv` si TS inválido). Gestión de `.meta` y `pendientes.idx`.
* **`reenviarBackupSD.*`**: procesa en lotes (p.e. 10) y por archivo, marca `OK` y añade `ts_envio`. Estrategia safe‑write.
* **Sensores**: tres módulos desacoplados con su ciclo parametrizable:

  * **Caudalímetro YF‑S201**: medición continua (pulsos) típicamente **seg 0‑29** de cada minuto.
  * **Termocupla MAX6675**: lectura instantánea **seg 35**.
  * **Voltaje ZMPT101B**: lectura instantánea **seg 40**.
  * *Todas las operaciones respetan timestamp válido y políticas de reintento/backup.*

---

## 4) FSM (resumen)

**Estados sugeridos:**

* `INICIALIZACION` → init WiFi, SD, DS3231, validación TS
* `LECTURA_CONTINUA_CAUDAL` (0–29s)
* `LECTURA_TEMPERATURA` (35s)
* `LECTURA_VOLTAJE` (40s)
* `REINTENTO_BACKUP` (no bloqueante, lotes pequeños)
* `IDLE` (esperas no activas)
* `ERROR_RECUPERABLE` (manejo centralizado y retorno seguro)

**Eventos:** `WIFI_READY`, `WIFI_DOWN`, `SD_OK`, `SD_FAIL`, `TS_INVALID`, `API_OK`, `API_ERR`, `BACKUP_OK`, `BACKUP_ERR`…
**Reglas clave:** no enviar si `timestamp` inválido; cerrar WiFi antes de SD; reintentar SD en frío si falla init; limitar verbosidad de logs.

---

## 5) Trazabilidad (LOG)

**Archivo:** `eventlog_YYYY.MM.DD.csv`
**Formato CSV recomendado:**

```
fecha,ts_us,nivel,codigo,detalle,contexto
2025-08-20,1750000123456789,INFO,BOOT,Inicio del sistema,version=v1.2.0
2025-08-20,1750000128456000,WARN,API_ERR,Timeout API,endpoint=/IoT/api.php&dur_ms=5000
2025-08-20,1750000130456000,INFO,BACKUP_OK,Registro almacenado,archivo=backup_20250820.csv
```

* **Niveles:** INFO, DEBUG, WARN, ERROR.
* **Buenas prácticas:**

  * Rate‑limit por `codigo` para evitar spam (p.e. `API_ERR` repetido).
  * Todo cambio de estado FSM debe loguearse (para auditoría y post‑mortem).

---

## 6) Respaldo SD y reintentos

**Archivos:**

* `backup_YYYYMMDD.csv` → registros `PENDIENTE`/`OK`.
* `backup_19700101.csv` → **nunca** se elimina; se ignora salvo que contenga pendientes (origen TS inválido).
* `backup_*.meta` → metadatos por archivo (progreso, últimos offsets, reintentos).
* `pendientes.idx` → índice de archivos con pendientes.

**Formato de línea en backup (CSV):**

```
measurement,sensor,valor,ts,mac,source,status,ts_envio
caudal,YF-S201,27.50,1752611394058000,34b7da60c44c,SD,PENDIENTE,
```

* Al reenviar y recibir `OK`, marcar `status=OK` y añadir `ts_envio`.
* **Safe‑write:** editar en archivo temporal y `rename()`; o mantener el archivo abierto + `seek()` solo a la línea afectada.
* **Lotes:** procesar 10 por ciclo para no bloquear; respetar prioridad (FIFO/LIFO) definida en `docs/FSM.md`.

---

## 7) Timestamps (µs) y sincronización

* Fuente primaria: **DS3231** (I²C 400 kHz si el cableado lo permite).
* Plausibilidad mínima: año ≥ 2020. Si `rtc.lostPower()` o fecha inválida, bloquear envíos y disparar estado `SYNC_TIEMPO`.
* **Cálculo µs en ESP32:** preferir `esp_timer_get_time()` para delta en µs (64‑bit), evitando rollover de `micros()` (\~71 min).
* Resync NTP: sólo si |Δ| > umbral (p.e. 2 s) y con histéresis/contador para evitar “bamboleo”.

---

## 8) Envío a API (HTTP GET → InfluxDB)

**Parámetros:** `api_key`, `measurement`, `sensor`, `valor`, `ts`, `mac`, `source`.
Ejemplo de query:

```
/IoT/api.php?api_key=XXXXX&measurement=caudal&sensor=YF-S201&valor=27.50&ts=1752611394058000&mac=34b7da60c44c&source=LIVE
```

**Políticas:**

* Timeout corto (p.e. 5 s).
* Cerrar WiFiClient antes de tocar SD.
* No consumir API si no hay IP asignada o TS inválido.

---

## 9) Métricas de salud

* **Watchdog** (task WDT) y contadores: reconexiones WiFi, fallos API, latencia (`ts_envio - ts`), pendientes en SD.
* **Fuentes de energía**: registrar caídas de alimentación y tiempo offline.

---

## 10) CI/CD (GitHub Actions)

* Workflow `build.yml`:

  * Caché de PlatformIO/PIP.
  * Build por entorno (`esp32dev`), tamaño binario y subida de artefactos.
  * **Release on tag `v*`**: adjunta `firmware.bin`/`.elf`/`.map` al Release.
* SemVer: `vMAJOR.MINOR.PATCH`.

---

## 11) Configuración y secretos

* `platformio.ini`: fijar plataforma (`espressif32@^6`) para builds reproducibles.
* **Secretos**: no commitear credenciales; usar `secrets.h` (excluido en `.gitignore`) o variables de entorno/`PIOCI_TOKEN`.

---

## 12) Convenciones de código

* C++17, `-Wall -Wextra`.
* Prototipos adelantados, llaves en `case {}` para variables locales.
* Nombres en `snake_case` para funciones; `CamelCase` para tipos.
* Logs con códigos estables (`API_ERR`, `TS_INVALID`, `BACKUP_OK`, `RETRY_SD`, `WIFI_WDT`).

---

## 13) Roadmap corto

* Reemplazar `micros()` por `esp_timer_get_time()` en `ds3231_time.cpp`.
* Completar `.meta` y `pendientes.idx` con limpieza automática.
* Añadir `lte_mgr` (fallback E8372) y health‑check.
* `pio check` / `cppcheck` en CI como job opcional.
* `docs/FSM.md` con diagrama y tabla de transiciones.

---

## 14) Troubleshooting

* **TS = 0/1970**: bloquear envíos, registrar `TS_INVALID`, intentar `SYNC_TIEMPO`.
* **SD “busy/cannot open”**: cerrar WiFi, resetear bus SPI, reintento escalonado y `safe‑write`.
* **API 5xx/timeout**: guardar en backup, aplicar rate‑limit a `API_ERR`, agendar `REINTENTO_BACKUP`.
* **Logs ruidosos**: consolidar por código + ventana de supresión.

---

## 15) Licencia y contribución

* Definir licencia (MIT/BSD/Apache‑2.0) en raíz.
* PRs: seguir convención de commits, CI verde y actualización de docs cuando aplique.

---

> **Nota final**: Mantener la trazabilidad completa es prioridad. Cada transición de estado, error recuperable y acción de reintento debe quedar en `eventlog_*.csv` para diagnóstico posterior (Grafana/Excel/ETL). Integrar reglas de IA/n8n para alertas proactivas ante anomalías (deriva de caudal, sobre‑temperatura, picos de voltaje).
