// ==================================================================
// SISTEMA IOT CLASIFICADOR DE HUEVOS POR PESO - UTS
// ESP32 + Celda de carga/HX711 + 2 servomotores + WiFi/MQTT
// ==================================================================

// ---------------------- BLOQUE 1: LIBRERÍAS -----------------------
#include <Arduino.h>
#include <WiFi.h>          // Conexión del ESP32 a la red WiFi
#include <PubSubClient.h>  // Cliente MQTT (connect, publish, subscribe, loop)
#include <HX711.h>         // Amplificador de la celda de carga
#include <ESP32Servo.h>    // Control de servomotores en ESP32
#include <time.h>          // Hora real por NTP para marcar cada huevo
#include "config.h"        // Pines, red, bróker y categorías

// Si config.h no lo define, la tara automática se hace cada 15 s
#ifndef INTERVALO_AUTOTARA_MS
#define INTERVALO_AUTOTARA_MS 15000UL
#endif

// ------------------ BLOQUE 2: OBJETOS GLOBALES --------------------
WiFiClient espClient;            // Socket TCP sobre WiFi
PubSubClient client(espClient);  // Cliente MQTT que usa ese socket
HX711 bascula;                   // Celda de carga
Servo servoCompuerta;            // Abre la báscula para soltar el huevo
Servo servoDesviador;            // Orienta la rampa hacia el compartimiento

// ------------------ BLOQUE 3: MÁQUINA DE ESTADOS ------------------
// ESPERANDO     -> báscula vacía, esperando un huevo
// ESTABILIZANDO -> hay peso, se esperan varias lecturas iguales
// POSICIONANDO  -> el desviador gira hacia el compartimiento
// DESCARGANDO   -> la compuerta se abre y el huevo cae
// VACIANDO      -> se espera que la báscula vuelva a quedar vacía
enum Estado { ESPERANDO, ESTABILIZANDO, POSICIONANDO, DESCARGANDO, VACIANDO };
Estado estado = ESPERANDO;
const char* NOMBRES_ESTADO[] = {"ESPERANDO", "ESTABILIZANDO", "POSICIONANDO", "DESCARGANDO", "VACIANDO"};

unsigned long tEstado = 0;        // Momento en que se entró al estado actual
unsigned long tUltimaLectura = 0;
float pesoActual = 0;
float pesoAnterior = 0;
int lecturasEstables = 0;
float sumaEstable = 0;
float pesoMedido = 0;             // Peso aceptado del huevo actual
int categoriaActual = -1;
unsigned long tUltimaTara = 0;    // Para la tara automática (corrige el corrimiento del cero)

// Modo simulación: permite inyectar un peso exacto por Serial o MQTT
bool simActivo = false;
float pesoSimulado = 0;

// ------------------ BLOQUE 4: ESTADÍSTICAS DE PRODUCCIÓN ----------
unsigned long conteo[NUM_CATEGORIAS] = {0};
unsigned long totalHuevos = 0;
float sumaPesos = 0;
float pesoMin = 0;
float pesoMax = 0;
unsigned long tUltimasEstadisticas = 0;
unsigned long tUltimoIntentoMqtt = 0;

// ------------------ BLOQUE 5: UTILIDADES --------------------------
void cambiarEstado(Estado nuevo) {
  estado = nuevo;
  tEstado = millis();
  Serial.printf("[ESTADO] %s\r\n", NOMBRES_ESTADO[nuevo]);
}

// Recorre la tabla de mayor a menor y devuelve el índice de la categoría
int clasificar(float peso) {
  for (int i = 0; i < NUM_CATEGORIAS; i++) {
    if (peso >= CATEGORIAS[i].pesoMin) return i;
  }
  return NUM_CATEGORIAS - 1;
}

// Hora real (epoch). Devuelve 0 si NTP aún no sincronizó.
unsigned long ahoraEpoch() {
  time_t t = time(nullptr);
  return (t > 1700000000) ? (unsigned long)t : 0;
}

float leerPeso() {
  if (simActivo) return pesoSimulado;
  float p = bascula.get_units(1);
  return (p < 0) ? 0 : p;  // La celda no debe reportar pesos negativos
}

// ------------------- BLOQUE 6: CONEXIÓN A WIFI --------------------
void setup_wifi() {
  Serial.printf("Conectando a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);                     // Modo estación (cliente de un router)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi conectado. IP: %s\r\n", WiFi.localIP().toString().c_str());
    configTime(-5 * 3600, 0, "pool.ntp.org");  // Hora de Colombia (UTC-5)
  } else {
    Serial.println("\nSin WiFi: el clasificador sigue funcionando sin enviar datos.");
  }
  randomSeed(micros());
}

// ------------------- BLOQUE 7: PUBLICACIÓN MQTT -------------------
void publicarEvento(float peso, int cat) {
  if (!client.connected()) return;
  char json[160];
  snprintf(json, sizeof(json),
           "{\"id\":%lu,\"peso\":%.1f,\"categoria\":\"%s\",\"ts\":%lu}",
           totalHuevos, peso, CATEGORIAS[cat].nombre, ahoraEpoch());
  client.publish(TOPIC_EVENTO, json);
  Serial.printf("[MQTT] %s -> %s\r\n", TOPIC_EVENTO, json);
}

