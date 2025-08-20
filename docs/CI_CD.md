# CI/CD del Proyecto (PlatformIO + GitHub Actions)

> Pipeline de integración continua para compilar firmware ESP32, publicar artefactos por commit y generar **Release** automático al taggear `v*`.

---

## Objetivos

* Builds reproducibles de **PlatformIO** (ESP32‑WROOM‑32).
* Artefactos (`firmware.bin`, `firmware.elf`, `firmware.map`) por cada push/PR.
* **Release on Tag** (`vMAJOR.MINOR.PATCH`) con binarios adjuntos.
* Aceleración por **caché**.

---

## Workflow principal

**Ruta:** `.github/workflows/build.yml`

**Disparadores:**

* `push` a `main` y `develop`.
* `pull_request` contra `main` y `develop`.
* `push` de **tags** `v*` → dispara job `release`.
* `workflow_dispatch` (manual).

**Jobs:**

1. **build**

   * Instala Python + PlatformIO.
   * Restaura caché de PlatformIO y PIP.
   * Compila `pio run -e esp32dev` (matriz expandible).
   * Genera artefactos en `dist/<env>/` y los sube.
2. **release** (condicional a tag `v*`)

   * Descarga artefactos del job **build**.
   * Crea/actualiza **Release** con archivos adjuntos.

---

## Matriz de entornos

Amplía según `platformio.ini`:

```yaml
strategy:
  matrix:
    env: [ esp32dev ]  # agrega esp32s3, etc.
```

---

## Caché

Acelera ejecución y reduce descargas:

```yaml
- uses: actions/cache@v4
  with:
    path: |
      ~/.platformio/.cache
      ~/.cache/pip
    key: ${{ runner.os }}-pio-${{ hashFiles('**/platformio.ini') }}
    restore-keys: |
      ${{ runner.os }}-pio-
```

---

## Artefactos

Se suben por cada job **build**:

```
dist/<env>/firmware-<env>-<sha>.bin
(Opt) dist/<env>/firmware-<env>.elf
(Opt) dist/<env>/firmware-<env>.map
```

*Consejo:* conservar `.elf` y `.map` en Releases para depuración de campo.

---

## Release automático

* Se ejecuta al pushear un tag `v*` (p. ej., `v1.3.0`).
* Usa `softprops/action-gh-release` con `generate_release_notes: true`.
* Adjunta los artefactos descargados del job **build**.

### Versionado (SemVer)

* `MAJOR`: cambios incompatibles.
* `MINOR`: nuevas funciones retro‑compatibles.
* `PATCH`: correcciones.

---

## Requisitos del repo

* **Acciones habilitadas** en la configuración del repositorio.
* `platformio.ini` con plataforma fijada para reproducibilidad (ej.: `espressif32@^6`).
* **No** subir secretos al repo: SSID, tokens, etc. Usar `secrets.h` en `.gitignore`.

---

## Cómo correr localmente

```bash
pip install -U platformio
pio run -e esp32dev
pio run -e esp32dev -t upload     # si tienes el board conectado
pio run -e esp32dev -t monitor    # monitor serie
```

---

## Depuración de fallos del pipeline

* Revisar la sección **Collect firmware artifact**: verifica ruta `.pio/build/<env>/`.
* Si falta `firmware.bin`, confirmar que el entorno `env` coincide con `platformio.ini`.
* Si la caché queda corrupta, **limpia caché** en Settings → Actions → Caches.
* Añade paso `pio system info` para diagnosticar.

---

## Mejores prácticas

* **Badges** en `README.md`:

  ```md
  ![CI](https://github.com/<org>/<repo>/actions/workflows/build.yml/badge.svg)
  ```
* **CHANGELOG.md** para cambios por versión.
* `pio check` (Cppcheck) como **job opcional** hasta estabilizar baseline:

  ```yaml
  - name: Static check (pio check)
    run: pio check -e esp32dev || true
  ```
* **Artefactos por PR**: permite a revisores probar el binario sin compilar.

---

## Roadmap CI/CD

* Separar workflows: PR‑only (lint/build) y Release‑only (tag).
* Subir símbolos de depuración y mapa de memoria a almacenamiento externo.
* Integrar **firmware OTA** firmado en Release.
* Tests de humo con **Hardware‑in‑the‑Loop** (HIL) a futuro.
