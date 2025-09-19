# API.md – Envío de Datos vía API HTTP (GET → InfluxDB)

Este documento describe la lógica implementada en `api.cpp`, encargada de construir la URL de envío y realizar la solicitud HTTP GET hacia una API PHP intermedia que escribe en InfluxDB.

---

## 🎯 Objetivo

- Enviar los datos de sensores al servidor con trazabilidad completa.
- Agregar metainformación (MAC, fuente, timestamp).
- Implementar manejo de errores, reintento y logging inteligente.

---

## 📤 Función principal

```cpp
bool enviarDatoAPI(const String& measurement, const String& sensor, float valor, unsigned long long timestamp, const String& source);
```

### Parámetros:
- `measurement`: tipo de dato (`caudal`, `temperatura`, etc.)
- `sensor`: nombre del sensor (`YF-S201`, `MAX6675`, etc.)
- `valor`: valor medido (2 decimales)
- `timestamp`: en microsegundos
- `source`: origen del dato (`wifi`, `backup`, etc.)

---

## 🔗 URL construida

```http
http://iotbcn.com/IoT/api.php?
  api_key=XXXXXX&
  measurement=caudal&
  sensor=YF-S201&
  valor=27.50&
  ts=1757431120000000&
  mac=34b7da60c44c&
  source=wifi
```

> Todos los parámetros se codifican con `urlEncode()` para evitar errores por caracteres especiales.

---

## 🔐 Seguridad básica

- `api_key` incluida como parámetro GET.
- Recomendación: validar en servidor y usar HTTPS si es posible.

---

## 📄 Manejo de resultado

```cpp
int httpCode = http.GET();
String payload = http.getString();
```

### Éxito:
- Código HTTP 200
- `payload` contiene "OK"
- Se registra `MOD_UP` (solo una vez)

### Fallo:
- Código 5xx, 404 o sin respuesta válida
- Se registra `API_5XX` una vez cada 30 segundos para evitar spam
- Devuelve `false` → el dato será guardado en backup SD

---

## 🔁 Rate Limit de logs

| Evento     | Condición             | Intervalo mínimo |
|------------|-----------------------|------------------|
| `API_SKIP` | No hay WiFi           | 10 s             |
| `API_5XX`  | Fallo en la respuesta | 30 s             |
| `MOD_UP`   | Primer éxito          | Solo una vez     |

---

## 🧪 Logs estructurados generados

| Código     | Significado |
|------------|-------------|
| `API_SKIP` | WiFi no disponible al intentar enviar |
| `API_5XX`  | Fallo HTTP o respuesta inesperada |
| `MOD_UP`   | Envío exitoso confirmado por primera vez |

---

## 🧼 Buenas prácticas

- Siempre cerrar el `HTTPClient` con `http.end()` para liberar recursos.
- Usar `http.setTimeout(7000)` para evitar cuelgues largos.
- No enviar si no hay IP (`WiFi.status() != WL_CONNECTED`)
- Construir `MAC` sin `:` para evitar errores de transmisión.

---

## 💡 Recomendaciones

- Migrar a POST con JSON para mejor trazabilidad (en versión futura).
- Cifrar tráfico con HTTPS.
- Validar `api_key` del lado del servidor.
- Loguear latencia entre `ts_envio - ts` en el servidor.

---

> Este módulo es crítico para mantener la trazabilidad completa de los datos. Si falla, el sistema garantiza respaldo local en SD para reintento posterior.