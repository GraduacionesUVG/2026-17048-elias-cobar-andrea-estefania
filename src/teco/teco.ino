// Blynk: datos del dispositivo en blynk.cloud > Device Info
// No escribir credenciales aqui: copiarlas en src/.env y generar
// secrets.h con python3 herramientas/env_a_secrets.py (ver README).
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef BLYNK_TEMPLATE_ID
#define BLYNK_TEMPLATE_ID   "PON_AQUI_TU_TEMPLATE_ID"
#endif
#ifndef BLYNK_TEMPLATE_NAME
#define BLYNK_TEMPLATE_NAME "Teco"
#endif
#ifndef BLYNK_AUTH_TOKEN
#define BLYNK_AUTH_TOKEN    "PON_AQUI_TU_AUTH_TOKEN"
#endif

// No guardar la red WiFi en el codigo: configurarla desde Ajustes

// 1 = usar WiFi y Blynk, 0 = funcionar sin internet
#define TECO_USE_BLYNK 1

// pines virtuales del tablero de Blynk
#define PIN_HAMBRE      V0
#define PIN_ENERGIA     V1
#define PIN_ESTADO      V2
#define PIN_MILPA       V3
#define PIN_HUIPILES    V4
#define PIN_ACIERTOS    V5
#define PIN_DIVERSION   V7
#define PIN_APRENDER    V8
#define PIN_LECCIONES   V9   // lecciones completadas (0 a 28)
#define PIN_MONEDAS     V10   // monedas (pwaq)
#define PIN_PERFIL      V14   // perfil activo (texto: N01 a N04)
// ordenes del tablero hacia Teco
#define PIN_COMER       V6
#define PIN_DORMIR      V11
#define PIN_ABRAZO      V12
#define PIN_VOLUMEN     V13

// ahorro de energia


// WiFi por intervalos (0 = siempre conectado)
#define TECO_WIFI_EVERY_MIN 0

// volumen maximo de la bocina (0-30)
#define TECO_MAX_VOLUME 24

// velocidad del procesador (MHz)
#define TECO_CPU_MHZ 160

// apagar el WiFi si no conecta en 1 minuto
#define TECO_WIFI_RETRY_LATER_MIN 10

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <BlynkSimpleEsp32.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <WiFiUdp.h>

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <pgmspace.h>
#include <HardwareSerial.h>

#include "fondos.h"

// Bolsas de palabras (una por juego). Recibir el numero de bolsa y no
// el tipo WordBag, para que compilen los prototipos automaticos del IDE.
#define BAG_MAX 112
struct WordBag { int16_t v[BAG_MAX]; uint8_t n; uint8_t pos; int16_t last; uint32_t sig; };
enum { BAG_BR, BAG_ANIMAL, BAG_FOOD, BAG_NUM, BAG_MILPA, BAG_COUNT };
WordBag bags[BAG_COUNT];
#include "teco_grande.h"
#include "teco_mini.h"
#include "milpa.h"
#include "telar.h"
#include "home_ui.h"
#include "lesson_cards.h"
#include "viaje.h"
#include "barrilete.h"
#include "teco_font.h"
#include "ui_kit.h"
#include "splash.h"

// pines de la pantalla TFT ILI9341 (SPI)

#define TFT_CS   32
#define TFT_DC   27
#define TFT_RST  33
#define TFT_MOSI 13
#define TFT_SCLK 14

// botones

#define BTN_LEFT   18
#define BTN_RIGHT  19
#define BTN_OK     25
#define BTN_BACK   26

// buzzer, sensor Hall y motor

#define BUZZER_PIN 4
#define HALL_PIN   15
#define DFPLAYER_RX 16   // RX del ESP32 <- TX del DFPlayer
#define DFPLAYER_TX 17   // TX del ESP32 -> RX del DFPlayer

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

HardwareSerial dfSerial(2);
bool dfPlayerReady = false;

// sensor tactil en GPIO2
#define TOUCH_PIN 2
bool lastTouchState = LOW;
unsigned long lastTouchAt = 0;
const unsigned long TOUCH_LOCK_MS = 350;

// colores

#define RGB565(r,g,b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define BG_COLOR        0xFFDD
#define SPRITE_BG       0xFFDD

#define FRAME_DARK      RGB565(110, 82, 60)

#define COLOR_TEXT      RGB565(70, 50, 38)

#define SELECT_COLOR    RGB565(255, 226, 122)
#define ICON_BORDER     RGB565(120, 82, 58)
#define PANEL_COLOR     RGB565(255, 239, 200)


// posiciones

#define EAT_FRAME_COUNT 3
#define PET_FRAME_COUNT 3

// estado general

enum AppMode {
  MODE_HOME,
  MODE_FEED,
  MODE_SLEEP,
  MODE_DRINK,
  MODE_PET,
  MODE_MILPA,
  MODE_TELAR,
  MODE_GAMEMENU,
  MODE_LESSON,
  MODE_SETTINGS,
  MODE_WIFISETUP,
  MODE_VIAJE,
  MODE_FOODPICK,
  MODE_BARRILETE,
  MODE_LEGEND
};

AppMode currentMode = MODE_HOME;

bool isNightMode = false;

unsigned long feedReturnAt = 0;
unsigned long drinkReturnAt = 0;

int hunger = 7;
int energy = 7;
int fun = 7;   // diversion: subir al jugar y con abrazos

// progreso de lecciones (guardado en memoria)
#define LESSON_MAX_WORDS 128
int coins = 0;   // monedas (pwaq)
int activeProfile = 0;   // dificultad adaptativa: nivel 1 a 5 por juego
const char* PROFILE_NS[4] = { "teco", "teco1", "teco2", "teco3" };
// dificultad adaptativa: nivel 1 a 5 por juego
int lvlMilpa = 1, lvlTelar = 1;
int lvlBarrilete = 1;
extern uint8_t albAnimals;
extern uint16_t albFoods;
extern uint32_t albNumbers;
extern int bestStars[3];
int lvlViaje[3] = { 1, 1, 1 };
int gameMistakes = 0;   // errores en la partida actual
int lastLevelChange = 0;   // +1 subio, -1 bajo, 0 igual
int bdDay = 0, bdMonth = 0, bdYear = 0;   // cumpleanos del nino (0 = sin configurar)
long tecoBorn = 0;   // dia de nacimiento de Teco (hora real)
int partyYear = 0;   // ultimo ano celebrado
uint8_t souvenirs = 0;   // recuerdos de viaje (un bit por destino)
// estampas: ganar una al terminar un juego, un viaje o una leccion
uint32_t stickerBits[4] = { 0, 0, 0, 0 };
bool stickerPending = false, stickerShowing = false;
int stickerWord = -1;
int lessonNext = 0;   // siguiente leccion sugerida
uint64_t lessonsDone = 0;   // lecciones completadas (un bit por leccion)
int learn = 7;   // medidor del libro (0-9)
uint8_t wordMiss[LESSON_MAX_WORDS];   // fallos por palabra

// hora real
bool sleptAtNight = false;   // dormido de noche: despertar solo
unsigned long nightAwakeUntil = 0;   // despertado de noche: no volver a dormir
const unsigned long NIGHT_AWAKE_MS = 30UL * 60UL * 1000UL;
long savedEpoch = 0;   // hora guardada al apagar
int savedDay = -1;   // dia del ano (para reiniciar aciertos)
int waterCount = 0;

// contadores para el tablero de Blynk
int milpaGames = 0;
int huipilesTejidos = 0;
int aciertosHoy = 0;   // reiniciar al encender
int dfVolume = TECO_MAX_VOLUME;

// animacion en reposo

#define IDLE_ANIMADO 1

// cuadros de la animacion en reposo
const int IDLE_SEQUENCE[] = { 0, 1, 2 };
const unsigned long IDLE_SEQUENCE_MS[] = { 1500, 900, 700 };
const int IDLE_STEPS = sizeof(IDLE_SEQUENCE) / sizeof(IDLE_SEQUENCE[0]);

int idleStep = 0;
int idleFrame = 0;
unsigned long lastIdleFrameAt = 0;
unsigned long idleWaitMs = 3000;

// Reiniciar la animacion en reposo desde el primer cuadro
void resetIdleBlink() {
  idleStep = 0;
  idleFrame = IDLE_SEQUENCE[0];
  lastIdleFrameAt = millis();
}

// animacion de beber

int drinkFrame = 0;
unsigned long lastDrinkFrameAt = 0;
const unsigned long DRINK_FRAME_MS = 350;

// animacion de comer

int eatFrame = 0;
unsigned long lastEatFrameAt = 0;
const unsigned long EAT_FRAME_MS = 350;

// animacion de dormir

int sleepFrame = 0;
unsigned long lastSleepFrameAt = 0;
const unsigned long SLEEP_DROWSY_MS = 900;   // duracion del cuadro adormilado
const unsigned long SLEEP_FRAME_MS  = 700;   // luego alternar cuadros 1 y 2

// animacion de caricia

int petFrame = 0;
unsigned long lastPetFrameAt = 0;
unsigned long petReturnAt = 0;
const unsigned long PET_FRAME_MS = 300;

// acciones de la casa

enum HomeAction {
  ACTION_FEED,
  ACTION_SLEEP,
  ACTION_GAME,
  ACTION_LESSON,
  ACTION_COUNT
};

int selectedAction = ACTION_FEED;

const char* actionNames[ACTION_COUNT] = {
  "COMER",
  "DORMIR",
  "JUGAR",
  "LECCIONES"
};

void startDrinkMode();
void startFeedMode();
void redrawHome();
void drawStaticScreen();
void startMilpa();
void milpaWater();
void milpaReplayAudio();
void updateMilpaMode();
void startTelar();
void startGameMenu();
void telarReplay();
void updateTelarMode();
void updateGameMenu();
void startLesson();
int eatFoodWord = -1;   // alimento elegido (-1 = mazorca)
const BigFrame* eatAnimFrames = bigEatFrames;   // animacion del alimento
int eatAnimCount = EAT_FRAME_COUNT;
unsigned long eatFrameMs = EAT_FRAME_MS;
void startSettings();
void startLegends();
void lgPicCard(int bx, int by, int bw, int rows, int pic, int x, int y, bool small);
void updateLegends();
bool hasSticker(int w);
int stickerCount();
void showSticker();
void drawDialogBox(const char* kaq, const char* es, const char* name, uint16_t tabColor);
void startBarrilete();
void updateBarrilete();
void settingsPanel(const char* title);
int bitCount(uint32_t v);
void clearSpeechBubble();
void startFoodPick();
void updateFoodPick();
void switchProfile(int p);
void loadActiveProfile();
void setupLog();
int logLineCount();
void logAnswer(const char* actividad, const char* correcta, const char* elegida, bool acierto);
void markQuestionStart();
void adaptLevel(int* lvl);
void startParty();
void startViajes();
void updateViajes();
void viajeReplay();
void updateSettings();
void updateWifiSetup();
void lsAfterAnswer();
void showGreeting();
void lessonReplay();
void updateLessonMode();
void startPetMode();
void drawSpeechBubble(const char* kaq, const char* es);
void drawPetBubble(const char* kaq, const char* es);
void drawTecoBubble(const char* kaq, const char* es);
void clearPetBubble();
void wakeUp();
void startSleepMode();
void markProgress();
void updateBlynkPending();
void dfPlayFolder(uint8_t folder, uint8_t track);
bool clockNight();
void clockLocal(struct tm* t);
void hbWifiIcon(int bx, int by, int bw, int rows);
int wifiIconLevel();
void updateWifiIcon();

// ---- Bolsa de palabras: no repetir una palabra hasta agotar las demas ----
uint32_t bagSig(const int* src, int n) { uint32_t s = 2166136261u ^ (uint32_t)n; for (int i = 0; i < n; i++) s = (s ^ (uint32_t)src[i]) * 16777619u; return s; }
int bagNext(uint8_t bagId, const int* src, int n) {
  WordBag& b = bags[bagId];
  if (n <= 0) return -1;
  if (n > BAG_MAX) n = BAG_MAX;
  uint32_t sig = bagSig(src, n);
  if (b.sig != sig || b.n != n || b.pos >= b.n) {   // bolsa nueva o cambio de grupo
    b.n = n; b.sig = sig; b.pos = 0;
    for (int i = 0; i < n; i++) b.v[i] = src[i];
    for (int i = n - 1; i > 0; i--) { int j = random(0, i + 1); int16_t t = b.v[i]; b.v[i] = b.v[j]; b.v[j] = t; }
    if (n > 1 && b.v[0] == b.last) { int16_t t = b.v[0]; b.v[0] = b.v[n - 1]; b.v[n - 1] = t; }   // no repetir entre bolsas
  }
  b.last = b.v[b.pos++];
  return b.last;
}

// ---- Dificultad en partida: subir de nivel con aciertos seguidos ----
int streakOk = 0, midLevelUps = 0;
void soundLevelUp();
void gameReset() { gameMistakes = 0; streakOk = 0; midLevelUps = 0; }
bool streakHit(int* lvl, int need) {
  streakOk++;
  if (streakOk >= need && *lvl < 5) {
    (*lvl)++; streakOk = 0; midLevelUps++;
    soundLevelUp();
    markProgress();
    Serial.print("Mas dificil -> nivel "); Serial.println(*lvl);
    return true;
  }
  return false;
}
void streakMiss() { streakOk = 0; }

// cola de estampas (un viaje puede dar varias)
int pickStickerFrom(const int* pool, int n);
void queueSticker(int w);
void brKite(int bx, int by, int bw, int rows, int cx, int cy, int r);
time_t clockNow();
bool clockValid();
bool greetingActive();
void endGreeting();

// DFPlayer Mini por UART directo, sin libreria

// cableado

// true = reproducir /MP3/0001.mp3 con el comando 0x12
#define AUDIO_USE_MP3_FOLDER true

#define AUDIO_WELCOME 1
#define AUDIO_EAT     1
#define AUDIO_DRINK   1
#define AUDIO_SLEEP   1

void sendDFCommand(uint8_t cmd, uint16_t param) {
  uint8_t packet[10];

  packet[0] = 0x7E;
  packet[1] = 0xFF;
  packet[2] = 0x06;
  packet[3] = cmd;
  packet[4] = 0x00;   // sin ACK para no bloquear
  packet[5] = (param >> 8) & 0xFF;
  packet[6] = param & 0xFF;

  uint16_t checksum = 0 - (packet[1] + packet[2] + packet[3] + packet[4] + packet[5] + packet[6]);
  packet[7] = (checksum >> 8) & 0xFF;
  packet[8] = checksum & 0xFF;
  packet[9] = 0xEF;

  dfSerial.write(packet, 10);
  dfSerial.flush();
}

void dfSetVolume(uint8_t volume) {
  if (volume > TECO_MAX_VOLUME) volume = TECO_MAX_VOLUME;
  if (volume > 30) volume = 30;
  sendDFCommand(0x06, volume);
}

void dfPlayRoot(uint16_t track) {
  // Reproducir archivos de la raiz: /0001.mp3, /0002.mp3, ...
  sendDFCommand(0x03, track);
}

void dfPlayMp3Folder(uint16_t track) {
  // Reproducir archivos de la carpeta MP3: /MP3/0001.mp3, ...
  sendDFCommand(0x12, track);
}

void playAudio(int track) {
  if (!dfPlayerReady) {
    Serial.println("AUDIO: UART directo no listo");
    return;
  }

  if (track <= 0) return;

  Serial.print("AUDIO UART DIRECTO: play track ");
  Serial.println(track);

  // Ajustar volumen y pausar al iniciar
  dfSetVolume(dfVolume);
  delay(80);

  if (AUDIO_USE_MP3_FOLDER) {
    dfPlayMp3Folder(track);   // /MP3/0001.mp3
  } else {
    dfPlayRoot(track);   // /0001.mp3
  }
}

void playWelcome() {
  playAudio(AUDIO_WELCOME);
}




unsigned long dfVolumeResendAt = 0;
int dfVolumeResends = 0;

// Reenviar el volumen dos veces despues del arranque
void updateDFVolumeResend() {
  if (!dfVolumeResendAt || (long)(millis() - dfVolumeResendAt) < 0) return;
  dfSetVolume(dfVolume);
  dfVolumeResends++;
  dfVolumeResendAt = (dfVolumeResends < 2) ? millis() + 5000 : 0;
}

void setupDFPlayer() {
  dfPlayerReady = false;

  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  delay(1000);

  // Omitir begin() de la libreria: el modulo se controla por UART directo
  dfPlayerReady = true;

  dfSetVolume(dfVolume);
  delay(300);
  dfVolumeResendAt = millis() + 3000;   // reenviar: el modulo tarda en leer la tarjeta

  Serial.println("DFPlayer listo por UART DIRECTO.");
}

// buzzer

void soundClick() {
  tone(BUZZER_PIN, 1200, 45);
}

void soundEat() {
  // sin DFPlayer ni delay para no congelar la animacion de comer
  tone(BUZZER_PIN, 900, 70);
}

void soundSleep() {
  // sin DFPlayer ni delay
  tone(BUZZER_PIN, 500, 90);
}

void soundWater() {
  // sin DFPlayer ni delay para no cortar la animacion de agua
  tone(BUZZER_PIN, 1100, 60);
}

void soundBack() {
  tone(BUZZER_PIN, 500, 80);
}

// Subir de nivel: tocar cuatro notas ascendentes sin detener el juego
const int LVUP_NOTES[4] = { 1047, 1319, 1568, 2093 };
int lvupStep = -1;
unsigned long lvupAt = 0;
void soundLevelUp() { lvupStep = 0; lvupAt = millis() + 220; }   // despues del sonido de acierto
void updateLevelUpSound() {
  if (lvupStep < 0 || (long)(millis() - lvupAt) < 0) return;
  tone(BUZZER_PIN, LVUP_NOTES[lvupStep], lvupStep == 3 ? 180 : 80);
  lvupAt = millis() + 100;
  if (++lvupStep >= 4) lvupStep = -1;
}

// botones

#define BTN_TOTAL 4

#define BTN_INDEX_LEFT   0
#define BTN_INDEX_RIGHT  1
#define BTN_INDEX_OK     2
#define BTN_INDEX_BACK   3

const int buttonPins[BTN_TOTAL] = {
  BTN_LEFT,
  BTN_RIGHT,
  BTN_OK,
  BTN_BACK
};

bool buttonLastRaw[BTN_TOTAL] = { HIGH, HIGH, HIGH, HIGH };
bool buttonStable[BTN_TOTAL] = { HIGH, HIGH, HIGH, HIGH };

unsigned long buttonLastChange[BTN_TOTAL] = { 0, 0, 0, 0 };
unsigned long buttonLastEvent[BTN_TOTAL] = { 0, 0, 0, 0 };

bool buttonPressedEvent[BTN_TOTAL] = { false, false, false, false };

const unsigned long BUTTON_DEBOUNCE_MS = 35;
const unsigned long BUTTON_EVENT_LOCK_MS = 160;

void setupButtons() {
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
}

void updateOneButton(int index) {
  buttonPressedEvent[index] = false;

  bool raw = digitalRead(buttonPins[index]);
  unsigned long now = millis();

  if (raw != buttonLastRaw[index]) {
    buttonLastRaw[index] = raw;
    buttonLastChange[index] = now;
  }

  if ((now - buttonLastChange[index]) >= BUTTON_DEBOUNCE_MS) {
    if (raw != buttonStable[index]) {
      buttonStable[index] = raw;

      if (buttonStable[index] == LOW) {
        if (now - buttonLastEvent[index] > BUTTON_EVENT_LOCK_MS) {
          buttonPressedEvent[index] = true;
          buttonLastEvent[index] = now;
        }
      }
    }
  }
}

void updateButtons() {
  for (int i = 0; i < BTN_TOTAL; i++) {
    updateOneButton(i);
  }
}

bool leftPressed() {
  return buttonPressedEvent[BTN_INDEX_LEFT];
}

bool rightPressed() {
  return buttonPressedEvent[BTN_INDEX_RIGHT];
}

bool okPressed() {
  return buttonPressedEvent[BTN_INDEX_OK];
}

bool backPressed() {
  return buttonPressedEvent[BTN_INDEX_BACK];
}

// sensor Hall

// Detectar el iman al instante por interrupcion
volatile unsigned long hallFallAt = 0;
volatile unsigned long hallRiseAt = 0;

const unsigned long HALL_BOOT_IGNORE_MS = 3000;   // ignorar el sensor al encender
const unsigned long HALL_MIN_PASS_MS = 15;   // duracion minima de un paso real

void IRAM_ATTR hallIsr() {
  if (digitalRead(HALL_PIN) == LOW) hallFallAt = millis();
  else hallRiseAt = millis();
}

void setupHall() {
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallIsr, CHANGE);
}

void updateHallSensor() {
  static bool lastRawState = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastRawChange = 0;
  static bool usedThisPresence = false;
  static unsigned long lastHandledFall = 0;
  static bool bootChecked = false;

  const unsigned long HALL_DEBOUNCE_MS = 80;

  unsigned long now = millis();

  // Ignorar el sensor unos segundos al encender
  if (now < HALL_BOOT_IGNORE_MS) return;
  if (!bootChecked) {
    bootChecked = true;
    lastHandledFall = hallFallAt;
    stableState = digitalRead(HALL_PIN);
    lastRawState = stableState;
    usedThisPresence = (stableState == LOW);
    return;
  }

  bool rawState = digitalRead(HALL_PIN);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastRawChange = now;
  }

  if ((now - lastRawChange) > HALL_DEBOUNCE_MS) {
    if (rawState != stableState) {
      stableState = rawState;

      if (stableState == LOW) {
        Serial.println("IMAN DETECTADO");
      } else {
        Serial.println("IMAN RETIRADO");
        usedThisPresence = false;
      }
    }
  }
  // paso rapido sin estabilizar: el iman ya se retiro
  if (stableState == HIGH && rawState == HIGH && (now - lastRawChange) > HALL_DEBOUNCE_MS) {
    usedThisPresence = false;
  }

  // iman detectado: fijo (estable) o paso rapido
  unsigned long fall = hallFallAt;
  unsigned long rise = hallRiseAt;
  bool stillLow = (digitalRead(HALL_PIN) == LOW);
  // paso rapido valido: activo al menos HALL_MIN_PASS_MS
  bool longEnough = stillLow ? (now - fall >= HALL_MIN_PASS_MS)
                             : (rise > fall && rise - fall >= HALL_MIN_PASS_MS);
  bool held = (stableState == LOW && !usedThisPresence);
  bool quickPass = (fall != 0 && fall != lastHandledFall && now - fall < 600 && longEnough && !usedThisPresence);

  if (held || quickPass) {
    if (currentMode == MODE_HOME || currentMode == MODE_MILPA) {
      lastHandledFall = fall;
      usedThisPresence = true;   // una sola accion hasta retirar el iman
      if (quickPass && !held) Serial.println("IMAN DETECTADO (paso rapido)");
      if (currentMode == MODE_HOME) startDrinkMode();
      else milpaWater();
    }
  }
}

// Dibujar la casa (fondo, Teco, nido, globo y menu)

#define BAND_H 8
uint16_t bandBuffer[FONDO_W * BAND_H];

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// Obtener el pixel del fondo actual (comprimido con paleta)
inline uint16_t bgPixel(int x, int y) {
  int i = y * FONDO_W + x;
  if (isNightMode) return pgm_read_word(&fondoNochePal[pgm_read_byte(&fondoNochePx[i])]);
  return pgm_read_word(&fondoDiaPal[pgm_read_byte(&fondoDiaPx[i])]);
}

// posiciones
#define NEST_CX    147   // centro del nido
#define NEST_FOOT  211   // pies de Teco sentado en el nido
#define BRANCH_FOOT 214   // pies de Teco parado en la rama

#define TILE_SIZE  34
#define TILE_Y     201
const int TILE_CX[4] = { 22, 60, 260, 298 };   // comer, dormir | jugar, lecciones

#define BUBBLE_X   192
#define BUBBLE_Y   64

// estado visual
const BigFrame* tecoSpr = &bigIdleFrames[0];
int tecoX = NEST_CX;
int tecoFoot = NEST_FOOT;
bool tecoFlip = false;
bool tecoInNest = true;

const MiniSprite* visSpr = nullptr;   // mariposa o pajarito (o nada)
int visX = 0, visY = 0;   // esquina superior izquierda

int bubbleIcon = -1;   // 1 = sin globo
int askTile = -1;   // cuadro que parpadea
bool askBlinkOn = false;

void tecoRect(int& rx, int& ry, int& rw, int& rh) {
  rw = tecoSpr->w;
  rh = tecoSpr->h;
  rx = tecoFlip ? tecoX - (tecoSpr->w - 1 - tecoSpr->ax) : tecoX - tecoSpr->ax;
  ry = tecoFoot - tecoSpr->ay;
}

// Dibujar pinceles sobre la franja
void hbSpriteMini(int bx, int by, int bw, int rows, const MiniSprite* s, int sx, int sy) {
  int xs = imax(sx, bx), xe = imin(sx + s->w, bx + bw);
  int ys = imax(sy, by), ye = imin(sy + s->h, by + rows);
  for (int y = ys; y < ye; y++)
    for (int x = xs; x < xe; x++) {
      uint16_t c = miniPx(s, (y - sy) * s->w + (x - sx));
      if (c != MINI_TRANSPARENT) bandBuffer[(y - by) * bw + (x - bx)] = c;
    }
}

void hbTeco(int bx, int by, int bw, int rows) {
  int rx, ry, rw, rh;
  tecoRect(rx, ry, rw, rh);
  int xs = imax(rx, bx), xe = imin(rx + rw, bx + bw);
  int ys = imax(ry, by), ye = imin(ry + rh, by + rows);
  const uint16_t* pal = tecoSpr->pal ? tecoSpr->pal : bigPalette;
  for (int y = ys; y < ye; y++) {
    const uint8_t* row = tecoSpr->px + (y - ry) * rw;
    for (int x = xs; x < xe; x++) {
      int lx = tecoFlip ? (rw - 1 - (x - rx)) : (x - rx);
      uint8_t idx = pgm_read_byte(&row[lx]);
      if (idx) bandBuffer[(y - by) * bw + (x - bx)] = pgm_read_word(&pal[idx]);
    }
  }
}

void hbNestFront(int bx, int by, int bw, int rows) {
  for (int i = 0; i < NEST_RUNS; i++) {
    int y = pgm_read_word(&nestRuns[i][0]);
    if (y < by || y >= by + rows) continue;
    int x0 = imax(pgm_read_word(&nestRuns[i][1]), bx);
    int x1 = imin(pgm_read_word(&nestRuns[i][2]), bx + bw);
    for (int x = x0; x < x1; x++) bandBuffer[(y - by) * bw + (x - bx)] = bgPixel(x, y);
  }
}

uint16_t meterColor(int v) {
  if (v > 5) return RGB565(80, 190, 80);
  if (v > 2) return RGB565(240, 190, 40);
  return RGB565(220, 60, 50);
}

int needValue(int tile);   // definido mas abajo

void hbTiles(int bx, int by, int bw, int rows) {
  for (int k = 0; k < 4; k++) {
    int x0 = TILE_CX[k] - TILE_SIZE / 2, y0 = TILE_Y;
    int x1 = x0 + TILE_SIZE, y1 = y0 + TILE_SIZE;
    if (x1 <= bx || x0 >= bx + bw || y1 <= by || y0 >= by + rows) continue;

    bool sel = (currentMode == MODE_HOME || currentMode == MODE_FEED || currentMode == MODE_DRINK ||
                currentMode == MODE_PET || currentMode == MODE_SLEEP) && k == selectedAction;
    uint16_t fill = sel ? RGB565(244, 176, 110) : RGB565(250, 212, 170);
    if (k == askTile && askBlinkOn) fill = RGB565(255, 236, 120);
    uint16_t border = RGB565(84, 44, 20);

    int ys = imax(y0, by), ye = imin(y1, by + rows);
    int xs = imax(x0, bx), xe = imin(x1, bx + bw);
    for (int y = ys; y < ye; y++) {
      for (int x = xs; x < xe; x++) {
        int lx = x - x0, ly = y - y0;
        // Dibujar esquinas redondeadas (radio ~4)
        int cx = lx < 4 ? 4 - lx : (lx > TILE_SIZE - 5 ? lx - (TILE_SIZE - 5) : 0);
        int cy = ly < 4 ? 4 - ly : (ly > TILE_SIZE - 5 ? ly - (TILE_SIZE - 5) : 0);
        if (cx * cx + cy * cy > 16) continue;
        bool edge = lx < 2 || ly < 2 || lx >= TILE_SIZE - 2 || ly >= TILE_SIZE - 2 ||
                    (cx * cx + cy * cy > 6);
        bandBuffer[(y - by) * bw + (x - bx)] = edge ? border : fill;
      }
    }

    const MiniSprite* ic = uiIcons[k];
    hbSpriteMini(bx, by, bw, rows, ic, TILE_CX[k] - ic->w / 2, y0 + 14 - ic->h / 2);

    // Dibujar medidor (lecciones sin medidor)
    int v = needValue(k);
    if (v >= 0) {
      int mx0 = TILE_CX[k] - 12, my = y0 + 27;
      for (int y = imax(my, by); y < imin(my + 4, by + rows); y++)
        for (int x = imax(mx0, bx); x < imin(mx0 + 25, bx + bw); x++) {
          bool inner = (y > my && y < my + 3 && x > mx0 && x < mx0 + 1 + (23 * v) / 9);
          bandBuffer[(y - by) * bw + (x - bx)] = inner ? meterColor(v) : border;
        }
    }
  }
}

// Componer y enviar una region
void renderHome(int x0, int y0, int w, int h) {
  if (stickerShowing) return;   // dibujar la estampa encima sin redibujar el fondo
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > FONDO_W) w = FONDO_W - x0;
  if (y0 + h > FONDO_H) h = FONDO_H - y0;
  if (w <= 0 || h <= 0) return;

  int maxRows = (FONDO_W * BAND_H) / w;
  if (maxRows > h) maxRows = h;

  bool showTeco = (currentMode != MODE_MILPA && currentMode != MODE_TELAR &&
                   currentMode != MODE_GAMEMENU && currentMode != MODE_LESSON &&
                   currentMode != MODE_SETTINGS && currentMode != MODE_WIFISETUP &&
                   currentMode != MODE_VIAJE && currentMode != MODE_BARRILETE && currentMode != MODE_LEGEND);

  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);

    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++)
        bandBuffer[r * w + x] = bgPixel(x0 + x, by + r);

    if (visSpr) hbSpriteMini(x0, by, w, rows, visSpr, visX, visY);
    if (showTeco) {
      hbTeco(x0, by, w, rows);
      if (tecoInNest) hbNestFront(x0, by, w, rows);
    }
    if (bubbleIcon >= 0) {
      hbSpriteMini(x0, by, w, rows, &uiBubbleFrames[0], BUBBLE_X, BUBBLE_Y);
      const MiniSprite* ic = uiIcons[bubbleIcon];
      hbSpriteMini(x0, by, w, rows, ic, BUBBLE_X + 22 - ic->w / 2, BUBBLE_Y + 15 - ic->h / 2);
    }
    if (showTeco) hbTiles(x0, by, w, rows);
    if (showTeco) hbWifiIcon(x0, by, w, rows);

    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void drawStaticScreen() {
  renderHome(0, 0, FONDO_W, FONDO_H);
}

// Cambiar lo que muestra Teco y redibujar solo lo necesario
void setTeco(const BigFrame* f, int x, int foot, bool flip, bool inNest) {
  int ax, ay, aw, ah;
  tecoRect(ax, ay, aw, ah);
  tecoSpr = f; tecoX = x; tecoFoot = foot; tecoFlip = flip; tecoInNest = inNest;
  int bx, by2, bw, bh;
  tecoRect(bx, by2, bw, bh);
  int ux = imin(ax, bx), uy = imin(ay, by2);
  int ux2 = imax(ax + aw, bx + bw), uy2 = imax(ay + ah, by2 + bh);
  renderHome(ux, uy, ux2 - ux, uy2 - uy);
}

void setTecoInNest(const BigFrame* f) {
  setTeco(f, NEST_CX, NEST_FOOT, false, true);
}

void setVisitor(const MiniSprite* s, int x, int y) {
  int ox = visX, oy = visY, ow = visSpr ? visSpr->w : 0, oh = visSpr ? visSpr->h : 0;
  visSpr = s; visX = x; visY = y;
  if (ow) renderHome(ox, oy, ow, oh);
  if (s) renderHome(x, y, s->w, s->h);
}

void showBubble(int icon) {
  bubbleIcon = icon;
  renderHome(BUBBLE_X, BUBBLE_Y, uiBubbleFrames[0].w, uiBubbleFrames[0].h);
}

void renderTile(int k) {
  renderHome(TILE_CX[k] - TILE_SIZE / 2, TILE_Y, TILE_SIZE, TILE_SIZE);
}

// Redibujar los 4 cuadros de la barra de acciones
void drawActionBar() {
  for (int k = 0; k < 4; k++) renderTile(k);
}

// Animaciones de la casa (Teco sentado en el nido)
void renderPetIdle()  { setTecoInNest(&bigIdleFrames[idleFrame]); }
void renderPetEat()   { setTecoInNest(&eatAnimFrames[eatFrame]); }
void renderPetDrink() { setTecoInNest(&bigDrinkFrames[drinkFrame]); }
void renderPetSleep() { setTecoInNest(&bigSleepFrames[sleepFrame]); }
void renderPetPet()   { setTecoInNest(&bigPetFrames[petFrame]); }

// Transicion dia / noche

const unsigned long BAND_DELAY_MS = 14;   // 0.5 s en total

// Anochecer (cortina hacia abajo) o amanecer (hacia arriba)
bool transitionActive = false;
bool transitionToNight = false;
int transitionY = 0;
unsigned long transitionNextAt = 0;
bool greetingPending = false;

void transitionBackground(bool toNight) {
  isNightMode = toNight;
  transitionToNight = toNight;
  transitionY = toNight ? 0 : FONDO_H - BAND_H;
  transitionNextAt = millis();
  transitionActive = true;
}

void updateTransition() {
  if (!transitionActive) return;
  if ((long)(millis() - transitionNextAt) < 0) return;
  renderHome(0, transitionY, FONDO_W, BAND_H);
  transitionNextAt = millis() + BAND_DELAY_MS;
  transitionY += transitionToNight ? BAND_H : -BAND_H;
  if (transitionY < 0 || transitionY >= FONDO_H) {
    transitionActive = false;
    if (greetingPending) { greetingPending = false; showGreeting(); }
  }
}

void stopLife();   // vida propia (mas abajo)
bool lifeBusy();

void redrawHome() {
  currentMode = MODE_HOME;
  stopLife();
  resetIdleBlink();
  tecoSpr = &bigIdleFrames[0]; tecoX = NEST_CX; tecoFoot = NEST_FOOT; tecoFlip = false; tecoInNest = true;
  renderHome(NEST_CX - 70, 60, 150, 156);
  drawActionBar();
}

// Animacion en reposo

void updateBlink() {
  if (currentMode != MODE_HOME) return;
  if (lifeBusy()) return;

  unsigned long now = millis();

#if IDLE_ANIMADO
  // Ciclar los 3 cuadros en reposo
  if (now - lastIdleFrameAt >= IDLE_SEQUENCE_MS[idleStep]) {
    lastIdleFrameAt = now;

    idleStep++;
    if (idleStep >= IDLE_STEPS) idleStep = 0;

    idleFrame = IDLE_SEQUENCE[idleStep];
    renderPetIdle();
  }
#else
  // Modo prueba: buho quieto (cuadro 0), solo cerrar los ojos (cuadro 2)
  if (idleFrame == 0 && now - lastIdleFrameAt >= idleWaitMs) {
    lastIdleFrameAt = now;
    idleFrame = 2;
    renderPetIdle();
  } else if (idleFrame != 0 && now - lastIdleFrameAt >= 160) {
    lastIdleFrameAt = now;
    idleFrame = 0;
    idleWaitMs = random(2500, 6500);
    renderPetIdle();
  }
#endif
}

// Necesidades: pedir lo que falta
#define AUDIO_FOLDER_TECO 6

#define NEED_LOW 3
const unsigned long ASK_EVERY_MS      = 60000;   // pausa entre peticiones
const unsigned long ASK_EVERY_LOW_MS  = 30000;   // nivel muy bajo (0-1)
const unsigned long ASK_SHOW_MS       = 4500;   // duracion del globo
const unsigned long ASK_BLINK_MS      = 400;

unsigned long nextAskAt = 20000;
unsigned long askUntil = 0;
unsigned long askBlinkAt = 0;

// valor del medidor de cada cuadro (-1 = sin medidor)
int needValue(int tile) {
  if (tile == ACTION_FEED)  return hunger;
  if (tile == ACTION_SLEEP) return energy;
  if (tile == ACTION_GAME)  return fun;
  return learn;   // lecciones
}

