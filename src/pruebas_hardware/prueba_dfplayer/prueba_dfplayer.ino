// Prueba del DFPlayer Mini: diagnosticar el modulo de audio
// Subir solo para revisar el modulo de audio.
// Abrir el Monitor Serie a 115200.
// Conexiones (las mismas de Teco):
//   GPIO17 (TX del ESP32) -> RX del DFPlayer (pin 2)
//   GPIO16 (RX del ESP32) <- TX del DFPlayer (pin 3)

#include <HardwareSerial.h>

#define DF_RX 16
#define DF_TX 17
HardwareSerial df(2);

void sendCmd(uint8_t cmd, uint16_t param, bool ack) {
  uint8_t p[10] = { 0x7E, 0xFF, 0x06, cmd, (uint8_t)(ack ? 1 : 0), (uint8_t)(param >> 8), (uint8_t)(param & 0xFF), 0, 0, 0xEF };
  uint16_t sum = 0 - (p[1] + p[2] + p[3] + p[4] + p[5] + p[6]);
  p[7] = sum >> 8; p[8] = sum & 0xFF;
  df.write(p, 10);
}

// Leer e interpretar las respuestas del modulo durante ms milisegundos
int readReplies(unsigned long ms) {
  unsigned long end = millis() + ms;
  uint8_t buf[10]; int n = 0, count = 0;
  while ((long)(millis() - end) < 0) {
    while (df.available()) {
      uint8_t b = df.read();
      if (n == 0 && b != 0x7E) continue;
      buf[n++] = b;
      if (n == 10) {
        n = 0; count++;
        uint8_t cmd = buf[3]; uint16_t par = (buf[5] << 8) | buf[6];
        Serial.print("  Respuesta: ");
        if (cmd == 0x3F) { Serial.print("modulo encendido, memoria detectada = "); Serial.println(par == 2 ? "TARJETA SD (bien)" : (par == 0 ? "NINGUNA (no ve la tarjeta)" : "otra")); }
        else if (cmd == 0x3A) Serial.println("tarjeta insertada");
        else if (cmd == 0x3B) Serial.println("tarjeta retirada");
        else if (cmd == 0x41) Serial.println("orden recibida (OK)");
        else if (cmd == 0x48) { Serial.print("archivos en la tarjeta = "); Serial.println(par); }
        else if (cmd == 0x3D) { Serial.print("termino de reproducir la pista "); Serial.println(par); }
        else if (cmd == 0x40) {
          Serial.print("ERROR ");
          const char* e[] = { "?", "modulo ocupado", "modulo dormido", "error de comunicacion", "error de suma", "pista fuera de rango", "archivo no encontrado", "?", "error de tarjeta" };
          Serial.println(par <= 8 ? e[par] : "desconocido");
        } else { Serial.print("comando 0x"); Serial.print(cmd, HEX); Serial.print(" valor "); Serial.println(par); }
      }
    }
  }
  return count;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== PRUEBA DEL DFPLAYER ===");
  df.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

  Serial.println("1) Esperando el arranque del modulo (3 s)...");
  int r = readReplies(3000);

  Serial.println("2) Reiniciando el modulo...");
  sendCmd(0x0C, 0, false);
  r += readReplies(3000);

  Serial.println("3) Preguntando cuantos archivos tiene la tarjeta...");
  sendCmd(0x48, 0, true);
  r += readReplies(1500);

  if (r == 0) {
    Serial.println();
    Serial.println(">>> El DFPlayer NO RESPONDE. Revisa, en este orden:");
    Serial.println("    - Que tenga energia: entre VCC (pin 1) y GND (pin 7) debe haber 3.3 a 5 V.");
    Serial.println("    - Que este bien metido en su zocalo y en la orientacion correcta.");
    Serial.println("    - El cable de su TX (pin 3) al GPIO16 del ESP32.");
    Serial.println("    (Si suena pero no responde, el problema es solo el cable TX -> GPIO16.)");
  }

  Serial.println("4) Volumen 20 y reproducir /MP3/0001.mp3 ...");
  sendCmd(0x06, 20, false);
  delay(100);
  sendCmd(0x12, 1, true);
  readReplies(3000);
  Serial.println("   La luz del modulo debe encenderse mientras suena.");
  Serial.println("   Si NO se enciende: revisa el cable GPIO17 -> RX del DFPlayer (pin 2).");
  Serial.println();
  Serial.println("Cada 8 s se vuelve a reproducir. Fin del diagnostico inicial.");
}

void loop() {
  delay(8000);
  Serial.println("Reproduciendo /MP3/0001.mp3 otra vez...");
  sendCmd(0x12, 1, true);
  readReplies(2000);
}
