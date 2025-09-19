# FSM.md – Arquitectura FSM (Finite State Machine) para Sistema IoT

Este documento describe la estructura actualizada de la máquina de estados finitos (FSM) del sistema IoT basado en ESP32, con RTC DS3231, respaldo en SD, reintento inteligente y trazabilidad estructurada mediante `logEventoM()`.

---

## 🧠 Principios clave

- **Modularidad**: cada módulo (WiFi, NTP, API, SD, sensores) gestiona sus propios estados y reportes.
- **Resiliencia**: el sistema opera con RTC inválido mediante fallback con `millis()` y reintento automático.
- **Backup inteligente**: los datos se almacenan y reenvían desde la SD usando índice `.idx` y `status=ENVIADO`.
- **Trazabilidad estructurada**: todos los eventos son registrados con nivel (`INFO`, `WARN`, `ERROR`, `DEBUG`), módulo y valores clave (kv).

---

## 🎯 Estados FSM principales (`main.cpp`)

| Estado                | Descripción                                                                 |
|-----------------------|------------------------------------------------------------------------------|
| `INICIALIZACION`      | Configura WiFi, RTC, SD y sensores. Genera resumen de módulos con `MOD_UP`. |
| `IDLE`                | Estado base. Evalúa segundo del minuto para decidir próxima acción.         |
| `LECTURA_CONTINUA_CAUDAL` | Mide caudal cada 1 s durante ventana 0–29 s. Usa interrupciones.      |
| `LECTURA_TEMPERATURA` | Lectura puntual en el segundo 35. Utiliza HSPI para MAX6675.                |
| `LECTURA_VOLTAJE`     | Lectura puntual en el segundo 40. Usa 500 muestras ADC para ZMPT101B.       |
| `REINTENTO_BACKUP`    | Reenvía datos pendientes desde SD si hay red. Incluye control de logs.      |
| `ERROR_RECUPERABLE`   | Reintenta recuperación de la SD en caso de fallo.                           |

---

## ⏱ Control de tiempo y ventanas

- El FSM se sincroniza con el segundo del minuto (`getUnixSeconds()`).
- Cada sensor tiene su **ventana temporal específica**, configurable vía `config.timing`.
- Si no hay RTC válido, se usa `millis()` como fallback.
- Ejemplo de transición:
  ```cpp
  if (segundo == config.timing.window_temp && epochMinute != lastEpochMinuteTemp) {
      estadoActual = LECTURA_TEMPERATURA;
  }
  ```

---

## 🧪 Trazabilidad por estado

Cada transición genera un evento como:

```cpp
char kv[24];
snprintf(kv, sizeof(kv), "state=%d", (int)estadoActual);
logEventoM("FSM", "FSM_STATE", kv);
```

- **Nivel:** `INFO`
- **Módulo:** `FSM`
- **Evento:** `FSM_STATE`
- **kv:** `state=ID`

---

## 🔁 Resumen de arranque (`STARTUP_SUMMARY`)

Registra cuántos módulos fueron inicializados con éxito:

```cpp
logEventoM("SYS", "STARTUP_SUMMARY", "up=6;fail=0;elapsed_ms=625");
```

- También se registra `BOOT_INFO`, `MOD_UP`, `MOD_FAIL`, `RTC_OK`, `RTC_ERR`, etc.

---

## ⚠️ Manejo de fallos y recuperación

| Escenario         | Manejo                                                                 |
|------------------|------------------------------------------------------------------------|
| WiFi no disponible | Backup en SD, reintento en estado `REINTENTO_BACKUP`                 |
| RTC inválido       | Uso de `millis()` y reintento de sincronización NTP cada 10s         |
| SD ausente         | Reintentos periódicos, estado `ERROR_RECUPERABLE`                    |
| API caída (HTTP 5xx)| Respaldo en SD + log `RESPALDO` y `API_ERR`                         |

---

## 🔄 Reintento desde SD

- FSM transiciona a `REINTENTO_BACKUP` si:
  - Se recupera WiFi (`WIFI_UP`)
  - Han pasado 30s sin reintento
- Se procesan archivos `backup_*.csv` y `.idx`
- Logs asociados:
  - `REINTENTO_INFO`, `REINTENTO_WAIT`, `REINTENTO_SUMMARY`

---

## 📎 Logs destacados por estado

| Estado                    | Logs relevantes                                                             |
|---------------------------|------------------------------------------------------------------------------|
| `INICIALIZACION`          | `MOD_UP`, `MOD_FAIL`, `RTC_OK`, `RTC_ERR`, `BOOT_INFO`                      |
| `LECTURA_CONTINUA_CAUDAL` | `API_OK`, `RESPALDO`, `TS_INVALID_BACKUP`                                   |
| `LECTURA_TEMPERATURA`     | `API_OK`, `RESPALDO`, `TS_INVALID_BACKUP`, `READ_ERR`, `LECTURA_OK`         |
| `LECTURA_VOLTAJE`         | `API_OK`, `RESPALDO`, `TS_INVALID_BACKUP`, `READ_OK`                        |
| `REINTENTO_BACKUP`        | `REINTENTO_INFO`, `REINTENTO_SUMMARY`, `REINTENTO_WAIT`, `ENVIADO`          |
| `ERROR_RECUPERABLE`       | `SD_OK`, `SD_FAIL`, `reinit_after_error`                                    |