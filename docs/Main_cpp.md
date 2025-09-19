# 📘 Análisis Detallado - main.cpp FSM IoT (ESP32 + DS3231 + SD + WiFi + API)

**Fecha de generación:** 2025-09-19

------------------------------------------------------------------------

## 1. Arquitectura FSM

-   Estados definidos: `IDLE`, `LECTURA_CONTINUA_CAUDAL`, `LECTURA_TEMPERATURA`, `LECTURA_VOLTAJE`, `REINTENTO_BACKUP`, `ERROR_RECUPERABLE`.
-   Transiciones claras entre estados, orientadas a eficiencia y control de tiempo.
-   FSM diseñada para funcionar correctamente incluso sin RTC válido.
-   Soporte para sensores con ventanas temporales específicas por segundo del minuto.
-   Ideal para sistemas autónomos en zonas rurales.

------------------------------------------------------------------------

## 2. Inicialización (`setup`)

-   Inicializa RTC (DS3231), tarjeta SD, módulo WiFi, sensores y HSPI.
-   Verifica presencia y validez del RTC antes de permitir operación.
-   Sincroniza reloj con NTP si es posible.
-   Cada módulo genera logs estructurados con `MOD_UP` o `MOD_FAIL`.
-   Registro resumen `'STARTUP_SUMMARY'` con contadores de éxito y fallos (`g_upCount`, `g_failCount`).

------------------------------------------------------------------------

## 3. Conectividad WiFi + NTP

-   Loop WiFi no bloqueante con detección de reconexión.
-   Sincronización automática por NTP al detectar reconexión.
-   Retry automático si el RTC es inválido (cada 10s).
-   Resincronización periódica cada 6h si hay conectividad.

------------------------------------------------------------------------

## 4. Manejo del Tiempo

-   Uso de `getTimestampMicros()` (RTC o NTP) como fuente principal.
-   Validación de timestamps con valores inválidos (`0`, `943920000000000`).
-   Fallback con `millis()` para mantener trazabilidad incluso sin hora válida.
-   Registro de origen del tiempo en logs.

------------------------------------------------------------------------

## 5. Lectura de Sensores

-   **YF-S201 (caudalímetro)**: lectura continua entre segundos 0–29, usando interrupciones.
-   **MAX6675 (termocupla)**: lectura en el segundo 35 mediante HSPI.
-   **ZMPT101B (voltaje AC)**: lectura en el segundo 40 por ADC.
-   Los valores son enviados a la API o respaldados en SD con timestamp.
-   Envíos con validación y fallback.

------------------------------------------------------------------------

## 6. Backup en SD y Reintento

-   Datos no enviados se almacenan en archivos `backup_YYYYMMDD.csv`.
-   Archivos `.idx` indican progreso de reintento.
-   Estado `REINTENTO_BACKUP` reenvía datos cada 30 s.
-   Registros enviados son marcados con `status=ENVIADO` y `ts_envio`.

------------------------------------------------------------------------

## 7. Logging Estructurado

-   Uso de `logEventoM(mod, code, kv)` en todos los módulos.
-   Formato CSV: `ts_iso,ts_us,level,mod,code,fsm,kv`
-   Soporte para niveles: INFO, WARN, ERROR, DEBUG.
-   Logs coalescentes para evitar repetición innecesaria.

------------------------------------------------------------------------

## 8. Robustez y Resiliencia

-   Sistema soporta operación con RTC inválido.
-   Reinicio de SD al fallar (`ERROR_RECUPERABLE`) y reintento automático.
-   Protege contra fallos de red o API.
-   Logging persistente y trazabilidad completa de eventos críticos.

------------------------------------------------------------------------

## 9. Recomendaciones Futuras

-   Añadir modo `sleep` profundo con RTC wakeup.
-   UI Web embebida para ajustes y diagnóstico.
-   Archivo `config.json` en SD para personalización remota.
-   Integración con MQTT o LoRa como alternativa a WiFi.
-   Soporte OTA con validación de versión y logs de actualización.