void endAsk() {
  if (bubbleIcon < 0 && askTile < 0) return;
  int t = askTile;
  bubbleIcon = -1;
  askTile = -1;
  askBlinkOn = false;
  renderHome(BUBBLE_X, BUBBLE_Y, uiBubbleFrames[0].w, uiBubbleFrames[0].h);
  if (t >= 0) renderTile(t);
}

void updateNeedsAsk(bool lifeBusy) {
  unsigned long now = millis();

  if (askTile >= 0) {
    if (currentMode != MODE_HOME || (long)(now - askUntil) >= 0) { endAsk(); return; }
    if (now - askBlinkAt >= ASK_BLINK_MS) {
      askBlinkAt = now;
      askBlinkOn = !askBlinkOn;
      renderTile(askTile);
    }
    return;
  }

  if (currentMode != MODE_HOME || isNightMode || lifeBusy || greetingActive()) return;
  if ((long)(now - nextAskAt) < 0) return;

  // Obtener la necesidad mas baja por debajo de NEED_LOW
  int best = -1, bestVal = NEED_LOW + 1;
  int tiles[4] = { ACTION_FEED, ACTION_SLEEP, ACTION_GAME, ACTION_LESSON };
  for (int i = 0; i < 4; i++) {
    int v = needValue(tiles[i]);
    if (v <= NEED_LOW && v < bestVal) { best = tiles[i]; bestVal = v; }
  }
  if (best < 0) { nextAskAt = now + 10000; return; }

  askTile = best;
  askUntil = now + ASK_SHOW_MS;
  askBlinkAt = now;
  askBlinkOn = true;
  showBubble(best);
  renderTile(best);
  dfPlayFolder(AUDIO_FOLDER_TECO, best == ACTION_FEED ? 1 : best == ACTION_SLEEP ? 2 : best == ACTION_GAME ? 3 : 5);
  nextAskAt = now + (bestVal <= 1 ? ASK_EVERY_LOW_MS : ASK_EVERY_MS);

  Serial.print("Teco pide: ");
  Serial.println(actionNames[best]);
}

// Vida propia: paseos, saltitos, saludos, bostezos y visitas

struct LifeStep {
  const BigFrame* f;
  int16_t x;
  int16_t foot;
  uint8_t flags;   // 1 = volteado, 2 = dentro del nido
  uint16_t ms;
};

#define LIFE_MAX_STEPS 48
LifeStep lifeSteps[LIFE_MAX_STEPS];
int lifeLen = 0;
int lifePos = -1;
unsigned long lifeStepAt = 0;
unsigned long nextLifeAt = 15000;

// visitante (mariposa o pajarito)
VisitKind visKind = VIS_NONE;
unsigned long visStart = 0;
unsigned long visFrameAt = 0;
int visFrame = 0;

bool lifeBusy() { return lifePos >= 0 || visKind != VIS_NONE; }

void lifeAdd(const BigFrame* f, int x, int foot, uint8_t flags, uint16_t ms) {
  if (lifeLen >= LIFE_MAX_STEPS) return;
  lifeSteps[lifeLen++] = { f, (int16_t)x, (int16_t)foot, flags, ms };
}

void lifeApply() {
  LifeStep& s = lifeSteps[lifePos];
  setTeco(s.f, s.x, s.foot, s.flags & 1, s.flags & 2);
  lifeStepAt = millis() + s.ms;
}

void lifeStart() {
  if (lifeLen == 0) return;
  lifePos = 0;
  lifeApply();
}

void stopLife() {
  bool wasBusy = lifeBusy();
  lifePos = -1;
  lifeLen = 0;
  if (visKind != VIS_NONE) {
    visKind = VIS_NONE;
    setVisitor(nullptr, 0, 0);
  }
  endAsk();
  if (wasBusy && currentMode == MODE_HOME) setTecoInNest(&bigIdleFrames[0]);
  nextLifeAt = millis() + random(20000, 45000);
}

// conductas
void buildPaseo() {
  lifeLen = 0;
  lifeAdd(&bigHopFrames[0], NEST_CX, NEST_FOOT, 2, 220);
  lifeAdd(&bigHopFrames[1], NEST_CX + 16, NEST_FOOT - 14, 0, 160);
  lifeAdd(&bigHopFrames[1], NEST_CX + 30, NEST_FOOT - 8, 0, 160);
  int x = 180;
  for (int i = 0; i < 6; i++) { lifeAdd(&bigWalkFrames[i % 4], x, BRANCH_FOOT, 0, 170); x += 5; }
  for (int i = 0; i < 4; i++) lifeAdd(&bigUpFrames[i % 2], x, BRANCH_FOOT, 0, 380);
  for (int i = 0; i < 6; i++) { x -= 5; lifeAdd(&bigWalkFrames[i % 4], x, BRANCH_FOOT, 1, 170); }
  lifeAdd(&bigHopFrames[1], NEST_CX + 20, NEST_FOOT - 12, 0, 160);
  lifeAdd(&bigHopFrames[0], NEST_CX, NEST_FOOT, 2, 220);
}

void buildSaltito() {
  lifeLen = 0;
  for (int i = 0; i < 2; i++) {
    lifeAdd(&bigHopFrames[0], NEST_CX, NEST_FOOT, 2, 200);
    lifeAdd(&bigHopFrames[1], NEST_CX, NEST_FOOT - 12, 0, 260);
  }
  lifeAdd(&bigHopFrames[0], NEST_CX, NEST_FOOT, 2, 200);
}

void buildSaludo() {
  lifeLen = 0;
  for (int i = 0; i < 6; i++) lifeAdd(&bigWaveFrames[i % 2], NEST_CX, NEST_FOOT, 2, 300);
  dfPlayFolder(AUDIO_FOLDER_TECO, 4);
}

void buildBostezo() {
  lifeLen = 0;
  lifeAdd(&bigYawnFrames[0], NEST_CX, NEST_FOOT, 2, 700);
  lifeAdd(&bigYawnFrames[1], NEST_CX, NEST_FOOT, 2, 1100);
  lifeAdd(&bigYawnFrames[0], NEST_CX, NEST_FOOT, 2, 500);
}

void buildMirarArriba(int steps) {
  lifeLen = 0;
  for (int i = 0; i < steps; i++) lifeAdd(&bigUpFrames[i % 2], NEST_CX, NEST_FOOT, 2, 400);
}

void startVisit(VisitKind k) {
  visKind = k;
  visStart = millis();
  visFrameAt = 0;
  visFrame = 0;
  buildMirarArriba(k == VIS_BUTTERFLY ? 16 : 18);
  lifeStart();
}

// Calcular la posicion del visitante: llegar, quedarse e irse
void updateVisitor() {
  if (visKind == VIS_NONE) return;
  unsigned long t = millis() - visStart;
  unsigned long now = millis();
  int x, y;
  const MiniSprite* spr;

  if (visKind == VIS_BUTTERFLY) {
    // Entrar por la derecha, revolotear cerca de Teco y salir hacia arriba
    if (t < 2500)      { x = 320 - (int)(t * 120 / 2500); y = 70 + (int)((t / 120) % 2) * 3; }
    else if (t < 5000) { x = 200 + (int)(((t / 200) % 4)) * 3; y = 72 - (int)((t / 250) % 3) * 2; }
    else if (t < 7000) { x = 200 + (int)((t - 5000) * 140 / 2000); y = 72 - (int)((t - 5000) * 80 / 2000); }
    else { visKind = VIS_NONE; setVisitor(nullptr, 0, 0); return; }
    if (now - visFrameAt < 140) return;
    visFrameAt = now;
    visFrame = 1 - visFrame;
    spr = &visButterflyFrames[visFrame];
  } else {
    // Volar a la rama, posarse un rato e irse
    const int sx = 330, sy = 120, px = 236, py = 178;
    if (t < 1800)      { x = sx + (px - sx) * (int)t / 1800; y = sy + (py - sy) * (int)t / 1800; spr = &visBirdFrames[1]; }
    else if (t < 5500) { x = px; y = py; spr = &visBirdFrames[0]; }
    else if (t < 7300) { int k = t - 5500; x = px + 100 * k / 1800; y = py - 110 * k / 1800; spr = &visBirdFrames[1]; }
    else { visKind = VIS_NONE; setVisitor(nullptr, 0, 0); return; }
    if (now - visFrameAt < 120 && spr == visSpr && x == visX && y == visY) return;
    visFrameAt = now;
  }
  setVisitor(spr, x, y);
}

void updateLife() {
  unsigned long now = millis();

  if (currentMode != MODE_HOME || isNightMode) return;
  if (greetingActive() && lifePos < 0) return;

  updateVisitor();

  if (lifePos >= 0) {
    if ((long)(now - lifeStepAt) >= 0) {
      lifePos++;
      if (lifePos >= lifeLen) {
        lifePos = -1;
        lifeLen = 0;
        setTecoInNest(&bigIdleFrames[0]);
        resetIdleBlink();
        nextLifeAt = now + random(20000, 45000);
      } else {
        lifeApply();
      }
    }
    return;
  }

  if (visKind != VIS_NONE || askTile >= 0) return;
  if ((long)(now - nextLifeAt) < 0) return;

  // Elegir la siguiente conducta segun el estado
  int r = random(0, 100);
  if (energy <= NEED_LOW) {
    buildBostezo(); lifeStart();
  } else if (r < 30) {
    buildPaseo(); lifeStart();
  } else if (r < 50) {
    buildSaltito(); lifeStart();
  } else if (r < 68) {
    buildSaludo(); lifeStart();
  } else if (r < 88) {
    startVisit(VIS_BUTTERFLY);
  } else {
    startVisit(VIS_BIRD);
  }
}

// modos

void startFeedMode() {
  stopLife();
  endGreeting();
  currentMode = MODE_FEED;
  feedReturnAt = millis() + 1500;

  hunger++;
  markProgress();
  if (hunger > 9) hunger = 9;

  // Usar la animacion de cada alimento o, si no tiene, la de la mazorca
  eatAnimFrames = bigEatFrames; eatAnimCount = EAT_FRAME_COUNT; eatFrameMs = EAT_FRAME_MS;
  for (int i = 0; i < EAT_ANIM_COUNT; i++)
    if (EAT_ANIMS[i].word == eatFoodWord) {
      eatAnimFrames = EAT_ANIMS[i].frames; eatAnimCount = EAT_ANIMS[i].count;
      eatFrameMs = 1500 / (eatAnimCount);   // 4 cuadros en el tiempo de comer
    }
  eatFrame = 0;
  lastEatFrameAt = millis();

  resetIdleBlink();

  renderPetEat();
  drawActionBar();

  // comer sin DFPlayer
  soundEat();

  Serial.print("COMER -> hambre = ");
  Serial.println(hunger);
}

void updateFeedMode() {
  if (backPressed()) {
    soundBack();
    redrawHome();
    return;
  }

  unsigned long now = millis();

  // 0 -> 1 -> 2 y luego masticar alternando 1 y 2
  if (now - lastEatFrameAt >= eatFrameMs) {
    lastEatFrameAt = now;
    eatFrame = (eatFrame < eatAnimCount - 1) ? eatFrame + 1 : eatAnimCount - 2;
    renderPetEat();
  }

  if ((long)(millis() - feedReturnAt) >= 0) {
    Serial.println("COMER -> regreso a HOME");
    eatFoodWord = -1;
    redrawHome();
    clearSpeechBubble();   // borrar el globo de la comida
  }
}

void startDrinkMode() {
  stopLife();
  endGreeting();
  currentMode = MODE_DRINK;
  drinkReturnAt = millis() + 1500;

  energy++;
  markProgress();
  if (energy > 9) energy = 9;

  waterCount++;

  drinkFrame = 0;
  lastDrinkFrameAt = millis();

  resetIdleBlink();

  renderPetDrink();
  drawActionBar();

  // beber sin bloquear la animacion
  dfPlayFolder(3, 2);   // sonido de agua (03/002)

  Serial.print("TOMAR AGUA / ja -> energia = ");
  Serial.print(energy);
  Serial.print(" | veces agua = ");
  Serial.println(waterCount);
}

void updateDrinkMode() {
  if (backPressed()) {
    soundBack();
    redrawHome();
    return;
  }

  unsigned long now = millis();

  if (now - lastDrinkFrameAt >= DRINK_FRAME_MS) {
    lastDrinkFrameAt = now;

    if (drinkFrame < 2) {
      drinkFrame++;
      renderPetDrink();
      drawActionBar();

      Serial.print("AGUA frame -> ");
      Serial.println(drinkFrame);
    }
  }

  if ((long)(millis() - drinkReturnAt) >= 0) {
    Serial.println("AGUA -> regreso a HOME");
    redrawHome();
  }
}

// Recuperar 1 de energia cada SLEEP_ENERGY_MS mientras duerme
const unsigned long SLEEP_ENERGY_MS = 15000;
const unsigned long SLEEP_MIN_MS    = 8000;

unsigned long sleepStartedAt = 0;
unsigned long lastEnergyAt = 0;

void startSleepMode() {
  currentMode = MODE_SLEEP;

  sleepFrame = 0;
  lastSleepFrameAt = millis();
  sleepStartedAt = millis();
  lastEnergyAt = millis();

  resetIdleBlink();

  soundSleep();

  // Componer el buho dormido sobre el fondo de noche y anochecer
  stopLife();
  endGreeting();
  if (clockNight()) sleptAtNight = true;
  tecoSpr = &bigSleepFrames[0]; tecoX = NEST_CX; tecoFoot = NEST_FOOT; tecoFlip = false; tecoInNest = true;
  transitionBackground(true);

  Serial.print("DORMIR -> cae la noche, energia = ");
  Serial.println(energy);
}

void wakeUp() {
  currentMode = MODE_HOME;
  resetIdleBlink();
  tecoSpr = &bigIdleFrames[0]; tecoX = NEST_CX; tecoFoot = NEST_FOOT; tecoFlip = false; tecoInNest = true;

  if (clockNight()) {
    // Despertado de noche: mantener la noche sin volver a dormir
    nightAwakeUntil = millis() + NIGHT_AWAKE_MS;
    sleptAtNight = false;
    isNightMode = true;
    drawStaticScreen();
  } else {
    // Componer el buho despierto sobre el fondo de dia y amanecer
    transitionBackground(false);
  }

  Serial.print("DESPIERTA, energia = ");
  Serial.println(energy);
}

void updateSleepMode() {
  if (okPressed() || backPressed()) {
    soundBack();
    wakeUp();
    return;
  }

  unsigned long now = millis();

  if (now - lastEnergyAt >= SLEEP_ENERGY_MS) {
    lastEnergyAt = now;
    if (energy < 9) {
      energy++;
      markProgress();
      Serial.print("Durmiendo... energia = ");
      Serial.println(energy);
    }
  }

  if (energy >= 9 && now - sleepStartedAt >= SLEEP_MIN_MS && !clockNight()) {
    soundClick();
    wakeUp();
    return;
  }

  // Mostrar adormilado (cuadro 0) y luego respirar alternando 1 y 2
  unsigned long wait = (sleepFrame == 0) ? SLEEP_DROWSY_MS : SLEEP_FRAME_MS;

  if (now - lastSleepFrameAt >= wait) {
    lastSleepFrameAt = now;
    sleepFrame = (sleepFrame == 1) ? 2 : 1;
    renderPetSleep();
  }
}

void startPetMode() {
  stopLife();
  endGreeting();
  currentMode = MODE_PET;
  petReturnAt = millis() + 2400;   // tiempo para leer el globo

  petFrame = 0;
  lastPetFrameAt = millis();

  resetIdleBlink();

  renderPetPet();
  drawActionBar();

  // Agradecer con una frase en Kaqchikel
  const char* PET_KAQ[3] = { "matyox", "\xADutz!", "quicot" };
  const char* PET_ES[3]  = { "gracias", "\xADqu\x82 bien!", "feliz" };
  int k = random(0, 3);
  drawPetBubble(PET_KAQ[k], PET_ES[k]);

  if (fun < 9) { fun++; markProgress(); }
  Serial.println("ACARICIAR -> touch GPIO2");
  dfPlayFolder(AUDIO_FOLDER_TECO, 6 + random(0, 3));   // sonido de caricia (06/006 a 06/008)
}

void updatePetMode() {
  if (backPressed()) {
    soundBack();
    currentMode = MODE_HOME;
    clearPetBubble();
    redrawHome();
    return;
  }

  unsigned long now = millis();

  if (now - lastPetFrameAt >= PET_FRAME_MS) {
    lastPetFrameAt = now;

    if (petFrame < PET_FRAME_COUNT - 1) {
      petFrame++;
      renderPetPet();
      drawActionBar();
    }
  }

  if ((long)(millis() - petReturnAt) >= 0) {
    currentMode = MODE_HOME;
    clearPetBubble();   // borrar el globo antes de volver a la casa
    redrawHome();
  }
}

// Juego: la milpa

// audios (MicroSD del DFPlayer)
#define AUDIO_FOLDER_NUMEROS 2
#define AUDIO_FOLDER_MILPA   3
#define AUDIO_MILPA_SIEMBRA  1
#define AUDIO_MILPA_RIEGO    2
#define AUDIO_MILPA_PREGUNTA 3
#define AUDIO_MILPA_BIEN     4
#define AUDIO_MILPA_OTRAVEZ  5

// palabras en pantalla
const char* NUM_WORDS[6] = { "", "jun", "ka'i'", "oxi'", "kaji'", "wo'o'" };

// distribucion
#define MILPA_GROUND_Y   130   // inicio del pasto
#define MILPA_FOOT       142   // linea de apoyo de plantas y Teco
#define MILPA_PLOTS      5
#define MILPA_TECO_CX    28
#define MILPA_PANEL_X    6
#define MILPA_PANEL_Y    192
#define MILPA_PANEL_W    308
#define MILPA_PANEL_H    44
#define MILPA_SCENE_H    MILPA_PANEL_Y   // escena compuesta hasta el panel

const int PLOT_X[MILPA_PLOTS] = { 80, 134, 188, 242, 296 };
const int PLOT_HALF_W = 26;
const int PLOT_TOP = 50;   // alto de la zona redibujable de cada surco
const int PLOT_BOTTOM = MILPA_FOOT + 14;

// Obtener la etapa segun las veces regada (0..3)
const int STAGE_FOR_WATER[4] = { 0, 2, 4, 5 };

const unsigned long WILT_AFTER_MS   = 15000;   // sed sin atender -> marchitar
const unsigned long GROW_STEP_MS    = 350;   // etapa intermedia al crecer
const unsigned long TECO_MOOD_MS    = 1500;   // duracion de "bien" / "uy"

MilpaPhase milpaPhase = PH_SIEMBRA;

struct Plot {
  bool planted;
  bool harvested;
  int water;   // 0..3
  int stage;   // etapa dibujada (0..5)
  int pendingStage;   // 1 = sin crecer
  unsigned long growAt;
  bool thirsty;
  unsigned long thirstySince;
  unsigned long restUntil;
};
Plot plots[MILPA_PLOTS];

int milpaEarned = 0;
int milpaTarget = 3;   // numero pedido en la siembra
int milpaSel = 0;   // seleccion actual
int milpaHarvested = 0;
int milpaOptions[3];
unsigned long milpaPhaseAt = 0;

// gota cayendo
int dropPlot = -1;
int dropStep = 0;
unsigned long dropAt = 0;

// Teco pequeno
TecoMood tecoMood = MOOD_NORMAL;
int tecoMiniFrame = 0;
unsigned long tecoFrameAt = 0;
unsigned long tecoMoodUntil = 0;


// audio
unsigned long dfLaterAt = 0;   // audio pendiente (0 = ninguno)
uint8_t dfLaterFolder = 0, dfLaterTrack = 0;
void dfPlayFolderNow(uint8_t folder, uint8_t track);
void dfPlayFolder(uint8_t folder, uint8_t track) {
  dfLaterAt = 0;   // cancelar el audio pendiente
  dfPlayFolderNow(folder, track);
}
void dfPlayLater(uint8_t folder, uint8_t track, unsigned long ms) {
  dfLaterFolder = folder; dfLaterTrack = track; dfLaterAt = millis() + ms;
  if (dfLaterAt == 0) dfLaterAt = 1;
}
void updateDfLater() {
  if (dfLaterAt && (long)(millis() - dfLaterAt) >= 0) { dfLaterAt = 0; dfPlayFolderNow(dfLaterFolder, dfLaterTrack); }
}
void dfPlayFolderNow(uint8_t folder, uint8_t track) {
  if (!dfPlayerReady) return;
  sendDFCommand(0x0F, ((uint16_t)folder << 8) | track);
}

void playNumber(int n) { dfPlayFolder(AUDIO_FOLDER_NUMEROS, n); }
void playMilpa(int t)  { dfPlayFolder(AUDIO_FOLDER_MILPA, t); }

// Componer por franjas

struct DrawItem {
  const MiniSprite* spr;
  int x;
  int y;
};

#define MAX_DRAW_ITEMS 20
DrawItem drawItems[MAX_DRAW_ITEMS];
int drawCount = 0;

void addItem(const MiniSprite* s, int cx, int bottom) {
  if (drawCount >= MAX_DRAW_ITEMS) return;
  drawItems[drawCount].spr = s;
  drawItems[drawCount].x = cx - s->w / 2;
  drawItems[drawCount].y = bottom - s->h;
  drawCount++;
}

const MiniSprite* tecoMiniSprite() {
  // Mantener el sombrero de Teco en la milpa, tambien al festejar o equivocarse
  if (tecoMood == MOOD_BIEN) return &tecoMiniGranjeroBienFrames[tecoMiniFrame];
  if (tecoMood == MOOD_UY)   return &tecoMiniGranjeroUyFrames[tecoMiniFrame];
  return &tecoMiniGranjeroFrames[tecoMiniFrame];
}

const MiniSprite* plotSprite(int i) {
  Plot& p = plots[i];
  if (p.stage >= 3 && p.stage <= 4 && p.thirsty &&
      millis() - p.thirstySince > WILT_AFTER_MS) {
    return &milpaMarchitaFrames[0];
  }
  return &milpaPlantaFrames[p.stage];
}

void buildDrawList() {
  drawCount = 0;

  addItem(tecoMiniSprite(), MILPA_TECO_CX, MILPA_FOOT);

  for (int i = 0; i < MILPA_PLOTS; i++) {
    Plot& p = plots[i];
    int cx = PLOT_X[i];

    if (!p.planted || p.harvested) {
      addItem(&milpaSurcoFrames[0], cx, MILPA_FOOT);
      continue;
    }

    if (p.stage == 0) {
      addItem(&milpaSurcoFrames[0], cx, MILPA_FOOT);
      addItem(&milpaPlantaFrames[0], cx, MILPA_FOOT - 9);
    } else {
      addItem(plotSprite(i), cx, MILPA_FOOT);
    }

    int top = MILPA_FOOT - (p.stage == 0 ? 20 : plotSprite(i)->h);

    if (p.thirsty && dropPlot != i) {
      addItem(&milpaGotaFrames[0], cx, top - 4);
    }

    if (dropPlot == i) {
      if (dropStep == 0) addItem(&milpaGotaFrames[1], cx, top - 14);
      else if (dropStep == 1) addItem(&milpaGotaFrames[1], cx, top - 2);
      else addItem(&milpaGotaFrames[2], cx, MILPA_FOOT - 2);
    }
  }
}

uint16_t milpaBgPixel(int x, int y) {
  if (y >= MILPA_GROUND_Y) {
    int ty = y - MILPA_GROUND_Y;
    if (ty >= TIERRA_H) {
      ty = TIERRA_DIRT_TOP + (ty - TIERRA_H) % (TIERRA_H - TIERRA_DIRT_TOP);
    }
    uint16_t c = pgm_read_word(&milpaTierra[ty * TIERRA_W + (x % TIERRA_W)]);
    if (c != MINI_TRANSPARENT) return c;
  }
  return bgPixel(x, y);
}

// Dibujar la flecha bajo el surco seleccionado
void drawSelectorIntoBand(int bx, int by, int w, int rows) {
  bool onPlot = (milpaPhase == PH_SIEMBRA && milpaSel < MILPA_PLOTS) ||
                milpaPhase == PH_RIEGO || milpaPhase == PH_COSECHA;
  if (!onPlot) return;

  int cx = PLOT_X[milpaSel];
  int tipY = MILPA_FOOT + 3;

  for (int r = 0; r < 8; r++) {
    int y = tipY + r;
    if (y < by || y >= by + rows) continue;
    for (int dx = -r - 1; dx <= r + 1; dx++) {
      int x = cx + dx;
      if (x < bx || x >= bx + w) continue;
      bool edge = (dx == -r - 1 || dx == r + 1 || r == 7);
      bandBuffer[(y - by) * w + (x - bx)] = edge ? FRAME_DARK : SELECT_COLOR;
    }
  }
}

void renderMilpaRegion(int x0, int y0, int w, int h) {
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > FONDO_W) w = FONDO_W - x0;
  if (y0 + h > MILPA_SCENE_H) h = MILPA_SCENE_H - y0;
  if (w <= 0 || h <= 0) return;

  buildDrawList();

  int maxRows = (FONDO_W * BAND_H) / w;
  if (maxRows > h) maxRows = h;

  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);

    for (int r = 0; r < rows; r++) {
      for (int x = 0; x < w; x++) {
        bandBuffer[r * w + x] = milpaBgPixel(x0 + x, by + r);
      }
    }

    for (int k = 0; k < drawCount; k++) {
      const MiniSprite* s = drawItems[k].spr;
      int sx = drawItems[k].x;
      int sy = drawItems[k].y;

      int ys = imax(sy, by), ye = imin(sy + s->h, by + rows);
      int xs = imax(sx, x0), xe = imin(sx + s->w, x0 + w);
      if (ys >= ye || xs >= xe) continue;

      for (int y = ys; y < ye; y++) {
        for (int x = xs; x < xe; x++) {
          uint16_t c = miniPx(s, (y - sy) * s->w + (x - sx));
          if (c != MINI_TRANSPARENT) bandBuffer[(y - by) * w + (x - x0)] = c;
        }
      }
    }

    drawSelectorIntoBand(x0, by, w, rows);
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void renderPlot(int i) {
  renderMilpaRegion(PLOT_X[i] - PLOT_HALF_W - 1, PLOT_TOP, PLOT_HALF_W * 2 + 3, PLOT_BOTTOM - PLOT_TOP);
}

void renderTecoMini() {
  renderMilpaRegion(0, MILPA_FOOT - 60, 56, 62);
}

// Dibujar la tierra bajo el panel (solo en redibujado completo)
void renderDirtBelowPanel() {
  for (int by = MILPA_SCENE_H; by < FONDO_H; by += BAND_H) {
    int rows = imin(BAND_H, FONDO_H - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < FONDO_W; x++)
        bandBuffer[r * FONDO_W + x] = milpaBgPixel(x, by + r);
    tft.drawRGBBitmap(0, by, bandBuffer, FONDO_W, rows);
  }
}

// Dibujar un sprite sobre un color solido (para el panel)
void drawSpriteOnColor(const MiniSprite* s, int x, int y, uint16_t bg) {
  int rowsPerChunk = (FONDO_W * BAND_H) / s->w;
  for (int r0 = 0; r0 < s->h; r0 += rowsPerChunk) {
    int rows = imin(rowsPerChunk, s->h - r0);
    for (int i = 0; i < rows * s->w; i++) {
      uint16_t c = miniPx(s, r0 * s->w + i);
      bandBuffer[i] = (c == MINI_TRANSPARENT) ? bg : c;
    }
    tft.drawRGBBitmap(x, y + r0, bandBuffer, s->w, rows);
  }
}

// Dibujar el panel inferior
uint16_t gTextColor = 0;
void setTxtColor(uint16_t c) { gTextColor = c; }

// Dibujar la fuente pixel a escala
int textScale(int size) { return size < 1 ? 1 : (size > 4 ? 4 : size); }

int textWidth(const char* s, int size) {
  const TecoFont* f = &tecoPixFont;
  int w = 0;
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < f->first) c = ' ';
    w += f->glyphs[c - f->first].adv;
  }
  return (w > 0 ? w - 1 : 0) * textScale(size);
}

void drawText(const char* s, int x, int y, int size) {
  const TecoFont* f = &tecoPixFont;
  int k = textScale(size);
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < f->first) c = ' ';
    const TecoGlyph& g = f->glyphs[c - f->first];
    for (int r = 0; r < g.h; r++) {
      int run = -1;
      for (int col = 0; col <= g.w; col++) {
        bool on = false;
        if (col < g.w) { int i = r * g.w + col; on = f->bits[g.offset + i / 8] & (0x80 >> (i % 8)); }
        if (on && run < 0) run = col;
        if (!on && run >= 0) {
          tft.fillRect(x + (g.xoff + run) * k, y + (g.yoff + r) * k, (col - run) * k, k, gTextColor);
          run = -1;
        }
      }
    }
    x += g.adv * k;
  }
}

// Pieza de 9 partes: esquinas fijas, bordes y centro repetidos
static inline bool nineColor(const MiniSprite* s, int dx, int dy, int dw, int dh, int c, uint16_t* out) {
  int sx, sy;
  if (dx < c) sx = dx; else if (dx >= dw - c) sx = s->w - (dw - dx); else sx = c + (dx - c) % (s->w - 2 * c);
  if (dy < c) sy = dy; else if (dy >= dh - c) sy = s->h - (dh - dy); else sy = c + (dy - c) % (s->h - 2 * c);
  uint16_t col = miniPx(s, sy * s->w + sx);
  if (col == MINI_TRANSPARENT) return false;
  *out = col; return true;
}

// Version para el compositor por franjas
void hbNine(int bx, int by, int bw, int rows, const MiniSprite* s, int x0, int y0, int x1, int y1, int c, int k) {
  int dw = (x1 - x0) / k, dh = (y1 - y0) / k;
  for (int y = imax(y0, by); y < imin(y0 + dh * k, by + rows); y++)
    for (int x = imax(x0, bx); x < imin(x0 + dw * k, bx + bw); x++) {
      uint16_t col;
      if (nineColor(s, (x - x0) / k, (y - y0) / k, dw, dh, c, &col)) bandBuffer[(y - by) * bw + (x - bx)] = col;
    }
}

// Version directa en pantalla (solo pixeles visibles)
void tftNine(const MiniSprite* s, int x0, int y0, int x1, int y1, int c, int k) {
  int dw = (x1 - x0) / k, dh = (y1 - y0) / k;
  for (int dy = 0; dy < dh; dy++) {
    int run = -1; uint16_t runCol = 0;
    for (int dx = 0; dx <= dw; dx++) {
      uint16_t col; bool on = dx < dw && nineColor(s, dx, dy, dw, dh, c, &col);
      if (run >= 0 && (!on || col != runCol)) { tft.fillRect(x0 + run * k, y0 + dy * k, (dx - run) * k, k, runCol); run = -1; }
      if (on && run < 0) { run = dx; runCol = col; }
    }
  }
}

// Dibujar un sprite pequeno directo en pantalla, a escala k
void tftSprite(const MiniSprite* s, int x0, int y0, int k) {
  for (int y = 0; y < s->h; y++)
    for (int x = 0; x < s->w; x++) {
      uint16_t col = miniPx(s, y * s->w + x);
      if (col != MINI_TRANSPARENT) tft.fillRect(x0 + x * k, y0 + y * k, k, k, col);
    }
}

void printCenteredW(const char* txt, int cx, int y, int size, int maxW) {
  while (size > 1 && textWidth(txt, size) > maxW) size--;
  if (textWidth(txt, size) <= maxW) { drawText(txt, cx - textWidth(txt, size) / 2, y, size); return; }
  char buf[64];
  int n = strlen(txt); if (n > 60) n = 60;
  while (n > 0) {
    memcpy(buf, txt, n); strcpy(buf + n, "..");
    if (textWidth(buf, size) <= maxW) break;
    n--;
  }
  drawText(buf, cx - textWidth(buf, size) / 2, y, size);
}

void printCentered(const char* txt, int cx, int y, int size) {
  printCenteredW(txt, cx, y, size, 312);
}

void drawButton(int x, int y, int w, int h, const char* txt, bool selected) {
  tft.fillRoundRect(x, y, w, h, 6, selected ? SELECT_COLOR : PANEL_COLOR);
  tft.drawRoundRect(x, y, w, h, 6, selected ? FRAME_DARK : ICON_BORDER);
  if (selected) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, FRAME_DARK);
  setTxtColor(COLOR_TEXT);
  printCentered(txt, x + w / 2, y + h / 2 - 7, 2);
}

void drawMilpaPanel() {
  int x = MILPA_PANEL_X, y = MILPA_PANEL_Y;
  tft.fillRoundRect(x, y, MILPA_PANEL_W, MILPA_PANEL_H, 8, PANEL_COLOR);
  tft.drawRoundRect(x, y, MILPA_PANEL_W, MILPA_PANEL_H, 8, ICON_BORDER);
  setTxtColor(COLOR_TEXT);

  if (milpaPhase == PH_SIEMBRA) {
    drawText("SIEMBRA:", x + 10, y + 5, 1);
    drawText(NUM_WORDS[milpaTarget], x + 10, y + 18, 2);
    drawButton(x + 206, y + 6, 94, 32, "LISTO", milpaSel == MILPA_PLOTS);

  } else if (milpaPhase == PH_RIEGO) {
    drawText("Riega las que tienen gotita:", x + 10, y + 7, 1);
    drawText("ponte encima y acerca el im\xA0n", x + 10, y + 23, 1);
    drawSpriteOnColor(&milpaGotaFrames[1], x + 284, y + 13, PANEL_COLOR);

  } else if (milpaPhase == PH_COSECHA || milpaPhase == PH_FIN) {
    char fin[32]; snprintf(fin, sizeof(fin), "\xADUtz! +%d pwaq", milpaEarned);
    drawText(milpaPhase == PH_FIN ? fin : "COSECHA con OK", x + 10, y + 5, 1);
    for (int i = 0; i < milpaHarvested; i++) {
      drawSpriteOnColor(&milpaMazorcaFrames[0], x + 10 + i * 22, y + 20, PANEL_COLOR);
    }
    if (milpaHarvested > 0) {
      drawText(NUM_WORDS[milpaHarvested], x + 200, y + 19, 2);
    }

  } else if (milpaPhase == PH_PREGUNTA) {
    for (int i = 0; i < 3; i++) {
      drawButton(x + 8 + i * 100, y + 6, 92, 32, NUM_WORDS[milpaOptions[i]], milpaSel == i);
    }
  }
}

void renderMilpaAll() {
  renderMilpaRegion(0, 0, FONDO_W, MILPA_SCENE_H);
  renderDirtBelowPanel();
  drawMilpaPanel();
}

// Teco pequeno
void setTecoMood(TecoMood m) {
  tecoMood = m;
  tecoMiniFrame = 0;
  tecoFrameAt = millis();
  tecoMoodUntil = millis() + TECO_MOOD_MS;
  renderTecoMini();
}

void updateTecoMini() {
  unsigned long now = millis();

  if (tecoMood == MOOD_NORMAL) {
    // Granjero: abrir ojos 2.5 s y parpadear 180 ms
    unsigned long wait = (tecoMiniFrame == 0) ? 2500 : 180;
    if (now - tecoFrameAt >= wait) {
      tecoFrameAt = now;
      tecoMiniFrame = (tecoMiniFrame == 0) ? 1 : 0;
      renderTecoMini();
    }
    return;
  }

  if ((long)(now - tecoMoodUntil) >= 0) {
    tecoMood = MOOD_NORMAL;
    tecoMiniFrame = 0;
    tecoFrameAt = now;
    renderTecoMini();
    return;
  }

  if (now - tecoFrameAt >= 280 && tecoMiniFrame < 2) {
    tecoFrameAt = now;
    tecoMiniFrame++;
    renderTecoMini();
  }
}

// fases
void milpaReplayAudio() {
  if (milpaPhase == PH_SIEMBRA) playNumber(milpaTarget);
  else if (milpaPhase == PH_RIEGO) playMilpa(AUDIO_MILPA_RIEGO);
  else if (milpaPhase == PH_PREGUNTA) playMilpa(AUDIO_MILPA_PREGUNTA);
  else if (milpaPhase == PH_COSECHA && milpaHarvested > 0) playNumber(milpaHarvested);
}

