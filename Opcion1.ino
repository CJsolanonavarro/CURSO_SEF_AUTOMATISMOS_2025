/*************************************************************
 * Ejemplo de Control de Bombeo con Caudalímetro y SSR
 * 
 * Objetivo:
 *  - Al pulsar el botón de Marcha (Start), iniciar el bombeo
 *    y contar el volumen que pasa por el caudalímetro.
 *  - Detener el bombeo automáticamente al alcanzar 2 litros.
 *  - Si se pulsa el botón de Paro (Stop) en cualquier momento,
 *    se detiene el bombeo de inmediato.
 * 
 * Hardware y conexiones (ESP32 como ejemplo):
 * 
 * 1) Botón de Marcha con resistencia pull-down externa
 *    - Un pin del botón a GPIO 12 (pinStart)
 *    - El otro pin del botón a VCC (3.3V o 5V, según la placa)
 *    - Resistencia de ~10 kΩ entre GPIO 12 y GND
 * 
 * 2) Botón de Paro con resistencia pull-down externa
 *    - Un pin del botón a GPIO 13 (pinStop)
 *    - El otro pin del botón a VCC
 *    - Resistencia de ~10 kΩ entre GPIO 13 y GND
 * 
 * 3) Caudalímetro (YF-S201):
 *    - Pin de señal a GPIO 18 (flowsensor)
 *    - VCC a 5V (o 3.3V) y GND a GND (según especificaciones)
 *    - Se usa attachInterrupt(digitalPinToInterrupt(18), ...) en RISING
 * 
 * 4) Relé de estado sólido (SSR):
 *    - Pin de control del SSR a GPIO 26 (pinSSR)
 *    - SSR a su fuente de alimentación/entrada adecuada y
 *      la bomba/solenoide a la salida del SSR (según datasheet).
 *    - Asegurar que la polaridad y niveles sean correctos
 *      (SSR AC vs DC, carga mínima, etc.).
 * 
 * 5) El factor de conversión de pulsos a caudal está basado en
 *    la fórmula estándar para el YF-S201:
 *         Frecuencia(Hz) = 7.5 * Caudal(L/min)
 *    Ajusta si tu sensor requiere calibración distinta.
 *************************************************************/

// Pines de botones (con resistencias pull-down externas)
const int pinStart = 12;  // Botón de Marcha
const int pinStop  = 13;  // Botón de Paro

// Pin para controlar el SSR (encender/apagar bomba)
const int pinSSR   = 26;

// Pin para el caudalímetro
const int flowsensor = 18;

// Variables que se usan para medir y calcular el flujo
volatile int flow_frequency = 0;   // Contador de pulsos en 1 segundo
float volumen = 0.0;               // Volumen acumulado en litros
unsigned long prevTime = 0;        // Para controlar intervalos de medición

// Variable booleana para saber si estamos bombeando o no
bool enBombeo = false;

/**
 * @brief Rutina de interrupción para contar los pulsos del caudalímetro.
 * Se ejecuta cada vez que se detecta un flanco ascendente (RISING).
 */
void IRAM_ATTR conteoFlujo() {
  flow_frequency++;
}

void setup() {
  // Iniciamos la comunicación serie para ver los mensajes en consola
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== SISTEMA DE BOMBA CON CAUDALIMETRO ===");
  Serial.println("Iniciando configuracion de pines...");

  // Configuramos los pines de los botones como entrada normal,
  // dado que estamos usando pull-down EXTERNAS.
  pinMode(pinStart, INPUT);
  pinMode(pinStop, INPUT);

  // Configuramos el pin para el SSR como salida y lo apagamos inicialmente
  pinMode(pinSSR, OUTPUT);
  digitalWrite(pinSSR, LOW);

  // Configuramos el pin del caudalímetro
  // Podríamos usar INPUT_PULLUP si no tuviésemos una resistencia externa, 
  // pero en este caso se asume la conexión apropiada.
  pinMode(flowsensor, INPUT_PULLUP);

  // Configuramos la interrupción para contar los pulsos (flancos ascendentes)
  attachInterrupt(digitalPinToInterrupt(flowsensor), conteoFlujo, RISING);

  // Guardamos el momento actual para el conteo periódico
  prevTime = millis();

  Serial.println("Configuracion de pines completa.");
  Serial.println("Sistema listo. Esperando accion del usuario...");
}