void publicarEstadisticas() {
  if (!client.connected()) return;
  char json[400];
  float promedio = totalHuevos ? sumaPesos / totalHuevos : 0;
  int n = snprintf(json, sizeof(json),
                   "{\"total\":%lu,\"promedio\":%.1f,\"min\":%.1f,\"max\":%.1f,\"categorias\":{",
                   totalHuevos, promedio, pesoMin, pesoMax);
  for (int i = 0; i < NUM_CATEGORIAS; i++) {
    n += snprintf(json + n, sizeof(json) - n, "%s\"%s\":%lu",
                  i ? "," : "", CATEGORIAS[i].nombre, conteo[i]);
  }
  snprintf(json + n, sizeof(json) - n, "},\"uptime_s\":%lu,\"ts\":%lu}",
           millis() / 1000, ahoraEpoch());
  client.publish(TOPIC_ESTADISTICAS, json, true);  // Retenido: el último resumen queda en el bróker
}

void reiniciarEstadisticas() {
  for (int i = 0; i < NUM_CATEGORIAS; i++) conteo[i] = 0;
  totalHuevos = 0;
  sumaPesos = pesoMin = pesoMax = 0;
  Serial.println("[CMD] Estadísticas reiniciadas");
  publicarEstadisticas();
}

void registrarHuevo(float peso, int cat) {
  conteo[cat]++;
  totalHuevos++;
  sumaPesos += peso;
  if (totalHuevos == 1 || peso < pesoMin) pesoMin = peso;
  if (totalHuevos == 1 || peso > pesoMax) pesoMax = peso;
  Serial.printf("[HUEVO #%lu] %.1f g -> %s\r\n", totalHuevos, peso, CATEGORIAS[cat].nombre);
  publicarEvento(peso, cat);
  publicarEstadisticas();
}

// ------------------- BLOQUE 8: COMANDOS (Serial y MQTT) -----------
// tara          -> pone la báscula en cero
// reset         -> reinicia las estadísticas
// sim:<gramos>  -> simula un huevo con ese peso (ej. sim:63.5)
// stats         -> publica e imprime el resumen
// peso          -> muestra la lectura actual de la báscula en gramos
void procesarComando(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd == "tara") {
    bascula.tare(10);
    tUltimaTara = millis();
    Serial.println("[CMD] Tara realizada");
  } else if (cmd == "peso") {
    Serial.printf("[PESO] Lectura actual: %.1f g\r\n", bascula.get_units(5));
  } else if (cmd == "reset") {
    reiniciarEstadisticas();
  } else if (cmd == "stats") {
    publicarEstadisticas();
    Serial.printf("[STATS] total=%lu promedio=%.1f\r\n", totalHuevos,
                  totalHuevos ? sumaPesos / totalHuevos : 0);
    for (int i = 0; i < NUM_CATEGORIAS; i++)
      Serial.printf("   %-5s: %lu\r\n", CATEGORIAS[i].nombre, conteo[i]);
  } else if (cmd.startsWith("sim:")) {
    if (estado != ESPERANDO) {
      Serial.println("[CMD] Espera a que termine el huevo actual");
      return;
    }
    pesoSimulado = cmd.substring(4).toFloat();
    simActivo = true;
    Serial.printf("[CMD] Simulando huevo de %.1f g\r\n", pesoSimulado);
  } else {
    Serial.printf("[CMD] Comando desconocido: %s\r\n", cmd.c_str());
  }
}

// ------------------- BLOQUE 9: CALLBACK MQTT ----------------------
// Se ejecuta cuando llega un mensaje a un tópico suscrito (TOPIC_CMD)
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("[MQTT] Mensaje en %s: %s\r\n", topic, msg.c_str());
  procesarComando(msg);
}

