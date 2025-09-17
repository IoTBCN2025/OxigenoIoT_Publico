# 📄 Sensor de Caudal YF-S201 – Módulo OxigenoIoT

Este módulo permite medir el caudal de agua en L/min usando el sensor **YF‑S201** en un entorno de agricultura inteligente basado en ESP32. Está integrado dentro de la arquitectura FSM y permite operar tanto en **modo real** como en **modo simulación**, configurable desde `config.h`.

---

## 🔌 Cableado y conexión (ESP32)

| Pin YF-S201 | Descripción        | Conectar a ESP32 |
|-------------|--------------------|------------------|
| Rojo        | VCC (5 V)          | VIN o 5 V        |
| Negro       | GND                | GND              |
| Amarillo    | Señal (pulso)      | GPIO27 (D27)     |

---

## ⚙️ Configuración en `config.cpp`

```cpp
.caudal = {
    Mode::REAL,  // o Mode::SIMULATION
    27,          // pin1: señal digital YF-S201 (GPIO27)
    0, 0, 0
}
```

> ⚠️ El pin GPIO27 debe estar libre y admite interrupciones.

---

## 🧠 Lógica de funcionamiento

- **Inicialización**: se configura el pin y se activa la interrupción por flanco de subida (`RISING`).
- **Lectura continua**: durante la ventana 0–29 s del minuto se acumulan pulsos.
- **Cálculo de caudal**:
  ```cpp
  caudalLPM = pulsos / 7.5;  // Según datasheet del sensor
  ```

---

## 🧪 Simulación

Cuando el modo está en `SIMULATION`, se generan valores aleatorios entre 2.00 y 8.00 L/min:

```cpp
caudalLPM = random(200, 800) / 100.0;
```

Esto permite probar el sistema sin necesidad del sensor físico.

---

## 📝 Logs esperados

Archivo: `eventlog_YYYY.MM.DD.csv`

```
...,INFO,YF-S201,MOD_UP,,sim=0           # Modo real activo
...,INFO,YF-S201,DATA,,valor=3.47        # Lectura real
...,INFO,YF-S201,DATA,,valor=6.25        # Simulada (si sim=1)
```

---

## 🛠️ Funciones clave en `sensores_CAUDALIMETRO_YF-S201.cpp`

```cpp
void inicializarSensorCaudal();    // Configura pin e interrupción
void comenzarLecturaCaudal();      // Habilita la interrupción
void detenerLecturaCaudal();       // Desactiva la interrupción
void actualizarCaudal();           // Calcula el caudal actual
float obtenerCaudalLPM();          // Devuelve el valor calculado
```

---

## 📌 Notas adicionales

- El sensor debe recibir al menos 5 V para operar con precisión.
- Los pulsos pueden ser ruidosos; se recomienda cableado corto y limpio.
- Compatible con otras FSM siempre que se controle el tiempo de lectura.

---

## 📎 Referencias

- [Datasheet YF-S201 (pdf)](https://components101.com/sites/default/files/component_datasheet/YF-S201-Water-Flow-Sensor-Datasheet.pdf)
- [Wikipedia: Caudalímetro](https://es.wikipedia.org/wiki/Caudal%C3%ADmetro)