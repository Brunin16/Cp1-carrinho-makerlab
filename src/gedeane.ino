#include <DabbleESP32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define IN1 13
#define IN2 4
#define IN3 16
#define IN4 17
#define ENA 19
#define ENB 18
#define BUZZER 5

#define TRIG_FRONT 14
#define ECHO_FRONT 25
#define TRIG_BACK  35
#define ECHO_BACK  34

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

const int PWM_FREQ = 20000;
const int PWM_RES  = 8;
const int SPEED    = 200;
const float DIST_LIMIT_CM = 15.0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum Face { FACE_IDLE, FACE_FORWARD, FACE_BACKWARD, FACE_LEFT, FACE_RIGHT, FACE_HORN, FACE_MUSIC };
Face currentFace = FACE_IDLE;
Face lastDrawnFace = FACE_MUSIC;

bool autopilotAtivo = false;
bool lastTriangleState = false;

void setup() {
  Serial.begin(9600);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(TRIG_FRONT, OUTPUT);
  pinMode(ECHO_FRONT, INPUT);
  pinMode(TRIG_BACK, OUTPUT);
  pinMode(ECHO_BACK, INPUT);

  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Falha ao encontrar o display OLED");
  }

  Dabble.begin("My Bluetooth Car");
  stopMotors();
}

void loop() {
  Dabble.processInput();

  bool triangle = GamePad.isTrianglePressed();
  if (triangle && !lastTriangleState) {
    autopilotAtivo = !autopilotAtivo;
    if (!autopilotAtivo) stopMotors();
    Serial.println(autopilotAtivo ? "Piloto automatico: ON" : "Piloto automatico: OFF");
  }
  lastTriangleState = triangle;

  if (autopilotAtivo) {
    rodarPilotoAutomatico();
  } else {
    rodarModoManual();
  }

  // ---- Buzzer (funciona nos dois modos) ----
  bool square = GamePad.isSquarePressed();
  bool circle = GamePad.isCirclePressed();
  if (square) {
    tone(BUZZER, 1000);
    currentFace = FACE_HORN;
  } else if (circle) {
    tocarMelodia();
    currentFace = FACE_MUSIC;
  } else {
    noTone(BUZZER);
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

  if (up)        { moveForward();  currentFace = FACE_FORWARD; }
  else if (down) { moveBackward(); currentFace = FACE_BACKWARD; }
  else if (left) { turnLeft();     currentFace = FACE_LEFT; }
  else if (right){ turnRight();    currentFace = FACE_RIGHT; }
  else           { stopMotors();   currentFace = FACE_IDLE; }
}

// ================= Piloto automático =================

void rodarPilotoAutomatico() {
  float distFrente = readDistanceCm(TRIG_FRONT, ECHO_FRONT);

  if (distFrente < 0 || distFrente > DIST_LIMIT_CM) {
    moveForward();
    currentFace = FACE_FORWARD;
    return;
  }

  // Obstáculo na frente: para, avalia ré, gira pra desviar
  stopMotors();
  currentFace = FACE_BACKWARD;
  delay(200);

  float distTras = readDistanceCm(TRIG_BACK, ECHO_BACK);
  if (distTras < 0 || distTras > DIST_LIMIT_CM) {
    moveBackward();
    delay(300);
    stopMotors();
    delay(100);
  }

  turnRight();
  currentFace = FACE_RIGHT;
  delay(400);

  stopMotors();
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

// ================= Motores =================

void moveForward() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  setSpeed(SPEED);
}
void moveBackward() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  setSpeed(SPEED);
}
void turnLeft() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  setSpeed(SPEED);
}
void turnRight() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  setSpeed(SPEED);
}
void stopMotors() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  setSpeed(0);
}
void setSpeed(int speed) {
  ledcWrite(ENA, speed);
  ledcWrite(ENB, speed);
}

// ================= Buzzer =================

void tocarMelodia() {
  int notas[]    = {659, 587, 494, 587};
  int duracoes[] = {150, 150, 150, 150};
  for (int i = 0; i < 4; i++) {
    tone(BUZZER, notas[i], duracoes[i]);
    delay(duracoes[i] + 30);
  }
  noTone(BUZZER);
}

// ================= Faces =================

void desenharFace(Face f) {
  display.clearDisplay();
  switch (f) {
    case FACE_IDLE:      faceFeliz(false); break;
    case FACE_FORWARD:   faceFeliz(true);  break;
    case FACE_BACKWARD:  faceSurpreso();   break;
    case FACE_LEFT:      faceOlhar(-8);    break;
    case FACE_RIGHT:     faceOlhar(8);     break;
    case FACE_HORN:      faceGritando();   break;
    case FACE_MUSIC:     faceCantando();   break;
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
