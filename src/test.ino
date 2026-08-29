#include <WiFi.h>
#include <WebServer.h>

// ---------------------------------------------------------------- CREDENCIAIS
const char* ssid     = "iPhone de Guilherme";
const char* password = "12345678";

WebServer server(80);

// --------------------------------------------------------------------- PINOS
// Motor A (esquerdo)
const int motor1Pin1 = 27;
const int motor1Pin2 = 26;
const int enable1Pin = 14;

// Motor B (direito)
const int motor2Pin1 = 33;
const int motor2Pin2 = 25;
const int enable2Pin = 32;

// Sensor ultrassônico
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;

// ----------------------------------------------------------------- PARÂMETROS
const int  DIST_SEGURA  = 20;      // cm — abaixo disso o carrinho não anda pra frente
const long TIMEOUT_ECHO = 30000;   // µs — ~5 m de alcance máximo

// PWM
const int freq       = 30000;
const int resolution = 8;
int dutyCycle        = 200;        // velocidade inicial (0–255)

// ----------------------------------------------------------------- ESTADO
enum Direcao { PARADO, FRENTE, TRAS, ESQUERDA, DIREITA };
Direcao direcaoAtual = PARADO;

long distanciaCm = 999;
bool obstaculo   = false;

unsigned long ultimaLeitura = 0;
const unsigned long INTERVALO_LEITURA = 60;  // ms

/* ==========================================================================
   SUB-ROTINAS DE MOVIMENTO (programação modular)
   ========================================================================== */
void frente() {
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, HIGH);
  digitalWrite(motor2Pin1, LOW);
  digitalWrite(motor2Pin2, HIGH);
  direcaoAtual = FRENTE;
}

void tras() {
  digitalWrite(motor1Pin1, HIGH);
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(motor2Pin1, HIGH);
  digitalWrite(motor2Pin2, LOW);
  direcaoAtual = TRAS;
}

void esquerda() {
  digitalWrite(motor1Pin1, LOW);   // motor esquerdo parado
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(motor2Pin1, LOW);   // motor direito para frente
  digitalWrite(motor2Pin2, HIGH);
  direcaoAtual = ESQUERDA;
}

void direita() {
  digitalWrite(motor1Pin1, LOW);   // motor esquerdo para frente
  digitalWrite(motor1Pin2, HIGH);
  digitalWrite(motor2Pin1, LOW);   // motor direito parado
  digitalWrite(motor2Pin2, LOW);
  direcaoAtual = DIREITA;
}

void parar() {
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(motor2Pin1, LOW);
  digitalWrite(motor2Pin2, LOW);
  direcaoAtual = PARADO;
}

/* ==========================================================================
   SENSOR ULTRASSÔNICO
   ========================================================================== */
long lerDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, TIMEOUT_ECHO);
  if (duracao == 0) return 999;          // nada detectado dentro do alcance
  return duracao * 0.0343 / 2;           // µs -> cm
}

/* ==========================================================================
   PÁGINA WEB
   ========================================================================== */
