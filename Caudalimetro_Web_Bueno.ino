#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ======================
// CONFIGURACIÓN DE LA PANTALLA SSD1306
// ======================
#define SCREEN_WIDTH 128   // Ancho en píxeles
#define SCREEN_HEIGHT 64   // Alto en píxeles
#define OLED_RESET    -1   // Pin de reset (no se usa en este ejemplo)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ======================
// CONFIGURACIÓN DEL SENSOR DHT22
// ======================
#define DHTPIN 4           // Pin digital para el DHT22
#define DHTTYPE DHT22      // Tipo de sensor
DHT dht(DHTPIN, DHTTYPE);

// ======================
// PINS DE BOTONES (con resistencias pull-down externas)
// ======================
const int pinStart = 12;   // Botón START (Marcha)
const int pinStop  = 13;   // Botón STOP (Paro) – se usa también para pulsación larga

// ======================
// PINS DE SSR/BOMBAS
// ======================
const int pinSSR1 = 26;    // SSR para Bomba #1 (modo NORMAL)
const int pinSSR2 = 27;    // SSR para Bomba #2 (modo RETORNO)

// ======================
// PINS DE CAUDALÍMETROS
// ======================
const int pinFlowSensor1 = 18;  // Caudalímetro para Bomba #1 (NORMAL)
const int pinFlowSensor2 = 19;  // Caudalímetro para Bomba #2 (RETORNO)
volatile int flow_frequency_1 = 0;  // Pulsos del sensor 1 (1 s)
volatile int flow_frequency_2 = 0;  // Pulsos del sensor 2 (1 s)

// ======================
// ACUMULADORES DE VOLUMEN (en litros)
// ======================
float volumenNormal  = 0.0;   // Volumen acumulado en modo NORMAL
float volumenRetorno = 0.0;   // Volumen acumulado en modo RETORNO

// ======================
// CONTROL DE TIEMPO (para mediciones cada 1 s)
// ======================
unsigned long prevTime = 0;   // Última medición

// ======================
// MÁQUINA DE ESTADOS DE BOMBEO
// ======================
enum ModoBombeo {
  BOMBEO_NORMAL,
  BOMBEO_RETORNO
};
ModoBombeo modoActual = BOMBEO_NORMAL;  // Empieza en modo NORMAL
bool enBombeo = false;                 // Indica si la bomba está encendida

// ======================
// VOLUMEN OBJETIVO (en litros)
// ======================
const float VOLUMEN_OBJETIVO = 0.3;

// ======================
// CONFIGURACIÓN DEL BOTÓN STOP CON PULSACIÓN LARGA
// ======================
bool stopWasPressed = false;           // Bandera para detectar pulsación
bool longPressAction = false;          // Para evitar acciones múltiples
unsigned long stopPressStartTime = 0;  // Tiempo en que se pulsó STOP
const unsigned long LONG_PRESS_TIME = 2000; // 2 segundos para pulsación larga

// ======================
// PINS PARA EL SEMÁFORO (LEDs)
// ======================
const int ledRed = 14;     // LED ROJO: PARO
const int ledYellow = 15;  // LED AMARILLO: RETORNO (bombeo)
const int ledGreen = 16;   // LED VERDE: NORMAL (bombeo)

// ======================
// CONFIGURACIÓN DEL SERVIDOR WEB Y CONEXIÓN WIFI (AP Mode)
// ======================
const char* ssid = "ESP32_control";
const char* password = "12345678";
WebServer server(80);

// --------------------------------------------------------------------------
// INTERRUPCIONES PARA LOS CAUDALÍMETROS
// --------------------------------------------------------------------------
void IRAM_ATTR conteoFlujo1() {
  flow_frequency_1++;
}

void IRAM_ATTR conteoFlujo2() {
  flow_frequency_2++;
}

// --------------------------------------------------------------------------
// FUNCIÓN updateDisplay()
// Actualiza la pantalla OLED con la información actual
// --------------------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  display.print("Modo: ");
  if (modoActual == BOMBEO_NORMAL)
    display.println("NORMAL");
  else
    display.println("RETORNO");

  display.print("Bombeo: ");
  display.println(enBombeo ? "SI" : "NO");
  
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
  
  float t = dht.readTemperature();
  display.setCursor(0, 40);
  display.print("Temp: ");
  if (isnan(t))
    display.println("Err");
  else {
    display.print(t, 1);
    display.print((char)247);
    display.println("C");
  }
  
  display.display();
}