void startMilpaPhase(MilpaPhase ph) {
  milpaPhase = ph;
  if (ph == PH_PREGUNTA) markQuestionStart();
  milpaPhaseAt = millis();
  milpaSel = 0;

  if (ph == PH_RIEGO) {
    unsigned long now = millis();
    int k = 0;
    for (int i = 0; i < MILPA_PLOTS; i++) {
      plots[i].thirsty = false;
      plots[i].restUntil = now + 600 + k * 900;
      if (plots[i].planted) k++;
    }
    for (int i = 0; i < MILPA_PLOTS; i++) if (plots[i].planted) { milpaSel = i; break; }
    playMilpa(AUDIO_MILPA_RIEGO);

  } else if (ph == PH_COSECHA) {
    for (int i = 0; i < MILPA_PLOTS; i++) if (plots[i].planted) { milpaSel = i; break; }

  } else if (ph == PH_PREGUNTA) {
    // Generar opciones: la correcta y 2 distintas, en orden aleatorio
    int correct = milpaHarvested;
    int a = correct, b = correct;
    if (lvlMilpa >= 3) {
      // Niveles altos: usar numeros vecinos como opciones
      a = (correct > 1) ? correct - 1 : correct + 2;
      b = (correct < 5) ? correct + 1 : correct - 2;   // numeros de la milpa del 1 al 5
    } else {
      while (a == correct) a = random(1, 6);
      while (b == correct || b == a) b = random(1, 6);
    }
    int pos = random(0, 3);
    int others[2] = { a, b }, o = 0;
    for (int i = 0; i < 3; i++) milpaOptions[i] = (i == pos) ? correct : others[o++];
    milpaSel = 1;
    playMilpa(AUDIO_MILPA_PREGUNTA);

  } else if (ph == PH_FIN) {
    milpaGames++;
    milpaEarned = (gameMistakes == 0) ? 4 : 3;   // monedas por la cosecha
    coins += milpaEarned;
    fun = imin(9, fun + 3);
    aciertosHoy++;
    markProgress();
    setTecoMood(MOOD_BIEN);
    tecoMoodUntil = millis() + 2500;
    playMilpa(AUDIO_MILPA_BIEN);
  }

  renderMilpaAll();
}

void startMilpa() {
  currentMode = MODE_MILPA;

  for (int i = 0; i < MILPA_PLOTS; i++) {
    plots[i] = { false, false, 0, 0, -1, 0, false, 0, 0 };
  }
  milpaHarvested = 0;
  // Nivel 1: 2 o 3 semillas; nivel 3+: de 2 a 5, sin repetir el numero anterior
  { int top = imin(5, 2 + lvlMilpa), vals[4], n = 0; for (int v = 2; v <= top; v++) vals[n++] = v; milpaTarget = bagNext(BAG_MILPA, vals, n); }
  gameReset();
  dropPlot = -1;

  tecoMood = MOOD_NORMAL;
  tecoMiniFrame = 0;
  tecoFrameAt = millis();

  startMilpaPhase(PH_SIEMBRA);
  playMilpa(AUDIO_MILPA_SIEMBRA);   // "Siembra las semillas"...
  dfPlayLater(AUDIO_FOLDER_NUMEROS, milpaTarget, 1600);   // ...y luego el numero

  Serial.print("MILPA -> siembra ");
  Serial.println(milpaTarget);
}

void exitMilpa(bool feedTeco) {
  drawStaticScreen();

  if (feedTeco && milpaHarvested > 0) {
    // startFeedMode ya suma 1
    hunger += milpaHarvested - 1;
    markProgress();
    if (hunger > 9) hunger = 9;
    startFeedMode();
  } else {
    redrawHome();
  }
}

void milpaSuccess() {
  aciertosHoy++;
  setTecoMood(MOOD_BIEN);
  soundClick();
  playMilpa(AUDIO_MILPA_BIEN);
}

void milpaFail() {
  gameMistakes++;
  setTecoMood(MOOD_UY);
  soundBack();
}

// Llamar desde el sensor de iman en la milpa
void milpaWater() {
  if (milpaPhase != PH_RIEGO || dropPlot >= 0) return;

  Plot& p = plots[milpaSel];
  if (!p.planted || !p.thirsty || p.pendingStage >= 0) {
    milpaFail();
    return;
  }

  p.thirsty = false;
  dropPlot = milpaSel;
  dropStep = 0;
  dropAt = millis();
  playMilpa(AUDIO_MILPA_RIEGO);   // sonido de agua por planta regada
  renderPlot(milpaSel);
}

void moveMilpaSel(int dir, int count, bool plotsOnly) {
  int old = milpaSel;
  // En riego y cosecha, saltar a los surcos sembrados
  for (int tries = 0; tries < count; tries++) {
    milpaSel = (milpaSel + dir + count) % count;
    if (!plotsOnly || milpaSel >= MILPA_PLOTS) break;
    if (plots[milpaSel].planted && !plots[milpaSel].harvested) break;
  }
  if (old == milpaSel) return;

  soundClick();
  if (old < MILPA_PLOTS && milpaPhase != PH_PREGUNTA) renderPlot(old);
  if (milpaSel < MILPA_PLOTS && milpaPhase != PH_PREGUNTA) renderPlot(milpaSel);
  if (milpaPhase == PH_PREGUNTA || old == MILPA_PLOTS || milpaSel == MILPA_PLOTS) drawMilpaPanel();
  if (milpaPhase == PH_PREGUNTA) playNumber(milpaOptions[milpaSel]);
}

void updateMilpaSiembra() {
  if (leftPressed())  moveMilpaSel(-1, MILPA_PLOTS + 1, false);
  if (rightPressed()) moveMilpaSel(+1, MILPA_PLOTS + 1, false);

  if (okPressed()) {
    if (milpaSel == MILPA_PLOTS) {
      int count = 0;
      for (int i = 0; i < MILPA_PLOTS; i++) if (plots[i].planted) count++;

      if (count == milpaTarget) {
        milpaSuccess();
        startMilpaPhase(PH_RIEGO);
      } else {
        milpaFail();
        playNumber(milpaTarget);
      }
    } else {
      plots[milpaSel].planted = !plots[milpaSel].planted;
      soundClick();
      renderPlot(milpaSel);
    }
  }
}

void updateMilpaRiego() {
  unsigned long now = millis();

  if (leftPressed())  moveMilpaSel(-1, MILPA_PLOTS, true);
  if (rightPressed()) moveMilpaSel(+1, MILPA_PLOTS, true);
  if (okPressed()) milpaWater();   // alternativa sin iman

  // Animar la gota
  if (dropPlot >= 0) {
    unsigned long stepMs = (dropStep < 2) ? 150 : 220;
    if (now - dropAt >= stepMs) {
      dropAt = now;
      dropStep++;
      if (dropStep > 2) {
        Plot& p = plots[dropPlot];
        p.water++;
        int target = STAGE_FOR_WATER[p.water];
        if (target - 1 > p.stage) {
          p.stage = target - 1;
          p.pendingStage = target;
          p.growAt = now + GROW_STEP_MS;
        } else {
          p.stage = target;
        }
        p.restUntil = now + random(1500, 3000);
        int done = dropPlot;
        dropPlot = -1;
        renderPlot(done);
      } else {
        renderPlot(dropPlot);
      }
    }
  }

  bool allDone = true;

  for (int i = 0; i < MILPA_PLOTS; i++) {
    Plot& p = plots[i];
    if (!p.planted) continue;

    if (p.pendingStage >= 0 && (long)(now - p.growAt) >= 0) {
      p.stage = p.pendingStage;
      p.pendingStage = -1;
      renderPlot(i);
    }

    if (p.water < 3) {
      allDone = false;
      if (!p.thirsty && dropPlot != i && p.pendingStage < 0 && (long)(now - p.restUntil) >= 0) {
        p.thirsty = true;
        p.thirstySince = now;
        renderPlot(i);
      }
      // Redibujar una vez al marchitarse
      if (p.thirsty && p.stage >= 3 && now - p.thirstySince > WILT_AFTER_MS &&
          now - p.thirstySince < WILT_AFTER_MS + 40) {
        renderPlot(i);
      }
    } else if (p.pendingStage >= 0) {
      allDone = false;
    }
  }

  if (allDone && dropPlot < 0) {
    milpaSuccess();
    startMilpaPhase(PH_COSECHA);
  }
}

unsigned long milpaAskAt = 0;

void updateMilpaCosecha() {
  if (milpaAskAt) {
    if ((long)(millis() - milpaAskAt) >= 0) { milpaAskAt = 0; startMilpaPhase(PH_PREGUNTA); }
    return;
  }
  if (leftPressed())  moveMilpaSel(-1, MILPA_PLOTS, true);
  if (rightPressed()) moveMilpaSel(+1, MILPA_PLOTS, true);

  if (okPressed()) {
    Plot& p = plots[milpaSel];
    if (p.planted && !p.harvested) {
      p.harvested = true;
      milpaHarvested++;
      playNumber(milpaHarvested);
      soundEat();
      renderPlot(milpaSel);
      drawMilpaPanel();

      bool left = false;
      for (int i = 0; i < MILPA_PLOTS; i++) {
        if (plots[i].planted && !plots[i].harvested) { left = true; break; }
      }

      if (left) {
        moveMilpaSel(+1, MILPA_PLOTS, true);
      } else {
        milpaAskAt = millis() + 900;   // esperar el ultimo numero
      }
    }
  }
}

void updateMilpaPregunta() {
  if (leftPressed())  moveMilpaSel(-1, 3, false);
  if (rightPressed()) moveMilpaSel(+1, 3, false);

  if (okPressed()) {
    logAnswer("milpa", NUM_WORDS[milpaHarvested], NUM_WORDS[milpaOptions[milpaSel]], milpaOptions[milpaSel] == milpaHarvested);
    if (milpaOptions[milpaSel] == milpaHarvested) {
      adaptLevel(&lvlMilpa);
      startMilpaPhase(PH_FIN);
    } else {
      milpaFail();
      playMilpa(AUDIO_MILPA_OTRAVEZ);
    }
  }
}

void updateMilpaMode() {
  if (backPressed()) {
    soundBack();
    exitMilpa(false);
    return;
  }

  if (milpaPhase == PH_SIEMBRA) updateMilpaSiembra();
  else if (milpaPhase == PH_RIEGO) updateMilpaRiego();
  else if (milpaPhase == PH_COSECHA) updateMilpaCosecha();
  else if (milpaPhase == PH_PREGUNTA) updateMilpaPregunta();
  else if (milpaPhase == PH_FIN) {
    if (millis() - milpaPhaseAt > 2800) {
      exitMilpa(true);
      return;
    }
  }

  if (currentMode == MODE_MILPA) updateTecoMini();
}

// Juego: el telar

// audios
#define AUDIO_FOLDER_COLORES 4
#define AUDIO_FOLDER_TELAR   5
#define AUDIO_TELAR_MIRA     1
#define AUDIO_TELAR_TURNO    2
#define AUDIO_TELAR_HUIPIL   3

// nombres en pantalla
const char* COLOR_WORDS[5] = { "k\x84q", "q'\x84n", "r\x84x", "s\x84q", "q'eq" };

const uint16_t TELAR_COLORS[5] = {
  RGB565(200, 40, 40),   // kaq
  RGB565(240, 200, 40),   // q'an
  RGB565(70, 160, 60),   // rax
  RGB565(245, 245, 240),   // saq
  RGB565(30, 30, 40)   // q'eq
};
const int TELAR_TONES[5] = { 523, 659, 784, 880, 392 };

#define TELAR_BAND_BG    RGB565(176, 140, 100)
#define TELAR_BAND_LINE  RGB565(120, 90, 60)
#define TELAR_WARP       RGB565(235, 225, 200)

// distribucion
#define TELAR_GROUND_Y  170
#define TELAR_X0        108
#define TELAR_X1        311
#define TELAR_TOP       20
#define TELAR_BOT       148
#define TELAR_BAND_H    24
#define TELAR_BANDS     4
#define TELAR_MAX_LEN   9
#define TELAR_TECO_X    6
#define TELAR_TECO_FOOT 180
#define TELAR_LABEL_X   8
#define TELAR_LABEL_Y   8
#define TELAR_LABEL_W   92
#define TELAR_LABEL_H   24

// Tiempos de la secuencia: mas rapidos en niveles altos
#define TELAR_SHOW_ON_MS  ((unsigned long)(720 - 70 * lvlTelar))
#define TELAR_SHOW_OFF_MS ((unsigned long)(280 - 30 * lvlTelar))

TelarPhase telarPhase = TL_SHOW;
int telarSeq[TELAR_MAX_LEN];
int telarLen = 2;
int telarPos = 0;   // color mostrado en la secuencia
bool telarGlowOn = false;
unsigned long telarAt = 0;
int telarSel = 0;
bool telarBandMiss = false;   // error en esta franja
int telarEarned = 0;
int telarBandsDone = 0;
int telarBandColors[TELAR_BANDS][TELAR_MAX_LEN];
int telarBandLen[TELAR_BANDS];
int telarTecoFrame = 0;
unsigned long telarTecoAt = 0;

// Dibujar dentro de la franja (bandBuffer)

void bandRect(int bx, int by, int bw, int rows, int x0, int y0, int x1, int y1, uint16_t c) {
  int xs = imax(x0, bx), xe = imin(x1, bx + bw);
  int ys = imax(y0, by), ye = imin(y1, by + rows);
  for (int y = ys; y < ye; y++)
    for (int x = xs; x < xe; x++)
      bandBuffer[(y - by) * bw + (x - bx)] = c;
}

void bandSprite(int bx, int by, int bw, int rows, const MiniSprite* s, int sx, int sy) {
  int xs = imax(sx, bx), xe = imin(sx + s->w, bx + bw);
  int ys = imax(sy, by), ye = imin(sy + s->h, by + rows);
  for (int y = ys; y < ye; y++)
    for (int x = xs; x < xe; x++) {
      uint16_t c = miniPx(s, (y - sy) * s->w + (x - sx));
      if (c != MINI_TRANSPARENT) bandBuffer[(y - by) * bw + (x - bx)] = c;
    }
}

// Pintar un motivo de un solo color (usar solo su forma)
void bandMotif(int bx, int by, int bw, int rows, int motif, int cx, int cy, uint16_t color) {
  const MiniSprite* s = &telarMotivoFrames[motif];
  int sx = cx - s->w / 2, sy = cy - s->h / 2;
  int xs = imax(sx, bx), xe = imin(sx + s->w, bx + bw);
  int ys = imax(sy, by), ye = imin(sy + s->h, by + rows);
  for (int y = ys; y < ye; y++)
    for (int x = xs; x < xe; x++) {
      uint16_t c = miniPx(s, (y - sy) * s->w + (x - sx));
      if (c != MINI_TRANSPARENT) bandBuffer[(y - by) * bw + (x - bx)] = color;
    }
}

uint16_t telarBgPixel(int x, int y) {
  if (y >= TELAR_GROUND_Y) {
    int ty = y - TELAR_GROUND_Y;
    if (ty >= TIERRA_H) ty = TIERRA_DIRT_TOP + (ty - TIERRA_H) % (TIERRA_H - TIERRA_DIRT_TOP);
    uint16_t c = pgm_read_word(&milpaTierra[ty * TIERRA_W + (x % TIERRA_W)]);
    if (c != MINI_TRANSPARENT) return c;
  }
  return bgPixel(x, y);
}

// Dibujar una franja: fondo y motivos tejidos hasta cells
void drawTelarBand(int bx, int by, int bw, int rows, int band, int len, const int* colors, int cells) {
  int y1 = TELAR_BOT + 6 - band * TELAR_BAND_H;
  int y0 = y1 - TELAR_BAND_H + 2;
  int inner = (TELAR_X1 - TELAR_X0 - 16);
  int motif = band % 6;

  for (int j = 0; j < cells; j++) {
    int x0 = TELAR_X0 + 8 + (j * inner) / len;
    int x1 = TELAR_X0 + 8 + ((j + 1) * inner) / len;
    bandRect(bx, by, bw, rows, x0, y0, x1, y1, TELAR_BAND_BG);
    bandMotif(bx, by, bw, rows, motif, (x0 + x1) / 2, (y0 + y1) / 2, TELAR_COLORS[colors[j]]);
  }
  if (cells > 0) {
    int xEnd = TELAR_X0 + 8 + (cells * inner) / len;
    bandRect(bx, by, bw, rows, TELAR_X0 + 8, y0, xEnd, y0 + 1, TELAR_BAND_LINE);
  }
}

// Dibujar el huipil final con las franjas tejidas
void drawHuipilIntoBand(int bx, int by, int bw, int rows, int hx, int hy) {
  const MiniSprite* s = &telarHuipilFrames[0];
  int xs = imax(hx, bx), xe = imin(hx + s->w, bx + bw);
  int ys = imax(hy, by), ye = imin(hy + s->h, by + rows);

  for (int y = ys; y < ye; y++) {
    int ly = y - hy;
    int band = (ly - 34) / 16;
    bool inBand = ly >= 34 && band < TELAR_BANDS && band < telarBandsDone && ((ly - 34) % 16) < 14;

    for (int x = xs; x < xe; x++) {
      uint16_t c = miniPx(s, ly * s->w + (x - hx));
      if (c == MINI_TRANSPARENT) continue;

      // Pintar el tejido en la parte crema del huipil
      bool cream = ((c >> 11) >= 24) && (((c >> 5) & 63) >= 48) && ((c & 31) >= 20);
      if (inBand && cream) c = TELAR_BAND_BG;
      bandBuffer[(y - by) * bw + (x - bx)] = c;
    }
  }

  // Dibujar motivos solo sobre la parte crema
  for (int b = 0; b < telarBandsDone; b++) {
    int n = telarBandLen[b];
    for (int j = 0; j < n; j++) {
      int cx = hx + 26 + ((2 * j + 1) * 90) / (2 * n);
      int cy = hy + 34 + b * 16 + 7;
      bandMotif(bx, by, bw, rows, b % 6, cx, cy, TELAR_COLORS[telarBandColors[b][j]]);
    }
  }
}

const MiniSprite* telarTecoSprite() {
  if (tecoMood == MOOD_BIEN) return &tecoMiniBienFrames[tecoMiniFrame];
  if (tecoMood == MOOD_UY)   return &tecoMiniUyFrames[tecoMiniFrame];
  if (telarPhase == TL_FINAL) return &tecoMiniBienFrames[1];
  return &telarTecoFrames[telarTecoFrame];
}

void renderTelarRegion(int x0, int y0, int w, int h) {
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > FONDO_W) w = FONDO_W - x0;
  if (y0 + h > MILPA_PANEL_Y) h = MILPA_PANEL_Y - y0;
  if (w <= 0 || h <= 0) return;

  int maxRows = (FONDO_W * BAND_H) / w;
  if (maxRows > h) maxRows = h;

  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);

    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++)
        bandBuffer[r * w + x] = telarBgPixel(x0 + x, by + r);

    if (telarPhase == TL_FINAL) {
      drawHuipilIntoBand(x0, by, w, rows, 110, 40);
    } else {
      // Dibujar la urdimbre (hilos verticales)
      for (int x = TELAR_X0 + 8; x < TELAR_X1 - 8; x += 5)
        bandRect(x0, by, w, rows, x, TELAR_TOP + 20, x + 1, TELAR_BOT + 8, TELAR_WARP);

      for (int b = 0; b < telarBandsDone; b++)
        drawTelarBand(x0, by, w, rows, b, telarBandLen[b], telarBandColors[b], telarBandLen[b]);

      if (telarPhase == TL_INPUT && telarBandsDone < TELAR_BANDS)
        drawTelarBand(x0, by, w, rows, telarBandsDone, telarLen, telarSeq, telarPos);

      bandSprite(x0, by, w, rows, &telarBarraFrames[0], TELAR_X0, TELAR_TOP);
      bandSprite(x0, by, w, rows, &telarBarraFrames[1], TELAR_X0, TELAR_BOT);
    }

    const MiniSprite* t = telarTecoSprite();
    int tx = (t == &telarTecoFrames[telarTecoFrame]) ? TELAR_TECO_X : TELAR_TECO_X + 43 - t->w / 2;
    bandSprite(x0, by, w, rows, t, tx, TELAR_TECO_FOOT - t->h);

    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void renderTelarTeco() { renderTelarRegion(0, TELAR_TECO_FOOT - 72, 104, 74); }
void renderTelarLoom() { renderTelarRegion(TELAR_X0, TELAR_TOP, TELAR_X1 - TELAR_X0, TELAR_BOT + 32 - TELAR_TOP); }

// Dibujar la etiqueta y el panel de bobinas
void drawTelarLabel(const char* txt) {
  tft.fillRoundRect(TELAR_LABEL_X, TELAR_LABEL_Y, TELAR_LABEL_W, TELAR_LABEL_H, 6, PANEL_COLOR);
  tft.drawRoundRect(TELAR_LABEL_X, TELAR_LABEL_Y, TELAR_LABEL_W, TELAR_LABEL_H, 6, ICON_BORDER);
  setTxtColor(COLOR_TEXT);
  int size = strlen(txt) <= 6 ? 2 : 1;
  printCentered(txt, TELAR_LABEL_X + TELAR_LABEL_W / 2, TELAR_LABEL_Y + TELAR_LABEL_H / 2 - 4 * size, size);
}

void drawTelarPanel(int glow) {
  int x = MILPA_PANEL_X, y = MILPA_PANEL_Y;
  tft.fillRoundRect(x, y, MILPA_PANEL_W, MILPA_PANEL_H, 8, PANEL_COLOR);
  tft.drawRoundRect(x, y, MILPA_PANEL_W, MILPA_PANEL_H, 8, ICON_BORDER);

  if (telarPhase == TL_FINAL) {
    setTxtColor(COLOR_TEXT);
    char fin[40]; snprintf(fin, sizeof(fin), "\xADUTZ! Huipil tejido   +%d pwaq", telarEarned);
    printCentered(fin, 160, y + 18, 1);
    return;
  }

  for (int i = 0; i < 5; i++) {
    int cx = 40 + i * 60, cy = y + 22;
    const MiniSprite* b = &telarBobinaFrames[i];
    uint16_t bg = PANEL_COLOR;
    if (i == glow) {
      tft.fillCircle(cx, cy, 21, SELECT_COLOR);
      bg = SELECT_COLOR;
    }
    drawSpriteOnColor(b, cx - b->w / 2, cy - b->h / 2, bg);
    if (telarPhase == TL_INPUT && i == telarSel) {
      tft.drawRoundRect(cx - 19, y + 3, 38, 38, 6, FRAME_DARK);
      tft.drawRoundRect(cx - 18, y + 4, 36, 36, 5, FRAME_DARK);
    }
  }
}

void renderTelarAll() {
  renderTelarRegion(0, 0, FONDO_W, MILPA_PANEL_Y);
  // Dibujar la tierra bajo el panel
  for (int by = MILPA_PANEL_Y; by < FONDO_H; by += BAND_H) {
    int rows = imin(BAND_H, FONDO_H - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < FONDO_W; x++)
        bandBuffer[r * FONDO_W + x] = telarBgPixel(x, by + r);
    tft.drawRGBBitmap(0, by, bandBuffer, FONDO_W, rows);
  }
  drawTelarPanel(-1);
}

// logica
void telarSetMood(TecoMood m) {
  tecoMood = m;
  tecoMiniFrame = 0;
  tecoFrameAt = millis();
  tecoMoodUntil = millis() + TECO_MOOD_MS;
  renderTelarTeco();
}

void telarStartShow() {
  telarPhase = TL_SHOW;
  telarPos = 0;
  telarGlowOn = false;
  telarAt = millis() + 700;   // pausa antes de empezar
  drawTelarLabel("Mira");
  drawTelarPanel(-1);
  dfPlayFolder(AUDIO_FOLDER_TELAR, AUDIO_TELAR_MIRA);
}

void telarNewPattern() {
  // No repetir el mismo color seguido
  for (int i = 0; i < telarLen; i++) { int c; do { c = random(0, 5); } while (i > 0 && c == telarSeq[i - 1]); telarSeq[i] = c; }
  telarBandMiss = false;
  telarStartShow();
}

void startTelar() {
  currentMode = MODE_TELAR;
  telarBandsDone = 0;
  telarLen = 1 + lvlTelar;   // nivel 1: 2 colores ... nivel 5: 6 colores
  gameReset();
  telarEarned = 0;
  telarSel = 0;
  tecoMood = MOOD_NORMAL;
  telarTecoFrame = 0;
  telarTecoAt = millis();
  telarPhase = TL_SHOW;

  renderTelarAll();
  telarNewPattern();
  Serial.println("TELAR -> inicio");
}

void exitTelar(bool happy) {
  drawStaticScreen();
  if (happy) startPetMode();   // Teco feliz con corazones
  else redrawHome();
}

void updateTelarShow() {
  unsigned long now = millis();
  if ((long)(now - telarAt) < 0) return;

  if (!telarGlowOn) {
    if (telarPos >= telarLen) {
      telarPhase = TL_INPUT;
      markQuestionStart();
      telarPos = 0;
      drawTelarLabel("Tu turno");
      drawTelarPanel(-1);
      dfPlayFolder(AUDIO_FOLDER_TELAR, AUDIO_TELAR_TURNO);
      return;
    }
    int c = telarSeq[telarPos];
    telarGlowOn = true;
    drawTelarPanel(c);
    drawTelarLabel(COLOR_WORDS[c]);
    tone(BUZZER_PIN, TELAR_TONES[c], 180);
    dfPlayFolder(AUDIO_FOLDER_COLORES, c + 1);
    telarAt = now + TELAR_SHOW_ON_MS;
  } else {
    telarGlowOn = false;
    drawTelarPanel(-1);
    telarPos++;
    telarAt = now + TELAR_SHOW_OFF_MS;
  }
}

void telarMoveSel(int dir) {
  telarSel = (telarSel + dir + 5) % 5;
  soundClick();
  drawTelarPanel(-1);
  dfPlayFolder(AUDIO_FOLDER_COLORES, telarSel + 1);
}

void telarPick() {
  int c = telarSel;
  tone(BUZZER_PIN, TELAR_TONES[c], 150);

  if (c == telarSeq[telarPos]) {
    telarPos++;
    aciertosHoy++;
    renderTelarLoom();

    if (telarPos >= telarLen) {
      // Franja completa
      for (int i = 0; i < telarLen; i++) telarBandColors[telarBandsDone][i] = telarSeq[i];
      telarBandLen[telarBandsDone] = telarLen;
      telarBandsDone++;
      coins++; telarEarned++;   // una moneda por franja
      // Subir de nivel tras tres franjas sin error
      if (!telarBandMiss) streakHit(&lvlTelar, 3);
      telarPhase = TL_BAND_DONE;
      telarAt = millis() + 1600;
      telarSetMood(MOOD_BIEN);
      playMilpa(AUDIO_MILPA_BIEN);
      drawTelarLabel("Utz!");
    }
  } else {
    { char sq[16]; snprintf(sq, sizeof(sq), "%d colores", telarLen); logAnswer("telar", sq, "error", false); }
    gameMistakes++;
    telarBandMiss = true;
    streakMiss();
    telarSetMood(MOOD_UY);
    playMilpa(AUDIO_MILPA_OTRAVEZ);
    telarPos = 0;
    renderTelarLoom();   // deshacer la franja
    telarStartShow();
    telarAt = millis() + 1500;
  }
}

void updateTelarTeco() {
  unsigned long now = millis();

  if (tecoMood != MOOD_NORMAL) {
    if ((long)(now - tecoMoodUntil) >= 0) {
      tecoMood = MOOD_NORMAL;
      renderTelarTeco();
    } else if (now - tecoFrameAt >= 280 && tecoMiniFrame < 2) {
      tecoFrameAt = now;
      tecoMiniFrame++;
      renderTelarTeco();
    }
    return;
  }

  // Animar a Teco tejiendo durante el juego
  if (telarPhase != TL_FINAL && now - telarTecoAt >= 500) {
    telarTecoAt = now;
    telarTecoFrame = 1 - telarTecoFrame;
    renderTelarTeco();
  }
}

void updateTelarMode() {
  if (backPressed()) {
    soundBack();
    exitTelar(false);
    return;
  }

  if (telarPhase == TL_SHOW) {
    updateTelarShow();
  } else if (telarPhase == TL_INPUT) {
    if (leftPressed()) telarMoveSel(-1);
    if (rightPressed()) telarMoveSel(+1);
    if (okPressed()) telarPick();
  } else if (telarPhase == TL_BAND_DONE) {
    if ((long)(millis() - telarAt) >= 0) {
      if (telarBandsDone >= TELAR_BANDS) {
        telarPhase = TL_FINAL;
        { char sq[16]; snprintf(sq, sizeof(sq), "%d colores", telarLen); logAnswer("telar", sq, sq, true); }
        adaptLevel(&lvlTelar);
        huipilesTejidos++;
        { int b = (gameMistakes <= 1) ? 3 : 2; coins += b; telarEarned += b; }
        fun = imin(9, fun + 3);
        markProgress();
        telarAt = millis() + 3500;
        tecoMood = MOOD_NORMAL;
        renderTelarAll();
        drawTelarLabel("Huipil!");
        dfPlayFolder(AUDIO_FOLDER_TELAR, AUDIO_TELAR_HUIPIL);
      } else {
        if (telarLen < TELAR_MAX_LEN) telarLen++;
        renderTelarLoom();
        telarNewPattern();
      }
    }
  } else if (telarPhase == TL_FINAL) {
    if ((long)(millis() - telarAt) >= 0) {
      exitTelar(true);
      return;
    }
  }

  if (currentMode == MODE_TELAR) updateTelarTeco();
}

void telarReplay() {
  if (telarPhase == TL_INPUT) {
    telarPos = 0;
    renderTelarLoom();
    telarStartShow();
  }
}

// Menu de juegos: dibujar por franjas, sin parpadeo

int gameMenuSel = 0;
#define GM_N 5
#define GM_Y 50
#define GM_W 60
#define GM_H 132
const char* GAME_NAMES[GM_N] = { "Milpa", "Telar", "Viajes", "Barrilete", "Leyendas" };
bool lgFromGames = false;   // leyendas abiertas desde este menu
int gmX(int i) { return 4 + i * 63; }

void renderGameMenuRegion(int x0, int y0, int w, int h) {
  int maxRows = imin(h, (FONDO_W * BAND_H) / w);
  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++) bandBuffer[r * w + x] = bgPixel(x0 + x, by + r);
    for (int i = 0; i < GM_N; i++) {
      int x = gmX(i), y = GM_Y;
      if (i == gameMenuSel) hbNine(x0, by, w, rows, &kitSeleccionFrames[0], x - 4, y - 4, x + GM_W + 4, y + GM_H + 4, 3, 1);
      hbNine(x0, by, w, rows, &kitPlacaFrames[0], x, y, x + GM_W, y + GM_H, 4, 1);
      if (i == 0) {
        const MiniSprite* m = &milpaPlantaFrames[4];
        hbSpriteMini(x0, by, w, rows, m, x + GM_W / 2 - m->w / 2, y + 92 - m->h);
      } else if (i == 1) {
        for (int k = 0; k < 2; k++) hbSpriteMini(x0, by, w, rows, &telarBobinaFrames[k * 2], x + 2 + k * 29, y + 34);
      } else if (i == 2) {
        const MiniSprite* t = &viajeTecoFrames[0];
        hbSpriteMini(x0, by, w, rows, t, x + GM_W / 2 - t->w / 2, y + 18);
      } else if (i == 3) {
        brKite(x0, by, w, rows, x + GM_W / 2 - 4, y + 40, 18);
      } else {
        int sz = leyendaMiniFrames[0].w + 8;
        lgPicCard(x0, by, w, rows, 0, x + GM_W / 2 - sz / 2, y + 22, true);
      }
    }
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void drawGameMenu() {
  renderGameMenuRegion(0, GM_Y - 6, FONDO_W, GM_H + 12);
  for (int i = 0; i < GM_N; i++) {
    int cx = gmX(i) + GM_W / 2;
    setTxtColor(COLOR_TEXT);
    printCenteredW(GAME_NAMES[i], cx, GM_Y + GM_H - 34, 1, GM_W - 1);
    char lv[16];
    if (i == 0) snprintf(lv, sizeof(lv), "Nivel %d", lvlMilpa);
    else if (i == 1) snprintf(lv, sizeof(lv), "Nivel %d", lvlTelar);
    else if (i == 2) snprintf(lv, sizeof(lv), "pwaq: %d", coins);
    else if (i == 3) snprintf(lv, sizeof(lv), "Nivel %d", lvlBarrilete);
    else snprintf(lv, sizeof(lv), "Cuentos");
    setTxtColor(RGB565(150, 90, 50));
    printCenteredW(lv, cx, GM_Y + GM_H - 20, 1, GM_W - 1);
  }
}

void startGameMenu() {
  stopLife();
  endGreeting();
  currentMode = MODE_GAMEMENU;
  drawStaticScreen();
  drawGameMenu();
}

void updateGameMenu() {
  if (leftPressed())  { gameMenuSel = (gameMenuSel + GM_N - 1) % GM_N; soundClick(); drawGameMenu(); }
  if (rightPressed()) { gameMenuSel = (gameMenuSel + 1) % GM_N; soundClick(); drawGameMenu(); }
  if (okPressed()) {
    soundClick();
    if (gameMenuSel == 0) startMilpa();
    else if (gameMenuSel == 1) startTelar();
    else if (gameMenuSel == 2) startViajes();
    else if (gameMenuSel == 3) startBarrilete();
    else { lgFromGames = true; startLegends(); }
  }
  if (backPressed()) {
    soundBack();
    drawStaticScreen();
    redrawHome();
  }
}


// Perfiles y registro de respuestas

bool logReady = false;
unsigned long questionStartAt = 0;

void markQuestionStart() { questionStartAt = millis(); }

void loadActiveProfile() {
  Preferences p;
  p.begin("tecocfg", true);
  activeProfile = constrain(p.getInt("prof", 0), 0, 3);
  p.end();
}

void saveActiveProfile() {
  Preferences p;
  p.begin("tecocfg", false);
  p.putInt("prof", activeProfile);
  p.end();
}

// Convertir textos CP437 a UTF-8 para el CSV
void cp437ToUtf8(const char* in, char* out, size_t n) {
  size_t k = 0;
  for (; *in && k + 3 < n; in++) {
    uint8_t c = (uint8_t)*in;
    const char* r = nullptr;
    switch (c) {
      case 0x84: r = "\xC3\xA4"; break; case 0x89: r = "\xC3\xAB"; break; case 0x8B: r = "\xC3\xAF"; break;
      case 0x94: r = "\xC3\xB6"; break; case 0x81: r = "\xC3\xBC"; break; case 0xA0: r = "\xC3\xA1"; break;
      case 0x82: r = "\xC3\xA9"; break; case 0xA1: r = "\xC3\xAD"; break; case 0xA2: r = "\xC3\xB3"; break;
      case 0xA3: r = "\xC3\xBA"; break; case 0xA4: r = "\xC3\xB1"; break; case 0xA8: r = "\xC2\xBF"; break;
      case 0xAD: r = "\xC2\xA1"; break;
    }
    if (r) { while (*r && k + 1 < n) out[k++] = *r++; }
    else if (c == ',' || c == '"' || c == '\n') out[k++] = ' ';
    else out[k++] = (char)c;
  }
  out[k] = 0;
}

void setupLog() {
  logReady = LittleFS.begin(true);
  if (!logReady) { Serial.println("Registro: no se pudo abrir la memoria LittleFS"); return; }
  if (!LittleFS.exists("/registro.csv")) {
    File f = LittleFS.open("/registro.csv", "w");
    if (f) { f.print("perfil,fecha_hora,actividad,correcta,elegida,acierto,tiempo_ms\n"); f.close(); }
  }
  Serial.println("Registro de respuestas: listo (/registro.csv)");
}

