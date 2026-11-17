// Prueba de tierra sin multimetro
// Verificar si dos pines del DFPlayer estan unidos entre si
// (como las dos tierras GND). Desconectar el VCC del DFPlayer
// durante la prueba.
//
// Cables:
//   - GND del ESP32  -> un pin del DFPlayer que creemos que es GND
//   - Pin 4 del ESP32 -> el otro pin que creemos que es GND
//
// Monitor Serie a 115200:
//   "CONECTADOS"  = los dos pines estan unidos: son las dos tierras.
// "NO conectados" = no son las tierras; probar otra pareja.

#define PIN_PRUEBA 4

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PRUEBA, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(PIN_PRUEBA) == LOW) Serial.println("CONECTADOS: son las tierras (GND)");
  else Serial.println("NO conectados");
  delay(1000);
}
