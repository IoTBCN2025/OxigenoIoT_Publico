# FSM del Proyecto (ESP32‑WROOM‑32)

> Arquitectura de máquina de estados finitos para operación robusta en entorno rural con sensores agrícolas, RTC DS3231, SD, API HTTP (InfluxDB) y trazabilidad completa.

---

## Objetivos

* Orquestar **ventanas de medición** por sensor en el minuto (caudal continuo 0–29s; temperatura 35s; voltaje 40s).
* Garantizar **timestamp válido en µs** (DS3231 + NTP) antes de cualquier envío.
* Soportar **fallos de red/API** con **respaldo en SD** y **reenvío incremental**.
* Minimizar bloqueos con **timeouts cortos**, **watchdog**, y **rate‑limit de logs**.

---

## Estados

* `INICIALIZACION`: setup de WiFi, SD, I2C (DS3231), validación de timestamp y lectura de configuración.
* `LECTURA_CONTINUA_CAUDAL`: captura de pulsos YF‑S201 en ventana 0–29 s del minuto (acumulado/flujo).
* `LECTURA_TEMPERATURA`: lectura MAX6675 (seg 35).
* `LECTURA_VOLTAJE`: lectura ZMPT101B (seg 40).
* `REINTENTO_BACKUP`: reenvío **no bloqueante** en lotes pequeños (p. ej., 10 líneas) desde SD.
* `IDLE`: espera activa/pasiva fuera de ventanas.
* `ERROR_RECUPERABLE`: errores transitorios; se intenta recuperación y vuelta al estado anterior.
* *(Opcional futuro)* `SYNC_TIEMPO`: si TS inválido, intenta NTP → set RTC y valida.

---

## Eventos/Disparadores

* `WIFI_READY` / `WIFI_DOWN`
* `SD_OK` / `SD_FAIL`
* `TS_VALID` / `TS_INVALID` (>= 2020, ≠0, ≠943920000000000, etc.)
* `TICK_1S` (marca de tiempo por segundo)
* `API_OK` / `API_ERR` (incluye timeout)
* `BACKUP_OK` / `BACKUP_ERR`
* `WDT_PET` (feed watchdog)
* `WINDOW_OPEN` / `WINDOW_CLOSE` (apertura de ventana por sensor)

---

## Temporización por minuto

```
seg  0–29  : LECTURA_CONTINUA_CAUDAL (conteo pulsos, cálculo caudal/volumen)
seg  35    : LECTURA_TEMPERATURA (MAX6675)
seg  40    : LECTURA_VOLTAJE (ZMPT101B)
resto      : IDLE + REINTENTO_BACKUP (lotes pequeños)
```

**Notas:**

* Ventanas parametrizables (definir en `config.h` o `secrets.h`).
* Prioridad a datos en vivo; reenvíos se interrumpen si se abre una ventana de sensor.

---

## Tabla de transiciones (extracto)

| Estado actual             | Evento        | Condición                      | Acción principal                                       | Siguiente estado            |
| ------------------------- | ------------- | ------------------------------ | ------------------------------------------------------ | --------------------------- |
| INICIALIZACION            | TS\_VALID     | —                              | Log `BOOT`; abrir SD; preparar ventanas                | IDLE                        |
| INICIALIZACION            | TS\_INVALID   | —                              | Log `TS_INVALID`; intentar `SYNC_TIEMPO`               | SYNC\_TIEMPO (opcional)     |
| IDLE                      | TICK\_1S      | segundo ∈ \[0..29]             | Abrir ventana caudal; habilitar ISR pulsos (si aplica) | LECTURA\_CONTINUA\_CAUDAL   |
| LECTURA\_CONTINUA\_CAUDAL | WINDOW\_CLOSE | segundo == 30                  | Cerrar ventana; calcular caudal; `sendOrBackup()`      | IDLE                        |
| IDLE                      | TICK\_1S      | segundo == 35                  | Leer MAX6675; `sendOrBackup()`                         | LECTURA\_TEMPERATURA → IDLE |
| IDLE                      | TICK\_1S      | segundo == 40                  | Leer ZMPT101B; `sendOrBackup()`                        | LECTURA\_VOLTAJE → IDLE     |
| IDLE                      | TICK\_1S      | ventana libre y hay pendientes | `reenviarDatosDesdeBackup(lote=10)`                    | REINTENTO\_BACKUP → IDLE    |
| Cualquiera                | WIFI\_DOWN    | —                              | Cancelar envíos; habilitar sólo backup SD              | (permanece/IDLE)            |
| Cualquiera                | API\_ERR      | timeout o 5xx                  | Guardar en SD; rate‑limit `API_ERR`                    | (permanece)                 |
| Cualquiera                | SD\_FAIL      | —                              | Cerrar WiFi; reintento SD escalonado; log `SD_FAIL`    | ERROR\_RECUPERABLE/IDLE     |

