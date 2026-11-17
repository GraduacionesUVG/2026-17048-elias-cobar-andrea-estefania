// Prueba de WiFi: intentar conectarse, sin pantalla ni audio.
// Usar los mismos datos de teco.ino, subir y abrir el Monitor Serie
// a 115200. Despues volver a subir teco.ino.

#include <WiFi.h>

const char* WIFI_SSID = "PON_AQUI_TU_RED";
const char* WIFI_PASS = "PON_AQUI_TU_CONTRASENA";

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    Serial.print("Desconectado, codigo: ");
    Serial.println(info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  Serial.println("Redes que ve el ESP32:");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Serial.print("  ");
    Serial.print(WiFi.SSID(i));
    Serial.print("  senal: ");
    Serial.println(WiFi.RSSI(i));
  }

  Serial.print("Conectando a: [");
  Serial.print(WIFI_SSID);
  Serial.println("]");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
  static unsigned long t = 0;
  if (millis() - t > 2000) {
    t = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("CONECTADO! IP ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("intentando...");
    }
  }
}