void loop() {
  /*************************************************************
   * Lectura de botones
   * 
   * Con resistencias pull-down externas, el pin estará en LOW
   * por defecto y pasará a HIGH cuando pulsemos el botón.
   *************************************************************/
  
  // == Botón de Marcha ==
  if (digitalRead(pinStart) == HIGH) {
    // Si no estábamos bombeando, iniciamos proceso
    if (!enBombeo) {
      enBombeo = true;       // Marcamos que estamos en proceso de bombeo
      volumen = 0.0;         // Reiniciamos el contador de volumen
      digitalWrite(pinSSR, HIGH); // Encendemos la bomba a través del SSR

      Serial.println("[INFO] Boton Marcha pulsado. Iniciando bombeo...");
      Serial.println("        Volumen reiniciado a 0 L");
    }
    delay(200); // Pequeña pausa para evitar rebotes de botón
  }

  // == Botón de Paro ==
  if (digitalRead(pinStop) == HIGH) {
    // Si estábamos bombeando, lo detenemos
    if (enBombeo) {
      enBombeo = false;
      digitalWrite(pinSSR, LOW); // Apagamos la bomba (SSR)

      Serial.println("[INFO] Boton Paro pulsado. Bombeo detenido manualmente.");
      Serial.print("        Volumen acumulado hasta el momento: ");
      Serial.print(volumen, 3);
      Serial.println(" L");
    }
    delay(200); // Evita posibles rebotes
  }

  /*************************************************************
   * Cálculo y actualización del flujo cada cierto intervalo
   * (por ejemplo, 1 segundo).
   *************************************************************/
  
  unsigned long currentTime = millis();
  if (currentTime - prevTime >= 1000) {
    // Ha pasado 1 segundo, realizamos el cálculo de caudal
    prevTime = currentTime;

    if (enBombeo) {
      /******************************************************
       * El caudalímetro YF-S201 típicamente usa la fórmula:
       *   Frecuencia(Hz) = 7.5 * Caudal(L/min)
       * => Caudal (L/min) = Frecuencia / 7.5
       * => Litros/segundo = Caudal(L/min) / 60
       * => Litros/segundo = Frecuencia / (7.5 * 60) = Frecuencia / 450
       ******************************************************/
      
      float litrosEnEsteSegundo = flow_frequency / 450.0;
      volumen += litrosEnEsteSegundo;  // Acumulamos al total

      // Mostramos en consola lo que ha sucedido este segundo
      Serial.println("-----------------------------------");
      Serial.print  ("Frecuencia de pulsos en 1s: ");
      Serial.print  (flow_frequency);
      Serial.println(" Hz");
      Serial.print  ("Litros en este segundo: ");
      Serial.print  (litrosEnEsteSegundo, 3);
      Serial.println(" L");
      Serial.print  ("Volumen total acumulado: ");
      Serial.print  (volumen, 3);
      Serial.println(" L");
      Serial.println("-----------------------------------");

      // Reseteamos el contador de pulsos para la próxima medición
      flow_frequency = 0;

      // Comprobamos si ya llegamos a los 2 litros
      if (volumen >= 2.0) {
        // Apagamos la bomba
        enBombeo = false;
        digitalWrite(pinSSR, LOW);

        Serial.println("[INFO] Se han bombeado al menos 2 litros. Deteniendo bombeo...");
        Serial.print  ("       Volumen final: ");
        Serial.print  (volumen, 3);
        Serial.println(" L");
      }
    } else {
      // Si no estamos bombeando, igual ponemos el contador a 0 
      // para que no se acumule entre lecturas.
      flow_frequency = 0;
    }
  }

  // El resto del tiempo, el sistema está a la espera de pulsar
  // Marcha o Paro, y de calcular el flujo una vez por segundo.
}
