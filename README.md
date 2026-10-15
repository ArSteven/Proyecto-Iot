# Sistema IoT clasificador de huevos por peso

Proyecto de IoT – Unidades Tecnológicas de Santander (UTS).

Un ESP32 pesa cada huevo con una celda de carga + HX711, lo clasifica según su peso y, con dos servomotores, lo envía al compartimiento correspondiente. Cada huevo y las estadísticas de producción se publican por WiFi/MQTT para registrarlas en la nube.

> **Fase actual:** montaje en ESP32 (simulado en Wokwi). La parte de nube/dashboard se agrega después.

## Cómo funciona

1. El huevo cae sobre la báscula (plataforma con compuerta).
2. Cuando el peso supera 30 g, el ESP32 espera 5 lecturas estables y promedia.
3. Busca la categoría y gira el **servo desviador** hacia esa rampa.
4. Abre el **servo compuerta**, el huevo cae por la rampa y la compuerta se cierra.
5. Registra el huevo y publica el evento y el resumen por MQTT.

```
ESPERANDO → ESTABILIZANDO → POSICIONANDO → DESCARGANDO → VACIANDO → ESPERANDO
```

La lógica no usa `delay()`: todo va con `millis()`, así el ESP32 sigue clasificando aunque el WiFi o el bróker se caigan.

## Categorías (Colombia)

| Categoría | Peso | Ángulo desviador |
|---|---|---|
| JUMBO | ≥ 78 g | 0° |
| AAA | 67 – 77,9 g | 30° |
| AA | 60 – 66,9 g | 60° |
| A | 53 – 59,9 g | 90° |
| B | 46 – 52,9 g | 120° |
| C | < 46 g | 150° |

Se ajustan en `include/config.h` (bloque F).

## Hardware y conexiones

| Componente | Pin componente | ESP32 |
|---|---|---|
| HX711 | VCC / GND | 5V / GND |
| HX711 | DT | GPIO 19 |
| HX711 | SCK | GPIO 18 |
| Servo compuerta | PWM | GPIO 13 |
| Servo desviador | PWM | GPIO 14 |
| Servos | V+ / GND | 5V / GND |

En el montaje físico, alimenta los servos con una fuente de 5 V aparte (GND común con el ESP32): dos servos moviéndose pueden reiniciar el ESP32 si toman corriente del USB.

## MQTT

Bróker: `broker.hivemq.com:1883` (se cambia por la IP del EMQX en AWS en `config.h`).

| Tópico | Mensaje | Publica | Suscribe | Cuándo |
|---|---|---|---|---|
| `uts/huevos/estado` | `online` / `offline` / alertas (retenido) | ESP32 (y el bróker por Last Will) | Dashboard | Al conectar / al caerse |
| `uts/huevos/evento` | `{"id":7,"peso":62.3,"categoria":"AA","ts":1792081815}` | ESP32 | Nube / dashboard | Por cada huevo |
| `uts/huevos/estadisticas` | total, promedio, mín, máx y conteo por categoría (retenido) | ESP32 | Nube / dashboard | Cada huevo y cada 10 s |
| `uts/huevos/cmd` | `tara`, `reset`, `stats`, `sim:<g>` | Dashboard | ESP32 | Cuando el usuario lo envía |

Para ver todo en MQTT Explorer suscríbete a `uts/huevos/#` (no a `#` solo, el bróker público lo rechaza).

## Estructura

```
├── include/
│   ├── config.h            # Pines, red, bróker, tiempos y categorías
│   └── secrets.example.h   # Plantilla para tu WiFi real
├── src/
│   ├── clasificador/main.cpp  # Programa principal
│   └── calibracion/main.cpp   # Obtiene el factor de la celda real
├── diagram.json            # Circuito para Wokwi
├── wokwi.toml
├── libraries.txt           # Solo para wokwi.com en el navegador
└── platformio.ini
```

## Correrlo en VS Code (PlatformIO + Wokwi)

1. Abre la carpeta del proyecto en VS Code.
2. Compila el entorno `clasificador`:
   ```
   python -m platformio run -e clasificador
   ```
3. `F1` → **Wokwi: Start Simulator**.

### Probar la clasificación

- **Con la celda:** haz clic en el HX711 y sube el peso. En Wokwi la celda de 5 kg tiene ~2,4 g de resolución, así que para valores exactos usa la opción siguiente.
- **Con comandos** (monitor serial o publicando en `uts/huevos/cmd`):
  ```
  sim:63.5    → simula un huevo AA
  sim:80      → simula un JUMBO
  stats       → muestra el resumen
  reset       → borra las estadísticas
  tara        → pone la báscula en cero
  ```

## Montaje físico

1. Copia `include/secrets.example.h` como `include/secrets.h` con tu red WiFi (no se sube a GitHub).
2. Sube el entorno de calibración con la báscula vacía:
   ```
   python -m platformio run -e calibracion -t upload -t monitor
   ```
3. Pon un peso conocido, escribe sus gramos y copia el factor en `FACTOR_CALIBRACION`.
4. Sube el clasificador: `python -m platformio run -e clasificador -t upload`.

## Librerías

- [PubSubClient](https://github.com/knolleary/pubsubclient) – cliente MQTT
- [HX711](https://github.com/bogde/HX711) – celda de carga
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) – servomotores
