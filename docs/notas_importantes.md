# Notas Importantes
- Fecha: 18/07/2025
- Autor: Jose Monje Ruiz
- Descripción: Este archivo contiene observaciones críticas sobre el proyecto y parte del funcionamiento clave para entender la logica del desarollo



📌 Estado Actual
El sistema ya se encuentra en un nivel operacional completo, con:
Respaldo local en SD ante fallos.
Timestamp confiable.
Comunicación vía HTTP hacia una API intermediaria con InfluxDB.
Reenvíos automáticos controlados.
Trazabilidad mediante logs.
Modo simulación para pruebas de sensores.


🔧 1. Arquitectura General: FSM modular
Tu enfoque modular desacopla claramente responsabilidades:
Módulo                  Responsabilidad principal
main.cpp	            FSM, ciclo principal, reintento desde backup
api.cpp	                Envío HTTP GET a la API PHP
ntp.cpp	                Sincronización horaria NTP + timestamp
sdlog.cpp	            Registro de eventos del sistema
sdbackup.cpp	        Respaldo de datos fallidos (modo PENDIENTE)
reenviarBackupSD.cpp	Reintentos desde archivos CSV
sensores_*.cpp	        Lectura y simulación de sensores (caudal, temperatura, voltaje)


✅ 2. Fortalezas del diseño actual
🕒 Sincronización precisa
Uso correcto de NTP vía configTime().
Validación de timestamp para evitar registros incorrectos (e.g. 0 o 943920000000000).
Uso de getTimestamp() para obtener timestamp en segundos y convertirlo a microsegundos.


📤 Envío a API HTTP
Llamada GET bien estructurada con:
measurement, sensor, valor, ts, source, api_key.
Timeout corto (5 segundos) para evitar bloqueos.
Manejo correcto de respuesta (200 + OK).
Manejo de error con timeout (5s) y logEvento() ante fallos.


💾 Manejo de SD sólido
Se generan logs diarios en formato CSV legible y ordenado.
Si no se puede enviar un dato, se guarda con status=PENDIENTE.
Formato backup: timestamp,measurement,sensor,valor,source,status,ts_envio.


💾 Backup automático en SD
Si el envío falla, los datos se guardan en:
/backup_YYYYMMDD.csv
con el formato:
timestamp,measurement,sensor,valor,source,status,ts_envio
status=PENDIENTE hasta que se reenvíen correctamente.
ts_envio permite medir la latencia entre recolección y reenvío.


🔄 Reenvío desde SD robusto
Lee todos los archivos backup_*.csv.
Reintenta hasta 5 veces por archivo y máximo 3 registros por ciclo.
Marca OK con timestamp de envío (ts_envio) si fue exitoso.
Actualiza el mismo archivo CSV con cambios (no borra, mantiene trazabilidad).
Si tiene éxito, actualiza el archivo con status=OK y nuevo ts_envio.


📈 Simulación de sensores
Implementación efectiva de modo simulación (#define SIMULACION_X) que facilita pruebas sin hardware físico.
Activada por #define SIMULACION_X.
Permite probar sin hardware físico real.
Sensores actuales:
Caudal: YF-S201
Temperatura: MAX6675
Voltaje: ZMPT101B


🧠 3. Buenas prácticas aplicadas
Modularidad clara y mantenible.
Uso de vector<DatoBackup> para flexibilidad.
Logs detallados en tarjeta SD.
Separación lógica por archivo.
Separación entre evento (log) y dato (backup).
Validación de timestamp y conectividad.
Uso de vector para manejar backups de manera flexible.
Manejo robusto de errores (logEvento() en cada punto crítico).
Formato de logs legible para auditoría en SD y monitor serial.


✅ Funcionalidades Implementadas
🕒 Sincronización NTP
Se conecta a pool.ntp.org con configTime().
Genera timestamp en microsegundos (getTimestamp() * 1000000ULL).
Valida que el tiempo no sea inválido (0 o 943920000000000).