void logAnswer(const char* actividad, const char* correcta, const char* elegida, bool acierto) {
  if (!logReady) return;
  char fecha[24];
  if (clockValid()) {
    struct tm t; clockLocal(&t);
    snprintf(fecha, sizeof(fecha), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    snprintf(fecha, sizeof(fecha), "sin_hora+%lus", millis() / 1000);
  }
  char a[48], b[48];
  cp437ToUtf8(correcta, a, sizeof(a));
  cp437ToUtf8(elegida, b, sizeof(b));
  unsigned long ms = questionStartAt ? millis() - questionStartAt : 0;
  File f = LittleFS.open("/registro.csv", "a");
  if (!f) return;
  char line[200];
  snprintf(line, sizeof(line), "N%02d,%s,%s,%s,%s,%d,%lu\n", activeProfile + 1, fecha, actividad, a, b, acierto ? 1 : 0, ms);
  f.print(line);
  f.close();
  questionStartAt = millis();   // medir la siguiente respuesta desde aqui
}

int logLineCount() {
  if (!logReady) return 0;
  File f = LittleFS.open("/registro.csv", "r");
  if (!f) return 0;
  int n = 0;
  while (f.available()) if (f.read() == '\n') n++;
  f.close();
  return n > 0 ? n - 1 : 0;   // sin encabezado
}

// Lecciones

// 1 = mostrar el significado en espanol (en Conoce y al acertar)
#define LESSON_SHOW_SPANISH 1

struct LessonWord {
  const char* text;   // en kaqchikel
  const char* es;   // en espanol
  const MiniSprite* card;
  uint8_t count;
  uint8_t folder;
  uint8_t track;
};

// Palabras de las lecciones
const LessonWord LESSON_WORDS[] = {
  // 0-4: colores (carpeta 04)
  { "k\x84q",  "rojo",     &telarBobinaFrames[0], 0, 4, 1 },
  { "q'\x84n", "amarillo", &telarBobinaFrames[1], 0, 4, 2 },
  { "r\x84x",  "verde",    &telarBobinaFrames[2], 0, 4, 3 },
  { "s\x84q",  "blanco",   &telarBobinaFrames[3], 0, 4, 4 },
  { "q'eq",     "negro",    &telarBobinaFrames[4], 0, 4, 5 },
  // 5-9: numeros (carpeta 02)
  { "jun",   "uno",    &milpaMazorcaFrames[0], 1, 2, 1 },
  { "ka'i'", "dos",    &milpaMazorcaFrames[0], 2, 2, 2 },
  { "oxi'",  "tres",   &milpaMazorcaFrames[0], 3, 2, 3 },
  { "kaji'", "cuatro", &milpaMazorcaFrames[0], 4, 2, 4 },
  { "wo'o'", "cinco",  &milpaMazorcaFrames[0], 5, 2, 5 },
  // palabras del libro (audios /07/001 en adelante)
  { "xsaqer",        "buenos d\xA1" "as", &card_xsaqer[0], 0, 7, 1 },
  { "matyox",        "gracias",           &card_matyox[0], 0, 7, 2 },
  { "chwa'q chik",   "hasta ma\xA4" "ana", &card_chwaq_chik[0], 0, 7, 3 },
  { "utz",           "bien",              &card_utz[0], 0, 7, 4 },
  { "waqi'",         "seis",              &milpaMazorcaFrames[0], 6, 2, 6 },
  { "wuqu'",         "siete",             &milpaMazorcaFrames[0], 7, 2, 7 },
  { "waqxaqi'",      "ocho",              &milpaMazorcaFrames[0], 8, 2, 8 },
  { "b'eleje'",      "nueve",             &milpaMazorcaFrames[0], 9, 2, 9 },
  { "lajuj",         "diez",              &milpaMazorcaFrames[0], 10, 2, 10 },
  { "tz'i'",         "perro",             &card_tzi[0], 0, 7, 5 },
  { "mes",           "gato",              &card_mes[0], 0, 7, 6 },
  { "\x84k'",        "pollo",             &card_ak[0], 0, 7, 7 },
  { "umul",          "conejo",            &card_umul[0], 0, 7, 8 },
  { "kej",           "caballo",           &card_kej[0], 0, 7, 9 },
  { "kuk",           "ardilla",           &card_kuk[0], 0, 7, 10 },
  { "ut",            "paloma",            &card_ut[0], 0, 7, 11 },
  { "k'oy",          "mico",              &card_koy[0], 0, 7, 12 },
  { "nute'",         "mi mam\xA0",        &card_nute[0], 0, 7, 13 },
  { "nutata'",       "mi pap\xA0",        &card_nutata[0], 0, 7, 14 },
  { "nan",           "mi abuela",         &card_watit[0], 0, 7, 15 },
  { "numama'",       "mi abuelo",         &card_numama[0], 0, 7, 16 },
  { "q'ij",          "sol",               &card_qij[0], 0, 7, 17 },
  { "ch'umil",       "estrella",          &card_chumil[0], 0, 7, 18 },
  { "che'",          "\xA0rbol",          &card_che[0], 0, 7, 19 },
  { "ya'",           "agua",              &card_ya[0], 0, 7, 20 },
  { "quicot",        "feliz",               &tecoMiniBienFrames[2], 0, 7, 21 },
  { "nb'ison",       "triste",             &tecoMiniUyFrames[2], 0, 7, 22 },
  { "tze'",          "r\xA1" "e",         &card_ntzeen[0], 0, 7, 23 },
  { "ok'ej",         "llora",             &card_ntoq[0], 0, 7, 24 },
  // segunda tanda
  { "ixim",            "ma\xA1z",             &card_ixim[0], 0, 7, 25 },
  { "w\x84y",          "tortilla",            &card_way[0], 0, 7, 26 },
  { "kin\x84q'",       "frijol",              &card_kinaq[0], 0, 7, 27 },
  { "j\x84l",          "mazorca",             &milpaMazorcaFrames[0], 0, 7, 28 },
  { "sub'an",          "tamal",               &card_suban[0], 0, 7, 29 },
  { "ik",              "chile",               &card_ik[0], 0, 7, 30 },
  { "oj",              "aguacate",            &card_oj[0], 0, 7, 31 },
  { "k'ix",            "g\x81isquil",         &card_chumak[0], 0, 7, 32 },
  { "tukur",           "tecolote",            &tecoMiniNormalFrames[0], 0, 7, 33 },
  { "q'uq'",           "quetzal",             &card_quq[0], 0, 7, 34 },
  { "b'alam malax",    "mariposa",            &visButterflyFrames[0], 0, 7, 35 },
  { "b'alam",          "jaguar",              &card_balam[0], 0, 7, 36 },
  { "wa'\x8Bm",        "comer",               &card_waim[0], 0, 7, 37 },
  { "uq'uiya'",       "beber",               &card_ukyaxik[0], 0, 7, 38 },
  { "w\x84r",          "dormir",              &card_waram[0], 0, 7, 39 },
  { "etz'anem",        "jugar",               &card_etzanem[0], 0, 7, 40 },
  { "xb'iyin",        "caminar",             &card_biyajnem[0], 0, 7, 41 },
  { "b'ixan\x8Bk",       "cantar",              &card_bixanem[0], 0, 7, 42 },
  { "tikomal",         "sembrar",             &card_tikonem[0], 0, 7, 43 },
  { "kemon\x8Bk",       "tejer",               &card_kemoj[0], 0, 7, 44 },
  { "kosn\x84q",       "cansado",             &card_kosnaq[0], 0, 7, 45 },
  { "wayjal",          "hambre",              &card_numnaq[0], 0, 7, 46 },
  { "royowal",         "enojado",             &card_petiyowal[0], 0, 7, 47 },
  { "xib'in",          "asustado",            &card_xibinaq[0], 0, 7, 48 },
  { "julajuj",         "once",                &milpaMazorcaFrames[0], 11, 2, 11 },
  { "kab'lajuj",       "doce",                &milpaMazorcaFrames[0], 12, 2, 12 },
  { "oxlajuj",         "trece",               &milpaMazorcaFrames[0], 13, 2, 13 },
  { "kajlajuj",        "catorce",             &milpaMazorcaFrames[0], 14, 2, 14 },
  { "wo'lajuj",        "quince",              &milpaMazorcaFrames[0], 15, 2, 15 },
  { "waklajuj",        "diecis\x82is",        &milpaMazorcaFrames[0], 16, 2, 16 },
  { "wuqlajuj",        "diecisiete",          &milpaMazorcaFrames[0], 17, 2, 17 },
  { "waqxlajuj",       "dieciocho",           &milpaMazorcaFrames[0], 18, 2, 18 },
  { "b'elejlajuj",     "diecinueve",          &milpaMazorcaFrames[0], 19, 2, 19 },
  // tercera tanda (audios /07/049 en adelante)
  { "xokaq'a'",          "buenas noches",         &card_xokaqa[0], 0, 7, 49 },
  { "\xA8\x81tz aw\x84" "ch?", "\xA8" "c\xA2mo est\xA0s?", &card_utz_awach[0], 0, 7, 50 },
  { "tab'ana' utzil",    "por favor",             &card_tabana[0], 0, 7, 51 },
  { "teopan",            "iglesia",               &card_teopan[0], 0, 7, 52 },
  { "tinam\x8Bt",        "pueblo",                &card_tinamit[0], 0, 7, 53 },
  { "b'ey",              "camino",                &card_beyaj[0], 0, 7, 54 },
  { "aw\x84n",              "milpa",                 &milpaPlantaFrames[5], 0, 7, 55 },
  { "j\x94" "b'",        "lluvia",                &card_job[0], 0, 7, 56 },
  { "kotz'i'j",          "flor",                  &card_kotzij[0], 0, 7, 57 },
  { "choy",              "lago",                  &card_choy[0], 0, 7, 58 },
  { "ixcanul",             "volc\xA0n",             &card_juyu[0], 0, 7, 59 },
  { "jolomaj",           "cabeza",                &card_jolomaj[0], 0, 7, 60 },
  { "naq' wachaj",       "ojos",                  &card_runak[0], 0, 7, 61 },
  { "tzamaj",            "nariz",                 &card_tzamaj[0], 0, 7, 62 },
  { "chi'aj",            "boca",                  &card_chiaj[0], 0, 7, 63 },
  { "xikinaj",           "orejas",                &card_xikinaj[0], 0, 7, 64 },
  { "q'ab'aj",           "manos",                 &card_qabaj[0], 0, 7, 65 },
  { "akanaj",            "pies",                  &card_akanaj[0], 0, 7, 66 },
  { "wi'aj",             "pelo",                  &card_wiaj[0], 0, 7, 67 },
  { "po't",              "huipil",                &card_pot[0], 0, 7, 68 },
  { "uq",                "corte",                 &card_uq[0], 0, 7, 69 },
  { "pas",               "faja",                  &card_pas[0], 0, 7, 70 },
  { "pawi'aj",           "sombrero",              &card_pawiaj[0], 0, 7, 71 },
  { "k'ayb\x84l",        "mercado",               &card_kaybal[0], 0, 7, 72 },
  { "pwaq",              "dinero",                &card_pwaq[0], 0, 7, 73 },
  { "aranx\x84x",        "naranja",               &card_aranxax[0], 0, 7, 74 },
  { "saq'ul",            "banano",                &card_saqul[0], 0, 7, 75 },
  // cuarta tanda (audios /07/076 en adelante)
  { "xqaq'ij",            "buenas tardes",          &card_qij[0], 0, 7, 76 },
  { "tz'ikin",            "p\xA0" "jaro",            &visBirdFrames[0], 0, 7, 77 },
  { "patix",              "pato",                   &card_patix[0], 0, 7, 78 },
  { "ral chij, alaj",     "cordero",                &card_karnel[0], 0, 7, 79 },
  { "kuruch\x8B" "ch",     "p\xA0" "jaro carpintero", &card_tuktuk[0], 0, 7, 80 },
  // viajes (audio /07/081)
  { "xkoya'",             "tomate",                 &viajeTomateFrames[0], 0, 7, 81 },
};
#define LESSON_WORD_COUNT (sizeof(LESSON_WORDS) / sizeof(LESSON_WORDS[0]))


struct Lesson {
  const char* title;
  uint8_t words[4];   // indices en LESSON_WORDS
};

const Lesson LESSONS[] = {
  { "Colores",  { 0, 1, 2, 3 } },
  { "N\xA3meros",  { 5, 6, 7, 8 } },
  { "Repaso",   { 4, 9, 2, 7 } },
  { "Saludos",      { 10, 11, 12, 13 } },
  { "N\xA3meros mayas", { 14, 15, 16, 17 } },
  { "Animales 1",   { 19, 20, 21, 22 } },
  { "Animales 2",   { 23, 24, 25, 26 } },
  { "Familia",      { 27, 28, 29, 30 } },
  { "Naturaleza",   { 31, 32, 33, 34 } },
  { "Sentimientos", { 35, 36, 37, 38 } },
  { "Repaso 2",     { 18, 19, 27, 31 } },
  { "Comida 1",     { 39, 40, 41, 42 } },
  { "Comida 2",     { 43, 44, 45, 46 } },
  { "Animales 3",   { 47, 48, 49, 50 } },
  { "Acciones 1",   { 51, 52, 53, 54 } },
  { "Acciones 2",   { 55, 56, 57, 58 } },
  { "Sentimientos 2", { 59, 60, 61, 62 } },
  { "N\xA3meros mayas 2", { 63, 64, 65, 66 } },
  { "N\xA3meros mayas 3", { 67, 68, 69, 70 } },
  { "Repaso 3",     { 71, 39, 51, 59 } },
  { "Saludos 2",    { 72, 73, 74, 99 } },
  { "Lugares",      { 75, 76, 77, 78 } },
  { "Naturaleza 2", { 79, 80, 81, 82 } },
  { "Cuerpo 1",     { 83, 84, 85, 86 } },
  { "Cuerpo 2",     { 87, 88, 89, 90 } },
  { "Ropa",         { 91, 92, 93, 94 } },
  { "Mercado",      { 95, 96, 97, 98 } },
  { "Animales 4",   { 100, 101, 102, 103 } },
};
#define LESSON_COUNT (sizeof(LESSONS) / sizeof(LESSONS[0]))

#define LESSON_MAX_Q 8   // 4 nuevas + 2 de repaso + hasta 2 repetidas por error

// estado de la leccion actual
LessonPhase lsPhase = LS_INTRO;
int lsLesson = 0;
int lsIntroIdx = 0;
int lsQuestion = 0;
int lsQueue[LESSON_MAX_Q];   // palabras a preguntar, en orden
int lsQueueLen = 0;
unsigned long lsWaitUntil = 0;   // espera tras responder (sin bloquear)
enum { LS_AFTER_NEXT, LS_AFTER_REPLAY };
int lsAfter = LS_AFTER_NEXT;
int lsTarget = 0;   // indice en LESSON_WORDS
int lsOptions[3];
int lsSel = 1;
unsigned long lsDoneAt = 0;

// distribucion
#define LS_PANEL_X 34
#define LS_PANEL_Y 22
#define LS_PANEL_W 252
#define LS_PANEL_H 192
#define LS_BIG_CARD 112
#define LS_CARD 70
const int LS_CARD_X[3] = { 46, 125, 204 };
#define LS_CARD_Y 84
#define LS_MENU_CARD_Y 86
#define LS_TECO_X 4

const uint16_t LS_PANEL_COLOR = RGB565(255, 244, 222);
const uint16_t LS_CARD_COLOR  = RGB565(255, 255, 250);
const uint16_t LS_CARD_BORDER = RGB565(150, 110, 70);
const uint16_t LS_SPANISH_COLOR = RGB565(150, 90, 50);
const uint16_t MAYA_COLOR = RGB565(110, 60, 30);

// Dibujo
void lsRect(int bx, int by, int bw, int rows, int x0, int y0, int x1, int y1, uint16_t c, int radius) {
  for (int y = imax(y0, by); y < imin(y1, by + rows); y++)
    for (int x = imax(x0, bx); x < imin(x1, bx + bw); x++) {
      int lx = x - x0, ly = y - y0, w = x1 - x0, h = y1 - y0;
      int cx = lx < radius ? radius - lx : (lx >= w - radius ? lx - (w - radius - 1) : 0);
      int cy = ly < radius ? radius - ly : (ly >= h - radius ? ly - (h - radius - 1) : 0);
      if (cx * cx + cy * cy > radius * radius) continue;
      bandBuffer[(y - by) * bw + (x - bx)] = c;
    }
}

void lsSpriteScaled(int bx, int by, int bw, int rows, const MiniSprite* s, int sx, int sy, int k) {
  int w = s->w * k, h = s->h * k;
  for (int y = imax(sy, by); y < imin(sy + h, by + rows); y++)
    for (int x = imax(sx, bx); x < imin(sx + w, bx + bw); x++) {
      uint16_t c = miniPx(s, ((y - sy) / k) * s->w + (x - sx) / k);
      if (c != MINI_TRANSPARENT) bandBuffer[(y - by) * bw + (x - bx)] = c;
    }
}

// Dibujar la tarjeta: fondo blanco, borde y dibujo
void lsCard(int bx, int by, int bw, int rows, int wordIdx, int x0, int y0, int size, bool selected) {
  if (selected) hbNine(bx, by, bw, rows, &kitSeleccionFrames[0], x0 - 6, y0 - 6, x0 + size + 6, y0 + size + 6, 3, 2);
  hbNine(bx, by, bw, rows, &kitMarcoFrames[0], x0, y0, x0 + size, y0 + size, 3, 1);

  const LessonWord& w = LESSON_WORDS[wordIdx];
  const MiniSprite* s = w.card;
  if (w.count == 0) {
    int k = imax(1, (size - 16) / imax(s->w, s->h));
    lsSpriteScaled(bx, by, bw, rows, s, x0 + (size - s->w * k) / 2, y0 + (size - s->h * k) / 2, k);
  } else if (w.count > 5) {
    // Dibujar un numero maya: puntos arriba y barras abajo, centrado
    int bars = w.count / 5, dots = w.count % 5;
    int barW = size * 6 / 10, barH = imax(4, size / 11), gap = imax(3, size / 16);
    int dotR = imax(3, size / 16);
    int totalH = bars * barH + (bars - 1) * gap + (dots ? (2 * dotR + gap) : 0);
    int y = y0 + (size - totalH) / 2;
    if (dots) {
      int span = dots * 2 * dotR + (dots - 1) * gap;
      int dx = x0 + (size - span) / 2 + dotR;
      for (int d = 0; d < dots; d++) {
        int cx = dx + d * (2 * dotR + gap), cy = y + dotR;
        for (int yy = cy - dotR; yy <= cy + dotR; yy++)
          for (int xx = cx - dotR; xx <= cx + dotR; xx++)
            if ((xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) <= dotR * dotR &&
                yy >= by && yy < by + rows && xx >= bx && xx < bx + bw)
              bandBuffer[(yy - by) * bw + (xx - bx)] = MAYA_COLOR;
      }
      y += 2 * dotR + gap;
    }
    for (int b = 0; b < bars; b++) {
      int bx0 = x0 + (size - barW) / 2;
      lsRect(bx, by, bw, rows, bx0, y, bx0 + barW, y + barH, MAYA_COLOR, 2);
      y += barH + gap;
    }
  } else {
    // Dibujar varios dibujos pequenos en filas de 3
    int n = w.count, cols = n < 3 ? n : 3, rowsN = (n + 2) / 3;
    int k = (cols * (s->w * 2 + 3) <= size - 8 && rowsN * (s->h * 2 + 3) <= size - 8) ? 2 : 1;
    int cw = s->w * k + 3, ch = s->h * k + 3;
    int gx = x0 + (size - cols * cw) / 2, gy = y0 + (size - rowsN * ch) / 2;
    for (int i = 0; i < n; i++) {
      int r = i / 3, c = i % 3;
      int inRow = (r == rowsN - 1) ? n - r * 3 : 3;
      int off = (cols - inRow) * cw / 2;
      lsSpriteScaled(bx, by, bw, rows, s, gx + off + c * cw, gy + r * ch, k);
    }
  }
}

const MiniSprite* lsTecoSprite() {
  if (tecoMood == MOOD_BIEN) return &tecoMiniBienFrames[tecoMiniFrame];
  if (tecoMood == MOOD_UY)   return &tecoMiniUyFrames[tecoMiniFrame];
  return &tecoMiniNormalFrames[tecoMiniFrame];
}

void renderLessonRegion(int x0, int y0, int w, int h) {
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > FONDO_W) w = FONDO_W - x0;
  if (y0 + h > FONDO_H) h = FONDO_H - y0;
  if (w <= 0 || h <= 0) return;
  int maxRows = (FONDO_W * BAND_H) / w;
  if (maxRows > h) maxRows = h;

  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++)
        bandBuffer[r * w + x] = bgPixel(x0 + x, by + r);

    hbNine(x0, by, w, rows, &kitPlacaFrames[0], LS_PANEL_X - 6, LS_PANEL_Y - 6, LS_PANEL_X + LS_PANEL_W + 6, LS_PANEL_Y + LS_PANEL_H + 6, 4, 2);

    const Lesson& L = LESSONS[imin(lsLesson, (int)LESSON_COUNT - 1)];
    if (lsPhase == LS_MENU && lsLesson == (int)LESSON_COUNT) {
      for (int i = 0; i < 5; i++)   // los cinco personajes en pequeno
        lgPicCard(x0, by, w, rows, i, 160 - 5 * 26 + i * 52 + 1, LS_MENU_CARD_Y, true);
    } else if (lsPhase == LS_MENU) {
      for (int i = 0; i < 4; i++)
        lsCard(x0, by, w, rows, L.words[i], 160 - 2 * 58 + i * 58 + 3, LS_MENU_CARD_Y, 52, false);
    } else if (lsPhase == LS_INTRO) {
      lsCard(x0, by, w, rows, L.words[lsIntroIdx], 160 - LS_BIG_CARD / 2, LS_PANEL_Y + 16, LS_BIG_CARD, false);
    } else if (lsPhase == LS_PRACTICE) {
      for (int i = 0; i < 3; i++)
        lsCard(x0, by, w, rows, lsOptions[i], LS_CARD_X[i], LS_CARD_Y, LS_CARD, i == lsSel);
    }

    const MiniSprite* t = lsTecoSprite();
    hbSpriteMini(x0, by, w, rows, t, LS_TECO_X, FONDO_H - 4 - t->h);

    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

// Dibujar textos sobre el panel
void lsTexts() {
  const Lesson& L = LESSONS[imin(lsLesson, (int)LESSON_COUNT - 1)];
  setTxtColor(COLOR_TEXT);
  char buf[32];

  if (lsPhase == LS_MENU && lsLesson == (int)LESSON_COUNT) {
    printCentered("Cuentos de Teco", 160, LS_PANEL_Y + 8, 1);
    printCenteredW("Leyendas de Guatemala", 160, LS_PANEL_Y + 22, 2, LS_PANEL_W - 20);
    printCentered("5 leyendas", 160, LS_PANEL_Y + 48, 1);
    setTxtColor(LS_SPANISH_COLOR);
    printCentered("\xAD" "Escucha!", 160, LS_MENU_CARD_Y + 62, 2);
    setTxtColor(COLOR_TEXT);
    printCentered("<  >: elegir     OK: empezar", 160, LS_PANEL_Y + LS_PANEL_H - 15, 1);
    printCentered("<", LS_PANEL_X + 7, LS_MENU_CARD_Y + 18, 2);
    printCentered(">", LS_PANEL_X + LS_PANEL_W - 7, LS_MENU_CARD_Y + 18, 2);
    return;
  }

  if (lsPhase == LS_MENU) {
    printCentered("Elige una lecci\xA2n", 160, LS_PANEL_Y + 8, 1);
    printCenteredW(L.title, 160, LS_PANEL_Y + 22, 2, LS_PANEL_W - 20);
    bool done = lessonsDone & (1ULL << lsLesson);
    bool next = (lsLesson == lessonNext % (int)LESSON_COUNT);
    snprintf(buf, sizeof(buf), "%d de %d", lsLesson + 1, (int)LESSON_COUNT);
    printCentered(buf, 160, LS_PANEL_Y + 48, 1);
    setTxtColor(LS_SPANISH_COLOR);
    printCentered(done ? "\xFB Completada" : (next ? "Te toca esta" : "Nueva"), 160, LS_MENU_CARD_Y + 62, 2);
    setTxtColor(COLOR_TEXT);
    printCentered("<  >: elegir     OK: empezar", 160, LS_PANEL_Y + LS_PANEL_H - 15, 1);
    // Dibujar flechas laterales
    printCentered("<", LS_PANEL_X + 7, LS_MENU_CARD_Y + 18, 2);
    printCentered(">", LS_PANEL_X + LS_PANEL_W - 7, LS_MENU_CARD_Y + 18, 2);
  } else if (lsPhase == LS_INTRO) {
    snprintf(buf, sizeof(buf), "%s  %d/4", L.title, lsIntroIdx + 1);
    printCentered(buf, 160, LS_PANEL_Y + 6, 1);
    const LessonWord& w = LESSON_WORDS[L.words[lsIntroIdx]];
    int ts = strlen(w.text) > 12 ? 2 : 3;   // letra mas pequena para palabras largas
    printCenteredW(w.text, 160, LS_PANEL_Y + LS_BIG_CARD + 20, 2, LS_PANEL_W - 20);
#if LESSON_SHOW_SPANISH
    setTxtColor(LS_SPANISH_COLOR);
    printCenteredW(w.es, 160, LS_PANEL_Y + LS_BIG_CARD + 46, 1, LS_PANEL_W - 20);
    setTxtColor(COLOR_TEXT);
#endif
    printCentered("OK: otra vez   >: siguiente", 160, LS_PANEL_Y + LS_PANEL_H - 15, 1);
  } else if (lsPhase == LS_PRACTICE) {
    snprintf(buf, sizeof(buf), "\xA8" "Cu\xA0l es?   %d/%d", lsQuestion + 1, lsQueueLen);
    printCentered(buf, 160, LS_PANEL_Y + 10, 2);
    printCentered("Toca a Teco: otra vez", 172, LS_PANEL_Y + LS_PANEL_H - 15, 1);
  } else if (lsPhase == LS_DONE) {
    printCentered("\xADUTZ!", 160, 66, 3);
    snprintf(buf, sizeof(buf), "Lecci\xA2n \"%s\" lista", L.title);
    printCentered(buf, 160, 120, 1);
    printCentered("Teco aprendi\xA2 contigo", 160, 140, 1);
  }
}

void renderLessonAll() {
  renderLessonRegion(0, 0, FONDO_W, FONDO_H);
  lsTexts();
}

void renderLessonTeco() {
  renderLessonRegion(LS_TECO_X, FONDO_H - 64, 60, 64);
}

// Logica
void lsPlayWord(int idx) {
  dfPlayFolder(LESSON_WORDS[idx].folder, LESSON_WORDS[idx].track);
}

void lsSetMood(TecoMood m) {
  tecoMood = m;
  tecoMiniFrame = 0;
  tecoFrameAt = millis();
  tecoMoodUntil = millis() + TECO_MOOD_MS;
  renderLessonTeco();
}

// Armar la lista de preguntas
void lsBuildQueue() {
  const Lesson& L = LESSONS[lsLesson];
  int cur[4] = { L.words[0], L.words[1], L.words[2], L.words[3] };
  for (int i = 3; i > 0; i--) { int j = random(0, i + 1); int t = cur[i]; cur[i] = cur[j]; cur[j] = t; }
  lsQueueLen = 0;
  for (int i = 0; i < 4; i++) lsQueue[lsQueueLen++] = cur[i];

  // Elegir candidatos de repaso (sin repetir los de esta leccion)
  int pool[LESSON_MAX_WORDS], weights[LESSON_MAX_WORDS], n = 0, total = 0;
  for (int l = 0; l < lsLesson && l < (int)LESSON_COUNT; l++) {
    for (int k = 0; k < 4; k++) {
      int idx = LESSONS[l].words[k];
      bool dup = false;
      for (int j = 0; j < n; j++) if (pool[j] == idx) dup = true;
      for (int j = 0; j < 4; j++) if (cur[j] == idx) dup = true;
      if (dup) continue;
      pool[n] = idx; weights[n] = 1 + wordMiss[idx] * 3; total += weights[n]; n++;
    }
  }
  for (int r = 0; r < 2; r++) {
    int pick = -1;
    if (n > 0) {
      int x = random(0, total);
      for (int j = 0; j < n; j++) { x -= weights[j]; if (x < 0) { pick = j; break; } }
      if (pick < 0) pick = n - 1;
      lsQueue[lsQueueLen++] = pool[pick];
      total -= weights[pick]; weights[pick] = 0;   // no repetir el mismo repaso
      if (total <= 0) n = 0;
    } else {
      // Primera leccion: repasar sus propias palabras sin repetir la anterior
      int w = cur[(r + 1) % 4];
      lsQueue[lsQueueLen++] = w;
    }
  }
}

void lsNewQuestion() {
  lsTarget = lsQueue[lsQuestion];
  markQuestionStart();

  // Elegir dos opciones falsas de la misma leccion
  int src = lsLesson;
  for (int l = lsLesson; l >= 0; l--) {
    bool has = false;
    for (int k = 0; k < 4; k++) if (LESSONS[l].words[k] == lsTarget) has = true;
    if (has) { src = l; break; }
  }
  int a = lsTarget, b = lsTarget;
  while (a == lsTarget) a = LESSONS[src].words[random(0, 4)];
  while (b == lsTarget || b == a) b = LESSONS[src].words[random(0, 4)];
  int pos = random(0, 3), o = 0;
  int others[2] = { a, b };
  for (int i = 0; i < 3; i++) lsOptions[i] = (i == pos) ? lsTarget : others[o++];
  lsSel = 1;
  renderLessonAll();
  lsPlayWord(lsTarget);
}

// Abrir el menu de lecciones desde el libro
void startLesson() {
  currentMode = MODE_LESSON;
  stopLife();
  endGreeting();
  lsLesson = lessonNext % LESSON_COUNT;
  lsPhase = LS_MENU;
  tecoMood = MOOD_NORMAL;
  tecoMiniFrame = 0;
  tecoFrameAt = millis();
  renderLessonAll();
  Serial.println("LECCIONES -> menu");
}

void lsBeginChosen() {
  lsPhase = LS_INTRO;
  lsIntroIdx = 0;
  lsQuestion = 0;
  renderLessonAll();
  lsPlayWord(LESSONS[lsLesson].words[0]);
  Serial.print("LECCION -> ");
  Serial.println(LESSONS[lsLesson].title);
}

void exitLesson(bool finished) {
  drawStaticScreen();
  if (finished) startPetMode();
  else redrawHome();
}

void updateLessonTeco() {
  unsigned long now = millis();
  if (tecoMood == MOOD_NORMAL) {
    unsigned long wait = (tecoMiniFrame == 2) ? 180 : 2500;
    if (now - tecoFrameAt >= wait) {
      tecoFrameAt = now;
      tecoMiniFrame = (tecoMiniFrame == 2) ? 0 : 2;
      renderLessonTeco();
    }
    return;
  }
  if ((long)(now - tecoMoodUntil) >= 0) {
    tecoMood = MOOD_NORMAL; tecoMiniFrame = 0; tecoFrameAt = now;
    renderLessonTeco();
  } else if (now - tecoFrameAt >= 280 && tecoMiniFrame < 2) {
    tecoFrameAt = now; tecoMiniFrame++;
    renderLessonTeco();
  }
}

void lessonReplay() {
  if (lsPhase == LS_INTRO) lsPlayWord(LESSONS[lsLesson].words[lsIntroIdx]);
  else if (lsPhase == LS_PRACTICE) lsPlayWord(lsTarget);
}

void updateLessonMode() {
  if (backPressed()) {
    soundBack();
    exitLesson(false);
    return;
  }

  if (lsPhase == LS_MENU) {
    // Agregar al final la entrada de leyendas
    if (leftPressed())  { lsLesson = (lsLesson + LESSON_COUNT) % (LESSON_COUNT + 1); soundClick(); renderLessonAll(); }
    if (rightPressed()) { lsLesson = (lsLesson + 1) % (LESSON_COUNT + 1); soundClick(); renderLessonAll(); }
    if (okPressed())    { soundClick(); if (lsLesson == (int)LESSON_COUNT) { lsLesson = 0; lgFromGames = false; startLegends(); } else lsBeginChosen(); }

  } else if (lsPhase == LS_INTRO) {
    if (okPressed()) { soundClick(); lessonReplay(); }
    if (leftPressed() && lsIntroIdx > 0) {
      soundClick(); lsIntroIdx--; renderLessonAll(); lessonReplay();
    }
    if (rightPressed()) {
      soundClick();
      if (lsIntroIdx < 3) {
        lsIntroIdx++; renderLessonAll(); lessonReplay();
      } else {
        lsPhase = LS_PRACTICE;
        lsQuestion = 0;
        lsBuildQueue();
        lsNewQuestion();
      }
    }

  } else if (lsPhase == LS_PRACTICE) {
    if (lsWaitUntil) {   // esperar mientras se muestra la respuesta
      if ((long)(millis() - lsWaitUntil) >= 0) { lsWaitUntil = 0; lsAfterAnswer(); }
      if (currentMode == MODE_LESSON) updateLessonTeco();
      return;
    }
    if (leftPressed())  { lsSel = (lsSel + 2) % 3; soundClick(); renderLessonAll(); }
    if (rightPressed()) { lsSel = (lsSel + 1) % 3; soundClick(); renderLessonAll(); }
    if (okPressed()) {
      logAnswer("leccion", LESSON_WORDS[lsTarget].text, LESSON_WORDS[lsOptions[lsSel]].text, lsOptions[lsSel] == lsTarget);
      if (lsOptions[lsSel] == lsTarget) {
        aciertosHoy++;
        if (wordMiss[lsTarget] > 0) wordMiss[lsTarget]--;
        soundClick();
        playMilpa(AUDIO_MILPA_BIEN);
        lsSetMood(MOOD_BIEN);
        lsAfter = LS_AFTER_NEXT;
#if LESSON_SHOW_SPANISH
        {
          char msg[40];
          snprintf(msg, sizeof(msg), "%s = %s", LESSON_WORDS[lsTarget].text, LESSON_WORDS[lsTarget].es);
          tft.fillRoundRect(LS_PANEL_X + 20, LS_CARD_Y + LS_CARD + 12, LS_PANEL_W - 40, 24, 6, SELECT_COLOR);
          setTxtColor(COLOR_TEXT);
          int ms = strlen(msg) > 17 ? 1 : 2;
          printCentered(msg, 160, LS_CARD_Y + LS_CARD + 17 + (2 - ms) * 4, ms);
        }
        lsWaitUntil = millis() + 1100;
#else
        lsWaitUntil = millis() + 600;
#endif
      } else {
        if (wordMiss[lsTarget] < 9) wordMiss[lsTarget]++;
        markProgress();
        // Volver a preguntar al final (si cabe y no esta pendiente)
        bool pending = false;
        for (int q = lsQuestion + 1; q < lsQueueLen; q++) if (lsQueue[q] == lsTarget) pending = true;
        if (!pending && lsQueueLen < LESSON_MAX_Q) lsQueue[lsQueueLen++] = lsTarget;
        soundBack();
        lsSetMood(MOOD_UY);
        playMilpa(AUDIO_MILPA_OTRAVEZ);
        lsAfter = LS_AFTER_REPLAY;
        lsWaitUntil = millis() + 900;
      }
    }

  } else if (lsPhase == LS_DONE) {
    if (millis() - lsDoneAt > 3500 || okPressed()) {
      exitLesson(true);
      return;
    }
  }

  if (currentMode == MODE_LESSON) updateLessonTeco();
}

// Continuar al terminar la espera tras una respuesta
void lsAfterAnswer() {
  if (lsAfter == LS_AFTER_REPLAY) {
    lsPlayWord(lsTarget);
    return;
  }
  lsQuestion++;
  if (lsQuestion >= lsQueueLen) {
    lsPhase = LS_DONE;
    lsDoneAt = millis();
    learn = 9;
    lessonsDone |= (1ULL << lsLesson);
    { int lw[4]; for (int k = 0; k < 4; k++) lw[k] = LESSONS[lsLesson].words[k]; coins += 3; queueSticker(pickStickerFrom(lw, 4)); }
    if (lsLesson == lessonNext % (int)LESSON_COUNT) lessonNext = (lsLesson + 1) % LESSON_COUNT;
    markProgress();
    renderLessonAll();
    lsSetMood(MOOD_BIEN);
    tecoMoodUntil = millis() + 3000;
  } else {
    lsNewQuestion();
  }
}

// Viajes

#define VJ_BOSQUE  0
#define VJ_TIKAL   1
#define VJ_MERCADO 2

const char* VJ_NAMES[3] = { "Biotopo", "Tikal", "Mercado" };
// Rondas por destino segun el nivel
int vjRounds(int d) { int base[3] = { 3, 5, 3 }; return base[d] + (lvlViaje[d] >= 4 ? 2 : (lvlViaje[d] >= 2 ? 1 : 0)); }
int vjTotalRounds = 5;   // rondas del viaje (fijas al salir aunque suba el nivel)
// pasaje de cada destino, en pwaq
const int VJ_PRICE[3] = { 5, 8, 10 };
int vjMissing = 0;   // monedas faltantes para el viaje elegido (0 = no mostrar)

// palabras de cada destino (indices en LESSON_WORDS)
const int VJ_ANIMALS[8] = { 48, 26, 49, 50, 100, 24, 23, 25 };   // q'uq', k'oy, b'alam malax (mariposa), b'alam, tz'ikin, kuk, kej, ut
// mercado: 11 alimentos
#define VJ_FOOD_COUNT 11
const int VJ_FOODS[VJ_FOOD_COUNT] = { 97, 98, 45, 44, 40, 104, 41, 46, 39, 42, 43 };   // aranxax, saq'ul, oj, ik, way, xkoya', kinaq'
int vjPoolSize(int d) {
  if (d == VJ_MERCADO) return imin(VJ_FOOD_COUNT, 2 + 2 * lvlViaje[d]);
  return imin(8, 3 + lvlViaje[d]);
}

