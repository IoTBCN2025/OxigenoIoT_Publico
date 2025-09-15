# FSM.md – Arquitectura FSM (Finite State Machine) para Sistema IoT

Este documento describe la estructura actualizada de la máquina de estados finitos (FSM) del sistema IoT basado en ESP32, con RTC DS3231, respaldo en SD, reintento inteligente y trazabilidad estructurada mediante `logEventoM()`.

## 🧠 Principios clave

- **Modularidad**: cada módulo (WiFi, NTP, API, SD, sensores) gestiona sus propios estados y reportes.
- **Resiliencia**: el sistema funciona con o sin RTC válido, y se recupera de desconexiones de red o SD.
- **Backup inteligente**: los datos se almacenan y reenvían desde la SD con trazabilidad total.
- **Trazabilidad**: todos los eventos relevantes quedan registrados con nivel, módulo y detalles clave.

---

## 🎯 Estados FSM principales (`main.cpp`)

| Estado | Descripción |
|--------|-------------|
| `INICIALIZACION` | Configura WiFi, RTC, SD y sensores. |
| `IDLE` | Espera activa verificando el segundo del minuto. |
| `LECTURA_CONTINUA_CAUDAL` | Mide caudal cada 1 s durante 30 s. |
| `LECTURA_TEMPERATURA` | Mide una vez por minuto (seg 35). |
| `LECTURA_VOLTAJE` | Mide una vez por minuto (seg 40). |
| `REINTENTO_BACKUP` | Reenvía datos pendientes desde SD. |
| `ERROR_RECUPERABLE` | Reintenta recuperación SD. |

---

## 📋 Registro de eventos FSM

Cada transición de estado se registra mediante:

```cpp
char kv[24];
snprintf(kv, sizeof(kv), "state=%d", (int)estadoActual);
logEventoM("FSM", "FSM_STATE", kv);
```

Nivel: `INFO`  
Módulo: `FSM`  
Evento: `FSM_STATE`  
Clave/valor: `state=ID`

---

## 🔁 Resumen de arranque (`STARTUP_SUMMARY`)

Muestra cuántos módulos fueron inicializados correctamente:

```cpp
logEventoM("SYS", "STARTUP_SUMMARY", "up=5;fail=1;elapsed_ms=472");
```

Nivel: `INFO`  
Módulo: `SYS`

---

## ⚠️ Fallos y recuperación

| Escenario | Manejo |
|----------|--------|
| WiFi caído | Backup en SD, reintento automático |
| RTC inválido | Fallback a `millis()`, reintento NTP |
| SD no disponible | Reintento periódico, backup suspendido |
| API falla | Registro en SD, log `RESPALDO` + `API_5XX` |

---

## 📎 Estados FSM con logs destacados

- `REINTENTO_BACKUP`:  
  - `REINTENTO_INFO`, `REINTENTO_OK`, `REINTENTO_SUMMARY`
- `LECTURA_CONTINUA_CAUDAL`:  
  - `API_OK`, `RESPALDO`, `TS_INVALID_BACKUP`
- `INICIALIZACION`:  
  - `MOD_UP`, `MOD_FAIL`, `RTC_OK`, `RTC_ERR`, `WIFI_WAIT`, `BOOT_INFO`