# Explicación del archivo `main.cpp` - FSM para ESP32-WROOM-32

Este documento describe la lógica y estructura del archivo `main.cpp` implementado para un sistema IoT basado en ESP32 con arquitectura de Máquina de Estados Finitos (FSM), orientado a la lectura de sensores, registro en SD y envío de datos a una API HTTP intermedia que reenvía a InfluxDB.

## 1. Objetivo general

Realizar:

* Lectura cíclica de sensores (caudalímetro, termocupla y voltaje AC)
* Enviar los datos a una API HTTP
* Guardar respaldo en tarjeta SD si falla el envío
* Reintentar el reenvío de datos desde SD
* Registrar eventos en logs

## 2. Componentes incluidos

* `api.h`: gestiona el envío HTTP a la API.
* `ntp.h`: sincroniza hora con NTP.
* `sdlog.h`: guarda eventos en logs.
* `sdbackup.h`: guarda datos en CSV de respaldo.
* `reenviarBackupSD.h`: reenvía datos pendientes desde SD.
* `sensores_*.h/cpp`: módulos individuales para cada sensor.

## 3. Estructura FSM (`enum Estado`)

```cpp
enum Estado {
  INICIALIZACION,
  LECTURA_SENSORES,
  ENVIO_DATOS,
  BACKUP_DATOS,
  REINTENTO_BACKUP,
  IDLE,
  ERROR_RECUPERABLE
};
```

## 4. Ciclo principal (`loop()`)

* **LECTURA\_SENSORES**:

  * Se verifica si pasaron 10 segundos desde la última lectura
  * Se actualizan y almacenan los valores de sensores
  * Se cambia a `ENVIO_DATOS`

* **ENVIO\_DATOS**:

  * Envía cada sensor a la API
  * Si todos son exitosos → `REINTENTO_BACKUP`, si falla alguno → `BACKUP_DATOS`

* **BACKUP\_DATOS**:

  * Si SD disponible, guarda datos en CSV
  * Cambia a `REINTENTO_BACKUP`

* **REINTENTO\_BACKUP**:

  * Llama a `reenviarDatosDesdeBackup()` si SD disponible
  * Luego pasa a `IDLE`

* **IDLE**:

  * Espera breve
  * Si pasaron 10 s desde la última lectura, vuelve a `LECTURA_SENSORES`

* **ERROR\_RECUPERABLE**:

  * Intenta reinicializar la SD cada segundo

## 5. Funciones externas

Estas funciones están implementadas en sus respectivos módulos:

```cpp
void actualizarCaudal();        // sensores_CAUDALIMETRO_YF-S201.cpp
void actualizarTermocupla();    // sensores_TERMOCUPLA_MAX6675.cpp
void actualizarVoltaje();       // sensores_VOLTAJE_ZMPT101B.cpp
```

## 6. Tiempos y sincronización

* El `timestamp` se calcula en microsegundos (`getTimestamp() * 1_000_000ULL`) para compatibilidad con InfluxDB
* Se utiliza NTP para inicializar el reloj

## 7. SD y fallos

* Se inicia con `SD.begin(5)`
* Se guarda el estado en `sdDisponible`
* Si la SD falla, entra en estado `ERROR_RECUPERABLE` y reintenta

## 8. Logs y eventos

* Todos los eventos críticos se registran mediante `logEvento(...)`
* Se informan éxitos y errores de envío y backup

---

Este sistema permite una operación resiliente en entornos rurales o con conectividad intermitente, garantizando trazabilidad completa de datos y errores.
