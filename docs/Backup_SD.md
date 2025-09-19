# Backup_SD.md – Lógica de respaldo y reintento desde tarjeta SD

Este documento describe cómo el sistema IoT maneja el **respaldo local en SD** cuando no hay conectividad (WiFi/API) y cómo realiza **reintentos automáticos** para reenviar los datos pendientes.

---

## 📦 Módulo `sdbackup.cpp`

### 🎯 Objetivo
Guardar localmente cada medición cuando:
- El envío a la API falla
- No hay conexión WiFi
- El timestamp es inválido

### 🧪 Validación de timestamp
```cpp
if (timestamp == 0ULL || timestamp == 943920000000000ULL) {
    // Usar millis() como fallback y marcar como UNSYNC
}
```

### 📝 Formato del archivo backup
```
/backup_YYYYMMDD.csv
```
Cabecera CSV:
```csv
timestamp,measurement,sensor,valor,source,status,ts_envio
```

Ejemplo de línea:
```csv
1757431090033513,caudal,YF-S201,6.85,backup,PENDIENTE,
```

### 🧠 Lógica clave
- Si no existe el archivo del día, se crea con cabecera.
- Cada línea representa un dato no enviado con `status=PENDIENTE`.
- Se registran logs de éxito (`BACKUP_OK`) o error (`SD_ERR`).

---

## 🔁 Módulo `reenviarBackupSD.cpp`

### 🎯 Objetivo
Reenviar automáticamente los datos `PENDIENTE` desde SD cuando el sistema detecta WiFi operativo.

### 📂 Estructura de archivos
- `backup_YYYYMMDD.csv`: archivo principal con datos.
- `backup_YYYYMMDD.csv.idx`: offset que indica por dónde continuar.
- `/sent/backup_YYYYMMDD.csv`: archivo de auditoría con datos reenviados (`status=ENVIADO`, `ts_envio`)
- `/sent/raw/backup_YYYYMMDD.csv`: archivo original movido al finalizar procesamiento.

### 🔐 Formato reenviado
```csv
timestamp,measurement,sensor,valor,source,status,ts_envio
1757431090033513,caudal,YF-S201,6.85,backup,ENVIADO,1757431124019235
```

### 🔄 Lógica principal
- Recorre archivos `backup_*.csv` (excepto `1970`).
- Carga desde `.idx` para evitar reprocesar datos.
- Reintenta hasta `MAX_REENVIOS_POR_LLAMADA` (ej. 6).
- En caso de éxito, añade línea a `/sent/backup_YYYYMMDD.csv`.
- Al finalizar un archivo, se mueve a `/sent/raw/`.

---

## 📌 Constantes configurables

```cpp
#define MAX_REENVIOS_POR_LLAMADA 6
#define RETRY_EVERY_MS 500
#define SCAN_BACKUPS_EVERY_MS 1000
```

---

## 📜 Logs estructurados generados

| Código         | Significado |
|----------------|-------------|
| `MOD_UP`       | SD operativa para backups |
| `MOD_FAIL`     | Falla al abrir/crear archivo |
| `BACKUP_OK`    | Registro escrito correctamente |
| `REINTENTO_OK` | Envío exitoso desde backup |
| `REINTENTO_ERR`| Falla al acceder al backup |
| `REINTENTO_SKIP_WIFI` | Sin conexión WiFi |
| `REINTENTO_EOF`| Fin de archivo alcanzado |
| `BACKUP_ARCHIVE`| Archivo movido a `/sent/raw/` |
| `BACKUP_WARN`  | Error al archivar archivo |

---

## 🧼 Buenas prácticas

- Nunca eliminar `backup_19700101.csv`
- Ignorar archivos con `TS inválido`, pero conservar para análisis forense
- Procesar los backups en lotes pequeños
- Usar `esp_timer_get_time()` para timestamp de `ts_envio`
- Evitar bloqueos: si WiFi cae, detener reintentos
- Registrar en log cada paso clave del reintento

---

## 🧠 Resumen

| Componente | Función |
|------------|---------|
| `sdbackup.cpp` | Guarda datos no enviados con `status=PENDIENTE` |
| `reenviarBackupSD.cpp` | Lee, reenvía y audita datos con `status=ENVIADO` |
| `*.idx` | Controla offset por archivo |
| `/sent/` | Almacena históricos reenviados con trazabilidad |

---

> Toda la lógica de respaldo y reintento está diseñada para mantener trazabilidad completa incluso en ambientes sin red. Ideal para zonas rurales y escenarios con caídas de energía o conectividad.