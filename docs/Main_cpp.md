# 📘 Análisis Detallado - main.cpp FSM IoT (ESP32 + DS3231 + SD + WiFi)

**Fecha de generación:** 2025-09-15 09:39

------------------------------------------------------------------------

## 1. Arquitectura FSM

-   Estados definidos: `IDLE`, `LECTURA_CONTINUA_CAUDAL`,
    `LECTURA_TEMPERATURA`, `LECTURA_VOLTAJE`, `REINTENTO_BACKUP`,
    `ERROR_RECUPERABLE`.
-   Estados bien segmentados por sensor.
-   Transiciones claras y manejables.
-   Permite crecimiento escalable.

------------------------------------------------------------------------

## 2. Inicialización (`setup`)

-   Registro de estado de cada módulo (WiFi, SD, RTC, Sensores).
-   Log resumen `'STARTUP_SUMMARY'`.
-   Usa contadores `g_upCount / g_failCount` para trazabilidad.

------------------------------------------------------------------------

## 3. Conectividad WiFi + NTP

-   Watchdog WiFi no bloqueante.
-   Flanco de subida WiFi activa sincronización y reintentos.
-   Backoff para NTP si el RTC es inválido.

------------------------------------------------------------------------

## 4. Manejo del Tiempo

-   RTC DS3231 con validación.
-   Fallback con NTP y `millis()`.
-   Protección contra timestamps inválidos.

------------------------------------------------------------------------

## 5. Lectura de Sensores

-   Caudalímetro continuo entre segundos 0--29.
-   Temperatura y voltaje leídos una vez por minuto.
-   Envío API o respaldo en SD con timestamp.

------------------------------------------------------------------------

## 6. Backup en SD y Reintento

-   Archivos CSV con índice `.idx` para control de lectura.
-   Detección de pendientes eficiente.
-   Reintento cada 30s con logs de control.

------------------------------------------------------------------------

## 7. Logging Trazable

-   Uso de `logEventoM(mod, code, kv)` en todos los módulos.
-   Eventos categorizados y con timestamps.
-   Diferencia INFO, WARN, ERROR, DEBUG.

------------------------------------------------------------------------

## 8. Robustez General

-   FSM bien estructurada, no bloqueante.
-   Permite fallback sin RTC válido.
-   Sistema resiliente y preparado para campo.

------------------------------------------------------------------------

## 9. Recomendaciones Futuras

-   Añadir modo sleep controlado.
-   WebUI embebida para control local.
-   Configuración externa vía JSON en SD.
-   Protocolo MQTT alternativo.
-   OTA para actualización remota.
