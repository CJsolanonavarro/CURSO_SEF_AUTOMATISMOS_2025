/*************************************************************
 * Ejemplo de Control de Bombeo con 2 Modos (NORMAL y RETORNO)
 * con 2 Bombas, 2 Caudalímetros, Sensor DHT22, Pantalla SSD1306 y
 * Semáforo (3 LEDs).  
 *
 * Funcionalidad:
 * - El botón START (pin 12) inicia el bombeo en el modo actual:
 *     • Si el modo es NORMAL, se enciende la Bomba #1 (SSR en pin 26)
 *       y se utiliza el caudalímetro conectado al pin 18.
 *     • Si el modo es RETORNO, se enciende la Bomba #2 (SSR en pin 27)
 *       y se utiliza el caudalímetro conectado al pin 19.
 *   En ambos casos se reinicia el acumulador de volumen para la sesión.
 *
 * - El botón STOP (pin 13) tiene doble función:
 *     • Pulsación corta (<2 s): detiene la bomba activa.
 *     • Pulsación larga (≥2 s): detiene la bomba (si está en marcha)
 *       y cambia de modo (NORMAL <-> RETORNO).
 *
 * - Se mide el caudal con la fórmula:
 *       Litros/seg = (pulsos en 1 s) / 450
 *   y se acumula el volumen. Cuando alcanza 0.3 L, se apaga la bomba.
 *
 * - La pantalla SSD1306 muestra:
 *     • El modo actual.
 *     • El estado de bombeo (SI/NO).
 *     • El volumen acumulado y los pulsos (según el sensor activo).
 *     • La temperatura leída del DHT22.
 *
 * - El semáforo de LEDs indica:
 *     • LED ROJO (pin 14): sistema PARADO (sin bombeo).
 *     • LED VERDE (pin 16): bombeo en modo NORMAL.
 *     • LED AMARILLO (pin 15): bombeo en modo RETORNO.
 *
 * Conexiones (ESP32 ejemplo):
 *   - Botón START: pin 12 (con resistencia pull-down externa).
 *   - Botón STOP:  pin 13 (con resistencia pull-down externa).
 *   - Bomba NORMAL (SSR1): pin 26.
 *   - Bomba RETORNO (SSR2): pin 27.
 *   - Caudalímetro 1: pin 18.
 *   - Caudalímetro 2: pin 19.
 *   - Pantalla SSD1306: I2C (SDA=21, SCL=22).
 *   - Sensor DHT22: pin 4.
 *   - LED rojo: pin 14, LED amarillo: pin 15, LED verde: pin 16.
 *************************************************************/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ======================
// CONFIGURACIÓN DE LA PANTALLA SSD1306
// ======================
#define SCREEN_WIDTH 128   // Ancho de la pantalla en píxeles
#define SCREEN_HEIGHT 64   // Alto de la pantalla en píxeles
#define OLED_RESET    -1   // Pin de reset (no usado en este ejemplo)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ======================
// CONFIGURACIÓN DEL SENSOR DHT22
// ======================
#define DHTPIN 4           // Pin digital conectado al DHT22
#define DHTTYPE DHT22      // Definición del tipo de sensor
DHT dht(DHTPIN, DHTTYPE);   // Objeto DHT

// ======================
// PINS DE BOTONES (con resistencias pull-down externas)
// ======================
const int pinStart = 12;   // Botón START (Marcha)
const int pinStop  = 13;   // Botón STOP (Paro) con función de pulsación larga

// ======================
// PINS DE SSR/BOMBAS
// ======================
const int pinSSR1 = 26;    // SSR que controla la Bomba #1 (modo NORMAL)
const int pinSSR2 = 27;    // SSR que controla la Bomba #2 (modo RETORNO)

// ======================
// PINS DE CAUDALÍMETROS
// ======================
const int pinFlowSensor1 = 18;  // Caudalímetro para Bomba #1 (modo NORMAL)
const int pinFlowSensor2 = 19;  // Caudalímetro para Bomba #2 (modo RETORNO)

// ======================
// VARIABLES DE CAUDALÍMETROS
// ======================
volatile int flow_frequency_1 = 0;  // Contador de pulsos (1 s) para caudalímetro 1
volatile int flow_frequency_2 = 0;  // Contador de pulsos (1 s) para caudalímetro 2

// ======================
// ACUMULADORES DE VOLUMEN (en litros)
// Se reinician al iniciar cada sesión de bombeo
// ======================
float volumenNormal  = 0.0;   // Volumen acumulado para el modo NORMAL
float volumenRetorno = 0.0;   // Volumen acumulado para el modo RETORNO

// ======================
// CONTROL DE TIEMPO (para mediciones cada 1 s)
// ======================
unsigned long prevTime = 0;   // Guarda el tiempo de la última medición

