#include <DabbleESP32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Brunin-project-1_inferencing.h>   // [CP2] biblioteca gerada pelo Edge Impulse
 
#define IN1 13
#define IN2 4
#define IN3 33
#define IN4 32
#define ENA 19
#define ENB 18
#define BUZZER 5
 
#define TRIG_FRONT 25
#define ECHO_FRONT 26
 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
 
const int PWM_FREQ = 20000;
const int PWM_RES  = 8;
const int SPEED    = 200;
const float DIST_LIMIT_CM = 15.0;
const float DIST_PERIGO_CM = 5.0;          // quase batendo -> cara de susto
const unsigned long TEMPO_SONO_MS = 8000;  // fica parado sem comando por isso -> dorme
const unsigned long MONITOR_INTERVALO_MS = 250;  // periodo do monitor de proximidade
 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
 
enum Face {
  FACE_IDLE, FACE_FORWARD, FACE_BACKWARD, FACE_LEFT, FACE_RIGHT,
  FACE_HORN, FACE_MUSIC, FACE_DANCE, FACE_SCARED, FACE_SLEEPY
};
Face currentFace = FACE_IDLE;
Face lastDrawnFace = FACE_MUSIC;
 
bool autopilotAtivo = false;
bool lastTriangleState = false;
unsigned long ultimaAcaoMs = 0;
 
// ---- estado para logs (evita flood no serial) ----
const char* ultimoMotor = "";
unsigned long ultimoMonitorMs = 0;
bool lastSquare = false, lastCircle = false, lastCross = false, lastStart = false;
 
// ================= [CP2] TinyML =================
// Novo modo ativado/desativado pelo botao SELECT do Dabble.
// Janela de 20 amostras a 10 Hz (2 s) -> mesma janela usada no treino (Raw data, 2000 ms).
const unsigned long TINYML_AMOSTRA_MS   = 100;   // 10 Hz (igual ao dataset)
const unsigned long TINYML_INFERENCIA_MS = 500;  // passo da janela (igual ao treino)
const float TINYML_CONF_MIN = 0.60;              // abaixo disso mantem a acao anterior
const bool  TINYML_FREIO_EMERGENCIA = true;      // true: parar se dist <= DIST_PERIGO_CM mesmo se o modelo errar
const int   SPEED_APROXIMANDO = SPEED / 2;       // velocidade reduzida quando classe = aproximando
 
bool tinyMlAtivo = false;
bool lastSelectState = false;
float janelaTiny[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];   // 20 valores, mais antigo -> mais novo
size_t nJanelaTiny = 0;
unsigned long tinyUltAmostraMs = 0, tinyUltInferenciaMs = 0, tinyBipeFimMs = 0;
int tinyClasseAtual = -1;   // -1 = ainda sem classificacao; 0 aproximando, 1 livre, 2 obstaculo
 
// ================= Logs =================
 
void logTs(const char* tag) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] [");
  Serial.print(tag);
  Serial.print("] ");
}
 
void logMsg(const char* tag, const char* msg) {
  logTs(tag);
  Serial.println(msg);
}
 
// Loga so quando a acao do motor muda (os motores sao chamados a cada loop)
void logMotor(const char* acao) {
  if (strcmp(acao, ultimoMotor) != 0) {
    logMsg("MOTOR", acao);
    ultimoMotor = acao;
  }
}
 
const char* classificarDistancia(float d) {
  if (d < 0) return "SEM ECO";
  if (d <= DIST_PERIGO_CM) return "PERIGO";
  if (d <= DIST_LIMIT_CM) return "ATENCAO";
  return "LIVRE";
}
 
void imprimirDistancia(const char* rotulo, float d) {
  Serial.print(rotulo);
  Serial.print("=");
  if (d < 0) Serial.print("--");
  else { Serial.print(d, 1); Serial.print("cm"); }
  Serial.print(" (");
  Serial.print(classificarDistancia(d));
  Serial.print(")");
}
 
