// Prueba de todo el hardware de Teco
// Revisar cada componente y mostrar el resultado en pantalla
// y en el Monitor Serie a 115200. Usar los mismos pines de Teco.
//
// Uso:
// 1. Subir el programa y abrir el Monitor Serie a 115200.
// 2. Seguir las instrucciones de la pantalla.
// 3. Presionar cada boton, acercar el iman y tocar el sensor tactil.
//    Cada componente que responde se marca OK en verde.
// 4. Tomar foto de la pantalla final y del Monitor Serie.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HardwareSerial.h>

#define TFT_CS   32
#define TFT_DC   27
#define TFT_RST  33
#define TFT_MOSI 13
#define TFT_SCLK 14
#define BTN_LEFT   18
#define BTN_RIGHT  19
#define BTN_OK     25
#define BTN_BACK   26
#define BUZZER_PIN 4
#define HALL_PIN   15
#define TOUCH_PIN  2
#define DF_RX 16
#define DF_TX 17

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial df(2);

#define VERDE  0x07E0
#define ROJO   0xF800
#define BLANCO 0xFFFF
#define NEGRO  0x0000
#define GRIS   0x8410

// componentes a revisar
enum { C_IZQ, C_DER, C_OK, C_BACK, C_IMAN, C_TACTIL, C_DFPLAYER, C_SD, C_TOTAL };
const char* NOMBRES[C_TOTAL] = {
  "Boton IZQUIERDA (pin 18)", "Boton DERECHA (pin 19)", "Boton OK (pin 25)", "Boton BACK (pin 26)",
  "Sensor de iman (pin 15)", "Sensor tactil (pin 2)", "DFPlayer responde (16/17)", "Tarjeta SD en el DFPlayer"
};
bool ok[C_TOTAL];
bool dibujado[C_TOTAL];

void fila(int i) {
  int y = 52 + i * 21;
  tft.fillRect(0, y, 320, 20, NEGRO);
  tft.setTextSize(1);
  tft.setTextColor(BLANCO);
  tft.setCursor(8, y + 6);
  tft.print(NOMBRES[i]);
  tft.setCursor(250, y + 6);
  tft.setTextColor(ok[i] ? VERDE : GRIS);
  tft.print(ok[i] ? "OK" : "esperando");
}

void marcar(int i) {
  if (ok[i]) return;
  ok[i] = true;
  fila(i);
  tone(BUZZER_PIN, 1800, 80);
  Serial.print("OK: ");
  Serial.println(NOMBRES[i]);
}

void sendCmd(uint8_t cmd, uint16_t param, bool ack) {
  uint8_t p[10] = { 0x7E, 0xFF, 0x06, cmd, (uint8_t)(ack ? 1 : 0), (uint8_t)(param >> 8), (uint8_t)(param & 0xFF), 0, 0, 0xEF };
  uint16_t sum = 0 - (p[1] + p[2] + p[3] + p[4] + p[5] + p[6]);
  p[7] = sum >> 8; p[8] = sum & 0xFF;
  df.write(p, 10);
}

// Leer respuestas del DFPlayer y marcar lo detectado
void leerDF() {
  static uint8_t buf[10]; static int n = 0;
  while (df.available()) {
    uint8_t b = df.read();
    if (n == 0 && b != 0x7E) continue;
    buf[n++] = b;
    if (n == 10) {
      n = 0;
      uint8_t cmd = buf[3]; uint16_t par = (buf[5] << 8) | buf[6];
      marcar(C_DFPLAYER);
      if ((cmd == 0x3F && (par & 0x02)) || cmd == 0x48 || cmd == 0x3A) marcar(C_SD);
      Serial.print("  DFPlayer respondio: comando 0x"); Serial.print(cmd, HEX);
      Serial.print(" valor "); Serial.println(par);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== PRUEBA DE HARDWARE DE TECO ===");

  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(HALL_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Buzzer: tocar 3 tonos al arrancar
  Serial.println("Buzzer: deben sonar 3 tonos.");
  tone(BUZZER_PIN, 800, 150); delay(200);
  tone(BUZZER_PIN, 1200, 150); delay(200);
  tone(BUZZER_PIN, 1600, 150); delay(200);

  // Pantalla: dibujar franjas de colores
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ROJO);  delay(300);
  tft.fillScreen(VERDE); delay(300);
  tft.fillScreen(0x001F); delay(300);
  tft.fillScreen(NEGRO);
  tft.setTextColor(BLANCO);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("Prueba de hardware");
  tft.setTextSize(1);
  tft.setCursor(8, 30);
  tft.print("Presiona cada boton, acerca el iman y toca a Teco");
  Serial.println("Pantalla: si ves franjas rojo, verde, azul y este texto, funciona.");
  for (int i = 0; i < C_TOTAL; i++) fila(i);

  // DFPlayer
  df.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
  delay(1500);
  sendCmd(0x0C, 0, false);   // reiniciar
  delay(1500);
  sendCmd(0x48, 0, true);    // consultar cuantos archivos hay en la tarjeta
  delay(300);
  sendCmd(0x06, 20, false);  // ajustar volumen
  delay(100);
  sendCmd(0x12, 1, true);    // reproducir /MP3/0001.mp3
  Serial.println("DFPlayer: se pidio reproducir /MP3/0001.mp3 (la luz del modulo debe prenderse).");
}

void loop() {
  if (digitalRead(BTN_LEFT) == LOW)  marcar(C_IZQ);
  if (digitalRead(BTN_RIGHT) == LOW) marcar(C_DER);
  if (digitalRead(BTN_OK) == LOW)    marcar(C_OK);
  if (digitalRead(BTN_BACK) == LOW)  marcar(C_BACK);
  if (digitalRead(HALL_PIN) == LOW)  marcar(C_IMAN);
  if (digitalRead(TOUCH_PIN) == HIGH) marcar(C_TACTIL);
  leerDF();

  // Cada 10 s: imprimir resumen y reintentar con el DFPlayer
  static unsigned long next = 10000;
  if (millis() > next) {
    next = millis() + 10000;
    Serial.println("--- Resumen ---");
    for (int i = 0; i < C_TOTAL; i++) {
      Serial.print(ok[i] ? "  OK      " : "  FALTA   ");
      Serial.println(NOMBRES[i]);
    }
    if (!ok[C_DFPLAYER]) { sendCmd(0x48, 0, true); sendCmd(0x12, 1, true); }
  }
  delay(10);
}
