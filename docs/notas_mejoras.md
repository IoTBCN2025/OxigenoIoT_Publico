# Notas Importantes
- Fecha: 18/07/2025
- Autor: Jose Monje Ruiz
- Descripción: Este archivo contiene observaciones críticas sobre el proyecto y parte del funcionamiento clave para entender la logica del desarollo


Modificar Fehca del nombre del eventoLog
eventlog_1970.01.01.csv


🔍 Sugerencias de mejora futura
| Propuesta                | Descripción                                                                                  |
| ------------------------ | -------------------------------------------------------------------------------------------- |
| FSM explícita            | Usar `enum Estado` para manejar estados como `LECTURA`, `ENVIO`, `BACKUP`, `REINTENTO`, etc. |
| Validaciones adicionales | Verificar que `valor` no sea `NaN` ni extremos absurdos.                                     |
| Reintentos por registro  | Contador adicional para limitar reintentos individuales.                                     |
| Post Batch               | Evaluar envío múltiple en un solo request (POST) si la API lo permite.                       |
| Historial de backups     | Mover archivos reenviados exitosamente a una carpeta `/enviados/` opcional.                  |


✨ Próximos pasos posibles
Usar MQTT como alternativa de comunicación.
Añadir detección de errores energéticos (picos o cortes).
Integrar TinyML para alertas inteligentes en borde.