// Album: animales encontrados, alimentos comprados y numeros
uint8_t albAnimals = 0;
uint16_t albFoods = 0;   // un bit por alimento (11)
uint32_t albNumbers = 0;   // bit n = numero n (1 a 19)
int vjNewItems = 0;   // piezas nuevas del album en este viaje
int vjNewWord = -1;   // primera pieza nueva (para mostrarla)
int bestStars[3] = { 0, 0, 0 };
int vjStars = 0;
bool vjRecord = false;
int vjAlbumPage = 0;   // 0 animales, 1 alimentos, 2 numeros

int vjIndexOf(const int* arr, int word) { int n = (arr == VJ_FOODS) ? VJ_FOOD_COUNT : 8; for (int i = 0; i < n; i++) if (arr[i] == word) return i; return -1; }
// Posicion de cada pieza del album: animales en 4x2, alimentos en 6x2
void albumSlot(int page, int i, int* cx, int* cy, int* sz) {
  if (page == 0) { *sz = 62; *cx = 24 + (i % 4) * 70; *cy = 44 + (i / 4) * 86; }
  else { *sz = 44; *cx = 16 + (i % 6) * 49; *cy = 50 + (i / 6) * 82; }
}

int bitCount(uint32_t v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }
void vjCollect(int kind, int idx, int word) {
  if (idx < 0) return;
  bool fresh = false;
  if (kind == 0 && !(albAnimals & (1 << idx))) { albAnimals |= (1 << idx); fresh = true; }
  if (kind == 1 && !(albFoods & (1 << idx)))   { albFoods |= (1 << idx); fresh = true; }
  if (kind == 2 && !(albNumbers & (1UL << idx))) { albNumbers |= (1UL << idx); fresh = true; }
  if (fresh) { vjNewItems++; if (vjNewWord < 0) vjNewWord = word; }
}


ViajePhase vjPhase = VJ_MAP;
int vjDest = 0;
int vjSel = 0;
int vjRound = 0;
int vjEarned = 0;
unsigned long vjWaitUntil = 0;
int vjAfter = 0;   // accion al terminar la espera
unsigned long vjAnimAt = 0;
int vjAnimFrame = 0;
int vjJaguarFoot();
#define VEND_X 62
#define VEND_Y 112
#define VEND_COUNTER_Y 172
int vjQFrame = 0;   // Teco quetzal: 0 normal, 1 feliz, 2 duda
unsigned long vjBlinkAt = 0;
bool vjVendTalking = false;   // vendedora hablando al pedir
unsigned long vjVendTalkUntil = 0;

// Bosque
int vjSpots[4];   // animal escondido en cada arbusto
bool vjRevealed[4];
int vjTarget = 0;
// Tikal
int vjNumber = 1;
int vjOptions[3];
int vjStep = 0;
// Mercado
bool vjPaying = false;
int vjPrice = 1;
int vjItems[3];
// Retos: elegir un tipo por ronda segun el nivel
enum { VJA_NONE, VJA_NEXT_ROUND, VJA_HIDE, VJA_PAY, VJA_REPLAY, VJA_CONTINUE };
void vjWait(unsigned long ms, int after);
void vjWordOptions();
int vjType = 0, vjLastType = -1;
int vjShown = 0;   // Tikal: numero mostrado
int vjBuy[2];   // mercado: pedido de la vendedora
int vjNeed = 1, vjGot = 0;   // cantidad pedida y alimentos comprados
int vjPrices[2];

const uint16_t VJ_BUSH  = RGB565(58, 118, 62);
const uint16_t VJ_BUSH2 = RGB565(76, 140, 72);
const uint16_t VJ_GOLD  = RGB565(240, 196, 60);

int vjNumberWord(int n) {   // indice en LESSON_WORDS del numero n (1 a 19)
  if (n <= 5) return 4 + n;
  if (n <= 10) return 8 + n;
  return 52 + n;
}

void vjShuffle(int* a, int n) {
  for (int i = n - 1; i > 0; i--) { int j = random(0, i + 1); int t = a[i]; a[i] = a[j]; a[j] = t; }
}

// Dibujo
const uint16_t STAR_ROWS[9] = { 0x010, 0x010, 0x038, 0x1FF, 0x0FE, 0x07C, 0x0EE, 0x0C6, 0x082 };
void vjStar(int bx, int by, int bw, int rows, int x0, int y0, bool full) {
  for (int r = 0; r < 9; r++) {
    int y = y0 + r; if (y < by || y >= by + rows) continue;
    for (int c = 0; c < 9; c++) {
      if (!(STAR_ROWS[r] & (0x100 >> c))) continue;
      int x = x0 + c; if (x < bx || x >= bx + bw) continue;
      bool left = c > 0 && (STAR_ROWS[r] & (0x100 >> (c - 1)));
      bool right = c < 8 && (STAR_ROWS[r] & (0x100 >> (c + 1)));
      bool edge = (r == 0 || r == 8 || !left || !right);
      bandBuffer[(y - by) * bw + (x - bx)] = full ? (edge ? RGB565(190, 120, 20) : VJ_GOLD) : (edge ? LS_CARD_BORDER : LS_PANEL_COLOR);
    }
  }
}
void vjStars3(int bx, int by, int bw, int rows, int cx, int y0, int n) {
  for (int k = 0; k < 3; k++)
    hbSpriteMini(bx, by, bw, rows, k < n ? &kitEstrellaLlenaFrames[0] : &kitEstrellaVaciaFrames[0], cx - 24 + k * 16, y0 - 3);
}
void vjMaya(int bx, int by, int bw, int rows, int n, int x0, int y0, int size) {
  int bars = n / 5, dots = n % 5;
  int barW = size * 6 / 10, barH = imax(4, size / 11), gap = imax(3, size / 16), dotR = imax(3, size / 16);
  int totalH = bars * barH + imax(0, bars - 1) * gap + (dots ? (2 * dotR + (bars ? gap : 0)) : 0);
  int y = y0 + (size - totalH) / 2;
  if (dots) {
    int span = dots * 2 * dotR + (dots - 1) * gap;
    int dx = x0 + (size - span) / 2 + dotR;
    for (int d = 0; d < dots; d++) {
      int cx = dx + d * (2 * dotR + gap), cy = y + dotR;
      for (int yy = imax(cy - dotR, by); yy <= imin(cy + dotR, by + rows - 1); yy++)
        for (int xx = imax(cx - dotR, bx); xx <= imin(cx + dotR, bx + bw - 1); xx++)
          if ((xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) <= dotR * dotR)
            bandBuffer[(yy - by) * bw + (xx - bx)] = MAYA_COLOR;
    }
    y += 2 * dotR + gap;
  }
  for (int b = 0; b < bars; b++) {
    int bx0 = x0 + (size - barW) / 2;
    lsRect(bx, by, bw, rows, bx0, y, bx0 + barW, y + barH, MAYA_COLOR, 2);
    y += barH + gap;
  }
}

void vjCoinsPile(int bx, int by, int bw, int rows, int n, int x0, int y0, int size) {
  // Dibujar pocas monedas grandes, 3 por fila
  int perRow = (n > 6) ? 4 : 3, r = (n > 6) ? 5 : 7, st = (n > 6) ? 12 : 16;
  int nRows = (n + perRow - 1) / perRow;
  int oy = y0 + size / 2 - (nRows - 1) * st / 2;
  for (int i = 0; i < n; i++) {
    int inRow = imin(perRow, n - (i / perRow) * perRow);
    int cx = x0 + size / 2 + (i % perRow) * st - (inRow - 1) * st / 2, cy = oy + (i / perRow) * st;
    for (int yy = imax(cy - r, by); yy <= imin(cy + r, by + rows - 1); yy++)
      for (int xx = imax(cx - r, bx); xx <= imin(cx + r, bx + bw - 1); xx++) {
        int d = (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy);
        if (d <= r * r) bandBuffer[(yy - by) * bw + (xx - bx)] = d >= (r - 1) * (r - 1) ? MAYA_COLOR : VJ_GOLD;
      }
  }
}

void vjBush(int bx, int by, int bw, int rows, int cx, int ybase) {
  const int blobs[4][3] = { { -20, -14, 18 }, { 0, -24, 22 }, { 20, -14, 18 }, { 0, -8, 24 } };
  for (int k = 0; k < 4; k++) {
    int ox = cx + blobs[k][0], oy = ybase + blobs[k][1], r = blobs[k][2];
    for (int y = imax(oy - r, by); y <= imin(oy + r, by + rows - 1); y++)
      for (int x = imax(ox - r, bx); x <= imin(ox + r, bx + bw - 1); x++) {
        int d = (x - ox) * (x - ox) + (y - oy) * (y - oy);
        if (d <= r * r) bandBuffer[(y - by) * bw + (x - bx)] = d >= (r - 2) * (r - 2) ? RGB565(40, 80, 44) : (k % 2 ? VJ_BUSH2 : VJ_BUSH);
      }
  }
}

// Atenuar un color del fondo para destacar el juego
uint16_t vjSoften(uint16_t c) {
  int r = ((c >> 11) & 31) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
  int lum = (r * 3 + g * 6 + b) / 10;
  r = (r * 45 + lum * 25 + 241 * 30) / 100;
  g = (g * 45 + lum * 25 + 224 * 30) / 100;
  b = (b * 45 + lum * 25 + 194 * 30) / 100;
  return RGB565(r, g, b);
}

uint16_t vjBgPixel(int x, int y) {
  // Mantener a Teco en casa durante el mapa
  if (vjPhase == VJ_MAP) return pgm_read_word(&viajeMapaPal[pgm_read_byte(&viajeMapaPx[y * FONDO_W + x])]);
  if (vjPhase == VJ_ALBUM) return bgPixel(x, y);
  const ViajeFondo& f = viajeFondos[vjDest];
  uint16_t c = pgm_read_word(&f.pal[pgm_read_byte(&f.px[y * FONDO_W + x])]);
  // No atenuar el bosque ni Tikal (ya tienen colores suaves)
  return c;   // fondos con colores suaves
}

const MiniSprite* vjTeco() { return &viajeTecoFrames[vjAnimFrame]; }

void renderViajeRegion(int x0, int y0, int w, int h) {
  int maxRows = imin(h, (FONDO_W * BAND_H) / w);
  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++) bandBuffer[r * w + x] = vjBgPixel(x0 + x, by + r);

    if (vjPhase == VJ_MAP) {
      // Mapa: marcar el destino elegido y los recuerdos obtenidos
      for (int i = 0; i < 3; i++) {
        if (i == vjSel) hbNine(x0, by, w, rows, &kitSeleccionFrames[0], VJ_MARK_X[i] - 18, VJ_MARK_Y[i] - 18, VJ_MARK_X[i] + 18, VJ_MARK_Y[i] + 18, 3, 2);
        if (souvenirs & (1 << i)) hbSpriteMini(x0, by, w, rows, &viajeRecuerdoFrames[i], VJ_MARK_X[i] + 14, VJ_MARK_Y[i] - 4);
      }
      // Dibujar la placa con la informacion del destino
      hbNine(x0, by, w, rows, &kitPlacaFrames[0], 190, 6, 316, 196, 4, 2);
      if (vjSel < 3) vjStars3(x0, by, w, rows, 253, 92, bestStars[vjSel]);
      hbSpriteMini(x0, by, w, rows, vjTeco(), 227, 112);
      // Dibujar el boton del album
      hbNine(x0, by, w, rows, vjSel == 3 ? &kitSeleccionFrames[0] : &kitCasillaFrames[0], 190, 202, 316, 236, 3, 2);
      if (vjSel != 3) {} else hbNine(x0, by, w, rows, &kitCasillaFrames[0], 196, 208, 310, 230, 3, 2);
    } else if (vjPhase == VJ_PLAY && vjDest == VJ_BOSQUE) {
      for (int i = 0; i < 4 && vjType != 1; i++) {   // dibujar primero los arbustos (excepto en "cuantos hay")
        int cx = 46 + i * 76;
        const MiniSprite* bsh = &arbustoFrames[i % 3];
        hbSpriteMini(x0, by, w, rows, bsh, cx - bsh->w / 2, 222 - bsh->h);   // base sobre el pasto
      }
      for (int i = 0; i < 4; i++)   // y encima el animal descubierto
        if (vjRevealed[i]) lsCard(x0, by, w, rows, vjSpots[i], 46 + i * 76 - 28, 84, 56, false);
      if (vjType == 1) {
        // Cuantos hay: repetir el mismo animal en dos filas
        for (int k = 0; k < vjNumber; k++) {
          int cx = (k < 5) ? 14 + k * 60 : 44 + (k - 5) * 60;
          int cy = (k < 5) ? 64 : 122;
          lsCard(x0, by, w, rows, vjTarget, cx, cy, 52, false);
        }
        hbSpriteMini(x0, by, w, rows, &tecoQuetzalFrames[vjQFrame], 8, 238 - tecoQuetzalFrames[0].h);
      } else {
        // Dibujar a Teco quetzal frente al arbusto elegido
        const MiniSprite* tq = &tecoQuetzalFrames[vjQFrame];
        hbSpriteMini(x0, by, w, rows, tq, 46 + vjSel * 76 - tq->w / 2 - 6, 238 - tq->h);
      }

    } else if (vjPhase == VJ_PLAY && vjDest == VJ_TIKAL) {
      if (vjType == 1) {
        // Nombre -> numero maya: tres tarjetas en columna a la izquierda
        for (int i = 0; i < 3; i++) {
          int cy = 62 + i * 58;
          if (i == vjSel) hbNine(x0, by, w, rows, &kitSeleccionFrames[0], 6, cy - 4, 70, cy + 56, 3, 2);
          hbNine(x0, by, w, rows, &kitMarcoFrames[0], 12, cy + 2, 64, cy + 50, 3, 1);
          vjMaya(x0, by, w, rows, vjOptions[i], 14, cy + 4, 46);
        }
      } else {
        hbNine(x0, by, w, rows, &kitMarcoFrames[0], 10, 64, 90, 144, 3, 1);
        vjMaya(x0, by, w, rows, vjShown, 13, 67, 74);
      }
      // Dibujar a Teco jaguar un escalon mas arriba por acierto
      const MiniSprite* tj = &tecoJaguarFrames[vjQFrame];
      hbSpriteMini(x0, by, w, rows, tj, 160 - tj->w / 2 - 6, vjJaguarFoot() - tj->h);

    } else if (vjPhase == VJ_PLAY && vjDest == VJ_MERCADO) {
      // Dibujar a la vendedora dentro del puesto, detras del mostrador
      {
        int vf = vjVendTalking ? 1 : (vjQFrame == 1 ? 2 : (vjQFrame == 2 ? 3 : 0));
        hbSpriteMini(x0, by, w, rows, &vendedoraFrames[vf], VEND_X, VEND_Y);
        for (int y = imax(VEND_COUNTER_Y, by); y < imin(VEND_Y + vendedoraFrames[0].h, by + rows); y++)
          for (int x = imax(VEND_X, x0); x < imin(VEND_X + vendedoraFrames[0].w, x0 + w); x++)
            bandBuffer[(y - by) * w + (x - x0)] = vjBgPixel(x, y);
      }
      for (int i = 0; i < 3; i++) {
        int cx = 12 + i * 76;
        if (vjPaying) {
          bool sel = (i == vjSel);
          if (sel) hbNine(x0, by, w, rows, &kitSeleccionFrames[0], cx - 6, 174, cx + 66, 240, 3, 2);
          hbNine(x0, by, w, rows, &kitMarcoFrames[0], cx, 180, cx + 60, 236, 3, 1);
          vjCoinsPile(x0, by, w, rows, vjOptions[i], cx, 178, 60);
        } else {
          lsCard(x0, by, w, rows, vjItems[i], cx, 180, 56, i == vjSel);
        }
      }
      // Dibujar a Teco de compras sobre el tejido
      const MiniSprite* tm = &tecoMercadoFrames[vjQFrame];
      hbSpriteMini(x0, by, w, rows, tm, 248, 236 - tm->h);
    } else if (vjPhase == VJ_ALBUM) {
      lsRect(x0, by, w, rows, 10, 8, 310, 232, LS_CARD_BORDER, 10);
      lsRect(x0, by, w, rows, 12, 10, 308, 230, LS_PANEL_COLOR, 9);
      if (vjAlbumPage < 2) {
        const int* arr = (vjAlbumPage == 0) ? VJ_ANIMALS : VJ_FOODS;
        uint16_t bits = (vjAlbumPage == 0) ? albAnimals : albFoods;
        int n = (vjAlbumPage == 0) ? 8 : VJ_FOOD_COUNT;
        for (int i = 0; i < n; i++) {
          int cx, cy, sz; albumSlot(vjAlbumPage, i, &cx, &cy, &sz);
          if (bits & (1 << i)) lsCard(x0, by, w, rows, arr[i], cx, cy, sz, false);
          else {
            lsRect(x0, by, w, rows, cx, cy, cx + sz, cy + sz, LS_CARD_BORDER, 6);
            lsRect(x0, by, w, rows, cx + 2, cy + 2, cx + sz - 2, cy + sz - 2, RGB565(226, 214, 192), 5);
          }
        }
      } else if (vjAlbumPage == 3) {
        // Dibujar las primeras 15 estampas en 5 x 3
        int k = 0;
        for (int wd = 0; wd < (int)LESSON_WORD_COUNT && k < 15; wd++) if (hasSticker(wd)) {
          lsCard(x0, by, w, rows, wd, 26 + (k % 5) * 55, 44 + (k / 5) * 56, 52, false);
          k++;
        }
      } else {
        for (int n = 1; n <= 19; n++) {
          int i = n - 1, cx = 22 + (i % 7) * 40, cy = 44 + (i / 7) * 54;
          lsRect(x0, by, w, rows, cx, cy, cx + 36, cy + 36, LS_CARD_BORDER, 5);
          bool got = albNumbers & (1UL << n);
          lsRect(x0, by, w, rows, cx + 2, cy + 2, cx + 34, cy + 34, got ? LS_CARD_COLOR : RGB565(226, 214, 192), 4);
          if (got) vjMaya(x0, by, w, rows, n, cx + 3, cy + 3, 30);
        }
      }
    } else if (vjPhase == VJ_REWARD) {
      // Mostrar el recuerdo sobre Teco con el lugar de fondo
      hbNine(x0, by, w, rows, &kitMarcoFrames[0], 120, 70, 200, 150, 3, 1);
      lsSpriteScaled(x0, by, w, rows, &viajeRecuerdoFrames[vjDest], 128, 78, 2);
      if (vjDest == VJ_BOSQUE) hbSpriteMini(x0, by, w, rows, &tecoQuetzalFrames[1], 160 - tecoQuetzalFrames[0].w / 2 - 6, 208 - tecoQuetzalFrames[0].h);
      else if (vjDest == VJ_TIKAL) hbSpriteMini(x0, by, w, rows, &tecoJaguarFrames[1], 160 - tecoJaguarFrames[0].w / 2 - 6, 208 - tecoJaguarFrames[0].h);
      else hbSpriteMini(x0, by, w, rows, &tecoMercadoFrames[1], 160 - tecoMercadoFrames[0].w / 2, 208 - tecoMercadoFrames[0].h);
      lsRect(x0, by, w, rows, 40, 210, 280, 236, LS_CARD_BORDER, 8);
      lsRect(x0, by, w, rows, 42, 212, 278, 234, LS_PANEL_COLOR, 7);
      vjStars3(x0, by, w, rows, 160, 54, vjStars);
    }
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void vjBanner(const char* kaq, const char* es) {
  if (vjPhase == VJ_PLAY && vjDest == VJ_MERCADO) {
    // Dibujar el globo de la vendedora con la colita hacia ella
    drawDialogBox(kaq, es, "Vendedora", RGB565(140, 60, 160));
    return;
  }
  tftNine(&kitPlacaFrames[0], 56, 2, 264, 58, 4, 2);
  setTxtColor(COLOR_TEXT);
  printCenteredW(kaq, 160, 11, 2, 180);
  setTxtColor(LS_SPANISH_COLOR);
  printCenteredW(es, 160, 37, 1, 180);
}

// Dibujar tres botones con nombres de numeros (Tikal y "cuantos hay")
void vjWordOptions() {
  for (int i = 0; i < 3; i++) {
    int x = 108 + i * 70;
    bool sel = (i == vjSel);
    if (sel) tftNine(&kitSeleccionFrames[0], x - 4, 192, x + 68, 230, 3, 2);
    tftNine(&kitCasillaFrames[0], x, 196, x + 64, 226, 3, 2);
    setTxtColor(COLOR_TEXT);
    printCenteredW(LESSON_WORDS[vjNumberWord(vjOptions[i])].text, x + 32, 204, 1, 56);
  }
}

void vjTexts() {
  char buf[40];
  if (vjPhase == VJ_MAP) {
    const char* name[3] = { "Biotopo", "Tikal", "Mercado" };
    const char* sub[3]  = { "del Quetzal", "Pet\x82n", "del altiplano" };
    setTxtColor(COLOR_TEXT);
    printCenteredW("\xA8" "A d\xA2nde vamos?", 253, 22, 1, 110);
    if (vjSel < 3) {
      printCenteredW(name[vjSel], 253, 38, 2, 112);
      printCenteredW(sub[vjSel], 253, 62, 1, 112);
      char lv[24]; snprintf(lv, sizeof(lv), "Nivel %d   Pasaje %d", lvlViaje[vjSel], VJ_PRICE[vjSel]);
      setTxtColor(RGB565(150, 90, 50));
      printCenteredW(lv, 253, 76, 1, 112);
    } else {
      printCenteredW("Mi \xA0lbum", 253, 44, 2, 112);
      setTxtColor(RGB565(150, 90, 50));
      printCentered("tu colecci\xA2n", 253, 74, 1);
    }
    // Mostrar las monedas y, si no alcanzan, cuantas faltan
    if (vjSel < 3 && coins < VJ_PRICE[vjSel]) {
      setTxtColor(RGB565(190, 50, 40));
      snprintf(buf, sizeof(buf), "Tienes %d, faltan %d", coins, VJ_PRICE[vjSel] - coins);
    } else {
      setTxtColor(COLOR_TEXT);
      snprintf(buf, sizeof(buf), "pwaq: %d", coins);
    }
    printCenteredW(buf, 253, 176, 1, 116);
    printCenteredW("Mi \xA0lbum", 253, 214, 1, 110);
  } else if (vjPhase == VJ_PLAY) {
    if (vjDest == VJ_BOSQUE) {
      bool showing = false;
      for (int i = 0; i < 4; i++) if (vjRevealed[i]) showing = true;
      if (vjType == 1) { vjBanner(LESSON_WORDS[vjTarget].text, "\xA8" "Cu\xA0ntos hay?"); vjWordOptions(); }
      else if (vjType == 2 && showing && vjWaitUntil) vjBanner("\xADMira bien!", "luego se esconden");
      else vjBanner(LESSON_WORDS[vjTarget].text, vjType == 2 ? "\xA8" "D\xA2nde estaba?" : "Busca en los arbustos");
    } else if (vjDest == VJ_TIKAL) {
      if (vjType == 1) vjBanner(LESSON_WORDS[vjNumberWord(vjNumber)].text, "\xA8" "Cu\xA0l es su n\xA3mero?");
      else {
        if (vjType == 2) vjBanner("?", "\xA8" "Qu\x82 n\xA3mero sigue?");
        vjWordOptions();
      }
    } else {
      if (vjPaying) {
        char pr[32];
        snprintf(pr, sizeof(pr), "%s pwaq", LESSON_WORDS[vjNumberWord(vjPrice)].text);
        vjBanner(pr, vjNeed == 2 && lvlViaje[VJ_MERCADO] >= 4 ? "Es el total. Paga con esas monedas" : "Paga con esa cantidad de monedas");
      } else if (vjNeed == 2 && vjGot == 0) {
        // Mercado: Teco pide dos alimentos
        char two[40];
        snprintf(two, sizeof(two), "%s y %s", LESSON_WORDS[vjBuy[0]].text, LESSON_WORDS[vjBuy[1]].text);
        drawTecoBubble(two, "Teco quiere comprar estos dos. \xA8" "Cu\xA0les son?");
      } else if (vjNeed == 2) {
        drawTecoBubble(LESSON_WORDS[(vjGot & 1) ? vjBuy[1] : vjBuy[0]].text, "Ahora falta este. \xA8" "Cu\xA0l es?");
      } else {
        drawTecoBubble(LESSON_WORDS[vjTarget].text, "Teco quiere comprar esto. \xA8" "Cu\xA0l es?");
      }
    }
  } else if (vjPhase == VJ_ALBUM) {
    const char* titles[4] = { "Animales del bosque", "Mercado", "N\xA3meros mayas", "Estampas" };
    int have = (vjAlbumPage == 0) ? bitCount(albAnimals) : (vjAlbumPage == 1) ? bitCount(albFoods) : (vjAlbumPage == 2) ? bitCount(albNumbers) : stickerCount();
    int total = (vjAlbumPage == 3) ? (int)LESSON_WORD_COUNT : (vjAlbumPage == 2) ? 19 : (vjAlbumPage == 1 ? VJ_FOOD_COUNT : 8);
    snprintf(buf, sizeof(buf), "%s  %d/%d", titles[vjAlbumPage], have, total);
    setTxtColor(COLOR_TEXT);
    printCenteredW(buf, 160, 18, 2, 280);
    if (vjAlbumPage < 2) {
      const int* arr = (vjAlbumPage == 0) ? VJ_ANIMALS : VJ_FOODS;
      uint16_t bits = (vjAlbumPage == 0) ? albAnimals : albFoods;
      int n = (vjAlbumPage == 0) ? 8 : VJ_FOOD_COUNT;
      for (int i = 0; i < n; i++) {
        int cx, cy, sz; albumSlot(vjAlbumPage, i, &cx, &cy, &sz);
        if (bits & (1 << i)) { setTxtColor(COLOR_TEXT); printCenteredW(LESSON_WORDS[arr[i]].text, cx + sz / 2, cy + sz + 4, 1, vjAlbumPage == 0 ? 76 : sz + 4); }
        else { setTxtColor(LS_CARD_BORDER); printCentered("?", cx + sz / 2, cy + sz / 2 - 9, sz > 50 ? 3 : 2); }
      }
    } else if (vjAlbumPage == 3) {
      if (stickerCount() == 0) { setTxtColor(LS_SPANISH_COLOR); printCenteredW("Viaja y termina lecciones para ganar estampas", 160, 110, 1, 270); }

    } else {
      for (int n = 1; n <= 19; n++) if (!(albNumbers & (1UL << n))) {
        int i = n - 1, cx = 22 + (i % 7) * 40, cy = 44 + (i / 7) * 54;
        setTxtColor(LS_CARD_BORDER); printCentered("?", cx + 18, cy + 10, 2);
      }
    }
    setTxtColor(COLOR_TEXT);
    printCentered("< >: p\xA0gina    OK o BACK: mapa", 160, 216, 1);
  } else if (vjPhase == VJ_REWARD) {
    const char* won[4] = { "", "Ganaste 1 estampa", "Ganaste 2 estampas", "Ganaste 3 estampas" };
    vjBanner("\xADUTZ!", won[imax(1, imin(3, vjStars))]);
    setTxtColor(COLOR_TEXT);
    if (lastLevelChange > 0) snprintf(buf, sizeof(buf), "\xAD" "Nivel %d!   Tienes %d pwaq", lvlViaje[vjDest], coins);
    else if (vjNewItems > 0) snprintf(buf, sizeof(buf), "Nuevo en tu \xA0lbum: %s", LESSON_WORDS[vjNewWord].text);
    else if (vjRecord) snprintf(buf, sizeof(buf), "\xAD" "R\x82" "cord!   Tienes %d pwaq", coins);
    else snprintf(buf, sizeof(buf), "Tienes %d pwaq", coins);
    printCenteredW(buf, 160, 219, 1, 228);
  }
}

void renderViajeAll() { renderViajeRegion(0, 0, FONDO_W, FONDO_H); vjTexts(); }

// Rondas
int vjTypesAllowed(int d) {
  int lv = lvlViaje[d];
  if (d == VJ_MERCADO) return 2;
  return lv >= 2 ? 3 : 2;
}

void vjPickType() {
  int n = vjTypesAllowed(vjDest);
  if (n == 1) { vjType = 0; return; }
  int t; do { t = random(0, n); } while (t == vjLastType);
  vjType = t; vjLastType = t;
}

// Generar tres opciones numericas distintas del 1 al hi
void vjNumOptions(int correct, int hi, bool neighbors) {
  int a = correct, b = correct;
  if (neighbors) {
    a = (correct > 1) ? correct - 1 : correct + 2;
    b = (correct < hi) ? correct + 1 : correct - 2;
  } else {
    while (a == correct) a = random(1, hi + 1);
    while (b == correct || b == a) b = random(1, hi + 1);
  }
  int o[3] = { correct, a, b }; vjShuffle(o, 3);
  for (int i = 0; i < 3; i++) vjOptions[i] = o[i];
}

int vjJaguarFoot() { return 194 - vjStep * (72 / imax(1, vjTotalRounds)); }   // de 194 (arriba de los botones) a 122

void vjNewRound() {
  vjQFrame = 0;
  markQuestionStart();
  vjPickType();
  if (vjDest == VJ_BOSQUE) {
    int n = vjPoolSize(VJ_BOSQUE);
    vjTarget = bagNext(BAG_ANIMAL, VJ_ANIMALS, n);   // no repetir hasta agotar todos
    int pool[8], m = 0; for (int i = 0; i < n; i++) if (VJ_ANIMALS[i] != vjTarget) pool[m++] = VJ_ANIMALS[i];
    vjShuffle(pool, m);
    int tpos = random(0, 4), o = 0;
    for (int i = 0; i < 4; i++) { vjSpots[i] = (i == tpos) ? vjTarget : pool[o++]; vjRevealed[i] = false; }
    vjSel = 0;
    if (vjType == 1) {
      // Cuantos hay: repetir el mismo animal
      vjNumber = random(2, imin(9, 3 + lvlViaje[VJ_BOSQUE]) + 1);
      vjNumOptions(vjNumber, 9, lvlViaje[VJ_BOSQUE] >= 4);
      vjSel = 1;
      renderViajeAll();
      lsPlayWord(vjTarget);
    } else if (vjType == 2) {
      // Memoria: mostrar todos un momento y luego esconderlos
      for (int i = 0; i < 4; i++) vjRevealed[i] = true;
      renderViajeAll();
      vjWait(2600 - 200 * lvlViaje[VJ_BOSQUE], VJA_HIDE);
    } else {
      renderViajeAll();
      lsPlayWord(vjTarget);
    }
  } else if (vjDest == VJ_TIKAL) {
    // nivel 1: del 1 al 5
    const int maxN[6] = { 0, 5, 10, 13, 19, 19 };
    int top = maxN[lvlViaje[VJ_TIKAL]];
    { int vals[19], k = 0; for (int v = 1; v <= top; v++) vals[k++] = v;
      vjNumber = bagNext(BAG_NUM, vals, k);
      if (vjType == 2 && vjNumber < 2) vjNumber = bagNext(BAG_NUM, vals, k); }
    vjShown = (vjType == 2) ? vjNumber - 1 : vjNumber;
    vjNumOptions(vjNumber, imax(top, 4), lvlViaje[VJ_TIKAL] >= 5 || vjType == 2);
    vjSel = 1;
    renderViajeAll();
  } else {
    int n = vjPoolSize(VJ_MERCADO);
    int want = bagNext(BAG_FOOD, VJ_FOODS, n);
    int pool[VJ_FOOD_COUNT], m = 0; for (int i = 0; i < n; i++) if (VJ_FOODS[i] != want) pool[m++] = VJ_FOODS[i];
    vjShuffle(pool, m);
    int first = random(0, 3), o = 0;
    for (int i = 0; i < 3; i++) vjItems[i] = (i == first) ? want : pool[o++];
    vjNeed = (vjType == 1) ? 2 : 1;
    vjGot = 0;
    vjBuy[0] = want;
    vjBuy[1] = vjItems[(first + 1 + random(0, 2)) % 3];
    vjTarget = vjBuy[0];
    vjPaying = false;
    vjVendTalking = false;
    vjSel = 1;
    renderViajeAll();
    lsPlayWord(vjTarget);
  }
}

void vjStartPaying() {
  markQuestionStart();
  vjVendTalking = true; vjVendTalkUntil = millis() + 1500;
  vjPaying = true;
  int topP = imin(5, 2 + lvlViaje[VJ_MERCADO]);   // nivel 1: 1 a 3 monedas ... nivel 3+: 1 a 5
  if (vjNeed == 2 && lvlViaje[VJ_MERCADO] >= 4) {
    // Nivel 4+: pagar el total de los dos alimentos
    int topEach = (lvlViaje[VJ_MERCADO] >= 5) ? 5 : 3;   // nivel 5: total de hasta 10
    vjPrices[0] = random(1, topEach + 1); vjPrices[1] = random(1, topEach + 1);
    vjPrice = vjPrices[0] + vjPrices[1];
  } else {
    vjPrice = random(1, topP + 1);
  }
  vjNumOptions(vjPrice, imax(5, vjPrice + 1), lvlViaje[VJ_MERCADO] >= 5);
  vjSel = 1;
  renderViajeAll();
  lsPlayWord(vjNumberWord(vjPrice));
}

// Estampas por destino: animales (Biotopo), numeros (Tikal), comida (Mercado)
int vjThemePool(int d, int* out) {
  int n = 0;
  if (d == VJ_MERCADO) { for (int i = 0; i < VJ_FOOD_COUNT; i++) out[n++] = VJ_FOODS[i]; return n; }
  for (int w = 0; w < (int)LESSON_WORD_COUNT && n < 24; w++) {
    const LessonWord& L = LESSON_WORDS[w];
    if (d == VJ_TIKAL && L.folder == 2) out[n++] = w;
    if (d == VJ_BOSQUE && L.folder == 7 && ((L.track >= 5 && L.track <= 12) || (L.track >= 33 && L.track <= 36) || (L.track >= 77 && L.track <= 80))) out[n++] = w;
  }
  return n;
}

void vjFinish() {
  vjStars = (gameMistakes == 0) ? 3 : (gameMistakes <= 2 ? 2 : 1);
  vjRecord = vjStars > bestStars[vjDest];
  if (vjRecord) bestStars[vjDest] = vjStars;
  adaptLevel(&lvlViaje[vjDest]);
  vjPhase = VJ_REWARD;
  souvenirs |= (1 << vjDest);
  { int pool[24]; int n = vjThemePool(vjDest, pool);
    for (int k = 0; k < vjStars; k++) queueSticker(pickStickerFrom(pool, n)); }
  fun = imin(9, fun + 3);
  markProgress();
  renderViajeAll();
  playMilpa(AUDIO_MILPA_BIEN);
  Serial.print("VIAJE terminado: ");
  Serial.print(VJ_NAMES[vjDest]);
  Serial.print(" | monedas = ");
  Serial.println(coins);
}

// Mostrar acierto o error y esperar sin bloquear

void vjWait(unsigned long ms, int after) { vjWaitUntil = millis() + ms; vjAfter = after; }

void vjCorrect() {
  vjQFrame = 1;
  if (vjDest == VJ_MERCADO) renderViajeAll();
  // Mercado: contar el acierto al pagar (escoger y pagar es una ronda)
  if (vjDest != VJ_MERCADO || vjPaying) streakHit(&lvlViaje[vjDest], 3);
  if (vjDest == VJ_BOSQUE) vjCollect(0, vjIndexOf(VJ_ANIMALS, vjTarget), vjTarget);
  else if (vjDest == VJ_TIKAL) vjCollect(2, vjNumber, vjNumberWord(vjNumber));
  else if (!vjPaying) vjCollect(1, vjIndexOf(VJ_FOODS, vjTarget), vjTarget);
  playMilpa(AUDIO_MILPA_BIEN);
  if (vjDest == VJ_TIKAL) { vjStep++; vjAnimFrame = 1; }
  renderViajeAll();
  vjBanner("\xADUtz!", "\xADMuy bien!");
  vjWait(1200, VJA_NEXT_ROUND);
}

void vjWrong() {
  vjQFrame = 2;
  renderViajeAll();
  gameMistakes++;
  streakMiss();
  soundBack();
  playMilpa(AUDIO_MILPA_OTRAVEZ);
  vjBanner("\xADUy!", "Intenta otra vez");
  vjWait(1100, vjDest == VJ_BOSQUE ? VJA_HIDE : VJA_REPLAY);
}

