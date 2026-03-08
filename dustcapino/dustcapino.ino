
/*
===============================================================================
 DustCapIno Observatory Controller Firmware
===============================================================================

 Author:
    Björn Bergman

 Platform:
    Arduino Nano / ATmega328P

 Description:
    Firmware for a motorized telescope dust cap and PWM flat-field panel
    controller designed for automated observatories using INDI / Ekos.

 Hardware:
    • Servo driven telescope dust cap
    • PWM controlled flat panel illumination
    • DHT22 environmental sensor

 Architecture Overview:
    The firmware is organized into logical subsystems:
        - Motion Engine        (servo movement & interpolation)
        - Safety System        (light safety + host watchdog)
        - Serial Protocol      (INDI command interface)
        - Diagnostics Engine   (DBG telemetry)
        - Hardware Interface   (buttons, PWM, sensors)

 Motion Algorithm:
    Servo motion uses the "smootherstep" interpolation function:
        t = t^3 ( t (6t − 15) + 10 )

    This produces smooth acceleration and deceleration of the cap motion
    and prevents mechanical shock to the servo mechanism.

 Serial Protocol (INDI compatible):

        CMD:OPEN
        CMD:CLOSE
        CMD:STOP
        CMD:STATUS
        CMD:ANGLE:<deg>
        CMD:LIGHT_ON
        CMD:LIGHT_OFF
        CMD:BRIGHTNESS:<0-255>
        CMD:READ_DHT

    Typical response:

        STATUS:IDLE_OPEN,0,0,SAFE
        STATUS:MOVING,120,0,SAFE
        DHT:21.5,38.2

 Diagnostics Telemetry:

        DBG pwm=1 angle=135 pulse=1500 VCC=4980 RAM=1234 moving=0 cap=1

 Firmware ID:
    DUSTCAPINO_V1.4

 Build:
    __DATE__ __TIME__

 License:
    MIT

===============================================================================


//=============================================================================
// SUBSYSTEM DOCUMENTATION
//=============================================================================

/*
Motion Engine
-------------
Responsible for smooth servo positioning and movement interpolation.
Also performs stall detection and initiates recovery if movement stops.

Safety System
-------------
Ensures the flat panel light cannot activate unless the cap is closed
(unless override mode is active). Also handles host watchdog timeout
which automatically closes the cap if communication stops.

Serial Protocol
---------------
Handles ASCII command interface used by the INDI driver. All commands
are newline terminated.

Diagnostics Engine
------------------
DBG messages provide runtime telemetry for driver diagnostics including:

    - servo PWM active state
    - servo angle
    - pulse width
    - supply voltage
    - free RAM
    - motion state

*/

//=============================================================================

#define FIRMWARE_ID "DUSTCAPINO_V1.4"
#define BUILD_DATE __DATE__ " " __TIME__
#include <avr/wdt.h>
#include <Servo.h>
#include <EEPROM.h>
#include <DHT.h>
#include <avr/io.h>

// ==========================================================
// GLOBALS
// ==========================================================

// ================= LOG LEVEL =================

#define LOG_DEBUG 2
#define LOG_INFO  1
#define LOG_NONE  0

int logLevel = LOG_INFO;
#define LOGD(x) if (logLevel >= LOG_DEBUG) Serial.println(x)
#define LOGI(x) if (logLevel >= LOG_INFO)  Serial.println(x)

uint8_t resetCause;
unsigned long lastStatusPush = 0;
const unsigned long STATUS_FAST = 400;
const unsigned long STATUS_IDLE = 2000;
unsigned long lastServoUpdate = 0;
unsigned long servoPowerFailTimer = 0;
float lastServoAngleCheck = 0;
const unsigned long SERVO_INTERVAL = 15;
float lastStatusAngle = -999;
unsigned long servoStallTimer = 0;
unsigned long lastDebugReport = 0;
int lastPulse = -1;
unsigned long moveWatchdog = 0;
unsigned long globalMoveTimeout = 0;
unsigned long lastLoopTime = 0;
unsigned long lastHostContact = 0;

#define DHTPIN 8
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ================= PINS =================
const int servoPin = 9;
const int lightPin = 3;
const uint8_t servoButton = A1;
const uint8_t lightButton = A2;

// ================= SERVO =================
Servo dustCapServo;

const int servoMinPulse = 500;
const int servoMaxPulse = 2500;
const float maxAngle = 270.0;
const float degreesPerSecond = 60.0;

// ================= EEPROM =================
const uint16_t EEPROM_MAGIC = 0xBEEF;
const int EEPROM_ADDR = 0;

struct SavedState {
  uint16_t magic;
  float angle;
};