// ======================
// MÁQUINA DE ESTADOS DE BOMBEO
// ======================
enum ModoBombeo {
  BOMBEO_NORMAL,   // Modo de bombeo normal (Bomba #1, caudalímetro 1)
  BOMBEO_RETORNO   // Modo de bombeo retorno (Bomba #2, caudalímetro 2)
};
ModoBombeo modoActual = BOMBEO_NORMAL;  // Empieza en modo NORMAL
bool enBombeo         = false;           // Indica si la bomba está encendida

// ======================
// VOLUMEN OBJETIVO (en litros)
// Cuando se acumulan 0.3 L, se detiene la bomba
// ======================
const float VOLUMEN_OBJETIVO = 0.3;

// ======================
// CONFIGURACIÓN DEL BOTÓN STOP CON PULSACIÓN LARGA
// ======================
bool stopWasPressed = false;           // Bandera para detectar pulsación en STOP
bool longPressAction = false;          // Para evitar ejecutar la acción varias veces
unsigned long stopPressStartTime = 0;  // Momento en que se pulsó STOP
const unsigned long LONG_PRESS_TIME = 2000; // Tiempo requerido para pulsación larga (2 s)

// ======================
// PINS PARA EL SEMÁFORO (LEDs)
// ======================
const int ledRed = 14;     // LED ROJO: indica PARO
const int ledYellow = 15;  // LED AMARILLO: modo RETORNO (bombeo)
const int ledGreen = 16;   // LED VERDE: modo NORMAL (bombeo)

// --------------------------------------------------------------------------
// INTERRUPCIONES PARA CONTAR LOS PULSOS DE LOS CAUDALÍMETROS
// --------------------------------------------------------------------------
void IRAM_ATTR conteoFlujo1() {
  flow_frequency_1++;   // Cada pulso en el sensor 1 se cuenta
}

void IRAM_ATTR conteoFlujo2() {
  flow_frequency_2++;   // Cada pulso en el sensor 2 se cuenta
}

// --------------------------------------------------------------------------
// FUNCIÓN updateDisplay()
// Se encarga de actualizar la pantalla SSD1306 con la información actual.
// Muestra el modo, el estado (bombeo o no), el volumen acumulado, los pulsos
// del sensor activo y la temperatura medida por el DHT22.
// Se usa un tamaño uniforme (setTextSize(1)) para que la información quepa.
// --------------------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();         // Borra la pantalla
  display.setTextSize(1);           // Tamaño de texto 1 (pequeño)
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  // Mostrar el modo actual
  display.print("Modo: ");
  if (modoActual == BOMBEO_NORMAL)
    display.println("NORMAL");
  else
    display.println("RETORNO");
  
  // Mostrar si se está bombeando
  display.print("Bombeo: ");
  display.println(enBombeo ? "SI" : "NO");
  
  // Mostrar datos del caudal y volumen (según modo activo)
  display.setCursor(0, 16);
  if (enBombeo) {
    if (modoActual == BOMBEO_NORMAL) {
      display.print("Vol (N): ");
      display.print(volumenNormal, 3);
      display.println(" L");
      display.print("Pulsos: ");
      display.println(flow_frequency_1);
    } else {
      display.print("Vol (R): ");
      display.print(volumenRetorno, 3);
      display.println(" L");
      display.print("Pulsos: ");
      display.println(flow_frequency_2);
    }
  } else {
    display.println("Sin bombeo");
  }
  
  // Mostrar la temperatura (DHT22)
  float t = dht.readTemperature();
  display.setCursor(0, 40);
  display.print("Temp: ");
  if (isnan(t)) {
    display.println("Err");
  } else {
    display.print(t, 1);
    display.print((char)247); // Símbolo de grado
    display.println("C");
  }
  
  display.display();  // Actualiza la pantalla
}

// --------------------------------------------------------------------------
// FUNCIÓN updateLEDs()
// Actualiza el estado del semáforo de LEDs según el estado del sistema.
// - Si no se está bombeando, se enciende el LED ROJO.
// - Si se está bombeando en modo NORMAL, se enciende el LED VERDE.
// - Si se está bombeando en modo RETORNO, se enciende el LED AMARILLO.
// --------------------------------------------------------------------------
void updateLEDs() {
  if (!enBombeo) {
    // Sistema parado: encender LED rojo, apagar los demás
    digitalWrite(ledRed, HIGH);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledGreen, LOW);
  } else {
    // Durante bombeo, según el modo activo:
    if (modoActual == BOMBEO_NORMAL) {
      digitalWrite(ledRed, LOW);
      digitalWrite(ledYellow, LOW);
      digitalWrite(ledGreen, HIGH);
    } else {  // Modo RETORNO
      digitalWrite(ledRed, LOW);
      digitalWrite(ledYellow, HIGH);
      digitalWrite(ledGreen, LOW);
    }
  }
}

