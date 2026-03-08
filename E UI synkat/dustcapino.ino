#include <Servo.h>
#include <EEPROM.h>

#define FW_VERSION "2.1-270DEBUG"

// ------------ Pins ------------
const int servoPin = 9;
const int lightPin = 3;

// ------------ 270° mapping ------------
const int servoMinPulse = 500;
const int servoMaxPulse = 2500;

const int logicalMin = 0;
const int logicalMax = 270;

// ------------ Safe mechanical limits ------------
const int mechanicalMin = 30;
const int mechanicalMax = 240;

int openAngle = 200;
int closedAngle = 120;

int currentAngle;
int targetAngle;

// ------------ Motion ------------
enum MotionState { IDLE_OPEN, IDLE_CLOSED, MOVING, PAUSED };
MotionState motionState;

const int minStep = 1;
const int maxStep = 5;
const int rampDistance = 20;
const int arrivalTolerance = 2;

const unsigned long servoInterval = 15;
const unsigned long maxMoveTime = 6000;

unsigned long lastServoMove = 0;
unsigned long moveStartTime = 0;

Servo capServo;

// =====================================================
// 270° pulse mapping
// =====================================================

int angleToPulse(int angle)
{
  angle = constrain(angle, logicalMin, logicalMax);
  return map(angle, logicalMin, logicalMax, servoMinPulse, servoMaxPulse);
}

void writeAngle(int angle)
{
  int pulse = angleToPulse(angle);
  capServo.writeMicroseconds(pulse);

  Serial.print("DBG: CUR=");
  Serial.print(currentAngle);
  Serial.print(" TAR=");
  Serial.print(targetAngle);
  Serial.print(" PULSE=");
  Serial.println(pulse);
}

// =====================================================
// Status
// =====================================================

void reportState()
{
  switch (motionState)
  {
    case IDLE_OPEN:   Serial.println("IDLE_OPEN"); break;
    case IDLE_CLOSED: Serial.println("IDLE_CLOSED"); break;
    case MOVING:      Serial.println("MOVING"); break;
    case PAUSED:      Serial.println("PAUSED"); break;
  }
}

// =====================================================
// Motion
// =====================================================

void startMove(int newTarget)
{
  targetAngle = constrain(newTarget, mechanicalMin, mechanicalMax);
  moveStartTime = millis();
  motionState = MOVING;
  reportState();
}

void updateServo()
{
  if (motionState != MOVING)
    return;

  if (millis() - moveStartTime > maxMoveTime)
  {
    Serial.println("ERR:STALL_TIMEOUT");
    motionState = PAUSED;
    return;
  }

  if (millis() - lastServoMove < servoInterval)
    return;

  lastServoMove = millis();

  int distance = targetAngle - currentAngle;

  if (abs(distance) <= arrivalTolerance)
  {
    currentAngle = targetAngle;
    writeAngle(currentAngle);

    motionState = (targetAngle == openAngle)
                  ? IDLE_OPEN
                  : IDLE_CLOSED;

    reportState();

    // ===== EKOS PATCH: skicka rätt svar =====
    if (motionState == IDLE_OPEN)
      Serial.println("OPENED");
    else if (motionState == IDLE_CLOSED)
      Serial.println("CLOSED");

    return;
  }

  int direction = (distance > 0) ? 1 : -1;
  int absDistance = abs(distance);

  int stepSize;
  if (absDistance < rampDistance)
    stepSize = map(absDistance, 0, rampDistance, minStep, maxStep);
  else
    stepSize = maxStep;

  currentAngle += direction * stepSize;
  currentAngle = constrain(currentAngle, mechanicalMin, mechanicalMax);

  writeAngle(currentAngle);
}

// =====================================================
// Serial
// =====================================================

void parseCommand(String cmd)
{
  cmd.trim();

  // ===== EKOS PATCH: stöd för O / C / L1 / L0 =====
  if (cmd == "O")
  {
    startMove(openAngle);
    return;
  }
  else if (cmd == "C")
  {
    startMove(closedAngle);
    return;
  }
  else if (cmd == "L1")
  {
    digitalWrite(lightPin, HIGH);
    return;
  }
  else if (cmd == "L0")
  {
    digitalWrite(lightPin, LOW);
    return;
  }

  // ===== DIN BEFINTLIGA LOGIK =====

  if (cmd == "CMD:OPEN")
    startMove(openAngle);

  else if (cmd == "CMD:CLOSE")
    startMove(closedAngle);

  else if (cmd == "CMD:STOP")
  {
    motionState = PAUSED;
    reportState();
  }

  else if (cmd.startsWith("CMD:SETOPEN"))
  {
    int val = cmd.substring(cmd.indexOf("VAL:") + 4).toInt();
    openAngle = constrain(val, mechanicalMin, mechanicalMax);
    Serial.println("OPEN_SET");
  }

  else if (cmd.startsWith("CMD:SETCLOSE"))
  {
    int val = cmd.substring(cmd.indexOf("VAL:") + 4).toInt();
    closedAngle = constrain(val, mechanicalMin, mechanicalMax);
    Serial.println("CLOSE_SET");
  }

  else if (cmd == "CMD:STATUS")
    reportState();

  else if (cmd == "CMD:INFO")
  {
    Serial.print("FW:");
    Serial.println(FW_VERSION);
  }
}

void handleSerial()
{
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    parseCommand(cmd);
  }
}

// =====================================================
// Setup / Loop
// =====================================================

void setup()
{
  Serial.begin(115200);

  currentAngle = closedAngle;
  targetAngle = closedAngle;
  motionState = IDLE_CLOSED;

  capServo.attach(servoPin);

  // ===== EKOS PATCH: init light pin =====
  pinMode(lightPin, OUTPUT);
  digitalWrite(lightPin, LOW);

  writeAngle(currentAngle);
  reportState();
}

void loop()
{
  handleSerial();
  updateServo();
}