void handleRoot() {
  const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Carrinho-robo ESP32</title>
  <link rel="icon" href="data:,">
  <style>
    body { font-family: Helvetica, Arial, sans-serif; background:#101418; color:#eee;
           text-align:center; margin:0; padding:20px; -webkit-user-select:none; user-select:none; }
    h1 { font-size:22px; font-weight:600; margin-bottom:18px; }
    .btn { background:#2d7ff9; border:none; color:#fff; padding:18px 0; width:110px;
           font-size:20px; margin:4px; border-radius:10px; cursor:pointer; }
    .btn:active { background:#1a5cc4; }
    .stop { background:#c0392b; }
    .painel { display:inline-block; }
    .dist { font-size:15px; margin-top:22px; }
    .valor { font-size:34px; font-weight:700; display:block; margin-top:4px; }
    .alerta { color:#ff5f52; font-weight:700; height:20px; margin-top:6px; }
    input[type=range] { width:260px; margin-top:14px; }
  </style>
</head>
<body>
  <h1>Carrinho-robo &mdash; Controle WiFi</h1>

  <div class="painel">
    <div><button class="btn" onclick="cmd('/frente')">FRENTE</button></div>
    <div>
      <button class="btn" onclick="cmd('/esquerda')">ESQ</button>
      <button class="btn stop" onclick="cmd('/parar')">PARAR</button>
      <button class="btn" onclick="cmd('/direita')">DIR</button>
    </div>
    <div><button class="btn" onclick="cmd('/tras')">TRAS</button></div>
  </div>

  <div class="dist">
    Distancia do obstaculo
    <span class="valor"><span id="dist">--</span> cm</span>
  </div>
  <div class="alerta" id="alerta"></div>

  <div>
    Velocidade: <span id="vel">78</span>%
    <br><input type="range" min="0" max="100" step="1" value="78"
               oninput="velocidade(this.value)">
  </div>

<script>
  function cmd(rota) { fetch(rota); }

  function velocidade(v) {
    document.getElementById('vel').innerHTML = v;
    fetch('/velocidade?valor=' + v);
  }

  setInterval(function() {
    fetch('/status').then(r => r.json()).then(d => {
      document.getElementById('dist').innerHTML = d.distancia;
      document.getElementById('alerta').innerHTML =
        d.obstaculo ? 'OBSTACULO! Frente bloqueada' : '';
    });
  }, 300);
</script>
</body>
</html>)rawliteral";

  server.send(200, "text/html", html);
}

/* ==========================================================================
   ROTAS DE COMANDO
   ========================================================================== */
void handleFrente() {
  if (obstaculo) {              // trava de segurança
    parar();
    server.send(200, "text/plain", "BLOQUEADO");
    return;
  }
  Serial.println("Frente");
  frente();
  server.send(200, "text/plain", "OK");
}

void handleTras() {
  Serial.println("Tras");
  tras();
  server.send(200, "text/plain", "OK");
}

void handleEsquerda() {
  Serial.println("Esquerda");
  esquerda();
  server.send(200, "text/plain", "OK");
}

void handleDireita() {
  Serial.println("Direita");
  direita();
  server.send(200, "text/plain", "OK");
}

void handleParar() {
  Serial.println("Parar");
  parar();
  server.send(200, "text/plain", "OK");
}

void handleVelocidade() {
  if (server.hasArg("valor")) {
    int valor = server.arg("valor").toInt();

    if (valor == 0) {
      dutyCycle = 0;
      parar();
    } else {
      // abaixo de ~150 os motores DC amarelos não vencem o atrito
      dutyCycle = map(valor, 1, 100, 150, 255);
    }

    ledcWrite(enable1Pin, dutyCycle);
    ledcWrite(enable2Pin, dutyCycle);
    Serial.println("Velocidade: " + String(valor) + "% (duty " + String(dutyCycle) + ")");
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"distancia\":" + String(distanciaCm) +
                ",\"obstaculo\":" + String(obstaculo ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

/* ==========================================================================
   SETUP
   ========================================================================== */
void setup() {
  Serial.begin(115200);

  // Pinos dos motores
  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  pinMode(motor2Pin1, OUTPUT);
  pinMode(motor2Pin2, OUTPUT);

  // Pinos do sensor
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // PWM nos pinos de enable
  ledcAttach(enable1Pin, freq, resolution);
  ledcAttach(enable2Pin, freq, resolution);
  ledcWrite(enable1Pin, dutyCycle);
  ledcWrite(enable2Pin, dutyCycle);

  parar();

  // Conexão WiFi
  Serial.print("Conectando em ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi conectado.");
  Serial.print("Abra no navegador: http://");
  Serial.println(WiFi.localIP());

  // Rotas
  server.on("/",           handleRoot);
  server.on("/frente",     handleFrente);
  server.on("/tras",       handleTras);
  server.on("/esquerda",   handleEsquerda);
  server.on("/direita",    handleDireita);
  server.on("/parar",      handleParar);
  server.on("/velocidade", handleVelocidade);
  server.on("/status",     handleStatus);

  server.begin();
  Serial.println("Servidor iniciado.");
}

/* ==========================================================================
   LOOP
   ========================================================================== */
void loop() {
  server.handleClient();

  // Leitura periódica do sensor (não trava o servidor)
  if (millis() - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = millis();
    distanciaCm = lerDistancia();

    if (distanciaCm < DIST_SEGURA) {
      obstaculo = true;
      if (direcaoAtual == FRENTE) {   // só bloqueia o avanço
        parar();
        Serial.println("Obstaculo a " + String(distanciaCm) + " cm — parando!");
      }
    } else {
      obstaculo = false;
    }
  }
}

/* ==========================================================================
   NOTA — ESP32 Arduino Core 2.x
   Se der erro de compilação em ledcAttach, troque no setup() por:

     ledcSetup(0, freq, resolution);
     ledcSetup(1, freq, resolution);
     ledcAttachPin(enable1Pin, 0);
     ledcAttachPin(enable2Pin, 1);
     ledcWrite(0, dutyCycle);
     ledcWrite(1, dutyCycle);

   e em handleVelocidade() use ledcWrite(0, dutyCycle) / ledcWrite(1, dutyCycle).
   ========================================================================== */
