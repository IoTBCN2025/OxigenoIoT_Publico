# Conexión Física – ESP32‑WROOM‑32 y Módulos del Proyecto (v1.0.0)

Guía práctica de cableado para el firmware **Oxígeno IoT** usando ESP32‑WROOM‑32. Incluye mapeo de pines, tensiones, notas de protección y recomendaciones de montaje para operación estable en campo.

---

## 🧩 Resumen de módulos

- **RTC DS3231** (I²C)
- **SD Card** (SPI – VSPI)
- **Termocupla MAX6675** (HSPI dedicado)
- **Caudalímetro YF‑S201** (salida de pulsos digitales)
- **ZMPT101B** (medición de voltaje AC – salida analógica)
- **WiFi** (integrado en ESP32)

> Todos los módulos funcionan a **3.3 V** (salvo ZMPT101B que se alimenta a 5 V/3.3 V según placa; la **salida analógica** es compatible 0–3.3 V). Compartir **GND común**.

---

## 🗺️ Mapa de pines (según `config.cpp`)

| Subsistema | Señal | ESP32 | Módulo | Notas |
|---|---|---|---|---|
| **I²C (RTC DS3231)** | SDA | GPIO **21** | DS3231 SDA | Pull‑up 4.7 kΩ a 3.3 V (muchas placas ya lo traen) |
|  | SCL | GPIO **22** | DS3231 SCL | Cable corto, trenzado GND si >20 cm |
| **SD (VSPI)** | CS | GPIO **5** | SD CS | Usar lector SD con conversión 3.3 V |
|  | SCK | GPIO **18** | SD SCK | |
|  | MISO | GPIO **19** | SD MISO | |
|  | MOSI | GPIO **23** | SD MOSI | |
| **MAX6675 (HSPI dedicado)** | CS | GPIO **15** | MAX6675 CS | Bus HSPI exclusivo para evitar colisiones SPI |
|  | SCK | GPIO **14** | MAX6675 SCK | |
|  | MISO | GPIO **12** | MAX6675 SO | **No hay MOSI** (solo lectura) |
| **YF‑S201 (Caudal)** | Pulsos | GPIO **27** | YF‑S201 OUT | Configurado `INPUT_PULLUP`, interrupción por **RISING** |
| **ZMPT101B (Voltaje AC)** | Analógica | GPIO **32** (ADC1_CH4) | ZMPT OUT | Atenuación ADC recomendada `ADC_11db` (0–3.3 V) |
| **Alimentación** | 3.3 V | 3V3 | RTC, MAX6675 (si placa 3.3 V) | Ver consumos totales |
|  | 5 V | 5V | SD (algunas), ZMPT101B | Confirmar compatibilidad de la placa SD con 3.3 V |
| **Común** | GND | GND | Todos | Masa común imprescindible |

---

## 🔌 Esquema lógico de cableado (texto)

```
[ESP32-WROOM-32]
  I2C:  SDA(21) ──────────── DS3231.SDA
        SCL(22) ──────────── DS3231.SCL
        3V3  ─────────────── DS3231.VCC
        GND  ─────────────── DS3231.GND

  VSPI: CS(5) ────────────── SD.CS
        SCK(18) ──────────── SD.SCK
        MISO(19) ─────────── SD.MISO
        MOSI(23) ─────────── SD.MOSI
        3V3/5V ───────────── SD.VCC (según módulo; preferible 3.3 V)
        GND  ─────────────── SD.GND

  HSPI: CS(15) ───────────── MAX6675.CS
        SCK(14) ──────────── MAX6675.SCK
        MISO(12) ─────────── MAX6675.SO
        3V3  ─────────────── MAX6675.VCC
        GND  ─────────────── MAX6675.GND

  Pulsos: D27 ────────────── YF-S201.OUT
          3V3/5V ─────────── YF-S201.VCC (la mayoría 5 V; salida tipo colector abierto o TTL)
          GND ────────────── YF-S201.GND

  ADC:  GPIO32 ───────────── ZMPT101B.OUT (0–3.3 V)
        5V   ─────────────── ZMPT101B.VCC (según placa; muchas son 5 V)
        GND  ─────────────── ZMPT101B.GND
```

