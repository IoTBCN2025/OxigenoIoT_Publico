# Sensor de Caudal YF-S201

El sensor YF-S201 mide el flujo de agua en **litros por minuto (L/min)** mediante una rueda de paletas que gira al paso del agua. Cada giro genera una señal de pulso digital que puede ser captada por una interrupción externa en el ESP32.

---

## ⚙️ Configuración del sensor

- **Modo:** `REAL` o `SIMULATION` (definido en `config.cpp`)
- **Pin de entrada:** GPIO `27` (interrupción digital)
- **Interrupción:** `RISING` edge
- **Constante de conversión:**  
  ```cpp
  caudal (L/min) = pulsos / 7.5
  ```
  Esta constante depende del fabricante (7.5 es la más común para YF-S201).

---

## 🧪 Funcionamiento en modo REAL

- Se cuenta el número de pulsos durante una ventana de tiempo.
- Cada pulso equivale aproximadamente a 1/7.5 L/min.
- Se usa interrupción para contar pulsos con precisión:
  ```cpp
  attachInterrupt(digitalPinToInterrupt(pin), contarPulso, RISING);
  ```

---

## 🧪 Funcionamiento en modo SIMULACIÓN

- Se generan valores aleatorios en el rango de:
  ```
  2.00 L/min a 8.00 L/min
  ```

---

## 🧠 Integración FSM

- **Ventana de tiempo activa:** segundo `0` a `29` de cada minuto
- Entra en estado `LECTURA_CONTINUA_CAUDAL` cada 1 segundo
- Llama periódicamente a `actualizarCaudal()` y `obtenerCaudalLPM()`
- Si falla el envío a la API o no hay WiFi, respalda en SD

---

## 📝 Logs generados

- `MOD_UP`: inicialización exitosa
- `API_OK`: dato enviado exitosamente
- `RESPALDO`: respaldo en SD si falla conexión/API
- `TS_INVALID_BACKUP`: se usó timestamp por fallback

---

## 🧾 Ejemplo

```txt
2025-09-19 12:00:01,1752612001000000,INFO,YF-S201,MOD_UP,,sim=0
2025-09-19 12:00:10,1752612010000000,INFO,API,API_OK,,sensor=YF-S201;valor=7.55
```