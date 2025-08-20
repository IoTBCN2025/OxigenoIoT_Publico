# Diseño de LOG y Trazabilidad

> Objetivo: obtener **auditabilidad completa** del sistema IoT (ESP32) con niveles, códigos estables y formato CSV optimizado para análisis posterior (Grafana/Excel/ETL).

---

## Archivo y rotación

* **Nombre:** `eventlog_YYYY.MM.DD.csv`
* **Ubicación:** raíz de la SD o `/logs/` (configurable).
* **Rotación:** un archivo por día; en cambio de fecha (RTC), cerrar y abrir el nuevo.
* **En RAM:** si SD falla, cachear eventos en memoria y **reintentar** su volcado cuando la SD vuelva (`reintentarLogsPendientes()`).

---

## Formato CSV

Cabecera:

```
fecha,ts_us,nivel,codigo,detalle,contexto
```

Campos:

* `fecha`: `YYYY-MM-DD HH:MM:SS` (derivado del RTC; zona horaria si aplica).
* `ts_us`: timestamp en **microsegundos**.
* `nivel`: `INFO|DEBUG|WARN|ERROR`.
* `codigo`: identificador corto *estable*.
* `detalle`: mensaje humano corto.
* `contexto`: pares `k=v` separados por `&` (flexible y fácil de parsear).

Ejemplos:

```
2025-08-20,1750000123456789,INFO,BOOT,Inicio del sistema,version=v1.2.0&mac=34b7da60c44c
2025-08-20,1750000128456000,WARN,API_ERR,Timeout API,endpoint=/IoT/api.php&dur_ms=5000
2025-08-20,1750000130456000,INFO,BACKUP_OK,Registro almacenado,archivo=backup_20250820.csv&bytes=112
2025-08-20,1750000131456000,ERROR,SD_FAIL,No se pudo abrir archivo,errno=2
```

---

## Niveles

* **DEBUG:** información detallada para desarrollo (desactivar en producción si afecta rendimiento).
* **INFO:** cambios de estado, eventos esperados, métricas resumidas.
* **WARN:** condiciones anómalas recuperables (timeouts cortos, reintentos, degradación temporal).
* **ERROR:** fallos que requieren acción de recuperación o impactan el servicio.

---

## Códigos de evento (recomendados)

* **BOOT**: arranque del sistema.
* **WIFI\_UP / WIFI\_DOWN / WIFI\_RECONN**: conectividad WiFi.
* **API\_OK / API\_ERR**: resultados del envío HTTP. `dur_ms`, `http_code`.
* **TS\_INVALID / TS\_SYNC\_OK / TS\_SYNC\_ERR**: estado del tiempo.
* **SD\_OK / SD\_FAIL / SD\_RETRY**: estado de la SD, reintentos de init/escritura.
* **BACKUP\_SAVE / BACKUP\_OK / BACKUP\_ERR**: respaldo y reenvíos.
* **RETRY\_SD**: operación de reintento SD en frío.
* **SENSOR\_READ / SENSOR\_ERR**: lectura de sensores con métricas.
* **WDT\_FEED / WDT\_RESET**: watchdog alimentado / reinicio por WDT.

> **Nota:** mantener la lista acotada y documentada. Nuevos códigos → PR que actualice esta tabla.

---

## Rate‑limiting de logs

Evitar spam cuando hay errores repetidos (ej. pérdidas de WiFi o API timeouts):

* Mantener un **contador por `codigo`** y una **ventana temporal** (p. ej., 60 s).
* Loggear la **primera** ocurrencia y luego **agrupar**: `API_ERR x23/60s`.
* Siempre loggear el **cambio de estado** (de `ERR` a `OK`).

Pseudocódigo:

```cpp
struct Rate { uint32_t count; uint64_t windowStartUs; };
bool shouldLog(const char* codigo) {
  auto &r = rateMap[codigo];
  uint64_t now = esp_timer_get_time();
  if (now - r.windowStartUs > 60ULL*1000000ULL) { r.windowStartUs = now; r.count = 0; }
  if (r.count++ == 0) return true;    // primera del periodo
  return (r.count % 10) == 0;         // luego cada 10 (ejemplo)
}
```

---

## Interfaz `logEvento()` (contrato)

```cpp
void logEvento(
  const char* nivel,      // "INFO|DEBUG|WARN|ERROR"
  const char* codigo,     // p. ej., "API_ERR"
  const char* detalle,    // breve
  const char* contexto    // pares k=v separados por '&' (opcional)
);
```

**Requisitos:**

* Timestamp en µs **siempre** (validado).
* Thread‑safe respecto a ISR de caudal (no llamar desde ISR si escribe SD).
* Si SD no está disponible: **buffer en RAM** (cola) con límite y purga controlada.
* Flushear en hitos: cambio de día, `API_OK` tras ráfaga de errores, cierre ordenado.

---

## Tamaño y rendimiento

* Abrir el archivo una vez y **mantener handle** (`FILE*`/`File`) cuando sea posible.
* Usar `
  ` (LF) y evitar `
  ` si no necesario.
* Empaquetar strings con `printf`/`snprintf` para minimizar `String` dinámico.
* Considerar un **buffer de escritura** (p. ej., 256–512 bytes) y flush a intervalos.

---

## Análisis en PC/Grafana

* Parsers simples (CSV) o ingestión a Influx/SQLite para análisis post‑mortem.
* Campos clave: `codigo`, `nivel`, `dur_ms`, `http_code`, `archivo`, `reintentos`, `lote`, `mac`.

---

## Errores comunes (y cómo verlos en logs)

* **TS inválido (1970/0):** `TS_INVALID` en arranque; no deben existir `API_OK` hasta `TS_SYNC_OK`.
* **SD ocupada/fallo de init:** `SD_FAIL` seguido de `SD_RETRY` y luego `SD_OK` cuando vuelva.
* **API timeouts:** ráfagas `API_ERR` con `dur_ms` alto; luego `BACKUP_SAVE` por cada registro.

---

## Buenas prácticas

* Códigos de evento **inmutables**; el texto `detalle` puede variar.
* Contexto siempre como `k=v` (sin espacios), p. ej., `endpoint=/IoT/api.php&bytes=112`.
* Evitar logs dentro de ISR; usar banderas/colas.
* Documentar los cambios de formato en `CHANGELOG`.
