// ==================================================================
// CONFIGURACIÓN DEL CLASIFICADOR DE HUEVOS (pines, red, MQTT, pesos)
// Todo lo que se ajusta sin tocar la lógica está en este archivo.
// ==================================================================
#pragma once

// ---------------------- BLOQUE A: RED WIFI ------------------------
// Si existe include/secrets.h (ignorado por git) se usan sus datos.
// Si no existe, se usa la red del simulador Wokwi.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID     "Wokwi-GUEST"
  #define WIFI_PASSWORD ""
#endif

// ---------------------- BLOQUE B: BRÓKER MQTT ---------------------
// Bróker público (igual que en el taller). Para usar tu EMQX en AWS,
// cambia por la IP pública de la instancia, ej. "3.15.1.1".
#define MQTT_SERVER   "broker.hivemq.com"
#define MQTT_PORT     1883
#define MQTT_TOPIC_BASE "uts/huevos"   // Cámbialo si compartes bróker con tu grupo

// Tópicos derivados de la base
#define TOPIC_ESTADO       MQTT_TOPIC_BASE "/estado"        // online / offline (retenido)
#define TOPIC_EVENTO       MQTT_TOPIC_BASE "/evento"        // un JSON por cada huevo
#define TOPIC_ESTADISTICAS MQTT_TOPIC_BASE "/estadisticas"  // resumen de producción (retenido)
#define TOPIC_CMD          MQTT_TOPIC_BASE "/cmd"           // órdenes hacia el ESP32

#define INTERVALO_ESTADISTICAS_MS 10000UL  // Cada cuánto se publica el resumen

// ---------------------- BLOQUE C: PINES ---------------------------
#define HX711_DT_PIN        19   // Datos del HX711
#define HX711_SCK_PIN       18   // Reloj del HX711
#define SERVO_COMPUERTA_PIN 13   // Servo que abre la báscula y deja caer el huevo
#define SERVO_DESVIADOR_PIN 14   // Servo que gira la rampa hacia el compartimiento

// ---------------------- BLOQUE D: BÁSCULA -------------------------
// En Wokwi la celda "5kg" entrega 0-2100 cuentas para 0-5 kg -> 0.42 cuentas/gramo.
// En hardware real este valor se obtiene con el programa de calibración (env:calibracion).
#define FACTOR_CALIBRACION 0.42f

#define PESO_MIN_DETECCION   30.0f   // g. Por debajo de esto la báscula se considera vacía
#define TOLERANCIA_ESTABLE    1.5f   // g. Variación máxima entre lecturas para considerar estable
#define LECTURAS_ESTABLES       5    // Lecturas seguidas estables para aceptar el peso
#define INTERVALO_LECTURA_MS  100UL  // Cada cuánto se lee la celda

// ---------------------- BLOQUE E: SERVOS --------------------------
#define COMPUERTA_CERRADA     0     // grados
#define COMPUERTA_ABIERTA    90     // grados
#define TIEMPO_POSICIONAR_MS 600UL  // Espera a que el desviador llegue a su ángulo
#define TIEMPO_DESCARGA_MS   800UL  // Tiempo que la compuerta queda abierta
#define TIEMPO_MAX_VACIADO_MS 3000UL // Si el huevo no sale en este tiempo, se avisa

// ---------------------- BLOQUE F: CATEGORÍAS ----------------------
// Clasificación por peso usada en Colombia (de mayor a menor).
// El huevo cae en la PRIMERA categoría cuyo peso mínimo alcance.
struct Categoria {
  const char* nombre;
  float pesoMin;        // gramos
  int anguloDesviador;  // grados del servo desviador
};

static const Categoria CATEGORIAS[] = {
  {"JUMBO", 78.0f,   0},
  {"AAA",   67.0f,  30},
  {"AA",    60.0f,  60},
  {"A",     53.0f,  90},
  {"B",     46.0f, 120},
  {"C",      0.0f, 150},
};
static const int NUM_CATEGORIAS = sizeof(CATEGORIAS) / sizeof(CATEGORIAS[0]);