void vjAfterWait() {
  int a = vjAfter; vjAfter = VJA_NONE;
  if (a == VJA_NEXT_ROUND) {
    if (vjDest == VJ_MERCADO && !vjPaying) { vjStartPaying(); return; }
    vjRound++;
    vjAnimFrame = 0;
    if (vjRound >= vjTotalRounds) vjFinish();
    else vjNewRound();
  } else if (a == VJA_HIDE) {
    vjQFrame = 0;
    for (int i = 0; i < 4; i++) vjRevealed[i] = false;
    renderViajeAll();
    lsPlayWord(vjTarget);
  } else if (a == VJA_CONTINUE) {
    renderViajeAll();
  } else if (a == VJA_REPLAY) {
    vjQFrame = 0;
    renderViajeAll();
    if (vjDest == VJ_MERCADO) lsPlayWord(vjPaying ? vjNumberWord(vjPrice) : vjTarget);
    else if (vjDest == VJ_TIKAL) lsPlayWord(vjNumberWord(vjNumber));
  }
}

// Modo
void startViajes() {
  currentMode = MODE_VIAJE;
  vjPhase = VJ_MAP;
  vjSel = 0;
  vjAnimFrame = 0;
  vjAnimAt = millis();
  renderViajeAll();
  Serial.println("VIAJES -> mapa");
}

void vjStartTrip() {
  vjDest = vjSel;
  vjPhase = VJ_PLAY;
  vjRound = 0;
  vjEarned = 0;
  vjStep = 0;
  gameReset();
  vjTotalRounds = vjRounds(vjDest);
  vjNewItems = 0;
  vjNewWord = -1;
  vjAnimFrame = 0;
  Serial.print("VIAJE -> ");
  Serial.println(VJ_NAMES[vjDest]);
  vjNewRound();
}

void viajeReplay() {
  if (vjPhase != VJ_PLAY || vjWaitUntil) return;
  if (vjDest == VJ_BOSQUE) lsPlayWord(vjTarget);
  else if (vjDest == VJ_TIKAL) lsPlayWord(vjNumberWord(vjNumber));
  else lsPlayWord(vjPaying ? vjNumberWord(vjPrice) : vjTarget);
}

void updateViajes() {
  unsigned long now = millis();
  if (vjWaitUntil) {
    if ((long)(now - vjWaitUntil) >= 0) { vjWaitUntil = 0; vjAfterWait(); }
    return;
  }
  if (backPressed()) {
    soundBack();
    if (vjPhase == VJ_PLAY || vjPhase == VJ_ALBUM) { vjSel = (vjPhase == VJ_ALBUM) ? 3 : vjDest; vjPhase = VJ_MAP; renderViajeAll(); }
    else { drawStaticScreen(); redrawHome(); }
    return;
  }
  // Parpadear a Teco quetzal mientras espera
  if (vjPhase == VJ_PLAY && !vjWaitUntil && (vjQFrame == 0 || vjQFrame == 3) && (long)(now - vjBlinkAt) >= 0) {
    vjQFrame = (vjQFrame == 0) ? 3 : 0;
    vjBlinkAt = now + (vjQFrame == 3 ? 160 : 3000 + random(0, 2000));
    if (vjDest == VJ_BOSQUE) {
      int qx = (vjType == 1) ? 8 : 46 + vjSel * 76 - tecoQuetzalFrames[0].w / 2 - 6;
      renderViajeRegion(imax(0, qx), 238 - tecoQuetzalFrames[0].h, tecoQuetzalFrames[0].w, tecoQuetzalFrames[0].h);
    } else if (vjDest == VJ_TIKAL) {
      const MiniSprite* tj = &tecoJaguarFrames[0];
      renderViajeRegion(160 - tj->w / 2 - 6, vjJaguarFoot() - tj->h, tj->w, tj->h);
    } else {
      const MiniSprite* tm = &tecoMercadoFrames[0];
      renderViajeRegion(248, 236 - tm->h, tm->w, tm->h);
    }
  }
  // Cerrar la boca de la vendedora despues de pedir
  if (vjVendTalking && vjPhase == VJ_PLAY && vjDest == VJ_MERCADO && (long)(now - vjVendTalkUntil) >= 0) {
    vjVendTalking = false;
    renderViajeRegion(VEND_X, VEND_Y, vendedoraFrames[0].w, VEND_COUNTER_Y - VEND_Y);
  }
  // Animar a Teco caminando en el mapa
  if (vjPhase == VJ_MAP && now - vjAnimAt >= 320) {
    vjAnimAt = now;
    vjAnimFrame = (vjAnimFrame == 1) ? 2 : 1;
    renderViajeRegion(227, 112, 52, 56);
  }
  if (vjPhase == VJ_ALBUM) {
    if (leftPressed())  { vjAlbumPage = (vjAlbumPage + 3) % 4; soundClick(); renderViajeAll(); }
    if (rightPressed()) { vjAlbumPage = (vjAlbumPage + 1) % 4; soundClick(); renderViajeAll(); }
    if (okPressed()) { soundBack(); vjPhase = VJ_MAP; vjSel = 3; renderViajeAll(); }
    return;
  }
  int nOpts = (vjPhase == VJ_MAP) ? 4 : ((vjDest == VJ_BOSQUE && vjType != 1) ? 4 : 3);
  if (vjPhase == VJ_MAP || vjPhase == VJ_PLAY) {
    if (leftPressed())  { vjSel = (vjSel + nOpts - 1) % nOpts; vjMissing = 0; soundClick(); renderViajeAll(); }
    if (rightPressed()) { vjSel = (vjSel + 1) % nOpts; vjMissing = 0; soundClick(); renderViajeAll(); }
  }
  if (!okPressed()) return;
  soundClick();
  if (vjPhase == VJ_MAP) {
    if (vjSel == 3) { vjPhase = VJ_ALBUM; vjAlbumPage = 0; renderViajeAll(); return; }
    if (coins < VJ_PRICE[vjSel]) {   // monedas insuficientes: jugar para ganar mas
      vjMissing = VJ_PRICE[vjSel] - coins;
      soundBack();
      renderViajeAll();
      Serial.print("VIAJE -> faltan "); Serial.print(vjMissing); Serial.println(" pwaq");
      return;
    }
    coins -= VJ_PRICE[vjSel];
    vjMissing = 0;
    markProgress();
    vjStartTrip();
    return;
  }
  if (vjPhase == VJ_REWARD) { drawStaticScreen(); startPetMode(); return; }

  if (vjDest == VJ_BOSQUE && vjType == 1) {
    logAnswer("bosque_contar", LESSON_WORDS[vjNumberWord(vjNumber)].text, LESSON_WORDS[vjNumberWord(vjOptions[vjSel])].text, vjOptions[vjSel] == vjNumber);
    if (vjOptions[vjSel] == vjNumber) vjCorrect(); else vjWrong();
  } else if (vjDest == VJ_BOSQUE) {
    vjRevealed[vjSel] = true;
    renderViajeAll();
    logAnswer(vjType == 2 ? "bosque_memoria" : "bosque", LESSON_WORDS[vjTarget].text, LESSON_WORDS[vjSpots[vjSel]].text, vjSpots[vjSel] == vjTarget);
    if (vjSpots[vjSel] == vjTarget) vjCorrect(); else vjWrong();
  } else if (vjDest == VJ_TIKAL) {
    logAnswer("tikal", LESSON_WORDS[vjNumberWord(vjNumber)].text, LESSON_WORDS[vjNumberWord(vjOptions[vjSel])].text, vjOptions[vjSel] == vjNumber);
    if (vjOptions[vjSel] == vjNumber) vjCorrect(); else vjWrong();
  } else {
    if (!vjPaying) {
      int w = vjItems[vjSel];
      int hit = -1;
      for (int k = 0; k < vjNeed; k++) if (w == vjBuy[k] && !(vjGot & (1 << k))) hit = k;
      logAnswer(vjNeed == 2 ? "mercado_dos" : "mercado", LESSON_WORDS[vjBuy[0]].text, LESSON_WORDS[w].text, hit >= 0);
      if (hit < 0) { vjWrong(); return; }
      vjGot |= (1 << hit);
      vjTarget = w;
      if (vjGot == (1 << vjNeed) - 1) vjCorrect();   // pedido completo: pagar
      else {   // falta otro alimento
        vjCollect(1, vjIndexOf(VJ_FOODS, w), w);
        playMilpa(AUDIO_MILPA_BIEN);
        vjBanner("\xADUtz!", "\xA8" "Y lo otro?");
        vjWait(1000, VJA_CONTINUE);
      }
    } else {
      logAnswer("mercado_pago", LESSON_WORDS[vjNumberWord(vjPrice)].text, LESSON_WORDS[vjNumberWord(vjOptions[vjSel])].text, vjOptions[vjSel] == vjPrice);
      if (vjOptions[vjSel] == vjPrice) vjCorrect(); else vjWrong();
    }
  }
}


// Leyendas de Guatemala (adaptadas para ninos)
struct LegPage { int word; const char* es; int pic = -1; };   // pic: personaje de la leyenda
struct LegQ { const char* q; int ans, o1, o2; };
struct Legend { const char* title; int pic; const LegPage* pages; int nPages; const LegQ* qs; int nQ; };

const LegPage LG_CADEJO[] = {
  { 77, "Hace mucho tiempo, la gente caminaba de noche por los caminos de los pueblos." },
  { 32, "Cuando sal\xA1" "an las estrellas, a veces aparec\xA1" "a un perro grande y blanco." },
  { 19, "Era el Cadejo, un perro grande y blanco, de ojos brillantes.", 0 },
  { 62, "La gente se asustaba al verlo, pero el Cadejo blanco no hac\xA1" "a da\xA4o." },
  { 55, "Caminaba junto a quien iba solo y lo cuidaba hasta llegar a su casa.", 0 },
  { 11, "Por eso dicen: si ves al Cadejo blanco, dale las gracias." },
};
const LegQ LG_CADEJO_Q[] = {
  { "\xA8Qu\x82 animal es el Cadejo?", 19, 20, 23 },
  { "\xA8" "Cu\xA0ndo aparece el Cadejo?", 32, 31, 79 },
};
const LegPage LG_SOMBRERON[] = {
  { 94, "El Sombrer\xA2n es un hombrecito con un sombrero enorme.", 1 },
  { 56, "De noche toca su guitarra y canta bajo las ventanas.", 1 },
  { 23, "Le gustan mucho los caballos: les hace trenzas en la crin mientras duermen." },
  { 90, "Tambi\x82n le gusta el pelo largo y brillante de las muchachas." },
  { 76, "En la ma\xA4" "ana, la gente del pueblo encuentra los caballos con trenzas." },
  { 53, "Por eso las abuelas dicen: de noche, adentro de la casa y a dormir temprano." },
};
const LegQ LG_SOMBRERON_Q[] = {
  { "\xA8Qu\x82 lleva puesto el Sombrer\xA2n?", 94, 91, 92 },
  { "\xA8" "A qu\x82 animal le hace trenzas?", 23, 19, 22 },
};
const LegPage LG_SIGUANABA[] = {
  { 34, "La Siguanaba aparece cerca del agua, en los r\xA1os y en las pilas.", 2 },
  { 90, "De lejos parece una mujer bonita, de pelo muy largo.", 2 },
  { 77, "Llama a los que andan solos de noche por los caminos." },
  { 23, "Pero cuando se acercan, \xADsu cara es de caballo!" },
  { 62, "Los que la ven se asustan mucho y corren a su casa." },
  { 76, "Por eso en el pueblo dicen: de noche no hay que andar solo lejos de casa." },
};
const LegQ LG_SIGUANABA_Q[] = {
  { "\xA8" "D\xA2nde aparece la Siguanaba?", 34, 33, 82 },
  { "\xA8" "C\xA2mo es su cara de cerca?", 23, 20, 25 },
};
const LegPage LG_LLORONA[] = {
  { 34, "Dicen que cerca de los r\xA1os se escucha llorar a una mujer." },
  { 38, "Es la Llorona: llora porque perdi\xA2 a sus hijos y los busca.", 3 },
  { 36, "Est\xA0 muy triste y camina toda la noche diciendo: \xAD" "Ay, mis hijos!", 3 },
  { 32, "Su llanto se oye cuando salen las estrellas." },
  { 27, "Por eso las mam\xA0s les piden a los ni\xA4os no ir solos al r\xA1o." },
  { 28, "Si alguna noche oyes un llanto, qu\x82" "date en casa con tu familia." },
};
const LegQ LG_LLORONA_Q[] = {
  { "\xA8Qu\x82 hace la Llorona?", 38, 37, 56 },
  { "\xA8" "D\xA2nde se escucha su llanto?", 34, 75, 95 },
};
const LegPage LG_PENITENTES[] = {
  { 76, "En la Antigua Guatemala cuentan la leyenda de los penitentes." },
  { 77, "Una noche, un se\xA4or vio pasar por el camino una procesi\xA2n muy silenciosa." },
  { 75, "Eran penitentes con t\xA3nicas que iban hacia la iglesia con velas.", 4 },
  { 88, "Uno de ellos le puso una vela en las manos y le pidi\xA2 cuidarla.", 4 },
  { 31, "Cuando sali\xA2 el sol, la vela se hab\xA1" "a convertido en un hueso." },
  { 62, "El se\xA4or, asustado, la devolvi\xA2 a la iglesia. Desde entonces respeta las procesiones." },
};
const LegQ LG_PENITENTES_Q[] = {
  { "\xA8" "A d\xA2nde iban los penitentes?", 75, 95, 81 },
  { "\xA8" "D\xA2nde le pusieron la vela?", 88, 89, 83 },
};
#define LEGEND_COUNT 5
const Legend LEGENDS[LEGEND_COUNT] = {
  { "El Cadejo",       0, LG_CADEJO,     6, LG_CADEJO_Q,     2 },
  { "El Sombrer\xA2n", 1, LG_SOMBRERON,  6, LG_SOMBRERON_Q,  2 },
  { "La Siguanaba",    2, LG_SIGUANABA,  6, LG_SIGUANABA_Q,  2 },
  { "La Llorona",      3, LG_LLORONA,    6, LG_LLORONA_Q,    2 },
  { "Los penitentes",  4, LG_PENITENTES, 6, LG_PENITENTES_Q, 2 },
};

enum { LG_MENU, LG_STORY, LG_QUIZ, LG_END };
int lgPhase = LG_MENU, lgSel = 0, lgPage = 0, lgQ = 0, lgOpts[3], lgOptSel = 1, lgRight = 0;
unsigned long lgWaitUntil = 0;
bool lgShowResult = false, lgLastOk = false;

void lgPicCard(int bx, int by, int bw, int rows, int pic, int x, int y, bool small) {
  const MiniSprite* sp = small ? &leyendaMiniFrames[pic] : &leyendaFrames[pic];
  int size = sp->w + 8;
  hbNine(bx, by, bw, rows, &kitMarcoFrames[0], x, y, x + size, y + size, 3, 1);
  hbSpriteMini(bx, by, bw, rows, sp, x + 4, y + 4);
}

uint16_t lgBg(int x, int y) { return pgm_read_word(&fondoNochePal[pgm_read_byte(&fondoNochePx[y * FONDO_W + x])]); }

void renderLegendRegion(int x0, int y0, int w, int h) {
  int maxRows = imin(h, (FONDO_W * BAND_H) / w);
  const Legend& L = LEGENDS[lgSel];
  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++) bandBuffer[r * w + x] = lgBg(x0 + x, by + r);
    if (lgPhase == LG_MENU || lgPhase == LG_END) {
      hbNine(x0, by, w, rows, &kitPlacaFrames[0], 40, 14, 280, 218, 4, 2);
      if (lgPhase == LG_MENU) lgPicCard(x0, by, w, rows, L.pic, 112, 62, false);
      else { const MiniSprite* t = &tecoExprFrames[3]; hbSpriteMini(x0, by, w, rows, t, 160 - t->w / 2, 70); }
    } else if (lgPhase == LG_STORY) {
      if (L.pages[lgPage].pic >= 0) lgPicCard(x0, by, w, rows, L.pages[lgPage].pic, 112, 12, false);
      else lsCard(x0, by, w, rows, L.pages[lgPage].word, 112, 12, 96, false);
      const MiniSprite* t = &tecoExprFrames[0];
      hbSpriteMini(x0, by, w, rows, t, 14, 112 - t->h);
    } else {
      for (int i = 0; i < 3; i++) lsCard(x0, by, w, rows, lgOpts[i], 22 + i * 96, 22, 84, i == lgOptSel);
    }
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

// Escribir texto en varias lineas dentro de un ancho
void drawWrapped(const char* s, int x, int y, int maxW, int lineH) {
  char line[64]; int ll = 0; line[0] = 0;
  const char* p = s;
  while (*p) {
    const char* e = p; while (*e && *e != ' ') e++;
    char word[40]; int wl = imin((int)(e - p), 38); memcpy(word, p, wl); word[wl] = 0;
    char test[64]; snprintf(test, sizeof(test), ll ? "%s %s" : "%s%s", line, word);
    if (ll && textWidth(test, 1) > maxW) { drawText(line, x, y, 1); y += lineH; snprintf(line, sizeof(line), "%s", word); }
    else snprintf(line, sizeof(line), "%s", test);
    ll = strlen(line);
    p = *e ? e + 1 : e;
  }
  if (ll) drawText(line, x, y, 1);
}

// Dibujar la caja de dialogo inferior para las leyendas
void lgBox(const char* kaq, const char* es, const char* body, const char* corner) {
  uint16_t ink = RGB565(30, 30, 30);
  tft.fillRoundRect(6, 124, 308, 112, 8, RGB565(255, 255, 255));
  tft.drawRoundRect(6, 124, 308, 112, 8, ink);
  tft.drawRoundRect(7, 125, 306, 110, 7, ink);
  tft.fillRoundRect(16, 114, 56, 20, 5, RGB565(225, 102, 28));
  tft.drawRoundRect(16, 114, 56, 20, 5, ink);
  setTxtColor(RGB565(255, 255, 255));
  printCenteredW("Teco", 44, 117, 1, 50);
  int y = 140;
  if (kaq) {
    setTxtColor(ink); drawText(kaq, 20, y, 2);
    if (es) { setTxtColor(RGB565(110, 110, 110)); drawText(es, 30 + textWidth(kaq, 2), y + 10, 1); }
    y += 30;
  }
  setTxtColor(ink);
  drawWrapped(body, 20, y, 284, 14);
  if (corner) { setTxtColor(RGB565(110, 110, 110)); drawText(corner, 314 - 12 - textWidth(corner, 1), 130, 1); }
}

void lgTexts() {
  const Legend& L = LEGENDS[lgSel];
  char b[48];
  if (lgPhase == LG_MENU) {
    setTxtColor(COLOR_TEXT);
    printCenteredW("Leyendas de Guatemala", 160, 26, 1, 220);
    printCenteredW(L.title, 160, 168, 2, 220);
    setTxtColor(LS_SPANISH_COLOR);
    snprintf(b, sizeof(b), "%d de %d   <  >   OK: escuchar", lgSel + 1, LEGEND_COUNT);
    printCenteredW(b, 160, 196, 1, 220);
  } else if (lgPhase == LG_STORY) {
    const LessonWord& w = LESSON_WORDS[L.pages[lgPage].word];
    snprintf(b, sizeof(b), "%d/%d", lgPage + 1, L.nPages);
    lgBox(w.text, w.es, L.pages[lgPage].es, b);
  } else if (lgPhase == LG_QUIZ) {
    if (lgShowResult) {
      if (lgLastOk) lgBox("\xADUtz!", NULL, "\xAD" "Muy bien! Escuchaste con atenci\xA2n.", NULL);
      else {
        snprintf(b, sizeof(b), "Era %s (%s).", LESSON_WORDS[L.qs[lgQ].ans].text, LESSON_WORDS[L.qs[lgQ].ans].es);
        lgBox("\xADUy!", NULL, b, NULL);
      }
    } else {
      snprintf(b, sizeof(b), "Pregunta %d/%d", lgQ + 1, L.nQ);
      lgBox(NULL, NULL, L.qs[lgQ].q, b);
    }
  } else {
    setTxtColor(COLOR_TEXT);
    printCenteredW("\xADUtz!", 160, 30, 2, 200);
    snprintf(b, sizeof(b), "Terminaste: %s", L.title);
    printCenteredW(b, 160, 150, 1, 220);
    snprintf(b, sizeof(b), "Aciertos: %d de %d   +3 pwaq", lgRight, L.nQ);
    printCenteredW(b, 160, 168, 1, 220);
    printCenteredW("OK: regresar", 160, 196, 1, 220);
  }
}

void renderLegendAll() { renderLegendRegion(0, 0, FONDO_W, FONDO_H); lgTexts(); }

void lgNewQuestion() {
  const LegQ& q = LEGENDS[lgSel].qs[lgQ];
  int o[3] = { q.ans, q.o1, q.o2 };
  for (int i = 2; i > 0; i--) { int j = random(0, i + 1); int t = o[i]; o[i] = o[j]; o[j] = t; }
  for (int i = 0; i < 3; i++) lgOpts[i] = o[i];
  lgOptSel = 1; lgShowResult = false;
  markQuestionStart();
  renderLegendAll();
}

void startLegends() {
  stopLife();
  endGreeting();
  currentMode = MODE_LEGEND;
  lgPhase = LG_MENU;
  renderLegendAll();
  Serial.println("LEYENDAS");
}

void updateLegends() {
  const Legend& L = LEGENDS[lgSel];
  if (lgPhase == LG_MENU) {
    if (leftPressed())  { lgSel = (lgSel + LEGEND_COUNT - 1) % LEGEND_COUNT; soundClick(); renderLegendAll(); }
    if (rightPressed()) { lgSel = (lgSel + 1) % LEGEND_COUNT; soundClick(); renderLegendAll(); }
    if (okPressed())    { soundClick(); lgPhase = LG_STORY; lgPage = 0; renderLegendAll(); lsPlayWord(L.pages[0].word); }
    if (backPressed())  { soundBack(); if (lgFromGames) { lgFromGames = false; startGameMenu(); } else { drawStaticScreen(); startPetMode(); } }
  } else if (lgPhase == LG_STORY) {
    if (backPressed()) { soundBack(); lgPhase = LG_MENU; renderLegendAll(); return; }
    if (leftPressed() && lgPage > 0) { soundClick(); lgPage--; renderLegendAll(); lsPlayWord(L.pages[lgPage].word); }
    if (rightPressed() || okPressed()) {
      soundClick();
      if (lgPage < L.nPages - 1) { lgPage++; renderLegendAll(); lsPlayWord(L.pages[lgPage].word); }
      else { lgPhase = LG_QUIZ; lgQ = 0; lgRight = 0; gameMistakes = 0; lgNewQuestion(); }
    }
  } else if (lgPhase == LG_QUIZ) {
    if (lgShowResult) {
      if ((long)(millis() - lgWaitUntil) < 0) return;
      lgQ++;
      if (lgQ < L.nQ) lgNewQuestion();
      else {
        lgPhase = LG_END;
        coins += 3; learn = imin(9, learn + 1);
        markProgress();
        renderLegendAll();
        playMilpa(AUDIO_MILPA_BIEN);
      }
      return;
    }
    if (backPressed())  { soundBack(); lgPhase = LG_MENU; renderLegendAll(); return; }
    if (leftPressed())  { lgOptSel = (lgOptSel + 2) % 3; soundClick(); renderLegendAll(); lsPlayWord(lgOpts[lgOptSel]); }
    if (rightPressed()) { lgOptSel = (lgOptSel + 1) % 3; soundClick(); renderLegendAll(); lsPlayWord(lgOpts[lgOptSel]); }
    if (okPressed()) {
      int ans = L.qs[lgQ].ans;
      lgLastOk = (lgOpts[lgOptSel] == ans);
      logAnswer("leyenda", LESSON_WORDS[ans].text, LESSON_WORDS[lgOpts[lgOptSel]].text, lgLastOk);
      if (lgLastOk) { lgRight++; playMilpa(AUDIO_MILPA_BIEN); } else { gameMistakes++; soundBack(); }
      lgShowResult = true;
      lgWaitUntil = millis() + 1600;
      lgTexts();
    }
  } else {
    if (okPressed() || backPressed()) { soundClick(); drawStaticScreen(); startPetMode(); }
  }
}

// Barrilete: atrapar la tarjeta con la palabra que dice Teco
// 3 corazones, racha (doble moneda), tarjeta dorada (+2) y velocidad creciente

#define BR_TOP 76   // tarjetas salen debajo de la caja de dialogo
#define BR_CARD 56
#define BR_KITE_Y 196
#define BR_CATCH (BR_KITE_Y - 18)
#define BR_MAXC 3
const int BR_COLX[3] = { 92, 182, 272 };
struct BrCard { int8_t col; int16_t y10; int16_t word; bool gold; bool live; bool judged; };
BrCard brC[BR_MAXC];
int brCol = 1;   // columna del barrilete
int brTarget = -1, brCaught = 0, brGoal = 7, brEarned = 0, brHearts = 3, brStreak = 0;
int brSinceTarget = 0, brLastCol = -1;
bool brDone = false;
enum { BR_AFTER_NONE, BR_AFTER_NEXT, BR_AFTER_RETRY };
int brAfter = BR_AFTER_NONE;
unsigned long brNextFrame = 0, brPauseUntil = 0;
int brPoolW[BAG_MAX];
int brPoolN = 0;

// Palabras del barrilete: las de lecciones completadas (minimo las primeras)
void brBuildPool() {
  static bool used[128];
  memset(used, 0, sizeof(used));
  brPoolN = 0;
  for (int pass = 0; pass < 2; pass++)
    for (int l = 0; l < (int)LESSON_COUNT; l++) {
      bool done = lessonsDone & (1ULL << l);
      if (pass == 0 && !done) continue;
      if (pass == 1 && brPoolN >= 12) break;
      for (int k = 0; k < 4; k++) {
        int w = LESSONS[l].words[k];
        if (w < 128 && !used[w] && brPoolN < BAG_MAX) { used[w] = true; brPoolW[brPoolN++] = w; }
      }
    }
}

int brSpeed() { return 14 + 6 * (lvlBarrilete - 1) + 2 * brCaught; }   // decimas de pixel por cuadro
int brGap() { return imax(BR_CARD + 8, 118 - 12 * lvlBarrilete); }      // distancia entre tarjetas

uint16_t brBgPixel(int x, int y) { return pgm_read_word(&barrileteFondoPal[pgm_read_byte(&barrileteFondoPx[y * 320 + x])]); }

// Dibujar el barrilete: gajos de colores, centro amarillo, fleco y cola
void brKite(int bx, int by, int bw, int rows, int cx, int cy, int r) {
  const uint16_t cols[8] = { RGB565(196, 48, 48), RGB565(244, 190, 40), RGB565(48, 140, 80), RGB565(52, 104, 190),
                             RGB565(225, 102, 28), RGB565(140, 60, 160), RGB565(230, 110, 150), RGB565(60, 170, 190) };
  for (int y = imax(cy - r - 2, by); y <= imin(cy + r + 2, by + rows - 1); y++)
    for (int x = imax(cx - r - 2, bx); x <= imin(cx + r + 2, bx + bw - 1); x++) {
      int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
      uint16_t c; bool on = true;
      if (d2 > (r + 2) * (r + 2)) on = false;
      else if (d2 > r * r) c = ((x + y) & 1) ? RGB565(255, 244, 222) : RGB565(196, 48, 48);
      else if (d2 > (r - 1) * (r - 1)) c = RGB565(52, 32, 24);
      else if (d2 <= 16) c = RGB565(244, 190, 40);
      else if (d2 <= 25) c = RGB565(52, 32, 24);
      else {
        float a = atan2f((float)dy, (float)dx) + 3.14159f;
        c = cols[(int)(a / (6.28318f / 8)) & 7];
      }
      if (on) bandBuffer[(y - by) * bw + (x - bx)] = c;
    }
  for (int k = 1; k <= 3; k++) {   // cola con monos
    int tx = cx + 4 + k * 5 + (int)(2 * sinf((millis() / 150.0f) + k));
    int ty = cy + r + k * 6;
    for (int y = imax(ty - 2, by); y <= imin(ty + 2, by + rows - 1); y++)
      for (int x = imax(tx - 4, bx); x <= imin(tx + 4, bx + bw - 1); x++)
        if (abs(x - tx) >= abs(y - ty)) bandBuffer[(y - by) * bw + (x - bx)] = (k % 2) ? RGB565(196, 48, 48) : RGB565(244, 190, 40);
  }
}

// Dibujar un corazon de 7x6 pixeles a escala 2x
void brHeart(int bx, int by, int bw, int rows, int x0, int y0, bool full) {
  static const char* H[6] = { ".##.##.", "#######", "#######", ".#####.", "..###..", "...#..." };
  uint16_t fill = full ? RGB565(214, 44, 52) : RGB565(205, 196, 184);
  uint16_t edge = full ? RGB565(120, 20, 30) : RGB565(150, 140, 128);
  for (int yy = 0; yy < 6; yy++)
    for (int xx = 0; xx < 7; xx++) {
      if (H[yy][xx] != '#') continue;
      bool border = (xx == 0 || xx == 6 || yy == 5 || H[yy][xx - (xx > 0)] != '#' || H[yy][xx + (xx < 6)] != '#' || (yy > 0 && H[yy - 1][xx] != '#') || (yy < 5 && H[yy + 1][xx] != '#'));
      uint16_t c = border ? edge : fill;
      for (int sy = 0; sy < 2; sy++)
        for (int sx = 0; sx < 2; sx++) {
          int px = x0 + xx * 2 + sx, py = y0 + yy * 2 + sy;
          if (px >= bx && px < bx + bw && py >= by && py < by + rows) bandBuffer[(py - by) * bw + (px - bx)] = c;
        }
    }
}

void renderBarrileteRegion(int x0, int y0, int w, int h) {
  int maxRows = imin(h, (FONDO_W * BAND_H) / w);
  int kx = BR_COLX[brCol];
  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++) bandBuffer[r * w + x] = brBgPixel(x0 + x, by + r);
    // Dibujar corazones
    for (int i = 0; i < 3; i++) brHeart(x0, by, w, rows, 10, BR_TOP + 8 + i * 18, i < brHearts);
    // Dibujar tarjetas que caen
    for (int i = 0; i < BR_MAXC; i++) if (brC[i].live) {
      int cx = BR_COLX[brC[i].col] - BR_CARD / 2, cy = brC[i].y10 / 10;
      if (brC[i].gold) lsRect(x0, by, w, rows, cx - 4, cy - 4, cx + BR_CARD + 4, cy + BR_CARD + 4, RGB565(244, 190, 40), 8);
      lsCard(x0, by, w, rows, brC[i].word, cx, cy, BR_CARD, false);
    }
    // Dibujar el hilo de la mano de Teco al barrilete
    for (int s2 = 0; s2 <= 100; s2++) {
      int x = 50 + (kx - 50) * s2 / 100, y = 206 + (BR_KITE_Y + 18 - 206) * s2 / 100 + (int)(4 * sinf(s2 * 3.14159f / 100));
      if (x >= x0 && x < x0 + w && y >= by && y < by + rows) bandBuffer[(y - by) * w + (x - x0)] = RGB565(250, 250, 250);
    }
    brKite(x0, by, w, rows, kx, BR_KITE_Y, 18);
    hbSpriteMini(x0, by, w, rows, &viajeTecoFrames[0], 2, 238 - viajeTecoFrames[0].h);
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void brRender() { renderBarrileteRegion(0, BR_TOP, FONDO_W, FONDO_H - BR_TOP); }

void brSay(const char* kaq, const char* es) { drawTecoBubble(kaq, es); }

void brAsk() {
  char es[44];
  if (lvlBarrilete < 3) snprintf(es, sizeof(es), "Atrapa: %s   (%d de %d)", LESSON_WORDS[brTarget].es, brCaught, brGoal);
  else snprintf(es, sizeof(es), "Atrapa esta palabra   (%d de %d)", brCaught, brGoal);
  brSay(LESSON_WORDS[brTarget].text, es);
}

void brClearCards() { for (int i = 0; i < BR_MAXC; i++) brC[i].live = false; }

void brNewTarget() {
  brTarget = bagNext(BAG_BR, brPoolW, brPoolN);   // no repetir hasta agotar todas
  brSinceTarget = 2;   // primera tarjeta = la buscada
  markQuestionStart();
  brAsk();
  lsPlayWord(brTarget);
}

void brSpawn() {
  int slot = -1;
  for (int i = 0; i < BR_MAXC; i++) if (!brC[i].live) { slot = i; break; }
  if (slot < 0) return;
  int col = random(0, 3);
  if (col == brLastCol && random(0, 3) > 0) col = (col + 1 + random(0, 2)) % 3;   // cambiar de columna casi siempre
  brLastCol = col;
  bool isTarget = (brSinceTarget >= 2) || random(0, 100) < 40;
  int wd = brTarget;
  if (isTarget) brSinceTarget = 0;
  else { do { wd = brPoolW[random(0, brPoolN)]; } while (wd == brTarget); brSinceTarget++; }
  brC[slot].col = col;
  brC[slot].y10 = (BR_TOP - BR_CARD + 4) * 10;
  brC[slot].word = wd;
  brC[slot].gold = isTarget && random(0, 8) == 0;
  brC[slot].live = true;
  brC[slot].judged = false;
}

void brLoseHeart(int got, bool passed) {
  gameMistakes++;
  brStreak = 0;
  streakMiss();
  brHearts--;
  logAnswer("barrilete", LESSON_WORDS[brTarget].text, got >= 0 ? LESSON_WORDS[got].text : "se fue", false);
  soundBack();
  char es[44];
  snprintf(es, sizeof(es), passed ? "Se fue: era %s" : "Era %s (%s)", LESSON_WORDS[brTarget].text, LESSON_WORDS[brTarget].es);
  brSay("\xADUy!", es);
  brClearCards();
  brAfter = BR_AFTER_RETRY;
  brPauseUntil = millis() + 1400;
}

void brGotIt(bool gold) {
  brCaught++;
  brStreak++;
  int g = (brStreak >= 3) ? 2 : 1;   // racha: doble moneda
  if (gold) g += 2;
  coins += g; brEarned += g;
  logAnswer("barrilete", LESSON_WORDS[brTarget].text, LESSON_WORDS[brTarget].text, true);
  playMilpa(AUDIO_MILPA_BIEN);
  streakHit(&lvlBarrilete, 3);   // sonar al subir de nivel (sin texto)
  char es[44];
  if (gold) snprintf(es, sizeof(es), "\xAD" "Tarjeta dorada! +%d pwaq", g);
  else if (brStreak >= 3) snprintf(es, sizeof(es), "\xAD" "Racha de %d!  +%d pwaq", brStreak, g);
  else snprintf(es, sizeof(es), "\xADLo atrapaste!  +%d pwaq", g);
  brSay("\xADUtz!", es);
  brClearCards();
  brAfter = BR_AFTER_NEXT;
  brPauseUntil = millis() + 1000;
}

void startBarrilete() {
  currentMode = MODE_BARRILETE;
  brBuildPool();
  brCaught = 0; brEarned = 0; brDone = false; brCol = 1;
  brHearts = 3; brStreak = 0; brLastCol = -1;
  gameReset();
  brGoal = 6 + lvlBarrilete;
  brClearCards();
  renderBarrileteRegion(0, 0, FONDO_W, FONDO_H);
  brNewTarget();
  brAfter = BR_AFTER_NONE;
  brNextFrame = millis(); brPauseUntil = millis() + 900;
  Serial.print("BARRILETE -> nivel "); Serial.print(lvlBarrilete);
  Serial.print(" | palabras: "); Serial.println(brPoolN);
}

void brFinish() {
  brDone = true;
  bool won = brHearts > 0;
  fun = imin(9, fun + 3);
  adaptLevel(&lvlBarrilete);
  settingsPanel(won ? "\xADUtz!" : "\xADUy! Sin corazones");
  const MiniSprite* t = &tecoExprFrames[3];
  drawSpriteOnColor(t, 160 - t->w / 2, 64, RGB565(241, 224, 194));
  char b[40];
  setTxtColor(COLOR_TEXT);
  snprintf(b, sizeof(b), "Atrapaste %d de %d palabras", brCaught, brGoal); printCentered(b, 160, 132, 1);
  snprintf(b, sizeof(b), "+%d pwaq", brEarned); printCentered(b, 160, 150, 2);
  if (lastLevelChange > 0) { snprintf(b, sizeof(b), "\xAD" "Nivel %d!", lvlBarrilete); printCentered(b, 160, 180, 1); }
  else if (!won) printCentered("\xADInt\x82ntalo otra vez!", 160, 180, 1);
  printCentered("OK: regresar", 160, 204, 1);
  playMilpa(won ? AUDIO_MILPA_BIEN : AUDIO_MILPA_OTRAVEZ);
  markProgress();
}