// --------------------------------------------------------------------------
// FUNCIÓN cambiarModo()
// Esta función se invoca cuando se realiza una pulsación larga en el botón STOP.
// Si hay una bomba en marcha, se detiene inmediatamente (para evitar que quede encendida).
// Luego se cambia el modo (NORMAL <-> RETORNO) y se actualiza el estado.
// --------------------------------------------------------------------------
void cambiarModo() {
  // Si se está bombeando, detener la bomba activa
  if (enBombeo) {
    if (modoActual == BOMBEO_NORMAL) {
      digitalWrite(pinSSR1, LOW);
      Serial.println("[LONG PRESS] Se detuvo Bomba #1 (NORMAL) para cambio de modo.");
    } else {
      digitalWrite(pinSSR2, LOW);
      Serial.println("[LONG PRESS] Se detuvo Bomba #2 (RETORNO) para cambio de modo.");
    }
    enBombeo = false;
    updateDisplay();
    updateLEDs();
  }
  
  // Cambiar el modo de bombeo
  if (modoActual == BOMBEO_NORMAL) {
    modoActual = BOMBEO_RETORNO;
    Serial.println("[LONG PRESS] Modo cambiado a BOMBEO_RETORNO");
  } else {
    modoActual = BOMBEO_NORMAL;
    Serial.println("[LONG PRESS] Modo cambiado a BOMBEO_NORMAL");
  }
}

// --------------------------------------------------------------------------
// setup()
// Se configuran todas las conexiones, se inician los dispositivos y se
// muestran los mensajes iniciales en la consola y en la pantalla.
// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SISTEMA DOS MODO DE BOMBEO + STOP LARGO + DHT22 + SEMAFORO ===");
  
  // Inicializar la pantalla SSD1306 (I2C, pines SDA=21 y SCL=22 por defecto)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error al iniciar SSD1306");
    for(;;);  // Detener si falla la pantalla
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Iniciando...");
  display.display();
  
  // Inicializar el sensor DHT22
  dht.begin();
  
  // Configurar los botones (se asume que tienen resistencias pull-down externas)
  pinMode(pinStart, INPUT);
  pinMode(pinStop, INPUT);
  
  // Configurar los SSR (bombas) y apagarlos al inicio
  pinMode(pinSSR1, OUTPUT);
  pinMode(pinSSR2, OUTPUT);
  digitalWrite(pinSSR1, LOW);
  digitalWrite(pinSSR2, LOW);
  
  // Configurar los caudalímetros y asignar interrupciones
  pinMode(pinFlowSensor1, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinFlowSensor1), conteoFlujo1, RISING);
  pinMode(pinFlowSensor2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinFlowSensor2), conteoFlujo2, RISING);
  
  // Configurar el semáforo (LEDs)
  pinMode(ledRed, OUTPUT);
  pinMode(ledYellow, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  // Estado inicial: sistema parado, encender LED rojo
  digitalWrite(ledRed, HIGH);
  digitalWrite(ledYellow, LOW);
  digitalWrite(ledGreen, LOW);
  
  // Inicializar las variables de tiempo y estado
  modoActual = BOMBEO_NORMAL;
  enBombeo   = false;
  prevTime   = millis();
  
  Serial.println("Modo inicial: BOMBEO_NORMAL. Esperando interacciones...");
  updateDisplay();
  updateLEDs();
}

