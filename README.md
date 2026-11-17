<p align="center">
  <img src="docs/logo_tecolotchi.png" alt="Logo de Tecolotchi" width="320">
</p>

# Tecolotchi: mascota virtual para la enseñanza de idiomas mayas

## Descripción

Tecolotchi (Teco) es una mascota virtual física con forma de tecolote que enseña vocabulario básico en **Kaqchikel** a niñas y niños. Al cuidar a Teco (darle de comer y de beber, acariciarlo y mandarlo a dormir), el niño escucha y practica palabras en Kaqchikel. Incluye lecciones, minijuegos inspirados en la cultura guatemalteca (la milpa, el telar, el barrilete, viajes por Guatemala y leyendas), recompensas con monedas (*pwaq*) y estampas, y un dashboard en Blynk para que madres, padres y docentes vean el estado de la mascota y el progreso del niño.

Trabajo de graduación en modalidad de trabajo profesional, Universidad del Valle de Guatemala, 2026.

## Contenido

- [Descripción](#descripción)
- [Funcionalidades](#funcionalidades)
- [Tecnologías utilizadas](#tecnologías-utilizadas)
- [Requisitos previos](#requisitos-previos)
- [Instalación](#instalación)
- [Ejecución](#ejecución)
- [Capturas de pantalla](#capturas-de-pantalla)
- [Video demo](#video-demo)
- [Documentación](#documentación)
- [Autora](#autora)
- [Licencia](#licencia)

### Funcionalidades

- **Cuidado de la mascota:** hambre, energía, diversión y aprendizaje. Teco come el alimento que se elige por su nombre en Kaqchikel, bebe al acercarle un imán, responde a las caricias y cambia entre día y noche según la hora real.
- **Lecciones:** 28 lecciones de vocabulario con dibujo y audio para cada palabra, práctica de escucha y repaso de las palabras falladas.
- **Minijuegos:** la milpa (números), el telar (colores y secuencias) y el barrilete (atrapar la palabra que dice Teco). La dificultad sube durante la partida.
- **Viajes por Guatemala:** Biotopo del Quetzal, Tikal (números mayas) y el mercado. El pasaje se paga con las monedas ganadas y cada viaje da estampas.
- **Álbum y leyendas:** álbum de estampas y leyendas guatemaltecas adaptadas para niños, con palabras en Kaqchikel.
- **Perfiles y ajustes:** hasta 4 perfiles, volumen, cumpleaños, nawal y configuración del WiFi desde el dispositivo.
- **IoT:** dashboard en Blynk con 15 datos (estado, lecciones completadas, monedas y aciertos) y acciones remotas (comer, dormir, abrazo y volumen).

## Tecnologías utilizadas

| Área | Tecnología |
|---|---|
| Microcontrolador | ESP32 (ESP32 Dev Module, 4 MB de flash) |
| Lenguaje y entorno | C++ con Arduino IDE 2 y el paquete `esp32` de Espressif 3.x |
| Pantalla | TFT ILI9341 de 320 × 240 por SPI, con Adafruit GFX y Adafruit ILI9341 |
| Audio | DFPlayer Mini con MicroSD (MP3), controlado por UART |
| Sensores y entradas | Sensor Hall (imán), sensor táctil capacitivo y 4 botones |
| IoT | Blynk IoT (pines virtuales V0–V14 y app móvil) |
| Persistencia | Preferences (NVS) del ESP32 |
| Hora real | NTP por WiFi |
| Gráficos | Ilustraciones en Procreate convertidas a sprites RGB565 con Python (Pillow) |
| Audio de las palabras | Grabaciones editadas con ffmpeg (MP3 mono) |
| Hardware | PCB diseñada en Fritzing y carcasa impresa en 3D |

## Requisitos previos

**Software**

- [Arduino IDE 2](https://www.arduino.cc/en/software)
- Paquete de tarjetas **esp32 de Espressif 3.x**
- Bibliotecas **Adafruit GFX Library**, **Adafruit ILI9341** y **Blynk**
- Python 3
- Controlador USB-serie de la tarjeta, si la computadora no la reconoce (por ejemplo, CP210x)
- Cuenta en [Blynk](https://blynk.cloud) (opcional; sin ella Teco funciona sin dashboard)

**Hardware**

- ESP32 Dev Module, pantalla TFT ILI9341 de 320 × 240, DFPlayer Mini con bocina, tarjeta MicroSD (FAT32), sensor Hall con imán, 4 botones, sensor táctil y batería LiPo de 3.7 V con su módulo de carga.

Conexiones principales del ESP32:

| Componente | Pines del ESP32 |
|---|---|
| Pantalla ILI9341 | CS 32 · DC 27 · RST 33 · MOSI 13 · SCLK 14 |
| Botones | Izquierda 18 · Derecha 19 · OK 25 · Regresar 26 |
| Sensor Hall | 15 |
| Sensor táctil | 2 |
| DFPlayer Mini | RX 16 (al TX del DFPlayer) · TX 17 (al RX del DFPlayer) |

El diagrama completo del circuito y de la PCB está en el [informe final](docs/informe_final.pdf).

## Instalación

1. **Clonar el repositorio**

   ```bash
   git clone https://github.com/[tu-usuario]/2026-17048-elias-cobar-andrea-estefania.git
   cd 2026-17048-elias-cobar-andrea-estefania
   ```

2. **Instalar el soporte de ESP32.** En Arduino IDE, abrir *Archivo › Preferencias* y agregar en *URLs adicionales de gestor de tarjetas*:

   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

   Después, en *Herramientas › Placa › Gestor de tarjetas*, instalar **esp32** de Espressif Systems.

3. **Instalar las bibliotecas.** En *Herramientas › Gestionar bibliotecas*, instalar **Adafruit GFX Library**, **Adafruit ILI9341** y **Blynk**.

4. **Configurar las variables de entorno (opcional).**

   ```bash
   cp src/.env.example src/.env
   # Editar src/.env con los datos de Blynk
   python3 src/herramientas/env_a_secrets.py
   ```

   El script genera `src/teco/secrets.h`. Ni `.env` ni `secrets.h` se suben al repositorio.

5. **Preparar la MicroSD.** Formatearla en **FAT32** y copiar en la raíz las carpetas `02` a `07` de [`src/microsd`](src/microsd). En macOS, ejecutar `dot_clean /Volumes/<nombre-de-la-tarjeta>` para borrar los archivos ocultos. Insertar la tarjeta en el DFPlayer Mini.

6. **Configurar la placa.** En *Herramientas*:
   - Placa: **ESP32 Dev Module**
   - Flash Size: **4MB (32Mb)**
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
   - Upload Speed: **115200** (si la carga falla en macOS)

## Ejecución

1. Abrir [`src/teco/teco.ino`](src/teco/teco.ino) en Arduino IDE.
2. Apagar el interruptor de la batería y conectar el ESP32 por USB.
3. Seleccionar el puerto y presionar **Subir**.
4. Al encender, Teco muestra la pantalla de inicio y aparece en su nido. Controles:
   - **Izquierda / Derecha:** moverse por los menús. **OK:** elegir. **Regresar:** volver.
   - **Imán:** darle agua y regar en la milpa.
   - **Sensor táctil:** acariciar a Teco.
5. **WiFi (opcional):** en *Ajustes › WiFi y datos*, conectarse desde el celular a la red **Teco-configuracion**, elegir la red de la casa (2.4 GHz) y escribir la contraseña.
6. **Dashboard:** con WiFi y token configurados, el estado de Teco aparece en la app de Blynk.
7. **Pruebas de hardware:** los programas de [`src/pruebas_hardware`](src/pruebas_hardware) revisan cada componente por separado (Monitor Serie a 115200).

## Capturas de pantalla

Las pantallas del dispositivo (320 × 240) se muestran ampliadas a 960 × 720.

| # | Captura | Descripción |
|---|---|---|
| 01 | [<img src="capturas/captura-01.png" width="240">](capturas/captura-01.png) | Saludo según la hora (*xsaqer*, buenos días) |
| 02 | [<img src="capturas/captura-02.png" width="240">](capturas/captura-02.png) | Comer: elegir el alimento por su nombre en Kaqchikel |
| 03 | [<img src="capturas/captura-03.png" width="240">](capturas/captura-03.png) | Beber: acercar el imán para darle agua |
| 04 | [<img src="capturas/captura-04.png" width="240">](capturas/captura-04.png) | Caricia con el sensor táctil |
| 05 | [<img src="capturas/captura-05.png" width="240">](capturas/captura-05.png) | Menú de las 28 lecciones |
| 06 | [<img src="capturas/captura-06.png" width="240">](capturas/captura-06.png) | Lección, fase *conoce*: palabra con dibujo y audio |
| 07 | [<img src="capturas/captura-07.png" width="240">](capturas/captura-07.png) | Lección, fase *practica*: elegir la palabra que se escucha |
| 08 | [<img src="capturas/captura-08.png" width="240">](capturas/captura-08.png) | Menú de juegos |
| 09 | [<img src="capturas/captura-09.png" width="240">](capturas/captura-09.png) | La milpa: regar cada planta con el imán |
| 10 | [<img src="capturas/captura-10.png" width="240">](capturas/captura-10.png) | La milpa: cosechar y contar las mazorcas |
| 11 | [<img src="capturas/captura-11.png" width="240">](capturas/captura-11.png) | El telar: repetir la secuencia de colores |
| 12 | [<img src="capturas/captura-12.png" width="240">](capturas/captura-12.png) | El telar: huipil terminado |
| 13 | [<img src="capturas/captura-13.png" width="240">](capturas/captura-13.png) | El barrilete: atrapar la palabra que dice Teco |
| 14 | [<img src="capturas/captura-14.png" width="240">](capturas/captura-14.png) | Mapa de viajes |
| 15 | [<img src="capturas/captura-15.png" width="240">](capturas/captura-15.png) | Biotopo del Quetzal: buscar el animal |
| 16 | [<img src="capturas/captura-16.png" width="240">](capturas/captura-16.png) | Tikal: números mayas |
| 17 | [<img src="capturas/captura-17.png" width="240">](capturas/captura-17.png) | Mercado: comprar y pagar |
| 18 | [<img src="capturas/captura-18.png" width="240">](capturas/captura-18.png) | Estampa nueva para el álbum |
| 19 | [<img src="capturas/captura-19.png" width="240">](capturas/captura-19.png) | Leyendas de Guatemala |
| 20 | [<img src="capturas/captura-20.png" width="240">](capturas/captura-20.png) | Leyenda de El Cadejo |
| 21 | [<img src="capturas/captura-21.png" width="240">](capturas/captura-21.png) | Ajustes |
| 22 | [<img src="capturas/captura-22.png" width="240">](capturas/captura-22.png) | Cumpleaños |
| 23 | [<img src="capturas/captura-23.png" width="240">](capturas/captura-23.png) | Dashboard de Blynk |

## Video demo

[`demo/demo.mp4`](demo/demo.mp4): recorrido de 4 min 47 s por el prototipo, el cuidado de la mascota, las lecciones, los minijuegos, los viajes, las leyendas, los ajustes y el dashboard.

## Documentación

- Informe final: [`docs/informe_final.pdf`](docs/informe_final.pdf)
- Consentimiento de uso de foto y video firmado: [`docs/consentimiento_firmado.pdf`](docs/consentimiento_firmado.pdf)

La guía de replicación (materiales, conexiones, PCB y carga del programa) está en los anexos del informe final.

## Autora

**Andrea Estefanía Elias Cobar** · Carné **17048**
Licenciatura en Ingeniería en Ciencias de la Computación y Tecnologías de la Información
Facultad de Ingeniería, Universidad del Valle de Guatemala

Asesor: Ing. Hans Alonzo

## Licencia

El código está bajo la licencia MIT.