// Monitor de proximidade: imprime frente/tras com limite de taxa
void imprimirProximidade(const char* modo, float frente, float tras, bool temTras) {
  logTs("PROX");
  Serial.print(modo);
  Serial.print(" | ");
  imprimirDistancia("frente", frente);
  if (temTras) {
    Serial.print(" | ");
    imprimirDistancia("tras", tras);
  }
  Serial.println();
}
 
void monitorProximidadeManual() {
  if (millis() - ultimoMonitorMs < MONITOR_INTERVALO_MS) return;
  ultimoMonitorMs = millis();
  float f = readDistanceCm(TRIG_FRONT, ECHO_FRONT);
  imprimirProximidade("manual", f, -1, false);
}
 
const char* nomeFace(Face f) {
  switch (f) {
    case FACE_IDLE:     return "IDLE (feliz)";
    case FACE_FORWARD:  return "FORWARD (feliz animado)";
    case FACE_BACKWARD: return "BACKWARD (surpreso)";
    case FACE_LEFT:     return "LEFT";
    case FACE_RIGHT:    return "RIGHT";
    case FACE_HORN:     return "HORN (gritando)";
    case FACE_MUSIC:    return "MUSIC (cantando)";
    case FACE_DANCE:    return "DANCE";
    case FACE_SCARED:   return "SCARED";
    case FACE_SLEEPY:   return "SLEEPY";
  }
  return "?";
}
 
// ================= Setup / Loop =================
 
void setup() {
  Serial.begin(115200);
  delay(200);
  logMsg("BOOT", "Iniciando carrinho 4WD");
 
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(BUZZER, OUTPUT);
 
  pinMode(TRIG_FRONT, OUTPUT);
  pinMode(ECHO_FRONT, INPUT);
  logMsg("BOOT", "Pinos configurados (motores, buzzer, sensor frontal)");
 
  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);
  logMsg("BOOT", "PWM configurado (ENA/ENB)");
 
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    logMsg("ERRO", "Falha ao encontrar o display OLED");
  } else {
    logMsg("BOOT", "Display OLED OK");
  }
 
  Dabble.begin("Carrinho Bluetooth Grupo Pedrao");
  logMsg("BT", "Dabble iniciado: 'My Bluetooth Car' (aguardando conexao)");
  stopMotors();
  ultimaAcaoMs = millis();
  logMsg("BOOT", "Setup concluido");
}
 