SavedState state;

// ================= MOTION =================
float currentAngle = 135.0;
float targetAngle  = 135.0;
float startAngle   = 135.0;
float totalDistance = 0.0;

float moveDuration = 0.0;
unsigned long moveStartTime = 0;
bool moving = false;


// ================= LIGHT =================
uint8_t lightBrightness = 0;

// ================= STATES =================
enum CapState { CLOSED, OPEN, MOVING };
enum SafetyMode { SAFE, OVERRIDE };

CapState capState = CLOSED;
SafetyMode safetyMode = SAFE;

// ================= SERIAL BUFFER =================
char inputBuffer[64];
uint8_t inputIndex = 0;

// ===== Manual Button State =====
static bool lastServoBtn  = HIGH;
static bool lastLightBtn  = HIGH;
static unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 150;
// ==========================================================
// Utility
// ==========================================================

int angleToPulse(float angle)
{
  float ratio = angle / maxAngle;
  return servoMinPulse + (ratio * (servoMaxPulse - servoMinPulse));
}
void hardServoReset()
{
  Serial.println("SERVO_RESET");

  dustCapServo.detach();
  delay(50);

  dustCapServo.attach(servoPin, servoMinPulse, servoMaxPulse);

  lastPulse = -1;   // viktigt

  int pulse = angleToPulse(currentAngle);
  dustCapServo.writeMicroseconds(pulse);

  for (int i = 0; i < 3; i++)
  {
    dustCapServo.writeMicroseconds(pulse);
    delay(20);
  }

  lastPulse = pulse;
}

void savePosition(float angle)
{
  state.magic = EEPROM_MAGIC;
  state.angle = angle;
  EEPROM.put(EEPROM_ADDR, state);
}

float loadPosition()
{
  EEPROM.get(EEPROM_ADDR, state);

  if (state.magic == EEPROM_MAGIC &&
      state.angle >= 0 &&
      state.angle <= maxAngle)
  {
    return state.angle;
  }

  return 270.0;
}

void enforceSafety()
{
  if (capState == MOVING)
  {
    if (lightBrightness > 0)
    {
      lightBrightness = 0;
      digitalWrite(lightPin, LOW);
      Serial.println("LIGHT_OFF");
    }
    return;
  }

  if (capState != CLOSED && safetyMode == SAFE)
  {
    if (lightBrightness > 0)
    {
      lightBrightness = 0;
      digitalWrite(lightPin, LOW);
      Serial.println("LIGHT_OFF");
    }
  }
}

extern int __heap_start, *__brkval;

int freeRam()
{
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

long readVcc()
{
  long result;

  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);

  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));

  result = ADCL;
  result |= ADCH << 8;

  result = 1125300L / result;

  return result;   // millivolt
}
bool servoPWMActive()
{
  // Servo library uses Timer1.
  // If Timer1 clock select bits are 0 → timer stopped.

  if ((TCCR1B & 0x07) == 0)
    return false;

  return true;
}
void sendHello()
{
  Serial.print("HELLO:DUSTCAPINO,");
  Serial.print(FIRMWARE_ID);
  Serial.print(",");

  if (resetCause & _BV(WDRF))
    Serial.print("WATCHDOG,");
  else if (resetCause & _BV(BORF))
    Serial.print("BROWNOUT,");
  else if (resetCause & _BV(PORF))
    Serial.print("POWERON,");
  else
    Serial.print("POWERON,");

  Serial.print(millis() / 1000);
  Serial.print(",");

  Serial.print(freeRam());
  Serial.print(",");

  Serial.println(BUILD_DATE);
  Serial.print("STATUS:");

  if (capState == OPEN)
    Serial.print("IDLE_OPEN");
  else
    Serial.print("IDLE_CLOSED");

  Serial.print(",");
  Serial.print(currentAngle, 0);
  Serial.print(",");
  Serial.print(lightBrightness);
  Serial.print(",");
  Serial.println(safetyMode == SAFE ? "SAFE" : "OVERRIDE");
  Serial.print(",VCC:");
  Serial.println(readVcc());
}

void startMove(float newTarget)
{
  startAngle = currentAngle;
  targetAngle = newTarget;
  totalDistance = targetAngle - startAngle;

  float distanceAbs = fabs(totalDistance);

  if (distanceAbs < 0.5)
  {
    currentAngle = newTarget;
    capState = (newTarget == 0.0) ? OPEN : CLOSED;
    enforceSafety();
    return;
  }

  moveDuration = (distanceAbs / degreesPerSecond) * 1000.0;
  if (moveDuration < 1)
    moveDuration = 1;

  moveStartTime = millis();
  moveWatchdog = millis();   // <-- ny
  globalMoveTimeout = millis();
  moving = true;
  capState = MOVING;
  lastServoUpdate = millis();
  enforceSafety();

  // --- Ny rad här ---
  Serial.print("STATUS:MOVING,");
  Serial.print(currentAngle, 0);
  Serial.print(",");
  Serial.print(lightBrightness);
  Serial.print(",");
  Serial.println(safetyMode == SAFE ? "SAFE" : "OVERRIDE");
}