---

## Pseudocódigo del loop (simplificado)

```cpp
void loop() {
  feedWatchdog();
  wifiLoop();               // gestiona reconexiones/backoff
  tick = nowSecond();       // del RTC/NTP validado

  switch (estado) {
    case INICIALIZACION: initAll(); break;

    case LECTURA_CONTINUA_CAUDAL:
      if (tick == 30) { cerrarVentanaCaudal(); sendOrBackup("caudal"); estado = IDLE; }
      break;

    case IDLE:
      if (secIn(0,29)) { abrirVentanaCaudal(); estado = LECTURA_CONTINUA_CAUDAL; break; }
      if (tick == 35)  { leerYEnviar("temp"); }
      if (tick == 40)  { leerYEnviar("volt"); }
      if (ventanaLibre() && hayBackupsPendientes()) {
        estado = REINTENTO_BACKUP;  // se sale rápido (no bloqueante)
      }
      break;

    case REINTENTO_BACKUP:
      reenviarDatosDesdeBackup(/*lote=*/10);
      estado = IDLE;  // nunca bloquear
      break;

    case ERROR_RECUPERABLE:
      intentarRecuperacion();
      estado = estadoAnterior;  // o IDLE
      break;
  }
}
```

---

## Políticas críticas

* **No enviar** si `timestamp` inválido (0, 1970, etc.).
* **Cerrar WiFiClient** antes de operar con SD; si SD falla, **reset SPI** y reintentar en frío.
* **Safe‑write** al actualizar líneas `PENDIENTE→OK` (archivo temporal + `rename()` o `seek()` controlado).
* **Lotes pequeños** en `REINTENTO_BACKUP` (p. ej., 10) para no afectar latencia de datos live.
* **Rate‑limit** de logs por código (`API_ERR`, `RETRY_SD`, etc.).

---

## Métricas/KPIs sugeridos

* Latencia de confirmación: `ts_envio - ts` (µs).
* Porcentaje de envíos directos vs. reenvíos.
* Tiempo en `WIFI_DOWN` y número de reconexiones.
* Pendientes en SD por archivo/día.
* Uso de flash/RAM y tiempo de ciclo.

---

## Extensiones futuras

* `lte_mgr` (fallback 4G, p. ej., Huawei E8372) con health‑check.
* OTA seguro (HTTPs) y particiones duales.
* Reglas IA/n8n para detección de anomalías (deriva de caudal, sobre‑temperatura, picos de voltaje).

---

## Checklist de pruebas

* [ ] Arranque con DS3231 sin hora → bloquea envíos, intenta sync.
* [ ] `WIFI_DOWN` prolongado → todo va a SD; al volver red, reenvía en lotes.
* [ ] `backup_19700101.csv` se ignora salvo pendientes; no se elimina.
* [ ] Rollover de `micros()` no afecta timestamp (usar `esp_timer_get_time()`).
* [ ] Rate‑limit de `API_ERR` y `RETRY_SD` reduce ruido.
* [ ] Safe‑write: no hay corrupción si se corta energía durante reenvío.