void loop() {
  Dabble.processInput();
 
  bool triangle = GamePad.isTrianglePressed();
  if (triangle && !lastTriangleState) {
    autopilotAtivo = !autopilotAtivo;
    if (!autopilotAtivo) stopMotors();
    logMsg("BOTAO", "TRIANGULO pressionado");
    logMsg("MODO", autopilotAtivo ? "Piloto automatico: ON" : "Piloto automatico: OFF");
  }
  lastTriangleState = triangle;
 
  // [CP2] SELECT liga/desliga o modo TinyML
  bool select = GamePad.isSelectPressed();
  if (select && !lastSelectState) {
    tinyMlAtivo = !tinyMlAtivo;
    logMsg("BOTAO", "SELECT pressionado");
    logMsg("MODO", tinyMlAtivo ? "TinyML: ON" : "TinyML: OFF");
    if (tinyMlAtivo) { autopilotAtivo = false; iniciarTinyML(); }
    else             { autopilotAtivo = false; stopMotors(); currentFace = FACE_IDLE; noTone(BUZZER); }
  }
  lastSelectState = select;
 
  if (tinyMlAtivo) {                       // [CP2] novo ramo
    rodarModoTinyML();
  } else if (autopilotAtivo) {
    rodarPilotoAutomatico();
  } else {
    rodarModoManual();
    monitorProximidadeManual();
  }
 
  // ---- Botões extras (têm prioridade visual sobre movimento) ----
  bool square = GamePad.isSquarePressed();
  bool circle = GamePad.isCirclePressed();
  bool cross  = GamePad.isCrossPressed();
  bool start  = GamePad.isStartPressed();
 
  if (square && !lastSquare) logMsg("BOTAO", "QUADRADO pressionado -> buzina");
  if (!square && lastSquare) logMsg("BOTAO", "QUADRADO solto -> buzina off");
  if (circle && !lastCircle) logMsg("BOTAO", "BOLA pressionado -> melodia");
  if (cross && !lastCross)   logMsg("BOTAO", "X pressionado -> danca");
  if (start && !lastStart)   logMsg("BOTAO", "START pressionado -> sirene");
  lastSquare = square; lastCircle = circle; lastCross = cross; lastStart = start;
 
  if (square) {
    tone(BUZZER, 1000);
    currentFace = FACE_HORN;
    ultimaAcaoMs = millis();
  } else if (circle) {
    tocarMelodia();
    currentFace = FACE_MUSIC;
    ultimaAcaoMs = millis();
  } else if (cross) {
    dancar();
    currentFace = FACE_DANCE;
    ultimaAcaoMs = millis();
  } else if (start) {
    tocarSirene();
    currentFace = FACE_SCARED;
    ultimaAcaoMs = millis();
  } else if (!tinyMlAtivo) {               // [CP2] no modo TinyML o buzzer e controlado por rodarModoTinyML()
    noTone(BUZZER);
  }
 
  // ---- Modo soninho: sem nenhum comando por um tempo ----
  if (currentFace == FACE_IDLE && (millis() - ultimaAcaoMs > TEMPO_SONO_MS)) {
    currentFace = FACE_SLEEPY;
    logMsg("ESTADO", "Sem comandos ha tempo demais -> modo soninho");
  }
 
  if (currentFace != lastDrawnFace) {
    desenharFace(currentFace);
    lastDrawnFace = currentFace;
  }
}
 
// ================= Modo manual =================
 
void rodarModoManual() {
  bool up     = GamePad.isUpPressed();
  bool down   = GamePad.isDownPressed();
  bool left   = GamePad.isLeftPressed();
  bool right  = GamePad.isRightPressed();
 
  if (up)        { moveForward();  currentFace = FACE_FORWARD;  ultimaAcaoMs = millis(); }
  else if (down) { moveBackward(); currentFace = FACE_BACKWARD; ultimaAcaoMs = millis(); }
  else if (left) { turnLeft();     currentFace = FACE_LEFT;     ultimaAcaoMs = millis(); }
  else if (right){ turnRight();    currentFace = FACE_RIGHT;    ultimaAcaoMs = millis(); }
  else           { stopMotors();   currentFace = FACE_IDLE; }
}
 
// ================= Piloto automático =================
 
void rodarPilotoAutomatico() {
  float distFrente = readDistanceCm(TRIG_FRONT, ECHO_FRONT);
 
  // Monitor de proximidade (limitado em taxa)
  if (millis() - ultimoMonitorMs >= MONITOR_INTERVALO_MS) {
    ultimoMonitorMs = millis();
    imprimirProximidade("auto", distFrente, -1, false);
  }
 
  if (distFrente < 0 || distFrente > DIST_LIMIT_CM) {
    moveForward();
    currentFace = FACE_FORWARD;
    ultimaAcaoMs = millis();
    return;
  }
 
  logMsg("AUTO", "Obstaculo a frente detectado");
  imprimirProximidade("auto/obstaculo", distFrente, -1, false);
 
  // Muito perto - susto de verdade, não só desvio calmo
  if (distFrente <= DIST_PERIGO_CM) {
    logMsg("AUTO", "PERIGO: parando e tocando sirene");
    stopMotors();
    currentFace = FACE_SCARED;
    tocarSirene();
  } else {
    logMsg("AUTO", "Parando para desviar");
    stopMotors();
    currentFace = FACE_BACKWARD;
  }
  ultimaAcaoMs = millis();
  delay(200);
 
  logMsg("AUTO", "Girando a direita para desviar");
  turnRight();
  currentFace = FACE_RIGHT;
  delay(400);
 
  stopMotors();
  logMsg("AUTO", "Manobra de desvio concluida");
}
 
float readDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
 
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return -1;
 
  return duration * 0.0343 / 2.0;
}
 
// ================= [CP2] TinyML: leitura filtrada + inferencia + acao =================
 
// Mesmo pre-processamento descrito no LEIAME_dataset.txt, para a inferencia ver dados iguais aos do treino:
//  sem eco ou >= 200 cm -> 200; leitura < 2 cm -> repete a ultima valida; mediana das 3 ultimas.
float lerDistanciaTratada() {
  static float ult = 200.0, h[3] = {200.0, 200.0, 200.0};
  float d = readDistanceCm(TRIG_FRONT, ECHO_FRONT);
  if (d < 0 || d >= 200.0) d = 200.0;
  else if (d < 2.0) d = ult;
  ult = d;
  h[0] = h[1]; h[1] = h[2]; h[2] = d;
  return max(min(h[0], h[1]), min(max(h[0], h[1]), h[2]));
}
 
void iniciarTinyML() {
  nJanelaTiny = 0;
  tinyClasseAtual = -1;
  tinyUltAmostraMs = millis();
  tinyUltInferenciaMs = millis();
  stopMotors();                      // fica parado ate a primeira janela completa (2 s)
  currentFace = FACE_IDLE;
  ultimaAcaoMs = millis();
  logMsg("TINYML", "Aguardando janela de 20 amostras (2 s)...");
}
 
// Roda a inferencia sobre a janela atual. Retorna o indice da classe vencedora (ou -1 em erro).
int classificarJanelaTiny(float* conf) {
  signal_t sig;
  if (numpy::signal_from_buffer(janelaTiny, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &sig) != 0) return -1;
  ei_impulse_result_t r = {0};
  if (run_classifier(&sig, &r, false) != EI_IMPULSE_OK) return -1;
  int best = 0;
  for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (r.classification[i].value > r.classification[best].value) best = i;
  }
  *conf = r.classification[best].value;
  logTs("TINYML");
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.print(r.classification[i].label);
    Serial.print("=");
    Serial.print(r.classification[i].value, 2);
    Serial.print(i < EI_CLASSIFIER_LABEL_COUNT - 1 ? " | " : "");
  }
  Serial.println();
  return best;
}
 
// Acao no carrinho a partir da classe prevista pelo modelo
void aplicarAcaoTiny(const char* label) {
  if (strcmp(label, "livre") == 0) {
    moveForward();
    currentFace = FACE_FORWARD;
  } else if (strcmp(label, "aproximando") == 0) {
    moveForward();
    setSpeed(SPEED_APROXIMANDO);       // reduz a velocidade
    currentFace = FACE_BACKWARD;       // cara de surpreso
    tone(BUZZER, 800, 80);             // bipe curto (nao bloqueante)
    tinyBipeFimMs = millis() + 100;
  } else if (strcmp(label, "obstaculo") == 0) {
    stopMotors();
    currentFace = FACE_SCARED;
    tone(BUZZER, 1500, 150);
    tinyBipeFimMs = millis() + 200;
  }
  ultimaAcaoMs = millis();
}
 