// ==========================================================
// COMMAND HANDLER (NON-BLOCKING)
// ==========================================================

void handleCommand(char *cmd)
{
#ifdef DEBUG
  Serial.print("RX:");
  Serial.println(cmd);
#endif
  if (strcmp(cmd, "CMD:OPEN") == 0)
  {
    startMove(0.0);
  }
  else if (strcmp(cmd, "CMD:CLOSE") == 0)
  {
    startMove(270.0);
  }
  else if (strcmp(cmd, "CMD:STOP") == 0)
  {
    moving = false;
    savePosition(currentAngle);
    lastPulse = -1;
    capState = (currentAngle <= 1.0) ? OPEN : CLOSED;
    enforceSafety();
  }
  else if (strcmp(cmd, "CMD:HELLO") == 0)
  {
    sendHello();
  }
  else if (strcmp(cmd, "CMD:SERVO_RESET") == 0)
  {
    hardServoReset();
  }
  else if (strncmp(cmd, "CMD:STATUS", 10) == 0)
  {
    Serial.print("STATUS:");

    if (capState == OPEN)
      Serial.print("IDLE_OPEN");
    else if (capState == CLOSED)
      Serial.print("IDLE_CLOSED");
    else
      Serial.print("MOVING");

    Serial.print(",");
    Serial.print(currentAngle, 0);
    Serial.print(",");
    Serial.print(lightBrightness);
    Serial.print(",");
    Serial.println(safetyMode == SAFE ? "SAFE" : "OVERRIDE");
  }
  else if (strncmp(cmd, "CMD:ANGLE:", 10) == 0)
  {
    float angle = atof(cmd + 10);
    angle = constrain(angle, 0, 270);
    startMove(angle);
  }
  else if (strcmp(cmd, "CMD:SAFE") == 0)
  {
    safetyMode = SAFE;
    enforceSafety();
    Serial.println("SAFE_MODE");
  }
  else if (strcmp(cmd, "CMD:OVERRIDE") == 0)
  {
    safetyMode = OVERRIDE;
    Serial.println("OVERRIDE_MODE");
  }
  else if (strcmp(cmd, "CMD:LIGHT_ON") == 0)
  {
    if (capState != CLOSED && safetyMode == SAFE)
    {
      Serial.println("LIGHT_BLOCKED");
      return;
    }

    lightBrightness = 255;
    analogWrite(lightPin, 255);
    Serial.println("LIGHT_ON");
  }
  else if (strcmp(cmd, "CMD:LIGHT_OFF") == 0)
  {
    lightBrightness = 0;
    digitalWrite(lightPin, LOW);
    Serial.println("LIGHT_OFF");
  }
  else if (strncmp(cmd, "CMD:BRIGHTNESS:", 15) == 0)
  {
    int value = atoi(cmd + 15);
    value = constrain(value, 0, 255);

    if (value > 0 && capState != CLOSED && safetyMode == SAFE)
    {
      Serial.println("LIGHT_BLOCKED");
      return;
    }

    lightBrightness = value;
    analogWrite(lightPin, value);
    Serial.println("BRIGHTNESS_OK");
  }
  else if (strcmp(cmd, "CMD:READ_DHT") == 0)
  {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h))
    {
      Serial.print("DHT:");
      Serial.print(t, 2);
      Serial.print(",");
      Serial.println(h, 2);
    }
    else
    {
      Serial.println("DHT:ERROR");
    }
  }
  else if (strcmp(cmd, "CMD:REBOOT") == 0)
  {
    Serial.println("REBOOTING");
    delay(100);

    wdt_enable(WDTO_15MS);
    while (1);
  }
  else if (strcmp(cmd, "CMD:RESET_WATCHDOG") == 0)
  {
    wdt_disable();
    Serial.println("WDT_DISABLED");
  }
  else if (strcmp(cmd, "CMD:UPLOAD_MODE") == 0)
  {
    Serial.println("UPLOAD_MODE");

    wdt_disable();     // stäng watchdog
    delay(100);

    // stanna här tills ny firmware flashas
    while (true)
    {
      delay(1000);
    }
  }
  else if (strcmp(cmd, "CMD:LOG_DEBUG") == 0)
  {
    logLevel = LOG_DEBUG;
    Serial.println("LOG_DEBUG");
  }
  else if (strcmp(cmd, "CMD:LOG_INFO") == 0)
  {
    logLevel = LOG_INFO;
    Serial.println("LOG_INFO");
  }
  else if (strcmp(cmd, "CMD:LOG_NONE") == 0)
  {
    logLevel = LOG_NONE;
    Serial.println("LOG_NONE");
  }
}

