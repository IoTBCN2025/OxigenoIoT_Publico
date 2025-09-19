# CI_CD.md – Flujo de CI/CD y Trazabilidad IoT

Este documento resume la estrategia de integración y despliegue continuo (CI/CD) para el firmware del sistema IoT Oxígeno, su trazabilidad estructurada en logs y control de versiones mediante GitHub Actions y PlatformIO.

---

## 🚦 Flujo general CI/CD

1. **Desarrollo local (PlatformIO + VSCode)**  
   - Proyecto estructurado con FSM y módulos desacoplados (`api.cpp`, `sdlog.cpp`, `main.cpp`, etc.)
   - Control de configuración centralizado en `config.{h,cpp}` y `secrets.h`
   - Pruebas locales con `pio run -e esp32dev`

2. **Versionado con Git**  
   - Ramas: `main`, `develop`, `feature/*`
   - Convención de tags: `vMAJOR.MINOR.PATCH` (ej. `v1.4.2`)
   - Releases automáticos con binarios adjuntos (`firmware.bin`, `.elf`, `.map`)

3. **Mirror GitHub (sync privado → público)**  
   - Repositorio privado (origen): `OxigenoIoT`
   - Repositorio público: `IoTBCN2025/OxigenoIoT_Publico`
   - Sync automático de `main`, `tags` y `releases` mediante workflow `sync-public.yml`

4. **Logs estructurados (eventlog y backup)**  
   - `eventlog_YYYY.MM.DD.csv`: eventos de sistema con nivel y trazabilidad (`MOD_UP`, `FSM_STATE`, etc.)
   - `backup_YYYYMMDD.csv`: respaldo de datos en caso de fallo WiFi/API

5. **Validación en tiempo real**  
   - Cada boot incluye `STARTUP_SUMMARY`, `MOD_UP`, `MOD_FAIL`
   - FSM gestiona fallback ante fallos RTC, SD, API, WiFi
   - Verificación continua de `timestamp`, latencia y estado de sensores

---

## ⚙️ Workflows GitHub Actions

### `.github/workflows/build.yml`

```yaml
name: Build Firmware

on:
  push:
    tags:
      - "v*"

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.11'
      - name: Install PlatformIO
        run: pip install platformio
      - name: Build firmware
        run: pio run -e esp32dev
      - name: Upload firmware
        uses: actions/upload-artifact@v3
        with:
          name: OxigenoIoT Firmware
          path: |
            .pio/build/esp32dev/firmware.bin
            .pio/build/esp32dev/firmware.elf
            .pio/build/esp32dev/firmware.map
```

### `.github/workflows/sync-public.yml`

Sincroniza `main` y `tags` hacia `IoTBCN2025/OxigenoIoT_Publico`.

---

## 📁 Archivos generados

### 🔹 Event Log

Archivo: `/eventlog_YYYY.MM.DD.csv`

Formato:
```csv
ts_iso,ts_us,level,mod,code,fsm,kv
2025-09-19 12:00:00,1757431200000000,INFO,FSM,FSM_STATE,IDLE,state=1
```

### 🔸 Backup de datos

Archivo: `/backup_YYYYMMDD.csv`

Formato:
```csv
timestamp,measurement,sensor,valor,source,status,ts_envio
1757431070000000,caudal,YF-S201,4.62,backup,PENDIENTE,
```

---

## 🧪 Logs relevantes CI/CD

- `MOD_UP`, `MOD_FAIL`: cada módulo (`WiFi`, `SD`, `RTC`, `NTP`, `API`, sensores)
- `STARTUP_SUMMARY`: conteo de módulos OK/fallidos y tiempo de arranque
- `API_OK`, `API_ERR`, `RESPALDO`, `REINTENTO_*`: resumen del flujo de datos

---

## ✅ Recomendaciones de versión

- Actualizar `FW_VERSION` y `FW_BUILD` en `main.cpp`:
```cpp
#define FW_VERSION "1.4.2"
#define FW_BUILD __DATE__ " " __TIME__
```

- Registrar cambios en `CHANGELOG.md`
- Añadir tag y release:
```bash
git tag -a v1.4.2 -m "Versión estable con sensores reales, trazabilidad completa y FSM robusta"
git push origin v1.4.2
```

---

## 📌 Buenas prácticas

- Incluir `secrets.h` en `.gitignore`
- Usar `platformio.ini` con plataforma fija (`espressif32@^6`)
- Validar logs y backups tras cada build en CI
- Adjuntar artefactos binarios al Release en GitHub

---

> Para cada versión estable, documentar cambios en `README.md`, `docs/*.md`, y generar zip con binarios y documentación para producción.