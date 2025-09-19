# Sensor de Voltaje ZMPT101B

Este sensor permite medir voltaje de corriente alterna (AC) de forma aislada, ideal para aplicaciones de monitoreo energético.

---

## ⚙️ Configuración

- **Modo:** `REAL` o `SIMULATION`
- **Pin de entrada analógica:** GPIO `32`
- **Conversión analógica a voltaje:**
  1. Se toman 500 muestras en 100 ms.
  2. Se determina el valor pico-a-pico:
     ```cpp
     picoPico = maxValor - minValor
     ```
  3. Se aplica un **factor de calibración**:
     ```cpp
     voltajeAC = picoPico * 0.2014
     ```
     Este factor se calcula calibrando con un multímetro (ej. 230 V ≈ 1142 p-p)

---

## 🧪 Modo SIMULACIÓN

- Genera voltajes entre `210.00` y `250.00` V

---

## 🧠 FSM y Ventana de Lectura

- **Segundo de lectura:** `40` del minuto
- Estado: `LECTURA_VOLTAJE`
- Envío directo por API o respaldo en SD

---

## 📝 Logs generados

- `MOD_UP`: inicialización
- `READ_OK`: lectura válida con valores de pico
- `RESPALDO`: dato respaldado en SD

---

## 🧾 Ejemplo

```txt
2025-09-19 12:00:40,1752612040000000,INFO,ZMPT101B,MOD_UP,,sim=0
2025-09-19 12:00:40,1752612040000000,INFO,API,API_OK,,sensor=ZMPT101B;valor=228.45
```