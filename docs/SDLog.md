# SDLog.md – Sistema de Logging Estructurado en Tarjeta SD

Este documento explica el diseño e implementación del sistema de logging del firmware IoT Oxígeno, centralizado en el módulo `sdlog.cpp`.

---

## 🎯 Objetivo

Registrar todos los eventos importantes del sistema con:
- Precisión temporal (timestamp en µs y formato ISO)
- Niveles de severidad (`INFO`, `WARN`, `ERROR`, `DEBUG`)
- Identificación del módulo y código del evento
- Detalles clave (`kv`)

---

## 🗂️ Estructura del archivo de log

**Ubicación:**  
```
/eventlog_YYYY.MM.DD.csv
```

**Cabecera CSV:**
```csv
ts_iso,ts_us,level,mod,code,fsm,kv
```

**Ejemplo:**
```csv
2025-09-19 12:00:00,1757431200000000,INFO,NTP,MOD_UP,-,phase=wifi_up
```

---

## 🧠 Lógica de funcionamiento

### 🔁 Buffer circular (RAM)
- Buffer de 16 líneas en RAM (`Q_SIZE = 16`)
- Cada línea máximo 240 caracteres
- Flush automático tras `logEventoM()` si la SD está lista

### 📅 Rotación diaria
- Detecta cambios de fecha con `current_ymd()`
- Cambia archivo automáticamente

### 🧪 Timestamp robusto
- Usa `getTimestampMicros()` (RTC/NTP)
- Fallback a `millis()` si no hay RTC válido

### 🔒 Protección contra spam (rate limit)
- Cada evento con `mod|code` se limita a 1 cada 2000 ms
- Eventos repetidos incrementan `count` y se agregan en el `kv`

---

## 🧾 Llamada principal: `logEventoM()`

```cpp
logEventoM("API", "API_ERR", "code=500;dur=248")
```

Genera:
```csv
2025-09-19 12:01:00,1757431210000000,ERROR,API,API_ERR,-,code=500;dur=248
```

---

## 🧼 Sanitización y seguridad

- El campo `kv` se limpia:
  - Reemplaza `,` por `.`
  - Elimina saltos de línea
- Longitud máxima: 120 caracteres útiles por línea

---

## 🧪 Niveles de log automáticos

El nivel se infiere a partir del `code`:

| Código contiene | Nivel |
|------------------|--------|
| `ERROR`, `FAIL`, `ERR` | `ERROR` |
| `WARN`, `RESPALDO`, `TS_INVALID_BACKUP` | `WARN` |
| `DEBUG` | `DEBUG` |
| Otro | `INFO` |

---

## 🔁 Reintento de logs perdidos

Función: `reintentarLogsPendientes()`
- Reintenta iniciar SD si no estaba disponible
- Vuelve a guardar todo el buffer en archivo actual

---

## 📌 Buenas prácticas

- Usar `logEventoM()` en cada módulo al iniciar (`MOD_UP`/`MOD_FAIL`)
- Loguear transiciones FSM (`FSM_STATE`)
- No abusar de `String` dentro del logger, usar `snprintf` cuando sea posible
- Evitar logs duplicados: el sistema ya limita repetición

---

## 🧠 Resumen técnico

| Componente | Descripción |
|------------|-------------|
| `logEventoM(mod, code, kv)` | Registra un evento |
| `flush_queue()` | Escribe todos los logs pendientes |
| `rotate_if_changed()` | Cambia de archivo si cambia el día |
| `ensure_header()` | Asegura cabecera CSV si el archivo es nuevo |
| `sanitize_kv()` | Limpia caracteres peligrosos |

---

> El sistema de logging garantiza trazabilidad completa, soporta apagones, y permite depurar el sistema en producción rural. Ideal para auditar errores o analizar el comportamiento a lo largo del tiempo.