// ------------------- BLOQUE 10: RECONEXIÓN MQTT -------------------
// A diferencia del taller, NO bloquea: si el bróker no responde se
// reintenta cada 5 s y mientras tanto la clasificación sigue trabajando.
void mantenerMqtt() {
  static bool estabaConectado = false;
  if (client.connected()) {
    estabaConectado = true;
    client.loop();
    return;
  }
  if (estabaConectado) {
    Serial.printf("[MQTT] Conexion perdida, rc=%d\r\n", client.state());
    estabaConectado = false;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  if (tUltimoIntentoMqtt && millis() - tUltimoIntentoMqtt < 5000) return;  // El primer intento es inmediato
  tUltimoIntentoMqtt = millis();

  String clientId = "ESP32Huevos-" + String(esp_random(), HEX);  // ID distinto en cada arranque
  Serial.print("Conectando a MQTT... ");
  // Last Will: si el ESP32 se cae, el bróker publica "offline" por él
  if (client.connect(clientId.c_str(), TOPIC_ESTADO, 1, true, "offline")) {
    Serial.println("conectado");
    client.publish(TOPIC_ESTADO, "online", true);
    client.subscribe(TOPIC_CMD);
    publicarEstadisticas();
  } else {
    Serial.printf("falló, rc=%d. Reintento en 5 s\r\n", client.state());
  }
}

// ------------------- BLOQUE 11: LÓGICA DE CLASIFICACIÓN -----------
void actualizarClasificador() {
  // Lectura periódica de la báscula
  if (millis() - tUltimaLectura >= INTERVALO_LECTURA_MS && (simActivo || bascula.is_ready())) {
    tUltimaLectura = millis();
    pesoAnterior = pesoActual;
    pesoActual = leerPeso();
  } else if (estado == ESPERANDO || estado == ESTABILIZANDO) {
    return;  // Sin lectura nueva no hay nada que evaluar en estos estados
  }

  switch (estado) {
    case ESPERANDO:
      if (pesoActual >= PESO_MIN_DETECCION) {
        lecturasEstables = 0;
        sumaEstable = 0;
        cambiarEstado(ESTABILIZANDO);
      } else if (!simActivo && millis() - tUltimaTara >= INTERVALO_AUTOTARA_MS) {
        bascula.tare(5);           // Báscula vacía: vuelve a poner el cero
        tUltimaTara = millis();
      }
      break;

    case ESTABILIZANDO:
      if (pesoActual < PESO_MIN_DETECCION) {           // Se retiró antes de pesarse
        cambiarEstado(ESPERANDO);
      } else if (fabs(pesoActual - pesoAnterior) <= TOLERANCIA_ESTABLE) {
        lecturasEstables++;
        sumaEstable += pesoActual;
        if (lecturasEstables >= LECTURAS_ESTABLES) {
          pesoMedido = sumaEstable / lecturasEstables;  // Promedio de lecturas estables
          categoriaActual = clasificar(pesoMedido);
          servoDesviador.write(CATEGORIAS[categoriaActual].anguloDesviador);
          Serial.printf("[PESO] %.1f g -> %s (desviador a %d°)\r\n", pesoMedido,
                        CATEGORIAS[categoriaActual].nombre,
                        CATEGORIAS[categoriaActual].anguloDesviador);
          cambiarEstado(POSICIONANDO);
        }
      } else {                                         // El peso aún se mueve
        lecturasEstables = 0;
        sumaEstable = 0;
      }
      break;

    case POSICIONANDO:
      if (millis() - tEstado >= TIEMPO_POSICIONAR_MS) {
        servoCompuerta.write(COMPUERTA_ABIERTA);
        cambiarEstado(DESCARGANDO);
      }
      break;

    case DESCARGANDO:
      if (millis() - tEstado >= TIEMPO_DESCARGA_MS) {
        servoCompuerta.write(COMPUERTA_CERRADA);
        if (simActivo) {             // En simulación el huevo "sale" al abrir la compuerta
          simActivo = false;
          pesoSimulado = 0;
        }
        registrarHuevo(pesoMedido, categoriaActual);
        cambiarEstado(VACIANDO);
      }
      break;

    case VACIANDO:
      if (pesoActual < PESO_MIN_DETECCION) {
        tUltimaTara = millis() - INTERVALO_AUTOTARA_MS + 1500;  // Re-tara 1,5 s después de vaciar
        cambiarEstado(ESPERANDO);
      } else if (millis() - tEstado >= TIEMPO_MAX_VACIADO_MS) {
        Serial.println("[ALERTA] La báscula no quedó vacía: posible huevo atascado");
        client.publish(TOPIC_ESTADO, "alerta: huevo atascado");
        tEstado = millis();          // Vuelve a avisar si sigue atascado
      }
      break;
  }
}

// ------------------- BLOQUE 12: SETUP -----------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Clasificador de huevos por peso - UTS ===");

  // Servos en posición inicial
  servoCompuerta.attach(SERVO_COMPUERTA_PIN, 500, 2400);
  servoDesviador.attach(SERVO_DESVIADOR_PIN, 500, 2400);
  servoCompuerta.write(COMPUERTA_CERRADA);
  servoDesviador.write(CATEGORIAS[0].anguloDesviador);

  // Báscula: escala y tara con la plataforma vacía
  bascula.begin(HX711_DT_PIN, HX711_SCK_PIN);
  bascula.set_scale(FACTOR_CALIBRACION);
  bascula.tare(10);
  tUltimaTara = millis();
  Serial.println("Báscula lista (tara hecha)");

  setup_wifi();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  client.setBufferSize(512);  // Por defecto son 256 bytes; el JSON de estadísticas es más largo

  Serial.println("Comandos: tara | peso | reset | stats | sim:<gramos>");
  cambiarEstado(ESPERANDO);
}

// ------------------- BLOQUE 13: LOOP ------------------------------
void loop() {
  mantenerMqtt();
  actualizarClasificador();

  // Comandos escritos en el monitor serial
  if (Serial.available()) {
    procesarComando(Serial.readStringUntil('\n'));
  }

  // Resumen periódico hacia la nube
  if (millis() - tUltimasEstadisticas >= INTERVALO_ESTADISTICAS_MS) {
    tUltimasEstadisticas = millis();
    publicarEstadisticas();
  }
}