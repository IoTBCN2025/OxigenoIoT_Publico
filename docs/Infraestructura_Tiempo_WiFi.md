# Infraestructura_Tiempo_WiFi.md – Módulos NTP, RTC DS3231 y Conectividad WiFi

Este documento describe la lógica de funcionamiento de los tres módulos clave que aseguran la disponibilidad de tiempo confiable y conectividad para el sistema IoT Oxígeno.

---

## 🌐 Módulo `wifi_mgr.cpp`

### 🎯 Objetivo
Establecer conexión WiFi de manera robusta, reconectando automáticamente ante cortes y registrando eventos relevantes.

### 🔁 Funciones principales

- `wifiSetup(ssid, pass)`  
  Inicializa el modo estación (`WIFI_STA`), registra eventos y comienza conexión.

- `wifiReady()`  
  Devuelve `true` si WiFi está conectado y estable por al menos 2.5 s.

- `wifiLoop()`  
  Llamada no bloqueante que reintenta conexión cada 4 s si no hay WiFi.

### 📄 Eventos registrados (via `logEventoM`)

- `MOD_UP`: conexión establecida, incluye IP, MAC y RSSI.
- `MOD_FAIL`: pérdida de conexión, desconexión o sin IP.

---

## 🕒 Módulo `ntp.cpp`

### 🎯 Objetivo
Sincronizar el tiempo del sistema con un servidor NTP cuando hay WiFi disponible.

### 🔁 Funciones clave

- `sincronizarNTP(intentos, intervalo)`
  - Espera WiFi.
  - Llama a `configTime()` con parámetros de `config.ntp`.
  - Realiza varios intentos de `getLocalTime()` con espera entre ellos.
  - Registra `MOD_UP`, `NTP_ERR`, `MOD_FAIL`.

- `getTimestamp()`
  - Devuelve `time(nullptr)` en segundos.

---

## ⏱️ Módulo `ds3231_time.cpp`

### 🎯 Objetivo
Gestionar el RTC DS3231 como fuente principal de tiempo (usado para timestamp µs).

### 🔁 Funciones principales

- `initDS3231(sda, scl)`
  - Inicia Wire/I2C y el objeto `RTC_DS3231`.
  - Verifica si el reloj perdió energía (`rtc.lostPower()`).
  - Registra `MOD_UP` y si necesita sincronización.

- `rtcIsPresent()`  
  Retorna si el módulo RTC responde.

- `rtcIsTimeValid()`  
  Verifica plausibilidad del timestamp (año >= 2020).

- `setRTCFromUnix(segundos)`  
  Ajusta el RTC usando un valor UNIX válido (valida plausibilidad).

- `getUnixSeconds()`  
  Devuelve el tiempo en segundos desde RTC, o `0` si es inválido.

- `getTimestampMicros()`  
  Calcula el timestamp en microsegundos combinando:
  ```cpp
  (uint64_t)segundos * 1_000_000 + delta_us
  ```

- `keepRTCInSyncWithNTP(ntpOk, unixSeconds)`
  - Compara diferencia entre RTC y NTP.
  - Si difiere más de ±2 s, ajusta RTC.

### 🧪 Validación de plausibilidad

```cpp
bool plausibleUnix(uint32_t t) {
  return (t >= 1577836800UL) && (t < 4102444800UL);  // Año 2020 a 2100
}
```

---

## 📜 Eventos registrados

| Módulo | Código | Descripción |
|--------|--------|-------------|
| WIFI   | `MOD_UP`     | Conexión establecida con IP y RSSI |
| WIFI   | `MOD_FAIL`   | Pérdida de conexión |
| NTP    | `MOD_UP`     | Sincronización exitosa |
| NTP    | `NTP_ERR`    | Error al sincronizar |
| RTC    | `MOD_UP`     | RTC presente y funcionando |
| RTC    | `MOD_FAIL`   | RTC no detectado o tiempo inválido |
| RTC    | `RTC_TIME_INVALID` | RTC necesita sincronización |

---

## 💡 Recomendaciones

- Siempre validar `rtcIsTimeValid()` antes de confiar en el tiempo.
- Usar `getTimestampMicros()` para todas las mediciones de sensores y eventos.
- Priorizar RTC sobre NTP para operación offline.
- Mantener sincronización periódica con `NTP` si WiFi está disponible.
- Usar `wifiLoop()` en el `loop()` principal para mantener conectividad.

---

> La combinación de RTC + NTP + reconexión WiFi asegura que el sistema tenga una fuente de tiempo confiable incluso en entornos rurales con conectividad intermitente. Los logs estructurados permiten auditar esta sincronización.