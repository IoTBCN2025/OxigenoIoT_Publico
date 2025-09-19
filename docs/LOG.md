# 📄 LOG.md — Estructura y formatos de logs en SD

---

## 🧠 Objetivo

Este documento describe el **formato y estructura** de los logs generados por el sistema IoT Oxígeno, tanto para:

- **Eventos del sistema** (`eventlog_YYYY.MM.DD.csv`)
- **Datos de respaldo** (`backup_YYYYMMDD.csv`)

Los logs permiten realizar **trazabilidad completa**, detectar errores, analizar métricas de rendimiento y auditar el comportamiento del sistema.

---

## 🗂️ 1. Log de eventos: `eventlog_YYYY.MM.DD.csv`

### 📌 Ubicación:
```
/eventlog_2025.09.19.csv
```

### 📄 Formato CSV:
```csv
ts_iso,ts_us,level,mod,code,fsm,kv
```

| Campo       | Descripción |
|-------------|-------------|
| `ts_iso`    | Timestamp legible en formato `YYYY-MM-DD HH:MM:SS` (de RTC o NTP). |
| `ts_us`     | Timestamp en microsegundos (`unsigned long long`). |
| `level`     | Nivel del evento: `INFO`, `WARN`, `ERROR`, `DEBUG`. |
| `mod`       | Módulo que generó el evento: `WIFI`, `RTC`, `SD_BACKUP`, `NTP`, `API`, etc. |
| `code`      | Código corto del evento: `MOD_UP`, `MOD_FAIL`, `RESPALDO`, etc. |
| `fsm`       | Estado FSM (si aplica). Actualmente `-` o valor de `estadoActual`. |
| `kv`        | Clave-valor con detalles adicionales (ej: `sensor=YF-S201;valor=6.85`). |

---

### 🧪 Ejemplo:

```csv
2025-09-19 12:00:01,1757431003039945,INFO,RTC,MOD_UP,-,source=RTC_only
2025-09-19 12:00:01,1757431003059492,INFO,YF-S201,MOD_UP,-,sim=0
2025-09-19 12:00:03,1757431003165047,INFO,SYS,BOOT,-,device_start
2025-09-19 12:00:10,1757431015058558,WARN,SD_BACKUP,RESPALDO,-,reason=no_wifi;sensor=MAX6675
2025-09-19 12:00:15,1757431029144442,INFO,SD_BACKUP,REINTENTO,-,enviados=6;saltados=0;path=/backup_20250919.csv
```

---

## 🧠 Niveles de log (campo `level`)

| Nivel  | Descripción breve |
|--------|--------------------|
| `INFO` | Evento estándar, flujo normal del sistema. |
| `WARN` | Advertencia: condición inusual, pero manejada. |
| `ERROR`| Error: fallo relevante que puede afectar funcionamiento. |
| `DEBUG`| Información técnica detallada (uso interno, análisis). |

---

## 🎯 Ejemplos comunes por módulo

#### ✅ MOD_UP
```csv
2025-09-19 12:00:01,...,INFO,NTP,MOD_UP,-,phase=wifi_up
```

#### ⚠️ MOD_FAIL
```csv
2025-09-19 12:00:02,...,ERROR,WIFI,MOD_FAIL,-,event=disconnect
```

#### 💾 RESPALDO
```csv
2025-09-19 12:00:10,...,WARN,SD_BACKUP,RESPALDO,-,reason=no_wifi;sensor=YF-S201
```

#### 🔁 REINTENTO OK
```csv
2025-09-19 12:00:15,...,INFO,SD_BACKUP,REINTENTO,-,enviados=6;saltados=0;path=/backup_20250919.csv
```

---

## 🗂️ 2. Log de respaldo de datos: `backup_YYYYMMDD.csv`

### 📌 Ubicación:
```
/backup_20250919.csv
/sent/backup_20250919.csv         ← después de envío exitoso
/sent/raw/backup_20250919.csv     ← archivo original (opcional)
```

### 📄 Formato CSV:
```csv
timestamp,measurement,sensor,valor,source,status,ts_envio
```

| Campo        | Descripción |
|--------------|-------------|
| `timestamp`  | Timestamp del dato en microsegundos. |
| `measurement`| Tipo de dato (ej: `caudal`, `temperatura`, `voltaje`). |
| `sensor`     | Nombre del sensor (ej: `YF-S201`, `MAX6675`, `ZMPT101B`). |
| `valor`      | Valor medido (2 decimales). |
| `source`     | Fuente del dato (`backup`, `wifi`, etc.). |
| `status`     | Estado del registro: `PENDIENTE` o `ENVIADO`. |
| `ts_envio`   | Timestamp real de reenvío (en microsegundos). Solo presente cuando `status=ENVIADO`. |

---

### 🧪 Ejemplo antes de enviar:
```csv
1757431090033513,caudal,YF-S201,6.85,backup,PENDIENTE,
```

### 🧪 Ejemplo después de reenviar:
```csv
1757431090033513,caudal,YF-S201,6.85,backup,ENVIADO,1757431124019235
```

---

## 📌 Archivos auxiliares

- `pendientes.idx` → índice con paths de archivos `.csv` que contienen líneas `PENDIENTE`.
- `.meta`          → archivo opcional con control de estado por cada backup (offset, fecha, etc.).

---

## 🧼 Reglas de limpieza y archivado

- Backups reenviados se mueven a `/sent/` y/o `/sent/raw/`.
- Archivos con timestamp `1970` o `unsync` se ignoran salvo que contengan líneas válidas.
- Archivos corruptos se renombran como `bad_*.csv` para análisis posterior.

---

## 💡 Recomendaciones

- Validar que `ts_us` sea distinto de `0` o `943920000000000` para asegurar trazabilidad.
- Analizar eventos `RESPALDO` y `MOD_FAIL` para detectar caídas de red, SD o RTC.
- Usar `REINTENTO_*` y `API_OK` como validación de reenvío exitoso.
- Limpiar la carpeta `/sent/` periódicamente y aplicar rotación semanal de logs.