// ==========================================================
// SETUP
// ==========================================================

void setup()
{
  lastHostContact = millis();
  uint8_t mcusr_mirror = MCUSR;
  MCUSR = 0;
  wdt_disable();

  resetCause = mcusr_mirror;
  pinMode(servoButton, INPUT_PULLUP);
  pinMode(lightButton, INPUT_PULLUP);

  delay(300);        // ge bootloader tid att få kontroll

  wdt_enable(WDTO_8S);

  Serial.begin(115200);
  Serial.println("BOOT_OK");
  delay(100);

  dht.begin();

  pinMode(lightPin, OUTPUT);
  digitalWrite(lightPin, LOW);

  dustCapServo.attach(servoPin, servoMinPulse, servoMaxPulse);
  delay(100);
  currentAngle = loadPosition();
  targetAngle  = currentAngle;

  capState = (currentAngle <= 1.0) ? OPEN : CLOSED;

  int pulse = angleToPulse(currentAngle);

  if (pulse != lastPulse)
  {
    dustCapServo.writeMicroseconds(pulse);
    lastPulse = pulse;
  }

  sendHello();
}

// ==========================================================
// LOOP
// ==========================================================

void loop()
{
  if (millis() - lastLoopTime >= 5)
  {
    lastLoopTime = millis();

    // loop body
  }
  wdt_reset();
  unsigned long now = millis();

  // -------- Servo motion --------
  if (!dustCapServo.attached())
  {
    dustCapServo.attach(servoPin, servoMinPulse, servoMaxPulse);

    int pulse = angleToPulse(currentAngle);
    dustCapServo.writeMicroseconds(pulse);
    lastPulse = pulse;

    Serial.println("SERVO_REATTACHED");
  }
  if (moving && now - lastServoUpdate > SERVO_INTERVAL)
  {
    lastServoUpdate = now;

    float elapsed = now - moveStartTime;
    float t = min(1.0f, elapsed / moveDuration);

    if (elapsed > moveDuration + 2000)   // failsafe
      t = 1.0;

    t = constrain(t, 0.0, 1.0);
    if (t >= 0.995)
    {
      moving = false;
      currentAngle = targetAngle;

      // ---- RELAX AT ENDSTOP ----
      if (targetAngle >= 269.0)
        currentAngle = 268.0;

      if (targetAngle <= 1.0)
        currentAngle = 2.0;

      int pulse = angleToPulse(currentAngle);

      if (pulse != lastPulse)
      {
        dustCapServo.writeMicroseconds(pulse);
        lastPulse = pulse;
      }
      savePosition(currentAngle);
      lastPulse = -1;
      savePosition(currentAngle);
      lastPulse = -1;

      capState = (targetAngle == 0.0) ? OPEN : CLOSED;
      enforceSafety();

      Serial.print("STATUS:");
      Serial.print(capState == OPEN ? "IDLE_OPEN" : "IDLE_CLOSED");
      Serial.print(",");
      Serial.print(currentAngle, 0);
      Serial.print(",");
      Serial.print(lightBrightness);
      Serial.print(",");
      Serial.println(safetyMode == SAFE ? "SAFE" : "OVERRIDE");

      lastStatusPush = now;
      return;   // viktigt
    }

    float s = t * t * t * (t * (6 * t - 15) + 10);

    currentAngle = startAngle + totalDistance * s;

    // ---- SERVO POWER FAIL DETECTION ----
    if (fabs(currentAngle - lastServoAngleCheck) < 0.05)
    {
      if (millis() - servoPowerFailTimer > 2000)
      {
        Serial.println("SERVO_POWER_FAIL");
        hardServoReset();
        servoPowerFailTimer = millis();
      }
    }
    else
    {
      servoPowerFailTimer = millis();
      lastServoAngleCheck = currentAngle;
    }

    // ---- SERVO STALL DETECTION ----
    if (fabs(currentAngle - lastStatusAngle) < 0.2)
    {
      if (millis() - servoStallTimer > 1500)
      {
        Serial.println("SERVO_STALL");

        hardServoReset();

        servoStallTimer = millis();
      }
    }
    else
    {
      servoStallTimer = millis();
    }

    int pulse = angleToPulse(currentAngle);

    if (pulse != lastPulse)
    {
      LOGD("SERVO_DEBUG");
      Serial.print("SERVO:");
      Serial.print(currentAngle);
      Serial.print(",");
      Serial.print(targetAngle);
      Serial.print(",");
      Serial.print(pulse);
      Serial.print(",");
      Serial.println(moving ? "MOVING" : "IDLE");

      dustCapServo.writeMicroseconds(pulse);
      lastPulse = pulse;
    }

    if (fabs(currentAngle - lastStatusAngle) > 1.0 ||
        now - lastStatusPush > 1000)
    {
      Serial.print("STATUS:MOVING,");
      Serial.print(currentAngle, 0);
      Serial.print(",");
      Serial.print(lightBrightness);
      Serial.print(",");
      Serial.println(safetyMode == SAFE ? "SAFE" : "OVERRIDE");

      lastStatusAngle = currentAngle;
      lastStatusPush = now;
    }
  }

  // -------- MOVE RECOVERY --------
  if (moving)
  {
    // försöker starta rörelsen igen
    if (millis() - moveWatchdog > 5000)
    {
      Serial.println("MOVE_RECOVERY");

      hardServoReset();   // <-- NY RAD

      startMove(targetAngle);

      moveWatchdog = millis();
    }

    // sista failsafe om allt annat misslyckas
    if (millis() - globalMoveTimeout > 15000)
    {
      Serial.println("MOVE_TIMEOUT");

      moving = false;
      currentAngle = targetAngle;

      capState = (targetAngle == 0.0) ? OPEN : CLOSED;

      Serial.print("STATUS:");
      Serial.print(capState == OPEN ? "IDLE_OPEN" : "IDLE_CLOSED");
      Serial.print(",");
      Serial.print(currentAngle, 0);
      Serial.print(",");
      Serial.print(lightBrightness);
      Serial.print(",");
      Serial.println(safetyMode == SAFE ? "SAFE" : "OVERRIDE");
    }
  }
  // -------- HOST WATCHDOG --------
  if (millis() - lastHostContact > 60000)
  {
    if (lightBrightness > 0)
    {
      Serial.println("HOST_TIMEOUT");

      lightBrightness = 0;
      digitalWrite(lightPin, LOW);
    }
  }
  // -------- Non-blocking serial --------
  while (Serial.available())
  {
    char c = Serial.read();

    if (c == '\r') continue;

    if (c == '\n')
    {
      inputBuffer[inputIndex] = 0;

      if (inputIndex > 0)
      {
        lastHostContact = millis();
        handleCommand(inputBuffer);
      }

      inputIndex = 0;
    }
    else if (inputIndex < sizeof(inputBuffer) - 1)
    {
      inputBuffer[inputIndex++] = c;
    }
  }

  bool servoNow = digitalRead(servoButton);
  bool lightNow = digitalRead(lightButton);

  unsigned long nowBtn = millis();

  // ---- Servo button (edge triggered) ----
  if (servoNow == LOW && lastServoBtn == HIGH &&
      nowBtn - lastButtonTime > debounceDelay)
  {
    lastButtonTime = nowBtn;

    if (capState == CLOSED)
      startMove(0.0);
    else if (capState == OPEN)
      startMove(270.0);
  }
  // ---- DEBUG STATUS ----
  if (millis() - lastDebugReport > 2000)
  {
    Serial.print("DBG ");

    Serial.print(" pwm=");
    Serial.print(servoPWMActive() ? 1 : 0);

    Serial.print("angle=");
    Serial.print(currentAngle, 1);

    Serial.print(" pulse=");
    Serial.print(lastPulse);

    Serial.print(" VCC=");
    Serial.print(readVcc());

    Serial.print(" RAM=");
    Serial.print(freeRam());

    Serial.print(" moving=");
    Serial.print(moving ? 1 : 0);

    Serial.print(" cap=");
    Serial.print(capState);

    Serial.println();

    lastDebugReport = millis();
  }
  // ---- Light button (edge triggered) ----
  if (lightNow == LOW && lastLightBtn == HIGH &&
      nowBtn - lastButtonTime > debounceDelay)
  {
    lastButtonTime = nowBtn;

    if (lightBrightness > 0)
    {
      lightBrightness = 0;
      digitalWrite(lightPin, LOW);
    }
    else
    {
      if (capState == CLOSED || safetyMode == OVERRIDE)
      {
        lightBrightness = 255;
        analogWrite(lightPin, 255);
      }
    }
  }

  lastServoBtn = servoNow;
  lastLightBtn = lightNow;
}