void updateBarrilete() {
  if (backPressed()) { soundBack(); drawStaticScreen(); redrawHome(); return; }
  if (brDone) { if (okPressed()) { soundClick(); drawStaticScreen(); startPetMode(); } return; }
  // Mover el barrilete de columna en columna
  int oc = brCol;
  if (leftPressed() && brCol > 0) brCol--;
  if (rightPressed() && brCol < 2) brCol++;
  if (brCol != oc) { soundClick(); brRender(); }
  unsigned long now = millis();
  if ((long)(now - brNextFrame) < 0) return;
  brNextFrame = now + 50;
  if (brPauseUntil) {
    if ((long)(now - brPauseUntil) < 0) return;
    brPauseUntil = 0;
    if (brCaught >= brGoal || brHearts <= 0) { brRender(); brFinish(); return; }
    if (brAfter == BR_AFTER_NEXT) brNewTarget();
    else if (brAfter == BR_AFTER_RETRY) { brAsk(); lsPlayWord(brTarget); brSinceTarget = 2; }
    brAfter = BR_AFTER_NONE;
    brRender();
    return;
  }
  // Lanzar una tarjeta cuando la anterior baje lo suficiente
  int top = 9999; bool any = false;
  for (int i = 0; i < BR_MAXC; i++) if (brC[i].live) { any = true; top = imin(top, brC[i].y10 / 10); }
  if (!any || top >= BR_TOP - BR_CARD + 4 + brGap()) brSpawn();
  // Bajar tarjetas y decidir al llegar al barrilete
  for (int i = 0; i < BR_MAXC; i++) {
    if (!brC[i].live) continue;
    brC[i].y10 += brSpeed();
    int y = brC[i].y10 / 10;
    if (!brC[i].judged && y + BR_CARD >= BR_CATCH) {
      brC[i].judged = true;
      bool isT = (brC[i].word == brTarget);
      if (brC[i].col == brCol) {
        if (isT) { brGotIt(brC[i].gold); brRender(); return; }
        brLoseHeart(brC[i].word, false); brRender(); return;
      } else if (isT) { brLoseHeart(-1, true); brRender(); return; }
    }
    if (y > FONDO_H) brC[i].live = false;   // tarjeta fuera de la pantalla
  }
  brRender();
}

// Elegir la comida (en Kaqchikel)
const int FOOD_WORDS[11] = { 39, 40, 41, 42, 43, 44, 45, 46, 97, 98, 104 };
int foodOpts[3];
int foodSel = 1;
#define FOOD_Y 164

void renderFoodRegion(int x0, int y0, int w, int h) {
  int maxRows = imin(h, (FONDO_W * BAND_H) / w);
  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++) bandBuffer[r * w + x] = bgPixel(x0 + x, by + r);
    lsRect(x0, by, w, rows, 60, FOOD_Y, 260, 238, LS_CARD_BORDER, 10);
    lsRect(x0, by, w, rows, 62, FOOD_Y + 2, 258, 236, LS_PANEL_COLOR, 9);
    for (int i = 0; i < 3; i++) lsCard(x0, by, w, rows, foodOpts[i], 76 + i * 60, FOOD_Y + 20, 50, i == foodSel);
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void drawFoodPick() {
  renderFoodRegion(56, FOOD_Y - 4, 208, 240 - FOOD_Y + 4);
  setTxtColor(COLOR_TEXT);
  printCenteredW("\xA8" "Qu\x82 le das de comer?", 160, FOOD_Y + 6, 1, 190);
}

void startFoodPick() {
  stopLife();
  endGreeting();
  currentMode = MODE_FOODPICK;
  int pool[11]; for (int i = 0; i < 11; i++) pool[i] = FOOD_WORDS[i];
  for (int i = 10; i > 0; i--) { int j = random(0, i + 1); int t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
  for (int i = 0; i < 3; i++) foodOpts[i] = pool[i];
  foodSel = 1;
  soundClick();
  drawFoodPick();
  Serial.println("COMER -> eligiendo la comida");
}

void updateFoodPick() {
  if (backPressed()) { soundBack(); redrawHome(); return; }
  if (leftPressed())  { foodSel = (foodSel + 2) % 3; soundClick(); drawFoodPick(); lsPlayWord(foodOpts[foodSel]); }
  if (rightPressed()) { foodSel = (foodSel + 1) % 3; soundClick(); drawFoodPick(); lsPlayWord(foodOpts[foodSel]); }
  if (okPressed()) {
    int w = foodOpts[foodSel];
    drawStaticScreen();   // quitar el panel de comida
    redrawHome();
    eatFoodWord = w;   // comer el alimento elegido
    startFeedMode();
    drawSpeechBubble(LESSON_WORDS[w].text, LESSON_WORDS[w].es);
    lsPlayWord(w);
    Serial.print("COMER -> eligio ");
    Serial.println(LESSON_WORDS[w].es);
  }
}

// Mi Teco (resumen del perfil)
void drawMiTeco() {
  settingsPanel("Mi Teco");
  char b[48];
  int done = 0; for (int i = 0; i < (int)LESSON_COUNT; i++) if (lessonsDone & (1ULL << i)) done++;
  int dias = 0;
  if (tecoBorn && clockValid()) dias = (int)(((long)clockNow() - tecoBorn) / 86400L);
  const char* lines[8];
  char l0[40], l1[40], l2[40], l3[40], l4[40], l5[40], l6[40];
  snprintf(l0, sizeof(l0), "Perfil: ni\xA4o N%02d", activeProfile + 1);
  if (tecoBorn) snprintf(l1, sizeof(l1), "Edad de Teco: %d d\xA1" "as", dias); else snprintf(l1, sizeof(l1), "Edad de Teco: sin hora real");
  snprintf(l2, sizeof(l2), "Lecciones: %d de %d (%d palabras)", done, (int)LESSON_COUNT, done * 4);
  snprintf(l3, sizeof(l3), "Monedas: %d pwaq", coins);
  snprintf(l4, sizeof(l4), "Mi \xA0lbum: %d anim., %d alim., %d n\xA3m.",
           bitCount(albAnimals), bitCount(albFoods), bitCount(albNumbers));
  snprintf(l5, sizeof(l5), "Recuerdos de viaje: %d de 3", bitCount(souvenirs));
  snprintf(l6, sizeof(l6), "Niveles: milpa %d, telar %d, viajes %d/%d/%d", lvlMilpa, lvlTelar, lvlViaje[0], lvlViaje[1], lvlViaje[2]);
  lines[0] = l0; lines[1] = l1; lines[2] = l2; lines[3] = l3; lines[4] = l4; lines[5] = l5; lines[6] = l6;
  for (int i = 0; i < 7; i++) {
    setTxtColor(i == 0 ? COLOR_TEXT : RGB565(90, 60, 40));
    printCenteredW(lines[i], 160, 62 + i * 20, 1, 240);
  }
  (void)b;
  setTxtColor(COLOR_TEXT);
  printCentered("OK o BACK: regresar", 160, 204, 1);
}

// Casa

void updateHome() {
  if (stickerShowing) {
    if (okPressed() || backPressed()) { soundClick(); stickerShowing = false; drawStaticScreen(); redrawHome(); }
    return;
  }
  if (stickerPending && !greetingActive()) { showSticker(); return; }
  if (leftPressed()) {
    selectedAction--;

    if (selectedAction < 0) {
      selectedAction = ACTION_COUNT - 1;
    }

    drawActionBar();
    soundClick();

    Serial.print("Accion seleccionada: ");
    Serial.println(actionNames[selectedAction]);
  }

  if (rightPressed()) {
    selectedAction++;

    if (selectedAction >= ACTION_COUNT) {
      selectedAction = 0;
    }

    drawActionBar();
    soundClick();

    Serial.print("Accion seleccionada: ");
    Serial.println(actionNames[selectedAction]);
  }

  if (okPressed()) {
    if (selectedAction == ACTION_FEED) {
      startFoodPick();
    } else if (selectedAction == ACTION_GAME) {
      soundClick();
      startGameMenu();
    } else if (selectedAction == ACTION_SLEEP) {
      startSleepMode();
    } else if (selectedAction == ACTION_LESSON) {
      soundClick();
      startLesson();
    }
  }

  if (backPressed()) {
    soundBack();
    Serial.println("BACK presionado");
  }

  // Mantener BACK 1.5 s para abrir ajustes
  static unsigned long backHoldStart = 0;
  if (digitalRead(BTN_BACK) == LOW) {
    if (backHoldStart == 0) backHoldStart = millis();
    else if (millis() - backHoldStart >= 1500) {
      backHoldStart = 0;
      startSettings();
      return;
    }
  } else {
    backHoldStart = 0;
  }
}

void updateCurrentMode() {
  if (currentMode == MODE_HOME) {
    updateHome();
  } else if (currentMode == MODE_FEED) {
    updateFeedMode();
  } else if (currentMode == MODE_SLEEP) {
    updateSleepMode();
  } else if (currentMode == MODE_DRINK) {
    updateDrinkMode();
  } else if (currentMode == MODE_PET) {
    updatePetMode();
  } else if (currentMode == MODE_MILPA) {
    updateMilpaMode();
  } else if (currentMode == MODE_TELAR) {
    updateTelarMode();
  } else if (currentMode == MODE_GAMEMENU) {
    updateGameMenu();
  } else if (currentMode == MODE_LESSON) {
    updateLessonMode();
  } else if (currentMode == MODE_SETTINGS) {
    updateSettings();
  } else if (currentMode == MODE_WIFISETUP) {
    updateWifiSetup();
  } else if (currentMode == MODE_VIAJE) {
    updateViajes();
  } else if (currentMode == MODE_FOODPICK) {
    updateFoodPick();
  } else if (currentMode == MODE_BARRILETE) {
    updateBarrilete();
  } else if (currentMode == MODE_LEGEND) {
    updateLegends();
  }
}

// Tiempo, progreso guardado y Blynk

const unsigned long HUNGER_DECAY_MS = 10UL * 60UL * 1000UL;   // 10 min
const unsigned long ENERGY_DECAY_MS = 15UL * 60UL * 1000UL;   // 15 min
const unsigned long FUN_DECAY_MS    = 12UL * 60UL * 1000UL;   // 12 min
const unsigned long LEARN_DECAY_MS  = 40UL * 60UL * 1000UL;   // 40 min
const unsigned long SAVE_EVERY_MS   = 30000;   // guardar maximo
const unsigned long BLYNK_SEND_MS   = 2000;
const unsigned long BLYNK_RETRY_MS  = 20000;

Preferences prefs;


unsigned long lastHungerDecay = 0;
unsigned long lastEnergyDecay = 0;
unsigned long lastFunDecay = 0;
unsigned long lastLearnDecay = 0;
unsigned long lastSaveAt = 0;
bool progressDirty = false;

void markProgress() { progressDirty = true; }

void loadProgress() {
  prefs.begin(PROFILE_NS[activeProfile], true);
  hunger = prefs.getInt("hunger", hunger);
  energy = prefs.getInt("energy", energy);
  fun = prefs.getInt("fun", fun);
  learn = prefs.getInt("learn", learn);
  coins = prefs.getInt("coins", 0);
  lvlMilpa = constrain(prefs.getInt("lvm", 1), 1, 5);
  lvlBarrilete = constrain(prefs.getInt("lvbr", 1), 1, 5);
  albAnimals = prefs.getInt("aba", 0);
  albFoods = prefs.getInt("abf", 0);
  albNumbers = prefs.getULong("abn", 0);
  for (int k = 0; k < 4; k++) { char key[5] = { 's', 'k', (char)('0' + k), 0 }; stickerBits[k] = prefs.getULong(key, 0); }
  bestStars[0] = prefs.getInt("st0", 0);
  bestStars[1] = prefs.getInt("st1", 0);
  bestStars[2] = prefs.getInt("st2", 0);
  lvlTelar = constrain(prefs.getInt("lvt", 1), 1, 5);
  for (int i = 0; i < 3; i++) { char k[4] = { 'l', 'v', (char)('0' + i), 0 }; lvlViaje[i] = constrain(prefs.getInt(k, 1), 1, 5); }
  souvenirs = prefs.getInt("souv", 0);
  bdDay = prefs.getInt("bdd", 0);
  bdMonth = prefs.getInt("bdm", 0);
  bdYear = prefs.getInt("bdy", 0);
  tecoBorn = prefs.getLong("tborn", 0);
  partyYear = prefs.getInt("party", 0);
  savedEpoch = prefs.getLong("epoch", 0);
  savedDay = prefs.getInt("day", -1);
  aciertosHoy = prefs.getInt("aciertos", 0);
  lessonNext = prefs.getInt("lesson", 0);
  lessonsDone = prefs.getULong64("ldone", 0);
  prefs.getBytes("miss", wordMiss, sizeof(wordMiss));
  milpaGames = prefs.getInt("milpa", 0);
  huipilesTejidos = prefs.getInt("huipil", 0);
  dfVolume = prefs.getInt("vol", TECO_MAX_VOLUME);
  if (dfVolume > TECO_MAX_VOLUME) dfVolume = TECO_MAX_VOLUME;
  if (dfVolume < 6) dfVolume = 18;   // recuperar un volumen audible si estaba en silencio
  prefs.end();
  Serial.print("Progreso cargado: hambre ");
  Serial.print(hunger);
  Serial.print(", energia ");
  Serial.println(energy);
}

void saveProgress() {
  prefs.begin(PROFILE_NS[activeProfile], false);
  prefs.putInt("hunger", hunger);
  prefs.putInt("energy", energy);
  prefs.putInt("fun", fun);
  prefs.putInt("learn", learn);
  prefs.putInt("coins", coins);
  prefs.putInt("lvm", lvlMilpa);
  prefs.putInt("lvbr", lvlBarrilete);
  prefs.putInt("aba", albAnimals);
  prefs.putInt("abf", albFoods);
  prefs.putULong("abn", albNumbers);
  for (int k = 0; k < 4; k++) { char key[5] = { 's', 'k', (char)('0' + k), 0 }; prefs.putULong(key, stickerBits[k]); }
  prefs.putInt("st0", bestStars[0]);
  prefs.putInt("st1", bestStars[1]);
  prefs.putInt("st2", bestStars[2]);
  prefs.putInt("lvt", lvlTelar);
  prefs.putInt("lv0", lvlViaje[0]);
  prefs.putInt("lv1", lvlViaje[1]);
  prefs.putInt("lv2", lvlViaje[2]);
  prefs.putInt("souv", souvenirs);
  prefs.putInt("bdd", bdDay);
  prefs.putInt("bdm", bdMonth);
  prefs.putInt("bdy", bdYear);
  prefs.putLong("tborn", tecoBorn);
  prefs.putInt("party", partyYear);
  if (clockValid()) prefs.putLong("epoch", (long)clockNow());
  prefs.putInt("day", savedDay);
  prefs.putInt("aciertos", aciertosHoy);
  prefs.putInt("lesson", lessonNext);
  prefs.putULong64("ldone", lessonsDone);
  prefs.putBytes("miss", wordMiss, sizeof(wordMiss));
  prefs.putInt("milpa", milpaGames);
  prefs.putInt("huipil", huipilesTejidos);
  prefs.putInt("vol", dfVolume);
  prefs.end();
  progressDirty = false;
  lastSaveAt = millis();
}

void updateNeeds() {
  unsigned long now = millis();

  if (now - lastHungerDecay >= HUNGER_DECAY_MS) {
    lastHungerDecay = now;
    if (hunger > 0) { hunger--; markProgress(); }
  }

  if (currentMode != MODE_SLEEP && now - lastEnergyDecay >= ENERGY_DECAY_MS) {
    lastEnergyDecay = now;
    if (energy > 0) { energy--; markProgress(); }
  }

  if (currentMode != MODE_SLEEP && now - lastFunDecay >= FUN_DECAY_MS) {
    lastFunDecay = now;
    if (fun > 0) { fun--; markProgress(); }
  }

  if (currentMode != MODE_SLEEP && now - lastLearnDecay >= LEARN_DECAY_MS) {
    lastLearnDecay = now;
    if (learn > 0) { learn--; markProgress(); }
  }

  if (progressDirty && now - lastSaveAt >= SAVE_EVERY_MS) {
    saveProgress();
  }
}

const char* modeName() {
  switch (currentMode) {
    case MODE_HOME:     return "En casa";
    case MODE_FEED:     return "Comiendo";
    case MODE_SLEEP:    return "Durmiendo";
    case MODE_DRINK:    return "Tomando agua";
    case MODE_PET:      return "Feliz";
    case MODE_MILPA:    return "Jugando: milpa";
    case MODE_TELAR:    return "Jugando: telar";
    case MODE_GAMEMENU: return "Eligiendo juego";
    case MODE_LESSON:   return "Estudiando";
    case MODE_SETTINGS:
    case MODE_WIFISETUP: return "En ajustes";
    case MODE_VIAJE:    return "De viaje";
    case MODE_FOODPICK: return "Eligiendo comida";
    case MODE_BARRILETE: return "Volando barrilete";
    case MODE_LEGEND: return "Escuchando leyendas";
  }
  return "";
}

#if TECO_USE_BLYNK

bool blynkConfigured = false;
// WiFi por intervalos (TECO_WIFI_EVERY_MIN > 0)
const unsigned long WIFI_WINDOW_MS = 45000;
bool wifiSleeping = false;
unsigned long wifiWindowStart = 0;
unsigned long wifiNextWindow = 0;

unsigned long lastWifiTry = 0;
bool wifiWasConnected = false;
const unsigned long WIFI_RETRY_MS = 30000;
const unsigned long WIFI_GIVEUP_MS = 60000;
volatile int wifiLastReason = 0;

// Explicar en espanol el motivo de desconexion (codigos del ESP32)
const char* wifiReasonText(int r) {
  switch (r) {
    case 2: case 15: case 202: case 204:
      return "contrasena incorrecta (revisa mayusculas y espacios)";
    case 201:
      return "no encuentra la red (nombre mal escrito, red de 5 GHz o router lejos)";
    case 3: case 8:
      return "el router cerro la conexion";
    case 200: case 4:
      return "se perdio la senal";
    case 203:
      return "el router no acepta la conexion (revisa que sea WPA2, no solo WPA3)";
  }
  return "intentando...";
}

const char* wifiStatusText(int st) {
  switch (st) {
    case WL_NO_SSID_AVAIL:   return "no encuentra la red (revisa el nombre, que sea 2.4 GHz y que el hotspot este visible)";
    case WL_CONNECT_FAILED:  return "la red rechazo la conexion (revisa la contrasena)";
    case WL_CONNECTION_LOST: return "se perdio la conexion";
    case WL_DISCONNECTED:    return "desconectado, intentando...";
    case WL_IDLE_STATUS:     return "esperando...";
  }
  return "intentando...";
}
unsigned long lastBlynkTry = 0;
unsigned long lastBlynkSend = 0;

// ultimos valores enviados (enviar solo cambios)
int sentFun = -1;
int sentLearn = -1;
int sentHunger = -1, sentEnergy = -1, sentMilpa = -1, sentHuipil = -1, sentAciertos = -1;
int sentMode = -1;

// Datos de conexion: configurar desde el celular y guardar
char cfgSsid[33] = "";
char cfgPass[65] = "";
char cfgToken[40] = "";

void loadWifiConfig() {
  Preferences wp;
  wp.begin("tecowifi", true);
  wp.getString("ssid", cfgSsid, sizeof(cfgSsid));
  wp.getString("pass", cfgPass, sizeof(cfgPass));
  wp.end();
  if (strncmp(BLYNK_AUTH_TOKEN, "PON_AQUI", 8) != 0) strncpy(cfgToken, BLYNK_AUTH_TOKEN, sizeof(cfgToken) - 1);
}

bool wifiDataFilled() { return cfgSsid[0] != 0; }
bool blynkTokenOk = false;   // usar Blynk solo si hay token

void setupBlynk() {
  loadWifiConfig();
  if (!wifiDataFilled()) {
    Serial.println("WiFi sin configurar: en la casa, manten BACK 1.5 s > Configurar WiFi.");
    Serial.println("Teco funciona sin internet mientras tanto.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    wifiLastReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(cfgSsid, cfgPass);   // conectar en segundo plano, sin esperar
  wifiWindowStart = millis();
  lastWifiTry = millis();
  Serial.print("WiFi: red \"");
  Serial.print(cfgSsid);
  Serial.println("\"");
  blynkTokenOk = (cfgToken[0] != 0);
  if (blynkTokenOk) Blynk.config(cfgToken);
  else Serial.println("Blynk: falta BLYNK_AUTH_TOKEN; Teco usa el WiFi solo para la hora real.");
  blynkConfigured = true;
  Serial.println("WiFi: conectando en segundo plano...");
}

void sendAllToBlynk(bool force) {
  if (force || hunger != sentHunger)        { Blynk.virtualWrite(PIN_HAMBRE, hunger);      sentHunger = hunger; }
  if (force || energy != sentEnergy)        { Blynk.virtualWrite(PIN_ENERGIA, energy);      sentEnergy = energy; }
  if (force || learn != sentLearn)          { Blynk.virtualWrite(PIN_APRENDER, learn); sentLearn = learn; }
  if (force || fun != sentFun)              { Blynk.virtualWrite(PIN_DIVERSION, fun);         sentFun = fun; }
  if (force || (int)currentMode != sentMode){ Blynk.virtualWrite(PIN_ESTADO, modeName());  sentMode = currentMode; }
  if (force || milpaGames != sentMilpa)     { Blynk.virtualWrite(PIN_MILPA, milpaGames);  sentMilpa = milpaGames; }
  if (force || huipilesTejidos != sentHuipil){ Blynk.virtualWrite(PIN_HUIPILES, huipilesTejidos); sentHuipil = huipilesTejidos; }
  if (force || aciertosHoy != sentAciertos) { Blynk.virtualWrite(PIN_ACIERTOS, aciertosHoy); sentAciertos = aciertosHoy; }
  static int sentLecc = -1, sentCoins = -1, sentProf = -1;
  int lecc = 0; for (int i = 0; i < (int)LESSON_COUNT; i++) if (lessonsDone & (1ULL << i)) lecc++;
  if (force || lecc != sentLecc)            { Blynk.virtualWrite(PIN_LECCIONES, lecc); sentLecc = lecc; }
  if (force || coins != sentCoins)          { Blynk.virtualWrite(PIN_MONEDAS, coins); sentCoins = coins; }
  if (force || activeProfile != sentProf)   { char p[8]; snprintf(p, sizeof(p), "N%02d", activeProfile + 1); Blynk.virtualWrite(PIN_PERFIL, p); sentProf = activeProfile; }
}


void wifiSleepNow(unsigned long minutes) {
  if (Blynk.connected()) {
    sendAllToBlynk(true);
    Blynk.run();
    Blynk.disconnect();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiSleeping = true;
  wifiWasConnected = false;
  wifiNextWindow = millis() + minutes * 60000UL;
  Serial.println("WiFi: apagado para ahorrar energia");
}

void wifiWakeNow() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSsid, cfgPass);
  wifiSleeping = false;
  wifiWindowStart = millis();
  lastWifiTry = millis();
  lastBlynkTry = 0;
  Serial.println("WiFi: prendiendo un rato para mandar datos");
}

// Hora rapida: consultar directo a un servidor NTP al conectarse
// (sin esperar el servicio automatico del ESP32, que tarda varios segundos)
extern long clockTestShift;   // definido junto al reloj, mas abajo
WiFiUDP ntpUdp;
bool ntpWaiting = false;
uint8_t ntpTries = 0;
unsigned long ntpSentAt = 0;
const char* NTP_HOSTS[3] = { "time.google.com", "time.cloudflare.com", "pool.ntp.org" };
void ntpQuickSend() {
  ntpSentAt = millis();
  ntpWaiting = true;
  IPAddress ip;
  const char* host = NTP_HOSTS[ntpTries % 3];
  ntpTries++;
  if (!WiFi.hostByName(host, ip)) return;   // sin respuesta del DNS: probar el siguiente servidor
  uint8_t pkt[48];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0x1B;   // NTP version 3, modo cliente
  ntpUdp.begin(2390);
  ntpUdp.beginPacket(ip, 123);
  ntpUdp.write(pkt, sizeof(pkt));
  ntpUdp.endPacket();
}
void ntpQuickStart() {
  ntpTries = 0;
  ntpWaiting = false;
  if (!clockValid()) ntpQuickSend();
}
void ntpQuickPoll() {
  if (!ntpWaiting) return;
  if (clockValid()) { ntpWaiting = false; ntpUdp.stop(); return; }   // hora ya recibida por la otra via
  if (ntpUdp.parsePacket() >= 48) {
    uint8_t b[48];
    ntpUdp.read(b, sizeof(b));
    uint32_t secs = ((uint32_t)b[40] << 24) | ((uint32_t)b[41] << 16) | ((uint32_t)b[42] << 8) | (uint32_t)b[43];
    ntpWaiting = false;
    ntpUdp.stop();
    if (secs > 2208988800UL) {
      struct timeval tv;
      tv.tv_sec = (time_t)(secs - 2208988800UL);   // de 1900 a 1970
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      clockTestShift = 0;
      Serial.print("Hora real: recibida por NTP directo en ");
      Serial.print(millis() - ntpSentAt);
      Serial.println(" ms");
    }
    return;
  }
  if (millis() - ntpSentAt > 1500) {   // sin respuesta: probar otro servidor (hasta 6 intentos)
    if (ntpTries < 6) ntpQuickSend();
    else { ntpWaiting = false; ntpUdp.stop(); }
  }
}

void updateBlynk() {
  if (!blynkConfigured) return;
  unsigned long now = millis();

  if (wifiSleeping) {
    if ((long)(now - wifiNextWindow) >= 0) wifiWakeNow();
    return;
  }

  if (TECO_WIFI_EVERY_MIN > 0) {
    if (now - wifiWindowStart >= WIFI_WINDOW_MS && WiFi.status() == WL_CONNECTED) {
      wifiSleepNow(TECO_WIFI_EVERY_MIN);
      return;
    }
  }

  int st = WiFi.status();

  if (st != WL_CONNECTED) {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      wifiWindowStart = now;   // esperar otro minuto para reconectar
      Serial.println("WiFi: se perdio la conexion");
    }
    // Informar el avance cada 30 s sin interrumpir la conexion
    if (now - lastWifiTry >= WIFI_RETRY_MS) {
      lastWifiTry = now;
      Serial.print("WiFi: ");
      if (wifiLastReason) {
        Serial.print(wifiReasonText(wifiLastReason));
        Serial.print(" (codigo ");
        Serial.print(wifiLastReason);
        Serial.println(")");
      } else {
        Serial.println(wifiStatusText(st));
      }
    }
    // Pausar el WiFi si no conecta en 1 minuto (evitar picos de consumo)
    if (now - wifiWindowStart >= WIFI_GIVEUP_MS) {
      Serial.print("WiFi: no se pudo conectar. Teco sigue sin internet y reintenta en ");
      Serial.print(clockValid() ? TECO_WIFI_RETRY_LATER_MIN : 2);
      Serial.println(" minutos.");
      wifiSleepNow(clockValid() ? TECO_WIFI_RETRY_LATER_MIN : 2);   // sin hora: reintentar pronto
    }
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    wifiWindowStart = now;
    wifiLastReason = 0;
    configTime(0, 0, "time.google.com", "pool.ntp.org");   // hora real (UTC) automatica
    ntpQuickStart();    // y consulta directa para tenerla de inmediato
    lastBlynkTry = 0;   // intentar Blynk de inmediato
    Serial.print("WiFi: conectado! IP ");
    Serial.println(WiFi.localIP().toString().c_str());
  }

  ntpQuickPoll();
  if (ntpWaiting) return;      // obtener primero la hora: Blynk bloquea hasta 2 s al conectar
  if (!blynkTokenOk) return;   // sin token: solo WiFi y hora real
  if (!Blynk.connected()) {
    if (now - lastBlynkTry >= BLYNK_RETRY_MS || lastBlynkTry == 0) {
      lastBlynkTry = now;
      Serial.println("Blynk: conectando...");
      Blynk.connect(2000);   // esperar maximo 2 s
    }
    return;
  }

  Blynk.run();
  updateBlynkPending();

  if (now - lastBlynkSend >= BLYNK_SEND_MS) {
    lastBlynkSend = now;
    sendAllToBlynk(false);
  }
}

BLYNK_CONNECTED() {
  Serial.println("Blynk: conectado!");
  Blynk.syncVirtual(PIN_VOLUMEN);   // leer el volumen guardado en el tablero
  sendAllToBlynk(true);
}

// Acciones desde el tablero
#define BLYNK_ACT_NONE  0
#define BLYNK_ACT_FEED  10
#define BLYNK_ACT_SLEEP 11
#define BLYNK_ACT_HUG   12

int blynkPending = BLYNK_ACT_NONE;

void logBlynkWrite(const char* name, int value) {
  Serial.print("Blynk -> ");
  Serial.print(name);
  Serial.print(" = ");
  Serial.print(value);
  Serial.print(" (Teco: ");
  Serial.print(modeName());
  Serial.println(")");
}

void runBlynkAction(int act) {
  if (act == BLYNK_ACT_FEED) {
    Serial.println("Blynk: dar de comer");
    startFeedMode();
  } else if (act == BLYNK_ACT_SLEEP) {
    Serial.println("Blynk: a dormir");
    startSleepMode();
  } else if (act == BLYNK_ACT_HUG) {
    Serial.println("Blynk: abrazo!");
    startPetMode();
  }
}

bool hasSticker(int w) { return stickerBits[w >> 5] & (1UL << (w & 31)); }
int stickerCount() { int n = 0; for (int w = 0; w < (int)LESSON_WORD_COUNT; w++) if (hasSticker(w)) n++; return n; }

// Elegir al azar una palabra que el nino no tenga
int giveSticker() {
  int free = (int)LESSON_WORD_COUNT - stickerCount();
  if (free <= 0) { coins += 5; return -1; }
  int pick = random(0, free);
  for (int w = 0; w < (int)LESSON_WORD_COUNT; w++)
    if (!hasSticker(w) && pick-- == 0) { stickerBits[w >> 5] |= (1UL << (w & 31)); markProgress(); return w; }
  return -1;
}

// Elegir una estampa del tema que el nino no tenga (o cualquiera si ya tiene todas)
int stickerQ[4];
int stickerQLen = 0;
int pickStickerFrom(const int* pool, int n) {
  int freeN = 0;
  for (int i = 0; i < n; i++) if (!hasSticker(pool[i])) freeN++;
  if (freeN == 0) return giveSticker();
  int pick = random(0, freeN);
  for (int i = 0; i < n; i++)
    if (!hasSticker(pool[i]) && pick-- == 0) { int w = pool[i]; stickerBits[w >> 5] |= (1UL << (w & 31)); markProgress(); return w; }
  return -1;
}
void queueSticker(int w) { if (stickerQLen < 4) stickerQ[stickerQLen++] = w; stickerPending = true; }

#define STK_Y 30
void renderStickerRegion(int x0, int y0, int w, int h) {
  int maxRows = imin(h, (FONDO_W * BAND_H) / w);
  for (int by = y0; by < y0 + h; by += maxRows) {
    int rows = imin(maxRows, y0 + h - by);
    for (int r = 0; r < rows; r++)
      for (int x = 0; x < w; x++) bandBuffer[r * w + x] = bgPixel(x0 + x, by + r);
    hbNine(x0, by, w, rows, &kitPlacaFrames[0], 50, STK_Y, 270, STK_Y + 186, 4, 2);
    if (stickerWord >= 0) lsCard(x0, by, w, rows, stickerWord, 112, STK_Y + 40, 96, false);
    tft.drawRGBBitmap(x0, by, bandBuffer, w, rows);
  }
}

void showSticker() {
  stopLife();
  stickerShowing = true;
  if (stickerQLen > 0) {   // estampas ganadas, en orden
    stickerWord = stickerQ[0];
    for (int i = 1; i < stickerQLen; i++) stickerQ[i - 1] = stickerQ[i];
    stickerQLen--;
  } else stickerWord = giveSticker();
  stickerPending = (stickerQLen > 0);
  renderStickerRegion(40, STK_Y - 4, 240, 196);
  setTxtColor(COLOR_TEXT);
  if (stickerWord >= 0) {
    printCenteredW("\xAD" "Estampa nueva!", 160, STK_Y + 14, 2, 200);
    printCenteredW(LESSON_WORDS[stickerWord].text, 160, STK_Y + 140, 2, 200);
    setTxtColor(LS_SPANISH_COLOR);
    char b[40]; snprintf(b, sizeof(b), "%s  -  %d de %d", LESSON_WORDS[stickerWord].es, stickerCount(), (int)LESSON_WORD_COUNT);
    printCenteredW(b, 160, STK_Y + 164, 1, 200);
    lsPlayWord(stickerWord);
  } else {
    printCenteredW("\xADLas tienes todas!", 160, STK_Y + 80, 2, 200);
    printCenteredW("+5 pwaq", 160, STK_Y + 120, 2, 200);
  }
  playMilpa(AUDIO_MILPA_BIEN);
  Serial.print("ESTAMPA -> "); Serial.println(stickerWord);
}

void requestBlynkAction(int act) {
  if (currentMode == MODE_HOME) {
    runBlynkAction(act);
  } else {
    blynkPending = act;
    Serial.println("Blynk: Teco no esta en casa, lo hara al regresar");
  }
}

void updateBlynkPending() {
  if (blynkPending != BLYNK_ACT_NONE && currentMode == MODE_HOME && !stickerShowing) {
    int act = blynkPending;
    blynkPending = BLYNK_ACT_NONE;
    runBlynkAction(act);
  }
}

// Dar de comer
BLYNK_WRITE(PIN_COMER) {
  int v = param.asInt();
  logBlynkWrite("comer", v);
  if (v == 1) {
    requestBlynkAction(BLYNK_ACT_FEED);
    Blynk.virtualWrite(PIN_COMER, 0);
  }
}

// Dormir / despertar
BLYNK_WRITE(PIN_DORMIR) {
  int v = param.asInt();
  logBlynkWrite("dormir", v);
  if (v != 1) return;
  Blynk.virtualWrite(PIN_DORMIR, 0);
  if (currentMode == MODE_SLEEP) {
    Serial.println("Blynk: despertar");
    wakeUp();
  } else {
    requestBlynkAction(BLYNK_ACT_SLEEP);
  }
}

// Abrazo: animacion de corazones
BLYNK_WRITE(PIN_ABRAZO) {
  int v = param.asInt();
  logBlynkWrite("abrazo", v);
  if (v == 1) {
    requestBlynkAction(BLYNK_ACT_HUG);
    Blynk.virtualWrite(PIN_ABRAZO, 0);
  }
}

// Volumen del DFPlayer (0..30)
BLYNK_WRITE(PIN_VOLUMEN) {
  int v = param.asInt();
  if (v < 0) v = 0;
  if (v > TECO_MAX_VOLUME) v = TECO_MAX_VOLUME;
  dfVolume = v;
  dfSetVolume(v);
  markProgress();
}

// Configurar WiFi desde el celular

#define SETUP_AP_NAME "Teco-configuracion"
WebServer setupServer(80);
DNSServer setupDns;
bool setupSaved = false;
unsigned long setupRestartAt = 0;
int setupNetCount = 0;
char setupNets[12][33];

// Escapar texto para insertarlo en el HTML
void htmlEscape(const char* in, char* out, size_t n) {
  size_t k = 0;
  for (; *in && k + 7 < n; in++) {
    const char* r = nullptr;
    if (*in == '<') r = "&lt;"; else if (*in == '>') r = "&gt;";
    else if (*in == '&') r = "&amp;"; else if (*in == '"') r = "&quot;";
    if (r) { while (*r) out[k++] = *r++; } else out[k++] = *in;
  }
  out[k] = 0;
}

void setupHandleRoot() {
  String html;
  html.reserve(3000);
  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Configurar a Teco</title><style>"
            "body{font-family:sans-serif;background:#FFF4DE;color:#4A2C1A;margin:0;padding:24px}"
            "h1{font-size:1.4em}label{display:block;margin-top:16px;font-weight:bold}"
            "select,input{width:100%;padding:10px;margin-top:6px;font-size:1em;border:2px solid #8A5A32;border-radius:8px;box-sizing:border-box}"
            "button{margin-top:24px;width:100%;padding:14px;font-size:1.1em;background:#E8873A;color:#fff;border:0;border-radius:10px}"
            "small{color:#8A5A32}</style></head><body>"
            "<h1>Configurar a Teco</h1><p>Elige la red WiFi de tu casa (de 2.4&nbsp;GHz) y escribe su contrase&ntilde;a.</p>"
            "<form method='POST' action='/guardar'><label>Red WiFi</label><select name='ssid'>");
  char esc[80];
  for (int i = 0; i < setupNetCount; i++) {
    htmlEscape(setupNets[i], esc, sizeof(esc));
    html += "<option";
    if (strcmp(setupNets[i], cfgSsid) == 0) html += " selected";
    html += ">"; html += esc; html += "</option>";
  }
  html += F("</select><label>Otra red (si no aparece arriba)</label><input name='otra' placeholder='Nombre exacto de la red'>"
            "<label>Contrase&ntilde;a</label><input name='pass' type='password'>"
            "<button type='submit'>Guardar</button></form>");
  char cnt[120];
  snprintf(cnt, sizeof(cnt), "<h2>Respuestas guardadas</h2><p>%d respuestas en el registro.</p>", logLineCount());
  html += cnt;
  html += F("<p><a href='/registro.csv'>Descargar registro (CSV para Excel)</a></p>"
            "<p><small><a href='/borrar'>Borrar el registro</a></small></p></body></html>");
  setupServer.send(200, "text/html", html);
}

