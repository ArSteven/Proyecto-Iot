// ==================================================================
// CALIBRACIÓN DE LA CELDA DE CARGA (solo para el montaje físico)
// 1. Sube este programa con la báscula VACÍA (env:calibracion).
// 2. Pon un objeto de peso conocido (ej. una moneda o pesa de 100 g).
// 3. Escribe su peso en gramos en el monitor serial y presiona Enter.
// 4. Copia el factor que aparece en FACTOR_CALIBRACION de config.h.
// ==================================================================
#include <Arduino.h>
#include <HX711.h>
#include "config.h"

HX711 bascula;

void setup() {
  Serial.begin(115200);
  bascula.begin(HX711_DT_PIN, HX711_SCK_PIN);
  Serial.println("\n=== Calibración HX711 ===");
  Serial.println("Deja la báscula vacía...");
  delay(2000);
  bascula.set_scale();   // Escala 1: get_units devuelve cuentas crudas
  bascula.tare(20);
  Serial.println("Tara lista. Pon el peso conocido y escribe su valor en gramos:");
}

void loop() {
  if (Serial.available()) {
    float gramos = Serial.readStringUntil('\n').toFloat();
    if (gramos <= 0) {
      Serial.println("Escribe un número mayor a 0");
      return;
    }
    float cuentas = bascula.get_value(20);  // Promedio de 20 lecturas menos la tara
    float factor = cuentas / gramos;
    bascula.set_scale(factor);
    Serial.printf("Cuentas: %.0f  ->  FACTOR_CALIBRACION = %.4f\n", cuentas, factor);
    Serial.println("Verificación (debe marcar el peso conocido):");
  }

  static unsigned long t = 0;
  if (millis() - t > 1000) {
    t = millis();
    Serial.printf("Lectura: %.1f\n", bascula.get_units(5));
  }
}