> **Importante:** Si la salida del ZMPT101B supera 3.3 V pico‑pico (según ajuste del potenciómetro), **reducir** con divisor resistivo o bajar la ganancia.

---

## ⚠️ Recomendaciones de hardware & EMC

- **Masa común (GND)** entre todos los módulos; topología estrella preferente.
- **Desacoplo**: 100 nF cerámicos cerca de VCC de cada módulo + 10 µF por rama.
- **Cables** de señal cortos; para sensores alejados usar cable apantallado y **GND trenzado**.
- **Protección** entrada pulsos YF‑S201: si cable >2 m, añadir RC (100 Ω + 100 pF) y **TVS** 5 V.
- **Separación** de rutas: mantener líneas SPI separadas de ADC (ZMPT) para reducir acople.
- **Común analógica**: GND analógica de ZMPT al punto más cercano al ESP32 (evitar bucles).
- **Fuente**: 5 V/2 A estable; regular a 3.3 V (LDO o buck de bajo ruido).
- **Conectores**: JST‑XH o KF2510; crimpar y etiquetar cada línea.
- **Montaje**: si ambiente húmedo/polvo, caja IP65 + prensaestopas, silica gel.

---

## 🔧 Ajustes de software (coherentes con el cableado)

- **`config.cpp`** (ya en proyecto):  
  - YF‑S201 → `pin1 = 27`, `mode = REAL`  
  - MAX6675 (HSPI) → `CS=15`, `SCK=14`, `SO=12`, `mode = REAL`  
  - ZMPT101B → `pin1 = 32`, `mode = REAL`  
  - DS3231 → `SDA=21`, `SCL=22`  
  - SD (VSPI) → `CS=5`, `SCK=18`, `MISO=19`, `MOSI=23`
- **Interrupciones**: YF‑S201 en `RISING` con `INPUT_PULLUP`.
- **ADC**: configurar atenuación adecuada (ej. 11 dB) y muestreo estable (500 muestras/100 ms).
- **SPI/HSPI**: inicializar `spi_temp` antes de MAX6675; SD en VSPI por defecto.

---

## 🧪 Checklist de puesta en marcha

- [ ] Verificar tensiones de cada módulo (3.3 V/5 V) con multímetro.
- [ ] Comprobar continuidad GND entre todos los puntos.
- [ ] Revisar que ZMPT no sature (>3.3 V p‑p) al nivel nominal de red.
- [ ] Confirmar interrupciones del YF‑S201 (contador de pulsos incrementa).
- [ ] Validar lectura del MAX6675 (temperaturas razonables y sin `READ_ERR`).
- [ ] Probar SD: creación de `eventlog_YYYY.MM.DD.csv` y `backup_YYYYMMDD.csv`.
- [ ] Verificar sincronización NTP y hora RTC.
- [ ] Inspeccionar logs `MOD_UP/MOD_FAIL` y `FSM_STATE`.

---

## 📎 Notas de seguridad (ZMPT101B / AC)

- Manipular **SIEMPRE** con la red **desconectada**.
- Asegurar **aislamiento** del módulo y bornes protegidos.
- Usar fusible y protección diferencial en la línea medida.
- Cumplir normativa local para mediciones de red.

---

## 🛠️ BOM (sugerida)

- ESP32‑WROOM‑32 DevKit (original o calidad A)
- RTC DS3231 con batería CR2032
- Lector microSD 3.3 V (CS=5)
- Módulo MAX6675 + termocupla tipo K con punta de contacto
- Sensor de flujo YF‑S201 + racores
- Módulo ZMPT101B
- TVS 5 V para línea de pulsos, RC anti‑rebote
- Cables dupont/teflón, conectores JST‑XH, caja IP65, prensaestopas, separadores

---

> Mantener trazabilidad: registrar en `docs/` cualquier cambio de pinout y actualizar `config.cpp`, `README.md` y `FSM.md`. Este documento sirve como referencia de montaje y auditoría de instalaciones.