// --------------------------------------------------------------------------
// FUNCIÓN updateLEDs()
// Actualiza los LEDs según el estado del sistema
// --------------------------------------------------------------------------
void updateLEDs() {
  if (!enBombeo) {
    digitalWrite(ledRed, HIGH);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledGreen, LOW);
  } else {
    if (modoActual == BOMBEO_NORMAL) {
      digitalWrite(ledRed, LOW);
      digitalWrite(ledYellow, LOW);
      digitalWrite(ledGreen, HIGH);
    } else {
      digitalWrite(ledRed, LOW);
      digitalWrite(ledYellow, HIGH);
      digitalWrite(ledGreen, LOW);
    }
  }
}

// --------------------------------------------------------------------------
// FUNCIÓN startPump()
// Inicia el bombeo (se usa desde botón físico y endpoint web)
// --------------------------------------------------------------------------
void startPump() {
  if (!enBombeo) {
    if (modoActual == BOMBEO_NORMAL) {
      volumenNormal = 0.0;
      digitalWrite(pinSSR1, HIGH);
      Serial.println("[START] BOMBEO_NORMAL: Bomba #1 encendida. Volumen reiniciado.");
    } else {
      volumenRetorno = 0.0;
      digitalWrite(pinSSR2, HIGH);
      Serial.println("[START] BOMBEO_RETORNO: Bomba #2 encendida. Volumen reiniciado.");
    }
    enBombeo = true;
    updateDisplay();
    updateLEDs();
  }
}

// --------------------------------------------------------------------------
// FUNCIÓN stopPump()
// Detiene el bombeo (se usa desde botón físico y endpoint web)
// --------------------------------------------------------------------------
void stopPump() {
  if (enBombeo) {
    if (modoActual == BOMBEO_NORMAL) {
      digitalWrite(pinSSR1, LOW);
      Serial.println("[STOP] Bomba #1 (NORMAL) detenida.");
    } else {
      digitalWrite(pinSSR2, LOW);
      Serial.println("[STOP] Bomba #2 (RETORNO) detenida.");
    }
    enBombeo = false;
    updateDisplay();
    updateLEDs();
  } else {
    Serial.println("[STOP] No hay bomba encendida.");
  }
}

// --------------------------------------------------------------------------
// FUNCIÓN changePumpMode()
// Cambia el modo de bombeo (deteniendo la bomba si es necesario)
// --------------------------------------------------------------------------
void changePumpMode() {
  if (enBombeo) {
    stopPump();
  }
  if (modoActual == BOMBEO_NORMAL) {
    modoActual = BOMBEO_RETORNO;
    Serial.println("[MODE] Cambiado a RETORNO.");
  } else {
    modoActual = BOMBEO_NORMAL;
    Serial.println("[MODE] Cambiado a NORMAL.");
  }
  updateDisplay();
  updateLEDs();
}

