#include <DabbleESP32.h>

// Definição dos pinos dos motores
#define IN1 16
#define IN2 17
#define IN3 18
#define IN4 19
#define ENA 5
#define ENB 6

void setup()
{
  // Configuração dos pinos como saída
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  // Inicializa o Bluetooth com o nome do carrinho
  Dabble.begin("My Bluetooth Car");
}

void loop()
{
  // Atualiza os dados recebidos do aplicativo
  Dabble.processInput();

  // Verificação dos botões do GamePad
  if (GamePad.isUpPressed())
  {
    moveForward();
  }
  else if (GamePad.isDownPressed())
  {
    moveBackward();
  }
  else if (GamePad.isLeftPressed())
  {
    turnLeft();
  }
  else if (GamePad.isRightPressed())
  {
    turnRight();
  }
  else
  {
    stopMotors();
  }
}

// Funções de movimentação dos motores
void moveForward()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void moveBackward()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void turnLeft()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void turnRight()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void stopMotors()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}
