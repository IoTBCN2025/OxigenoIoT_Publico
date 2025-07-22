#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <vector>
#include "api.h"
#include "sdlog.h"

void reenviarDatosDesdeBackup() {
  logEvento("REINTENTO_DEBUG", "Entrando a funcion reenviarDatosDesdeBackup()");

  File root = SD.open("/");
  while (true) {
    File archivo = root.openNextFile();
    if (!archivo) break;

    String nombreArchivo = archivo.name();

    // Ignorar archivos con fecha inválida
    if (!nombreArchivo.startsWith("backup_") || !nombreArchivo.endsWith(".csv") || nombreArchivo.indexOf("1970") >= 0) {
      archivo.close();
      continue;
    }

    logEvento("REINTENTO_DEBUG", "Procesando archivo: " + nombreArchivo);

    std::vector<String> lineas;
    archivo.readStringUntil('\n'); // Saltar encabezado
    while (archivo.available()) {
      String linea = archivo.readStringUntil('\n');
      if (linea.length() > 10) lineas.push_back(linea);
    }
    archivo.close();

    logEvento("REINTENTO_DEBUG", "Registros leidos: " + String(lineas.size()));

    std::vector<String> pendientes;
    int enviadosEnEsteCiclo = 0;

    for (auto& l : lineas) {
      if (enviadosEnEsteCiclo >= 5) break;

      int pos = 0;
      String campos[7];
      for (int i = 0; i < 7; i++) {
        int coma = l.indexOf(',', pos);
        if (coma == -1) coma = l.length();
        campos[i] = l.substring(pos, coma);
        pos = coma + 1;
      }

      String ts = campos[0];
      String m = campos[1];
      String s = campos[2];
      String v = campos[3];
      String src = campos[4];
      String status = campos[5];

      if (status != "PENDIENTE") {
        pendientes.push_back(l); // Mantener los OK también
        continue;
      }

      if (WiFi.isConnected() && enviarDatoAPI(m, s, v.toFloat(), strtoull(ts.c_str(), NULL, 10), "backup")) {
        logEvento("API_OK", "Dato enviado a API correctamente: " + s + "=" + v + " (backup)");
        logEvento("REINTENTO", "Enviado desde backup: " + s + "=" + v);

        unsigned long long ts_envio = strtoull(ts.c_str(), NULL, 10);  // Usamos mismo ts para simplificar
        String nuevaLinea = ts + "," + m + "," + s + "," + v + "," + src + ",OK," + String(ts_envio);
        pendientes.push_back(nuevaLinea);

        enviadosEnEsteCiclo++;
      } else {
        pendientes.push_back(l);
      }
    }

    logEvento("REINTENTO_DEBUG", "Registros pendientes: " + String(pendientes.size()));

    if (enviadosEnEsteCiclo >= 5) {
      logEvento("REINTENTO_STOP", "Maximo de reintentos alcanzado para archivo: " + nombreArchivo);
    }

    // Actualizar archivo
    File actualizado = SD.open("/" + nombreArchivo, FILE_WRITE);
    if (actualizado) {
      actualizado.seek(0);
      actualizado.print("timestamp,measurement,sensor,valor,source,status,ts_envio\n");
      for (auto& r : pendientes) actualizado.println(r);
      actualizado.flush();
      actualizado.close();
      logEvento("REINTENTO", "Archivo actualizado tras reenvio");
    }

  } // while
  root.close();
}
