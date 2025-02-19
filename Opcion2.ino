#include <Arduino.h>

// Pines
const int pinCaudalimetro = 2; // Pin del caudalímetro YF-S201
const int pinInicio = 3;       // Pin del pulsador de inicio
const int pinParo = 4;         // Pin del pulsador de paro
const int pinSSR = 5;          // Pin del SSR

// Variables
volatile unsigned int pulsos = 0; // Contador de pulsos del caudalímetro
float litros = 0.0;               // Litros contados
const float factorConversion = 450.0; // Factor de conversión, pulsos por litro

bool enFuncionamiento = false;    // Estado del sistema

// Interrupción para contar pulsos del caudalímetro
void contarPulsos() {
  pulsos++;
}

void setup() {
  // Configuración de pines
  pinMode(pinCaudalimetro, INPUT);
  pinMode(pinInicio, INPUT);
  pinMode(pinParo, INPUT);
  pinMode(pinSSR, OUTPUT);

  // Iniciar el puerto serie
  Serial.begin(9600);

  // Configuración de la interrupción
  attachInterrupt(digitalPinToInterrupt(pinCaudalimetro), contarPulsos, RISING);

  // Inicializar el SSR apagado
  digitalWrite(pinSSR, LOW);

  // Log inicial
  Serial.println("Sistema iniciado. Estado inicial: Desactivado");
}

void loop() {
  // Leer estado de los pulsadores
  bool inicioPresionado = digitalRead(pinInicio) == HIGH;
  bool paroPresionado = digitalRead(pinParo) == HIGH;

  // Manejar el estado del sistema
  if (inicioPresionado) {
    enFuncionamiento = true;
    pulsos = 0; // Reiniciar contador de pulsos
    litros = 0.0; // Reiniciar contador de litros
    digitalWrite(pinSSR, HIGH); // Encender SSR
    Serial.println("Sistema activado");
  }

  if (paroPresionado) {
    enFuncionamiento = false;
    digitalWrite(pinSSR, LOW); // Apagar SSR
    Serial.println("Sistema desactivado");
  }

  // Si el sistema está en funcionamiento, contar litros y verificar el límite
  if (enFuncionamiento) {
    litros = pulsos / factorConversion;
    Serial.print("Litros: ");
    Serial.println(litros);

    if (litros >= 2.0) {
      enFuncionamiento = false;
      digitalWrite(pinSSR, LOW); // Apagar SSR
      Serial.println("Sistema desactivado: Se alcanzaron 2 litros");
    }
  }

  // Pequeña pausa para evitar lecturas muy rápidas
  delay(100);
}