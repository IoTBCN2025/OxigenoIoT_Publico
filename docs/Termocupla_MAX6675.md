# Sensor de Temperatura MAX6675 (Termocupla tipo K)

El sensor MAX6675 permite leer la temperatura de una termocupla tipo K con precisión de 0.25 °C, usando una interfaz SPI (usualmente HSPI en ESP32).

---

## ⚙️ Configuración

- **Modo:** `REAL` o `SIMULATION`
- **Interfaz SPI dedicada:** HSPI
- **Pines en `config.cpp`:**
  - CS (Chip Select): GPIO `15`
  - SCK: GPIO `14`
  - SO (MISO): GPIO `12`

---

## 🔍 Protocolo de comunicación

- Se realiza un `transfer16(0x0000)` a través de SPI para obtener un valor de 16 bits (`raw`).
- El dato válido se encuentra en los bits 15:3.
- La temperatura se calcula así:
  ```cpp
  temperatura (°C) = ((raw >> 3) & 0x0FFF) * 0.25
  ```

- Verificaciones de error:
  - `raw == 0x0000`: sin respuesta
  - `raw & 0x0004`: termocupla desconectada

---

## 🧪 Modo SIMULACIÓN

- Genera valores aleatorios entre `25.00` y `35.00` °C

---

## 🧠 FSM y Ventana de Lectura

- **Segundo de lectura:** `35` del minuto
- Estado: `LECTURA_TEMPERATURA`
- Si se detecta timestamp inválido, se hace respaldo en SD

---

## 📝 Logs generados

- `MOD_UP`: inicialización correcta
- `LECTURA_OK`: lectura válida
- `READ_ERR`: error en lectura
- `RESPALDO`: respaldo en SD ante falla de red/API

---

## 🧾 Ejemplo

```txt
2025-09-19 12:00:35,1752612035000000,INFO,MAX6675,MOD_UP,,sim=0
2025-09-19 12:00:35,1752612035000000,INFO,API,API_OK,,sensor=MAX6675;valor=29.75
```