// --------------------------------------------------------------------------
// FUNCIONES DEL SERVIDOR WEB
// --------------------------------------------------------------------------
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Control de Bomba - ESP32</title>
  <style>
    html, body { height: 100%; margin: 0; padding: 0; }
    body { font-family: Arial, sans-serif; color: #333; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    @keyframes gradientAnimation { 0% { background-position: 0% 50%; } 50% { background-position: 100% 50%; } 100% { background-position: 0% 50%; } }
    .bg-stopped { background: linear-gradient(45deg, #ff4d4d, #cc0000); background-size: 400% 400%; animation: gradientAnimation 15s ease infinite; }
    .bg-normal { background: linear-gradient(45deg, #00cc66, #00994d); background-size: 400% 400%; animation: gradientAnimation 15s ease infinite; }
    .bg-retorno { background: linear-gradient(45deg, #ffff66, #ffcc00); background-size: 400% 400%; animation: gradientAnimation 15s ease infinite; }
    .container { max-width: 480px; width: 100%; padding: 10px; background: rgba(255,255,255,0.9); border-radius: 8px; box-sizing: border-box; }
    .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0, 0, 0, 0.7); z-index: 2000; justify-content: center; align-items: center; }
    @keyframes modalEntrance { 0% { opacity: 0; transform: scale(0.8); } 100% { opacity: 1; transform: scale(1); } }
    .modal-content { background: #fff; padding: 20px; border-radius: 8px; text-align: center; max-width: 300px; box-shadow: 2px 2px 10px rgba(0,0,0,0.5); animation: modalEntrance 0.3s ease-out; }
    .modal-content h2 { margin: 0 0 10px; font-size: 1.5em; color: #cc0000; }
    .modal-content p { margin: 0 0 15px; font-size: 1em; color: #333; }
    .modal-content button { padding: 10px 20px; border: none; border-radius: 5px; background-color: orange; color: #fff; font-size: 1em; cursor: pointer; }
    header { display: flex; align-items: center; justify-content: space-between; background-color: #000; color: #fff; padding: 10px; border-radius: 8px; }
    header img { height: 50px; }
    header .info { text-align: right; }
    header .info p { margin: 2px 0; }
    table { width: 100%; border-collapse: collapse; margin: 10px 0; background-color: #fff; border-radius: 8px; overflow: hidden; }
    table th, table td { border: 1px solid #ccc; padding: 8px; text-align: center; }
    .led-container { display: flex; justify-content: space-around; margin: 20px 0; }
    .led { width: 50px; height: 50px; border-radius: 50%; background-color: #555; box-shadow: inset 0 0 10px rgba(0,0,0,0.5); }
    .led.active.red { background-color: red; }
    .led.active.green { background-color: green; }
    .led.active.yellow { background-color: yellow; }
    .switch { display: flex; align-items: center; justify-content: center; margin: 20px 0; }
    .switch input[type="checkbox"] { display: none; }
    .switch-label { position: relative; display: inline-block; width: 100px; height: 50px; background-color: #ccc; border-radius: 25px; cursor: pointer; }
    .switch-label::after { content: ""; position: absolute; top: 5px; left: 5px; width: 40px; height: 40px; background-color: orange; border-radius: 50%; transition: 0.3s; }
    .switch input[type="checkbox"]:checked + .switch-label::after { transform: translateX(50px); }
    .switch-text { margin-left: 15px; font-size: 1.2em; }
    .buttons { display: flex; justify-content: space-around; margin: 20px 0; }
    .buttons button { width: 40%; padding: 15px; font-size: 1.1em; border: none; border-radius: 5px; cursor: pointer; box-shadow: 2px 2px 5px rgba(0,0,0,0.3); }
    .buttons .start { background-color: orange; color: #fff; }
    .buttons .stop { background-color: #555; color: #fff; }
  </style>
</head>
<body class="bg-stopped">
  <div id="modal" class="modal">
    <div class="modal-content">
      <h2>¡Error!</h2>
      <p id="modal-message">Debes parar la bomba antes de cambiar de modo.</p>
      <button id="modal-accept">Aceptar</button>
    </div>
  </div>
  <div class="container">
    <header>
      <img src="logo.png" alt="Logo">
      <div class="info">
        <p id="temp">Temp: 0°C</p>
        <p id="time">00:00:00</p>
      </div>
    </header>
    <table>
      <thead>
        <tr>
          <th>Modo de Bomba</th>
          <th>Caudal Izq</th>
          <th>Caudal Der</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td id="pumpMode">Normal</td>
          <td id="flowLeft">0.0 L/min</td>
          <td id="flowRight">0.0 L/min</td>
        </tr>
      </tbody>
    </table>
    <div class="led-container">
      <div class="led red" id="ledRed"></div>
      <div class="led green" id="ledGreen"></div>
      <div class="led yellow" id="ledYellow"></div>
    </div>
    <div class="switch">
      <input type="checkbox" id="modeSwitch">
      <label for="modeSwitch" class="switch-label"></label>
      <span class="switch-text" id="modeText">Modo Normal</span>
    </div>
    <div class="buttons">
      <button class="start" id="startBtn">MARCHA</button>
      <button class="stop" id="stopBtn">PARO</button>
    </div>
  </div>
  <script>
    let systemRunning = false;
    let lastMode = false;

    function showModal(message) {
      const modal = document.getElementById('modal');
      document.getElementById('modal-message').textContent = message;
      modal.style.display = 'flex';
    }

    function hideModal() {
      document.getElementById('modal').style.display = 'none';
    }
    document.getElementById('modal-accept').addEventListener('click', hideModal);

    function updateTime() {
      const now = new Date();
      document.getElementById('time').textContent = now.toLocaleTimeString();
    }
    setInterval(updateTime, 1000);
    updateTime();

    // Función para actualizar el estado (incluyendo temperatura real) desde el servidor
    function updateStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('temp').textContent = 'Temp: ' + data.temp + '°C';
          document.getElementById('pumpMode').textContent = data.modo;
          // Puedes actualizar otros elementos (como caudales) si agregas esos datos en el endpoint /status
        })
        .catch(error => console.error(error));
    }
    setInterval(updateStatus, 2000);
    updateStatus();

    const ledRed = document.getElementById('ledRed');
    const ledGreen = document.getElementById('ledGreen');
    const ledYellow = document.getElementById('ledYellow');

    function updateLEDs(state) {
      ledRed.classList.remove('active');
      ledGreen.classList.remove('active');
      ledYellow.classList.remove('active');
      document.body.classList.remove("bg-stopped", "bg-normal", "bg-retorno");
      if (state === 'stopped') {
        ledRed.classList.add('active');
        document.body.classList.add("bg-stopped");
      } else if (state === 'normal') {
        ledGreen.classList.add('active');
        document.body.classList.add("bg-normal");
      } else if (state === 'retorno') {
        ledYellow.classList.add('active');
        document.body.classList.add("bg-retorno");
      }
    }
    updateLEDs('stopped');

    const modeSwitch = document.getElementById('modeSwitch');
    const modeText = document.getElementById('modeText');
    const pumpModeDisplay = document.getElementById('pumpMode');
    modeSwitch.addEventListener('change', function() {
      if (systemRunning) {
        showModal("¡Error! Debes parar la bomba antes de cambiar de modo.");
        modeSwitch.checked = lastMode;
        return;
      }
      lastMode = modeSwitch.checked;
      // Enviar petición para cambiar el modo en el servidor
      fetch('/changeMode')
        .then(response => response.text())
        .then(data => {
          console.log(data);
          if (modeSwitch.checked) {
            modeText.textContent = 'Modo Retorno';
            pumpModeDisplay.textContent = 'Retorno';
          } else {
            modeText.textContent = 'Modo Normal';
            pumpModeDisplay.textContent = 'Normal';
          }
          updateLEDs('stopped');
        })
        .catch(error => console.error(error));
    });

    document.getElementById('startBtn').addEventListener('click', function() {
      systemRunning = true;
      fetch('/start')
        .then(response => response.text())
        .then(data => {
          console.log(data);
          if (modeSwitch.checked) {
            updateLEDs('retorno');
          } else {
            updateLEDs('normal');
          }
        });
    });

    document.getElementById('stopBtn').addEventListener('click', function() {
      systemRunning = false;
      fetch('/stop')
        .then(response => response.text())
        .then(data => {
          console.log(data);
          updateLEDs('stopped');
        });
    });
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleStart() {
  startPump();
  server.send(200, "text/plain", "Bomba iniciada");
}

void handleStop() {
  stopPump();
  server.send(200, "text/plain", "Bomba detenida");
}

void handleChangeMode() {
  changePumpMode();
  server.send(200, "text/plain", "Modo cambiado");
}

void handleStatus() {
  float t = dht.readTemperature();
  if (isnan(t)) {
    t = 0;
  }
  String json = "{";
  json += "\"modo\":\"" + String((modoActual == BOMBEO_NORMAL) ? "NORMAL" : "RETORNO") + "\",";
  json += "\"bombeo\":\"" + String(enBombeo ? "SI" : "NO") + "\",";
  json += "\"volumenNormal\":" + String(volumenNormal, 3) + ",";
  json += "\"volumenRetorno\":" + String(volumenRetorno, 3) + ",";
  json += "\"temp\":" + String(t, 1);
  json += "}";
  server.send(200, "application/json", json);
}

// --------------------------------------------------------------------------
// setupHardware()
// Configura hardware, pines, display, sensores y más
// --------------------------------------------------------------------------
void setupHardware() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SISTEMA DOS MODO DE BOMBEO + WEB CONTROL + DHT22 + SEMAFORO ===");
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Error al iniciar SSD1306");
    while(1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Iniciando...");
  display.display();
  
  dht.begin();
  
  pinMode(pinStart, INPUT);
  pinMode(pinStop, INPUT);
  
  pinMode(pinSSR1, OUTPUT);
  pinMode(pinSSR2, OUTPUT);
  digitalWrite(pinSSR1, LOW);
  digitalWrite(pinSSR2, LOW);
  
  pinMode(pinFlowSensor1, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinFlowSensor1), conteoFlujo1, RISING);
  pinMode(pinFlowSensor2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinFlowSensor2), conteoFlujo2, RISING);
  
  pinMode(ledRed, OUTPUT);
  pinMode(ledYellow, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  digitalWrite(ledRed, HIGH);
  digitalWrite(ledYellow, LOW);
  digitalWrite(ledGreen, LOW);
  
  modoActual = BOMBEO_NORMAL;
  enBombeo = false;
  prevTime = millis();
  
  Serial.println("Modo inicial: BOMBEO_NORMAL. Esperando interacciones...");
  updateDisplay();
  updateLEDs();
}

// --------------------------------------------------------------------------
// loopHardware()
// Contiene la lógica de lectura de botones, medición de caudal y actualización de display/LEDs
// --------------------------------------------------------------------------
void loopHardware() {
  // Botón START (Marcha)
  if (digitalRead(pinStart) == HIGH) {
    if (!enBombeo) {
      if (modoActual == BOMBEO_NORMAL) {
        volumenNormal = 0.0;
        digitalWrite(pinSSR1, HIGH);
        Serial.println("[START] BOMBEO_NORMAL: Bomba #1 encendida. Volumen reiniciado.");
      } else {
        volumenRetorno = 0.0;
        digitalWrite(pinSSR2, HIGH);
        Serial.println("[START] BOMBEO_RETORNO: Bomba #2 encendida. Volumen reiniciado.");
      }
      enBombeo = true;
      updateDisplay();
      updateLEDs();
    }
    delay(200);  // Anti-rebote
  }
  
  // Botón STOP (Paro) – pulsación corta y larga
  bool stopButtonState = (digitalRead(pinStop) == HIGH);
  if (stopButtonState && !stopWasPressed) {
    stopWasPressed = true;
    longPressAction = false;
    stopPressStartTime = millis();
  } else if (!stopButtonState && stopWasPressed) {
    unsigned long pressDuration = millis() - stopPressStartTime;
    if (!longPressAction && pressDuration < LONG_PRESS_TIME) {
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
    stopWasPressed = false;
  }
  if (stopWasPressed && !longPressAction) {
    unsigned long pressDuration = millis() - stopPressStartTime;
    if (pressDuration >= LONG_PRESS_TIME) {
      longPressAction = true;
      changePumpMode();
      updateDisplay();
      updateLEDs();
    }
  }
  
  // Medición de caudal cada 1 segundo
  unsigned long currentTime = millis();
  if (currentTime - prevTime >= 1000) {
    prevTime = currentTime;
    if (modoActual == BOMBEO_NORMAL && enBombeo) {
      float litrosSeg = flow_frequency_1 / 450.0;
      volumenNormal += litrosSeg;
      Serial.println("-----------------------------------");
      Serial.print("[NORMAL] Pulsos (1s): ");
      Serial.print(flow_frequency_1);
      Serial.println(" Hz");
      Serial.print("          Litros en este segundo: ");
      Serial.print(litrosSeg, 3);
      Serial.println(" L");
      Serial.print("          Volumen acumulado: ");
      Serial.print(volumenNormal, 3);
      Serial.println(" L");
      Serial.println("-----------------------------------");
      flow_frequency_1 = 0;
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
    } else if (modoActual == BOMBEO_RETORNO && enBombeo) {
      float litrosSeg = flow_frequency_2 / 450.0;
      volumenRetorno += litrosSeg;
      Serial.println("===================================");
      Serial.print("[RETORNO] Pulsos (1s): ");
      Serial.print(flow_frequency_2);
      Serial.println(" Hz");
      Serial.print("           Litros en este segundo: ");
      Serial.print(litrosSeg, 3);
      Serial.println(" L");
      Serial.print("           Volumen acumulado: ");
      Serial.print(volumenRetorno, 3);
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
    } else {
      flow_frequency_1 = 0;
      flow_frequency_2 = 0;
    }
    updateDisplay();
    updateLEDs();
  }
}

void setup() {
  setupHardware();
  
  // Configuración como punto de acceso (AP)
  WiFi.softAP(ssid, password);
  Serial.println("AP iniciado. IP: " + WiFi.softAPIP().toString());
  
  // Rutas del servidor web
  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/changeMode", handleChangeMode);
  server.on("/status", handleStatus);
  
  server.begin();
  Serial.println("Servidor web iniciado");
}

void loop() {
  server.handleClient();
  loopHardware();
}
