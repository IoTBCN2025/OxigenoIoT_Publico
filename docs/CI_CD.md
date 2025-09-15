# CI_CD.md – Flujo de CI/CD y Trazabilidad IoT

Este documento resume la estrategia de integración y despliegue continuo (CI/CD) para el firmware del sistema IoT y su trazabilidad completa mediante archivos estructurados.

---

## 🚦 Flujo general CI/CD

1. **Desarrollo local (PlatformIO + VSCode)**  
   - Código modular por archivo: `main.cpp`, `sdlog.cpp`, `api.cpp`, etc.
   - FSM, sensores y lógica por separado.

2. **Versionado con Git**  
   - Ramas: `main`, `dev`, `experimental`
   - Tags semánticos: `v1.3.0`, `v1.3.1`, etc.

3. **Mirror GitHub (sync)**  
   - Repositorio público: `IoTBCN2025/OxigenoIoT_Publico`
   - Sync automático de `tags` y `main` desde privado

4. **Trazabilidad estructurada (eventlog y backup)**  
   - Logs `.csv` diarios con nivel, módulo y código evento
   - Backups `.csv` por día, con estado de envío y timestamp de reenvío

5. **Validación continua (manual + logs)**  
   - Cada ejecución incluye `STARTUP_SUMMARY` y `MOD_UP`/`MOD_FAIL`
   - Validación de RTC, SD, NTP, WiFi en tiempo real

---

## 📁 Archivos generados

### 🔹 Event Log

Archivo: `/eventlog_YYYY.MM.DD.csv`

Formato:
```
ts_iso,ts_us,level,mod,code,fsm,kv
2025-09-09 17:19:30,1757431170000000,INFO,FSM,FSM_STATE,-,state=5
```

### 🔸 Backup de datos

Archivo: `/backup_YYYYMMDD.csv`

Formato:
```
timestamp,measurement,sensor,valor,source,status,ts_envio
1757431070000000,caudal,YF-S201,4.62,backup,PENDIENTE,
```

---

## 🧪 Logs relevantes CI/CD

- `MOD_UP`/`MOD_FAIL` por cada módulo (`WiFi`, `SD`, `RTC`, `NTP`, `API`)
- `STARTUP_SUMMARY` resume estado del arranque
- `API_OK`, `API_5XX`, `RESPALDO` y `REINTENTO_*` detallan ejecución

---

## ✅ Recomendaciones de versión

- Actualizar `FW_VERSION` y `FW_BUILD` en cada despliegue:
```cpp
#define FW_VERSION "1.3.2"
#define FW_BUILD __DATE__ " " __TIME__
```
- Marcar cambios en `CHANGELOG.md`
- Sincronizar `tag` y `release` con GitHub