void rodarModoTinyML() {
  unsigned long agora = millis();
 
  // desliga o bipe quando o tempo acaba (o noTone do loop nao roda neste modo)
  if (tinyBipeFimMs && agora >= tinyBipeFimMs) { noTone(BUZZER); tinyBipeFimMs = 0; }
 
  // 1) amostragem a 10 Hz com janela deslizante
  if (agora - tinyUltAmostraMs >= TINYML_AMOSTRA_MS) {
    // se o loop ficou preso (ex.: danca, sirene, melodia), a janela perdeu continuidade -> descarta
    if (agora - tinyUltAmostraMs > 4 * TINYML_AMOSTRA_MS) {
      nJanelaTiny = 0;
      logMsg("TINYML", "Janela descartada (loop bloqueado), reenchendo...");
    }
    tinyUltAmostraMs = agora;
    float d = lerDistanciaTratada();
    memmove(janelaTiny, janelaTiny + 1, sizeof(float) * (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 1));
    janelaTiny[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 1] = d;
    if (nJanelaTiny < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) nJanelaTiny++;
 
    // freio de emergencia (opcional): independe do modelo
    if (TINYML_FREIO_EMERGENCIA && d <= DIST_PERIGO_CM) {
      stopMotors();
      currentFace = FACE_SCARED;
      ultimaAcaoMs = agora;
    }
  }
 
  // 2) inferencia a cada 500 ms, com janela completa
  if (nJanelaTiny == EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE && agora - tinyUltInferenciaMs >= TINYML_INFERENCIA_MS) {
    tinyUltInferenciaMs = agora;
    float conf = 0;
    int c = classificarJanelaTiny(&conf);
    if (c >= 0 && conf >= TINYML_CONF_MIN) {
      tinyClasseAtual = c;
      aplicarAcaoTiny(ei_classifier_inferencing_categories[c]);
      logTs("TINYML");
      Serial.print("ACAO -> ");
      Serial.println(ei_classifier_inferencing_categories[c]);
    }
  }
}
 
// ================= Motores =================
 
void moveForward() {
  logMotor("FRENTE");
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  setSpeed(SPEED);
}
void moveBackward() {
  logMotor("RE");
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  setSpeed(SPEED);
}
void turnLeft() {
  logMotor("ESQUERDA");
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  setSpeed(SPEED);
}
void turnRight() {
  logMotor("DIREITA");
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  setSpeed(SPEED);
}
void stopMotors() {
  logMotor("PARADO");
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  setSpeed(0);
}
void setSpeed(int speed) {
  ledcWrite(ENA, speed);
  ledcWrite(ENB, speed);
}
 
// Gira alternando rápido pros dois lados - "dancinha" (bloqueia ~600ms)
void dancar() {
  logMsg("DANCA", "Inicio da danca");
  turnLeft();  setSpeed(SPEED); delay(150);
  turnRight(); setSpeed(SPEED); delay(150);
  turnLeft();  setSpeed(SPEED); delay(150);
  turnRight(); setSpeed(SPEED); delay(150);
  stopMotors();
  logMsg("DANCA", "Fim da danca");
}
 
// ================= Buzzer =================
 
void tocarMelodia() {
  logMsg("BUZZER", "Melodia: inicio");
  int notas[]    = {659, 587, 494, 587};
  int duracoes[] = {150, 150, 150, 150};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER, notas[i], duracoes[i]);
    delay(duracoes[i] + 30);
  }
  noTone(BUZZER);
  logMsg("BUZZER", "Melodia: fim");
}
 
// Sirene tipo "carro de polícia" - sweep de frequência
void tocarSirene() {
  logMsg("BUZZER", "Sirene: inicio");
  for (int f = 400; f <= 1200; f += 40) {
    tone(BUZZER, f);
    delay(8);
  }
  for (int f = 1200; f >= 400; f -= 40) {
    tone(BUZZER, f);
    delay(8);
  }
  noTone(BUZZER);
  logMsg("BUZZER", "Sirene: fim");
}
 
// ================= Faces =================
 
void desenharFace(Face f) {
  logTs("DISPLAY");
  Serial.print("Face -> ");
  Serial.println(nomeFace(f));
 
  display.clearDisplay();
  switch (f) {
    case FACE_IDLE:      faceFeliz(false); break;
    case FACE_FORWARD:   faceFeliz(true);  break;
    case FACE_BACKWARD:  faceSurpreso();   break;
    case FACE_LEFT:      faceOlhar(-8);    break;
    case FACE_RIGHT:     faceOlhar(8);     break;
    case FACE_HORN:      faceGritando();   break;
    case FACE_MUSIC:     faceCantando();   break;
    case FACE_DANCE:     faceDancando();   break;
    case FACE_SCARED:    faceAssustado();  break;
    case FACE_SLEEPY:    faceDormindo();   break;
  }
  display.display();
}
 