void setupHandleSave() {
  String ssid = setupServer.arg("otra");
  if (ssid.length() == 0) ssid = setupServer.arg("ssid");
  String pass = setupServer.arg("pass");
  if (ssid.length() == 0) {
    setupServer.send(200, "text/html", F("<meta charset='utf-8'><p>Falta elegir una red. <a href='/'>Regresar</a></p>"));
    return;
  }
  Preferences wp;
  wp.begin("tecowifi", false);
  wp.putString("ssid", ssid.c_str());
  wp.putString("pass", pass.c_str());
  wp.end();
  setupServer.send(200, "text/html", F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
            "<body style='font-family:sans-serif;background:#FFF4DE;color:#4A2C1A;padding:24px'>"
            "<h1>&iexcl;Listo!</h1><p>Teco se va a reiniciar para conectarse a tu red.</p></body></html>"));
  setupSaved = true;
  setupRestartAt = millis() + 2500;
  Serial.print("WiFi configurado desde el celular: ");
  Serial.println(ssid.c_str());
}

// Redirigir cualquier otra ruta a la pagina principal
void setupHandleOther() {
  setupServer.sendHeader("Location", "http://192.168.4.1/", true);
  setupServer.send(302, "text/plain", "");
}

void drawWifiSetupScreen(const char* line1, const char* line2, const char* line3) {
  tftNine(&kitPlacaFrames[0], 18, 28, 302, 212, 4, 2);
  setTxtColor(COLOR_TEXT);
  printCentered("Configurar WiFi", 160, 46, 2);
  printCentered(line1, 160, 86, 1);
  printCentered(line2, 160, 106, 2);
  printCentered(line3, 160, 136, 1);
  printCentered("BACK: cancelar", 160, 186, 1);
}

void startWifiSetup() {
  currentMode = MODE_WIFISETUP;
  setupSaved = false;
  drawStaticScreen();   // limpiar el menu de ajustes
  drawWifiSetupScreen("Buscando redes...", "", "");
  Blynk.disconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  setupNetCount = 0;
  for (int i = 0; i < n && setupNetCount < 12; i++) {
    String nm = WiFi.SSID(i);   // guardar el texto antes de usarlo
    const char* name = nm.c_str();
    if (!name[0]) continue;
    bool dup = false;
    for (int j = 0; j < setupNetCount; j++) if (strcmp(setupNets[j], name) == 0) dup = true;
    if (dup) continue;
    strncpy(setupNets[setupNetCount], name, 32);
    setupNets[setupNetCount][32] = 0;
    setupNetCount++;
  }
  WiFi.softAP(SETUP_AP_NAME);
  setupDns.start(53, "*", WiFi.softAPIP());
  setupServer.on("/", setupHandleRoot);
  setupServer.on("/guardar", HTTP_POST, setupHandleSave);
  setupServer.on("/registro.csv", []() {
    File f = LittleFS.open("/registro.csv", "r");
    if (!f) { setupServer.send(404, "text/plain", "Sin registro"); return; }
    setupServer.sendHeader("Content-Disposition", "attachment; filename=registro_teco.csv");
    setupServer.streamFile(f, "text/csv");
    f.close();
  });
  setupServer.on("/borrar", []() {
    if (setupServer.arg("seguro") == "si") {
      LittleFS.remove("/registro.csv");
      setupLog();
      setupServer.send(200, "text/html", F("<meta charset='utf-8'><body style='font-family:sans-serif;padding:24px'><p>Registro borrado.</p><a href='/'>Regresar</a></body>"));
    } else {
      setupServer.send(200, "text/html", F("<meta charset='utf-8'><body style='font-family:sans-serif;padding:24px'><p>&iquest;Seguro que quieres borrar todas las respuestas guardadas?</p><a href='/borrar?seguro=si'>S&iacute;, borrar</a> &nbsp; <a href='/'>No</a></body>"));
    }
  });
  setupServer.onNotFound(setupHandleOther);
  setupServer.begin();
  drawWifiSetupScreen("Desde tu celular, con\x82" "ctate a la red:", SETUP_AP_NAME,
                      "Se abrir\xA0 una p\xA0gina para elegir tu red.");
  Serial.println("WiFi: modo configuracion (red Teco-configuracion)");
}

void stopWifiSetup() {
  setupServer.stop();
  setupDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void updateWifiSetup() {
  setupDns.processNextRequest();
  setupServer.handleClient();

  if (setupSaved) {
    static bool shown = false;
    if (!shown) { drawWifiSetupScreen("Datos guardados.", "\xADListo!", "Teco se reinicia para conectarse."); shown = true; }
    if ((long)(millis() - setupRestartAt) >= 0) ESP.restart();
    return;
  }
  if (backPressed()) {
    soundBack();
    stopWifiSetup();
    if (wifiDataFilled()) { WiFi.begin(cfgSsid, cfgPass); wifiWindowStart = millis(); }
    drawStaticScreen();
    redrawHome();
  }
}

bool wifiAvailable() { return true; }
void wifiStatusLine(char* out, size_t n) {
  if (!wifiDataFilled()) { snprintf(out, n, "WiFi: sin configurar"); return; }
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(out, n, blynkTokenOk && Blynk.connected() ? "WiFi y Blynk: conectados" : "WiFi: conectado a %s", cfgSsid);
    return;
  }
  if (wifiSleeping) { snprintf(out, n, "WiFi: no conecta, reintenta luego"); return; }
  snprintf(out, n, "WiFi: buscando %s...", cfgSsid);
}
int wifiIconLevel() {
  if (!blynkConfigured || wifiSleeping) return -1;
  if (WiFi.status() != WL_CONNECTED) return 0;
  int r = WiFi.RSSI();
  return r > -60 ? 3 : (r > -75 ? 2 : 1);
}

#else
void setupBlynk() {}
void updateBlynk() {}
bool wifiAvailable() { return false; }
void wifiStatusLine(char* out, size_t n) { snprintf(out, n, "WiFi desactivado en el codigo"); }
int wifiIconLevel() { return -1; }
void startWifiSetup() {}
void updateWifiSetup() { if (backPressed()) { drawStaticScreen(); redrawHome(); } }
#endif

// Ajustes (mantener BACK 1.5 s en la casa)

// Cumpleanos y nawales (Cholq'ij)

const char* NAWAL_NAMES[20] = {
  "Imox", "Iq'", "Aq'ab'al", "K'at", "Kan", "Keme'", "Kej", "Q'anil", "Toj", "Tz'i'",
  "B'atz'", "E", "Aj", "I'x", "Tz'ikin", "Ajmaq", "No'j", "Tijax", "Kawoq", "Ajpu"
};
const char* NAWAL_MEANING[20] = {
  "El agua y la lluvia", "El viento y el aire", "El amanecer", "La red que une",
  "La serpiente emplumada", "Los abuelos que nos cuidan", "El venado y la fuerza",
  "La semilla", "La ofrenda y el agradecimiento", "La justicia y la amistad",
  "El hilo, el tejido y el arte", "El camino", "La ca\xA4" "a y el hogar",
  "El jaguar y la madre tierra", "El p\xA0jaro y la abundancia", "El perd\xA2n",
  "La sabidur\xA1" "a y las ideas", "La obsidiana que sana", "El trueno y la comunidad",
  "El sol y la vida"
};

long julianDay(int y, int m, int d) {
  int a = (14 - m) / 12;
  long yy = y + 4800 - a;
  int mm = m + 12 * a - 3;
  return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

// Calcular numero (1 a 13) y nawal (0 a 19) del Cholq'ij para una fecha
void cholqij(int y, int m, int d, int* num, int* sign) {
  long k = julianDay(y, m, d) - 584283L;
  *num = (int)(((k + 3) % 13 + 13) % 13) + 1;
  *sign = (int)(((k + 19) % 20 + 20) % 20);
}


int currentYear() {
  if (!clockValid()) return 2030;
  struct tm t; clockLocal(&t);
  return t.tm_year + 1900;
}

int daysInMonth(int m, int y) {
  const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[m - 1];
}

// Centrar texto en hasta dos lineas de maxChars caracteres
void printWrapped(const char* txt, int cx, int y, int maxChars) {
  int len = strlen(txt);
  if (len <= maxChars) { printCentered(txt, cx, y, 1); return; }
  int cut = maxChars;
  while (cut > 0 && txt[cut] != ' ') cut--;
  if (cut == 0) cut = maxChars;
  char a[40], b[40];
  strncpy(a, txt, cut); a[cut] = 0;
  strncpy(b, txt + cut + (txt[cut] == ' ' ? 1 : 0), sizeof(b) - 1); b[sizeof(b) - 1] = 0;
  printCentered(a, cx, y, 1);
  printCentered(b, cx, y + 12, 1);
}

// Dibujar un numero maya (puntos y barras) directo en pantalla
void drawMayaTft(int n, int x, int y, int size) {
  int bars = n / 5, dots = n % 5;
  int barW = size * 6 / 10, barH = imax(4, size / 11), gap = imax(3, size / 16), r = imax(3, size / 16);
  int totalH = bars * barH + imax(0, bars - 1) * gap + (dots ? 2 * r + (bars ? gap : 0) : 0);
  int yy = y + (size - totalH) / 2;
  if (dots) {
    int span = dots * 2 * r + (dots - 1) * gap, dx = x + (size - span) / 2 + r;
    for (int i = 0; i < dots; i++) tft.fillCircle(dx + i * (2 * r + gap), yy + r, r, MAYA_COLOR);
    yy += 2 * r + gap;
  }
  for (int b = 0; b < bars; b++) {
    tft.fillRoundRect(x + (size - barW) / 2, yy, barW, barH, 2, MAYA_COLOR);
    yy += barH + gap;
  }
}

void drawNawalCard(int y, int m, int d, int cx, int top, const char* title) {
  int num, sign;
  cholqij(y, m, d, &num, &sign);
  tft.fillRoundRect(cx - 34, top, 68, 68, 8, LS_CARD_BORDER);
  tft.fillRoundRect(cx - 32, top + 2, 64, 64, 7, LS_CARD_COLOR);
  drawMayaTft(num, cx - 30, top + 4, 60);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d %s", num, NAWAL_NAMES[sign]);
  setTxtColor(COLOR_TEXT);
  printCenteredW(title, cx, top - 12, 1, 118);
  printCenteredW(buf, cx, top + 74, 2, 118);
  setTxtColor(LS_SPANISH_COLOR);
  printWrapped(NAWAL_MEANING[sign], cx, top + 94, 17);
}

#define SETTINGS_ITEMS 7
int settingsSel = 0;
enum { SUB_MENU, SUB_DATE, SUB_NAWAL, SUB_PARTY };
int settingsSub = SUB_MENU;
int dateField = 0;   // 0 dia, 1 mes, 2 ano
int editDay = 1, editMonth = 1, editYear = 2020;
void startWifiSetup();
bool wifiAvailable();
void wifiStatusLine(char* out, size_t n);

void settingsPanel(const char* title) {
  tftNine(&kitPlacaFrames[0], 28, 20, 292, 224, 4, 2);
  setTxtColor(COLOR_TEXT);
  printCentered(title, 160, 34, 2);
}

void drawSettingsItem(int i) {
  uint16_t panel = RGB565(255, 244, 222), border = RGB565(138, 90, 50), sel = RGB565(255, 214, 120);
  char txt[32];
  if (i == 0) strcpy(txt, "WiFi y datos");
  else if (i == 1) snprintf(txt, sizeof(txt), "Volumen: %d", imin(dfVolume, TECO_MAX_VOLUME));
  else if (i == 2) snprintf(txt, sizeof(txt), "Perfil: ni\xA4o N%02d", activeProfile + 1);
  else if (i == 3) strcpy(txt, "Mi Teco");
  else if (i == 4) {
    if (bdDay) snprintf(txt, sizeof(txt), "Cumplea\xA4" "os: %d/%d/%d", bdDay, bdMonth, bdYear);
    else snprintf(txt, sizeof(txt), "Cumplea\xA4" "os");
  }
  else if (i == 5) strcpy(txt, "Mi nawal");
  else strcpy(txt, "Salir");
  int y = 62 + i * 20;
  tft.fillRect(62, y, 196, 18, i == settingsSel ? sel : RGB565(241, 224, 194));
  tft.drawRect(62, y, 196, 18, i == settingsSel ? RGB565(43, 19, 11) : border);
  setTxtColor(COLOR_TEXT);
  printCenteredW(txt, 160, y + 3, 1, 186);
}

void drawSettings() {
  settingsPanel("Ajustes");
  for (int i = 0; i < SETTINGS_ITEMS; i++) drawSettingsItem(i);
  setTxtColor(COLOR_TEXT);
  char st[48];
  wifiStatusLine(st, sizeof(st));
  printCenteredW(st, 160, 206, 1, 240);
}

void drawDateField(int i) {
  int vals[3] = { editDay, editMonth, editYear };
  const char* lab[3] = { "D\xA1" "a", "Mes", "A\xA4" "o" };
  int x = 58 + i * 72, w = (i == 2) ? 76 : 60;
  bool sel = (i == dateField);
  tft.fillRoundRect(x, 92, w, 40, 8, sel ? RGB565(255, 214, 120) : RGB565(255, 252, 240));
  tft.drawRoundRect(x, 92, w, 40, 8, RGB565(138, 90, 50));
  char b[8]; snprintf(b, sizeof(b), "%d", vals[i]);
  setTxtColor(COLOR_TEXT);
  printCenteredW(b, x + w / 2, 104, 2, w - 6);
}

void drawDateEntry() {
  settingsPanel("Cumplea\xA4" "os");
  printCentered("Fecha de nacimiento del ni\xA4" "o", 160, 58, 1);
  int vals[3] = { editDay, editMonth, editYear };
  const char* lab[3] = { "D\xA1" "a", "Mes", "A\xA4" "o" };
  for (int i = 0; i < 3; i++) {
    int x = 58 + i * 72, w = (i == 2) ? 76 : 60;
    bool sel = (i == dateField);
    tft.fillRoundRect(x, 92, w, 40, 8, sel ? RGB565(255, 214, 120) : RGB565(255, 252, 240));
    tft.drawRoundRect(x, 92, w, 40, 8, RGB565(138, 90, 50));
    char b[8]; snprintf(b, sizeof(b), "%d", vals[i]);
    setTxtColor(COLOR_TEXT);
    printCentered(b, x + w / 2, 104, 2);
    printCentered(lab[i], x + w / 2, 138, 1);
  }
  printCentered("< >: cambiar   OK: siguiente", 160, 176, 1);
  printCentered("BACK: regresar", 160, 192, 1);
}

void drawNawalScreen() {
  settingsPanel("Nawal");
  if (bdDay) {
    drawNawalCard(bdYear, bdMonth, bdDay, tecoBorn ? 98 : 160, 66, "Tu nawal");
  } else {
    setTxtColor(COLOR_TEXT);
    printCentered("Primero escribe tu cumplea\xA4" "os", 160, 96, 1);
  }
  if (tecoBorn) {
    time_t t = (time_t)tecoBorn - 6 * 3600;   // hora de Guatemala (UTC-6)
    struct tm b; gmtime_r(&t, &b);
    drawNawalCard(b.tm_year + 1900, b.tm_mon + 1, b.tm_mday, bdDay ? 222 : 160, 66, "Nawal de Teco");
  }
  setTxtColor(COLOR_TEXT);
  printCentered("Para conocer su significado completo,", 160, 186, 1);
  printCentered("consulta a un Ajq'ij.", 160, 198, 1);
}

void drawParty() {
  // Dibujar confeti de colores
  const uint16_t cols[5] = { RGB565(214, 58, 58), RGB565(248, 196, 40), RGB565(58, 150, 82), RGB565(64, 120, 210), RGB565(200, 80, 170) };
  for (int i = 0; i < 90; i++) tft.fillRect(random(0, 316), random(0, 236), 4, 4, cols[i % 5]);
  settingsPanel("\xAD" "Feliz cumplea\xA4" "os!");
  const MiniSprite* t = &tecoMiniBienFrames[1];
  drawSpriteOnColor(t, 52, 74, RGB565(241, 224, 194));
  tftSprite(&sombreroFiestaFrames[0], 52 + t->w / 2 - 8, 56, 1);   // sombrero de fiesta
  tftSprite(&pastelFrames[0], 52 + t->w / 2 - 18, 140, 1);   // pastel
  if (bdDay) drawNawalCard(bdYear, bdMonth, bdDay, 196, 70, "Tu nawal");
  setTxtColor(COLOR_TEXT);
  printCentered("OK: gracias, Teco", 160, 200, 1);
}

void startSettings() {
  currentMode = MODE_SETTINGS;
  stopLife();
  endGreeting();
  settingsSel = 0;
  settingsSub = SUB_MENU;
  soundClick();
  drawSettings();
  Serial.println("AJUSTES");
}

void startParty() {
  currentMode = MODE_SETTINGS;
  stopLife();
  endGreeting();
  settingsSub = SUB_PARTY;
  playMilpa(AUDIO_MILPA_BIEN);
  drawParty();
  Serial.println("CUMPLEANOS: fiesta");
}

void adaptLevel(int* lvl) {
  lastLevelChange = 0;
  if (gameMistakes >= 3 && *lvl > 1) { (*lvl)--; lastLevelChange = -1; }
  else if (midLevelUps > 0) lastLevelChange = 1;   // nivel ya subido durante la partida
  else if (gameMistakes <= 1 && *lvl < 5) { (*lvl)++; lastLevelChange = 1; }
  markProgress();
  Serial.print("Dificultad: errores = "); Serial.print(gameMistakes);
  Serial.print(" -> nivel "); Serial.println(*lvl);
}

void resetProgressDefaults() {
  hunger = 7; energy = 7; fun = 7; learn = 7;
  milpaGames = 0; huipilesTejidos = 0; aciertosHoy = 0;
  lessonNext = 0; lessonsDone = 0;
  for (int i = 0; i < (int)LESSON_WORD_COUNT; i++) wordMiss[i] = 0;
  coins = 0; souvenirs = 0;
  lvlMilpa = 1; lvlTelar = 1; lvlBarrilete = 1; lvlViaje[0] = lvlViaje[1] = lvlViaje[2] = 1;
  albAnimals = 0; albFoods = 0; albNumbers = 0;
  for (int k = 0; k < 4; k++) stickerBits[k] = 0;
  stickerPending = false; stickerQLen = 0;
  bestStars[0] = bestStars[1] = bestStars[2] = 0;
  bdDay = bdMonth = bdYear = 0; partyYear = 0;
}

void switchProfile(int p) {
  saveProgress();
  activeProfile = p;
  saveActiveProfile();
  resetProgressDefaults();
  loadProgress();
  Serial.print("PERFIL -> N0"); Serial.println(activeProfile + 1);
}

void exitSettings() {
  drawStaticScreen();
  redrawHome();
}

void updateSettings() {
  if (settingsSub == SUB_PARTY) {
    if (okPressed() || backPressed()) { soundClick(); drawStaticScreen(); startPetMode(); }
    return;
  }
  if (settingsSub == SUB_NAWAL) {
    if (okPressed() || backPressed()) { soundBack(); settingsSub = SUB_MENU; drawSettings(); }
    return;
  }
  if (settingsSub == SUB_DATE) {
    int* v = (dateField == 0) ? &editDay : (dateField == 1) ? &editMonth : &editYear;
    int lo = (dateField == 2) ? 1920 : 1;
    int hi = (dateField == 0) ? daysInMonth(editMonth, editYear) : (dateField == 1) ? 12 : currentYear();
    if (leftPressed())  { *v = (*v <= lo) ? hi : *v - 1; soundClick(); drawDateField(dateField); }
    if (rightPressed()) { *v = (*v >= hi) ? lo : *v + 1; soundClick(); drawDateField(dateField); }
    // Mantener presionado para avanzar rapido (util para el ano)
    static unsigned long holdSince = 0, nextRepeat = 0;
    int dir = (digitalRead(BTN_RIGHT) == LOW) ? 1 : (digitalRead(BTN_LEFT) == LOW) ? -1 : 0;
    unsigned long nowH = millis();
    if (dir == 0) holdSince = 0;
    else if (holdSince == 0) holdSince = nowH;
    else if (nowH - holdSince > 450 && (long)(nowH - nextRepeat) >= 0) {
      *v += dir;
      if (*v > hi) *v = lo;
      if (*v < lo) *v = hi;
      nextRepeat = nowH + 70;
      drawDateField(dateField);
    }
    if (okPressed()) {
      soundClick();
      if (editDay > daysInMonth(editMonth, editYear)) editDay = daysInMonth(editMonth, editYear);
      if (dateField < 2) { dateField++; drawDateField(dateField - 1); drawDateField(dateField); }
      else {
        bdDay = editDay; bdMonth = editMonth; bdYear = editYear;
        partyYear = 0;
        markProgress();
        settingsSub = SUB_NAWAL;
        drawNawalScreen();
        Serial.print("Cumpleanos guardado: ");
        Serial.print(bdDay); Serial.print("/"); Serial.print(bdMonth); Serial.print("/"); Serial.println(bdYear);
      }
    }
    if (backPressed()) {
      soundBack();
      if (dateField > 0) { dateField--; drawDateField(dateField + 1); drawDateField(dateField); }
      else { settingsSub = SUB_MENU; drawSettings(); }
    }
    return;
  }
  if (backPressed()) { soundBack(); exitSettings(); return; }
  // Redibujar solo las dos opciones que cambian (sin parpadeo)
  if (leftPressed())  { int o = settingsSel; settingsSel = (settingsSel + SETTINGS_ITEMS - 1) % SETTINGS_ITEMS; soundClick(); drawSettingsItem(o); drawSettingsItem(settingsSel); }
  if (rightPressed()) { int o = settingsSel; settingsSel = (settingsSel + 1) % SETTINGS_ITEMS; soundClick(); drawSettingsItem(o); drawSettingsItem(settingsSel); }
  if (okPressed()) {
    soundClick();
    if (settingsSel == 0) {
      if (wifiAvailable()) startWifiSetup();
    } else if (settingsSel == 1) {
      // niveles 6, 12, 18 y 24
      dfVolume = (dfVolume >= TECO_MAX_VOLUME) ? 6 : imin(TECO_MAX_VOLUME, (dfVolume / 6 + 1) * 6);
      dfSetVolume(dfVolume);
      markProgress();
      drawSettingsItem(1);
    } else if (settingsSel == 2) {
      switchProfile((activeProfile + 1) % 4);
      drawSettings();
    } else if (settingsSel == 3) {
      settingsSub = SUB_NAWAL;   // regresar igual que en nawales
      drawMiTeco();
    } else if (settingsSel == 4) {
      settingsSub = SUB_DATE;
      dateField = 0;
      if (bdDay) { editDay = bdDay; editMonth = bdMonth; editYear = bdYear; }
      else { editDay = 1; editMonth = 1; editYear = currentYear() - 6; }   // fecha inicial: nino de unos 6 anos
      drawDateEntry();
    } else if (settingsSel == 5) {
      settingsSub = SUB_NAWAL;
      drawNawalScreen();
    } else {
      exitSettings();
    }
  }
}

// Dibujar el icono de WiFi en la esquina superior derecha
int wifiIconShown = -2;
void hbWifiIcon(int bx, int by, int bw, int rows) {
  int lvl = wifiIconLevel();
  if (lvl < 0) return;
  for (int b = 0; b < 3; b++) {
    int h = 4 + b * 3, x0 = 302 + b * 5, y1 = 16, y0 = y1 - h;
    uint16_t c = (lvl > b) ? RGB565(255, 255, 255) : RGB565(120, 130, 140);
    for (int y = imax(y0 - 1, by); y < imin(y1 + 1, by + rows); y++)
      for (int x = imax(x0 - 1, bx); x < imin(x0 + 4, bx + bw); x++) {
        bool inner = (y >= y0 && y < y1 && x >= x0 && x < x0 + 3);
        bandBuffer[(y - by) * bw + (x - bx)] = inner ? c : RGB565(52, 32, 24);
      }
  }
}

void updateWifiIcon() {
  if (currentMode != MODE_HOME || transitionActive) return;
  int lvl = wifiIconLevel();
  if (lvl != wifiIconShown) {
    wifiIconShown = lvl;
    renderHome(300, 2, 18, 16);
  }
}


// Hora real (por internet)

#define CLOCK_UTC_OFFSET   (-6 * 3600)   // Guatemala, sin horario de verano
#define NIGHT_START_HOUR   18   // anochecer cerca de las 6 pm
#define NIGHT_END_HOUR     6
const unsigned long CLOCK_CHECK_MS   = 5000;
const unsigned long CLOCK_SAVE_MS    = 5UL * 60UL * 1000UL;   // guardar la hora cada 5 min
const unsigned long GREETING_MS      = 4500;
#define OFFLINE_MAX_DROP   5   // maximo que baja una necesidad estando apagado

long clockTestShift = 0;   // solo para pruebas en simulacion
bool clockReady = false;
unsigned long lastClockCheck = 0;
unsigned long lastClockSave = 0;

unsigned long greetingUntil = 0;
#define GREET_X 186
#define GREET_Y 50
#define GREET_W 132
#define GREET_H 52

time_t clockNow() { return time(nullptr) + clockTestShift; }
bool clockValid() { return clockNow() > 1700000000; }

void clockLocal(struct tm* t) {
  time_t local = clockNow() + CLOCK_UTC_OFFSET;
  gmtime_r(&local, t);
}

bool clockNight() {
  if (!clockValid()) return false;
  struct tm t; clockLocal(&t);
  return t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR;
}

bool greetingActive() { return greetingUntil != 0; }

// Saludar segun la hora con un globo junto a Teco
#define PETB_X 186
#define PETB_Y 50
#define PETB_W 132
#define PETB_H 52
// Dibujar la caja de dialogo de Teco (estilo juego de rol)
#define DLG_X 6
#define DLG_Y 6
#define DLG_W 308
#define DLG_H 54
void drawDialogBox(const char* kaq, const char* es, const char* name, uint16_t tabColor);
void drawTecoBubble(const char* kaq, const char* es) { drawDialogBox(kaq, es, "Teco", RGB565(225, 102, 28)); }

void drawDialogBox(const char* kaq, const char* es, const char* name, uint16_t tabColor) {
  uint16_t ink = RGB565(30, 30, 30);
  tft.fillRoundRect(DLG_X, DLG_Y, DLG_W, DLG_H, 8, RGB565(255, 255, 255));
  tft.drawRoundRect(DLG_X, DLG_Y, DLG_W, DLG_H, 8, ink);
  tft.drawRoundRect(DLG_X + 1, DLG_Y + 1, DLG_W - 2, DLG_H - 2, 7, ink);
  // Dibujar la pestana con el nombre de quien habla
  int tw = imax(56, textWidth(name, 1) + 20);
  tft.fillRoundRect(DLG_X + 10, DLG_Y + DLG_H - 6, tw, 20, 5, tabColor);
  tft.drawRoundRect(DLG_X + 10, DLG_Y + DLG_H - 6, tw, 20, 5, ink);
  tft.drawRoundRect(DLG_X + 11, DLG_Y + DLG_H - 5, tw - 2, 18, 4, ink);
  setTxtColor(RGB565(255, 255, 255));
  printCenteredW(name, DLG_X + 10 + tw / 2, DLG_Y + DLG_H - 3, 1, tw - 6);
  // Escribir texto alineado a la izquierda
  setTxtColor(ink);
  drawText(kaq, DLG_X + 14, DLG_Y + 7, 2);
  setTxtColor(RGB565(110, 110, 110));
  drawText(es, DLG_X + 14, DLG_Y + 35, 1);
}

void drawPetBubble(const char* kaq, const char* es) { drawTecoBubble(kaq, es); }
void clearPetBubble() { renderHome(0, 0, 320, DLG_Y + DLG_H + 18); }

void clearSpeechBubble() { renderHome(0, 0, 320, DLG_Y + DLG_H + 18); }

// Dibujar el globo de Teco: palabra en Kaqchikel y su traduccion
void drawSpeechBubble(const char* kaq, const char* es) {
  drawTecoBubble(kaq, es);
}

void showGreeting() {
  if (transitionActive) { greetingPending = true; return; }
  if (!clockValid() || currentMode != MODE_HOME) return;
  struct tm t; clockLocal(&t);
  const char* kaq; const char* es;
  if (t.tm_hour >= 6 && t.tm_hour < 12)       { kaq = "xsaqer";      es = "buenos d\xA1" "as"; }
  else if (t.tm_hour >= 12 && t.tm_hour < 19) { kaq = "xqaq'ij";     es = "buenas tardes"; }
  else                                        { kaq = "xokaq'a'";    es = "buenas noches"; }

  endAsk();
  drawSpeechBubble(kaq, es);

  dfPlayFolder(AUDIO_FOLDER_TECO, 4);   // saludar con sonido y mostrar el saludo en Kaqchikel
  greetingUntil = millis() + GREETING_MS;

  // Animar el ala de Teco mientras saluda
  lifeLen = 0;
  for (int i = 0; i < 6; i++) lifeAdd(&bigWaveFrames[i % 2], NEST_CX, NEST_FOOT, 2, 300);
  lifeStart();

  Serial.print("Teco saluda: ");
  Serial.println(kaq);
}

void endGreeting() {
  if (!greetingActive()) return;
  greetingUntil = 0;
  renderHome(0, 0, 320, DLG_Y + DLG_H + 18);
}

// Primera hora valida: descontar el tiempo apagado y saludar
void onClockReady() {
  long now = (long)clockNow();
  if (tecoBorn == 0) { tecoBorn = now; markProgress(); Serial.println("Teco nacio hoy (fecha guardada)"); }
  if (savedEpoch > 1700000000 && now > savedEpoch) {
    long secs = now - savedEpoch;
    int dh = imin(OFFLINE_MAX_DROP, (int)(secs / (long)(HUNGER_DECAY_MS / 1000)));
    int df = imin(OFFLINE_MAX_DROP, (int)(secs / (long)(FUN_DECAY_MS / 1000)));
    int dl = imin(OFFLINE_MAX_DROP, (int)(secs / (long)(LEARN_DECAY_MS / 1000)));
    hunger = imax(1, hunger - dh);
    fun = imax(1, fun - df);
    learn = imax(1, learn - dl);
    Serial.print("Teco estuvo apagado ");
    Serial.print(secs / 60);
    Serial.println(" min: se le bajaron hambre, diversion y aprendizaje.");
    markProgress();
  }
  struct tm t; clockLocal(&t);
  if (savedDay != t.tm_yday) { aciertosHoy = 0; savedDay = t.tm_yday; markProgress(); }
  showGreeting();
}

void updateClock() {
  unsigned long now = millis();

  if (greetingActive() && (long)(now - greetingUntil) >= 0) endGreeting();

  if (now - lastClockCheck < (clockReady ? CLOCK_CHECK_MS : 500UL)) return;   // sin hora: revisar seguido
  lastClockCheck = now;
  if (!clockValid()) return;

  if (!clockReady) {
    clockReady = true;
    struct tm t0; clockLocal(&t0);
    Serial.printf("Hora real: lista, en Guatemala son las %02d:%02d\n", t0.tm_hour, t0.tm_min);
    onClockReady();
  }

  struct tm t; clockLocal(&t);
  if (t.tm_yday != savedDay) {   // nuevo dia: reiniciar aciertos
    savedDay = t.tm_yday;
    aciertosHoy = 0;
    markProgress();
  }
  if (now - lastClockSave >= CLOCK_SAVE_MS) {
    lastClockSave = now;
    markProgress();   // guardar tambien la hora
  }

  // Cumpleanos: celebrar una vez al ano, en casa y de dia
  if (bdDay && t.tm_mday == bdDay && t.tm_mon + 1 == bdMonth && partyYear != t.tm_year + 1900 &&
      currentMode == MODE_HOME && !greetingActive() && !clockNight()) {
    partyYear = t.tm_year + 1900;
    markProgress();
    startParty();
    return;
  }

  bool night = clockNight();

  // Hora de dormir: dormir solo si esta en casa
  if (night && currentMode == MODE_HOME && !greetingActive() &&
      (long)(now - nightAwakeUntil) >= 0) {
    Serial.println("Hora real: es de noche, Teco se va a dormir");
    sleptAtNight = true;
    startSleepMode();
    return;
  }

  // Amanecer: despertar solo y saludar si durmio de noche
  if (!night && currentMode == MODE_SLEEP && sleptAtNight) {
    Serial.println("Hora real: amanecio");
    sleptAtNight = false;
    wakeUp();
    showGreeting();
    return;
  }

  // Amanecer con fondo de noche en casa: pasar a dia
  if (!night && currentMode == MODE_HOME && isNightMode) {
    tecoSpr = &bigIdleFrames[0];
    transitionBackground(false);
  }
}

// Setup

// Mostrar la pantalla de inicio con el logo y una barra de carga
void drawSplash(int pct) {
  uint16_t border = RGB565(138, 90, 50);
  if (pct == 0) {
    uint16_t row[320];
    for (int y = 0; y < 240; y++) {
      for (int x = 0; x < 320; x++) row[x] = pgm_read_word(&splashPal[pgm_read_byte(&splashPx[y * 320 + x])]);
      tft.drawRGBBitmap(0, y, row, 320, 1);
    }
    tft.drawRoundRect(70, 214, 180, 14, 6, border);
  }
  int w = 176 * pct / 100;
  if (w > 0) tft.fillRoundRect(72, 216, w, 10, 4, RGB565(225, 102, 28));
}

void setup() {
  setCpuFrequencyMhz(TECO_CPU_MHZ);
  // Iniciar primero la pantalla para acortar el blanco del encendido
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.begin();
  tft.setRotation(1);
  tft.cp437(true);   // mostrar bien la a y la e con dieresis

  drawSplash(0);

  Serial.begin(115200);
  loadActiveProfile();
  setupLog();
  loadProgress();

  setupButtons();
  setupHall();

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  pinMode(TOUCH_PIN, INPUT);

  randomSeed(analogRead(34));

  // esperar ~1.3 s a que el DFPlayer este listo
  drawSplash(30);
  setupDFPlayer();
  dfSetVolume(dfVolume);
  drawSplash(70);

  setupBlynk();
  drawSplash(100);
  delay(250);

  drawStaticScreen();
  redrawHome();

  // Reproducir bienvenida despues de iniciar pantalla y mascota

  Serial.println("Tecolotchi UART DIRECTO: audio solo en JUGAR/TOUCH, comer y agua seguros");
}


// Sensor tactil (GPIO2)

void updateTouchSensor() {
  bool touchNow = digitalRead(TOUCH_PIN);
  unsigned long now = millis();
  if (touchNow != lastTouchState) {
    Serial.print("Sensor tactil: ");
    Serial.println(touchNow == HIGH ? "TOCADO" : "suelto");
  }

  if (touchNow == HIGH && lastTouchState == LOW && (now - lastTouchAt) > TOUCH_LOCK_MS) {
    lastTouchAt = now;

    if (currentMode == MODE_HOME) {
      startPetMode();   // mostrar la animacion de caricia
    } else if (currentMode == MODE_MILPA) {
      milpaReplayAudio();   // milpa: repetir la instruccion
    } else if (currentMode == MODE_TELAR) {
      telarReplay();   // telar: repetir la secuencia
    } else if (currentMode == MODE_LESSON) {
      lessonReplay();   // lecciones: repetir la palabra
    } else if (currentMode == MODE_VIAJE) {
      viajeReplay();   // viajes: repetir la palabra
    } else {
      Serial.println("TOUCH GPIO2 -> Teco dice hola (06/004)");
      dfPlayFolder(AUDIO_FOLDER_TECO, 4);
    }
  }

  lastTouchState = touchNow;
}

// Loop

void loop() {
  if (transitionActive) {   // mientras anochece o amanece
    updateTransition();
    updateBlynk();
    return;
  }
  updateButtons();
  updateLevelUpSound();
  updateDfLater();
  updateTouchSensor();
  updateHallSensor();
  updateCurrentMode();
  updateBlink();
  updateNeeds();
  updateClock();
  updateLife();
  updateNeedsAsk(lifeBusy());
  updateBlynk();
  updateWifiIcon();
  updateDFVolumeResend();
}