// --------------------------------------------------------------------------
// loop()
// Se ejecuta continuamente y se encarga de:
// - Leer los botones START y STOP.
// - Controlar la pulsación corta y larga del botón STOP.
// - Realizar la medición de caudal cada 1 s y actualizar el volumen.
// - Actualizar la pantalla y los LEDs con la información actual.
// --------------------------------------------------------------------------
void loop() {
  // -------------------------------
  // LÓGICA BOTÓN START (MARCHA)
  // -------------------------------
  if (digitalRead(pinStart) == HIGH) {
    // Solo inicia el bombeo si no hay una bomba encendida
    if (!enBombeo) {
      if (modoActual == BOMBEO_NORMAL) {
        // Reiniciar el acumulador para el modo NORMAL y encender Bomba #1
        volumenNormal = 0.0;
        digitalWrite(pinSSR1, HIGH);
        Serial.println("[START] BOMBEO_NORMAL: Bomba #1 encendida. Volumen reiniciado a 0 L.");
      } else {
        // Reiniciar el acumulador para el modo RETORNO y encender Bomba #2
        volumenRetorno = 0.0;
        digitalWrite(pinSSR2, HIGH);
        Serial.println("[START] BOMBEO_RETORNO: Bomba #2 encendida. Volumen reiniciado a 0 L.");
      }
      enBombeo = true;
      updateDisplay();
      updateLEDs();
    }
    delay(200);  // Anti-rebote
  }
  
  // -------------------------------
  // LÓGICA BOTÓN STOP (PARO) – con pulsación larga
  // -------------------------------
  bool stopButtonState = (digitalRead(pinStop) == HIGH);
  if (stopButtonState && !stopWasPressed) {
    // Se detecta el flanco ascendente del botón STOP
    stopWasPressed = true;
    longPressAction = false;
    stopPressStartTime = millis();
  }
  else if (!stopButtonState && stopWasPressed) {
    // Al soltar el botón STOP, se calcula el tiempo de pulsación
    unsigned long pressDuration = millis() - stopPressStartTime;
    if (!longPressAction && pressDuration < LONG_PRESS_TIME) {
      // Pulsación corta: detener la bomba activa
      if (enBombeo) {
        if (modoActual == BOMBEO_NORMAL) {
          digitalWrite(pinSSR1, LOW);
          Serial.println("[STOP corto] Bomba #1 (NORMAL) detenida.");
        } else {
          digitalWrite(pinSSR2, LOW);
          Serial.println("[STOP corto] Bomba #2 (RETORNO) detenida.");
        }
        enBombeo = false;
        updateDisplay();
        updateLEDs();
      } else {
        Serial.println("[STOP corto] No hay bomba encendida.");
      }
    }
    stopWasPressed = false;  // Resetea el estado del botón STOP
  }
  // Si se mantiene pulsado STOP, y supera 2 s, se cambia de modo
  if (stopWasPressed && !longPressAction) {
    unsigned long pressDuration = millis() - stopPressStartTime;
    if (pressDuration >= LONG_PRESS_TIME) {
      longPressAction = true;
      cambiarModo();
      updateDisplay();
      updateLEDs();
    }
  }
  
  // -------------------------------
  // MEDICIÓN DE CAUDAL CADA 1 SEGUNDO
  // -------------------------------
  unsigned long currentTime = millis();
  if (currentTime - prevTime >= 1000) {
    prevTime = currentTime;
    // Para el modo NORMAL y si se está bombeando
    if (modoActual == BOMBEO_NORMAL && enBombeo) {
      float litrosSeg = flow_frequency_1 / 450.0;  // Conversión: L/s = pulsos/450
      volumenNormal += litrosSeg;  // Se acumula el volumen
      Serial.println("-----------------------------------");
      Serial.print  ("[NORMAL] Pulsos (1s): ");
      Serial.print  (flow_frequency_1);
      Serial.println(" Hz");
      Serial.print  ("          Litros en este segundo: ");
      Serial.print  (litrosSeg, 3);
      Serial.println(" L");
      Serial.print  ("          Volumen acumulado: ");
      Serial.print  (volumenNormal, 3);
      Serial.println(" L");
      Serial.println("-----------------------------------");
      flow_frequency_1 = 0;  // Reinicia el contador
      // Si se alcanza el volumen objetivo (0.3 L), se detiene la bomba
      if (volumenNormal >= VOLUMEN_OBJETIVO) {
        digitalWrite(pinSSR1, LOW);
        enBombeo = false;
        Serial.println("[INFO] BOMBEO_NORMAL: 0.3 L alcanzados. Bomba #1 detenida.");
        Serial.print("Volumen final = ");
        Serial.print(volumenNormal, 3);
        Serial.println(" L");
        updateDisplay();
        updateLEDs();
      }
    }
    // Para el modo RETORNO y si se está bombeando
    else if (modoActual == BOMBEO_RETORNO && enBombeo) {
      float litrosSeg = flow_frequency_2 / 450.0;
      volumenRetorno += litrosSeg;
      Serial.println("===================================");
      Serial.print  ("[RETORNO] Pulsos (1s): ");
      Serial.print  (flow_frequency_2);
      Serial.println(" Hz");
      Serial.print  ("           Litros en este segundo: ");
      Serial.print  (litrosSeg, 3);
      Serial.println(" L");
      Serial.print  ("           Volumen acumulado: ");
      Serial.print  (volumenRetorno, 3);
      Serial.println(" L");
      Serial.println("===================================");
      flow_frequency_2 = 0;
      if (volumenRetorno >= VOLUMEN_OBJETIVO) {
        digitalWrite(pinSSR2, LOW);
        enBombeo = false;
        Serial.println("[INFO] BOMBEO_RETORNO: 0.3 L alcanzados. Bomba #2 detenida.");
        Serial.print("Volumen final = ");
        Serial.print(volumenRetorno, 3);
        Serial.println(" L");
        updateDisplay();
        updateLEDs();
      }
    }
    else {
      // Si no se está bombeando, se reinician ambos contadores
      flow_frequency_1 = 0;
      flow_frequency_2 = 0;
    }
    updateDisplay();
    updateLEDs();
  }
}