void sorrisoBase() {
  for (int x = 0; x <= 40; x++) {
    int y = 45 + (x * (40 - x)) / 60;
    display.drawPixel(44 + x, y, SSD1306_WHITE);
    display.drawPixel(45 + x, y, SSD1306_WHITE);
  }
}
 
void faceFeliz(bool animado) {
  display.fillCircle(40, 24, 8, SSD1306_WHITE);
  display.fillCircle(88, 24, 8, SSD1306_WHITE);
  if (animado) {
    display.fillCircle(64, 48, 7, SSD1306_WHITE);
  } else {
    sorrisoBase();
  }
}
 
void faceSurpreso() {
  display.drawCircle(40, 26, 9, SSD1306_WHITE);
  display.drawCircle(88, 26, 9, SSD1306_WHITE);
  display.fillCircle(40, 26, 4, SSD1306_WHITE);
  display.fillCircle(88, 26, 4, SSD1306_WHITE);
  display.fillCircle(64, 48, 6, SSD1306_WHITE);
}
 
void faceOlhar(int deslocamento) {
  display.drawCircle(40, 24, 8, SSD1306_WHITE);
  display.drawCircle(88, 24, 8, SSD1306_WHITE);
  display.fillCircle(40 + deslocamento, 24, 4, SSD1306_WHITE);
  display.fillCircle(88 + deslocamento, 24, 4, SSD1306_WHITE);
  sorrisoBase();
}
 
void faceGritando() {
  display.fillCircle(40, 22, 7, SSD1306_WHITE);
  display.fillCircle(88, 22, 7, SSD1306_WHITE);
  display.fillRoundRect(50, 40, 28, 16, 4, SSD1306_WHITE);
}
 
void faceCantando() {
  display.drawLine(32, 26, 48, 20, SSD1306_WHITE);
  display.drawLine(48, 20, 32, 14, SSD1306_WHITE);
  display.drawLine(80, 26, 96, 20, SSD1306_WHITE);
  display.drawLine(96, 20, 80, 14, SSD1306_WHITE);
  sorrisoBase();
  display.fillCircle(110, 44, 3, SSD1306_WHITE);
  display.drawLine(113, 44, 113, 30, SSD1306_WHITE);
  display.drawLine(113, 30, 118, 32, SSD1306_WHITE);
}
 
// Olhos em espiral (tontura) - aproximado com círculos concêntricos
void faceDancando() {
  display.drawCircle(40, 24, 8, SSD1306_WHITE);
  display.drawCircle(40, 24, 5, SSD1306_WHITE);
  display.drawCircle(40, 24, 2, SSD1306_WHITE);
  display.drawCircle(88, 24, 8, SSD1306_WHITE);
  display.drawCircle(88, 24, 5, SSD1306_WHITE);
  display.drawCircle(88, 24, 2, SSD1306_WHITE);
  // boca ondulada
  display.drawLine(44, 48, 54, 44, SSD1306_WHITE);
  display.drawLine(54, 44, 64, 48, SSD1306_WHITE);
  display.drawLine(64, 48, 74, 44, SSD1306_WHITE);
  display.drawLine(74, 44, 84, 48, SSD1306_WHITE);
}
 
// Olhos em "X" (assustado)
void faceAssustado() {
  display.drawLine(32, 18, 48, 32, SSD1306_WHITE);
  display.drawLine(48, 18, 32, 32, SSD1306_WHITE);
  display.drawLine(80, 18, 96, 32, SSD1306_WHITE);
  display.drawLine(96, 18, 80, 32, SSD1306_WHITE);
  display.fillCircle(64, 48, 8, SSD1306_WHITE); // boca em "O" de grito
}
 
// Olhos fechados retos + "Z" de sono
void faceDormindo() {
  display.drawLine(32, 24, 48, 24, SSD1306_WHITE);
  display.drawLine(80, 24, 96, 24, SSD1306_WHITE);
  sorrisoBase();
  display.setTextSize(1);
  display.setCursor(100, 4);
  display.print("z");
  display.setCursor(108, 0);
  display.print("Z");
}