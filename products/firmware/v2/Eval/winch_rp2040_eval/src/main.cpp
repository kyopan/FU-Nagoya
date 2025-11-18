/***********************************************************************
 *  _       _______   __________  __   __  _____
 * | |     / /  _/ | / / ____/ / / /  / / / /__ \
 * | | /| / // //  |/ / /   / /_/ /  / / / /__/ /
 * | |/ |/ // // /|  / /___/ __  /  / /_/ // __/
 * |__/|__/___/_/ |_/\____/_/ /_/   \____//____/
 *
 * FU Winch v2 - RP2040 Eval
 * Continuous 1m Up/Down Cycle Test
 *
 * Author: Kyopalab. LLC
 * Creation Date: 2025/11/18
 * Version: 0.5-eval
 * License: Proprietary License
 * MCU: Waveshare RP2040-Zero
 ************************************************************************/

#include <Adafruit_NeoPixel.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

// Pin definitions
#define PIN_ONBOARD_LED   16
#define PIN_LED_PWM       14
#define PIN_SENSOR        15  // End switch (DIGITAL INPUT_PULLUP)
#define PIN_SOLENOID      26
#define PIN_TMC2209_EN    2
#define PIN_TMC2209_TX    0
#define PIN_TMC2209_RX    1
#define PIN_TMC2209_STEP  7
#define PIN_TMC2209_DIR   8

// Motor parameters
const long STEPS_PER_REV = 200L * 16;  // 200 steps/rev × 16 microsteps
const int WINCH_RADIUS = 25;           // mm
const long HOMING_SPEED = 500;         // Minimum speed for calibration (steps/sec)

// Random cycling parameters
const long MIN_DISTANCE_MM = 300;      // 30cm minimum
const long MAX_DISTANCE_MM = 1500;     // 1.5m maximum
const long MIN_SPEED = 500;            // Minimum cycling speed (steps/sec)
const long MAX_SPEED = 5000;           // Maximum cycling speed (steps/sec)
const long MIN_ACCL = 1000;            // Minimum acceleration for slow speeds
const long MAX_ACCL = 2500;            // Maximum acceleration for high speeds

const unsigned long CALIBRATION_TIMEOUT_MS = 120000;  // 2 minutes

// LED fade parameters
const unsigned long LED_FADE_DURATION_MS = 2000;  // 2 seconds

// TMC2209 UART setup
#define TMC_SERIAL Serial1
#define R_SENSE 0.11f
#define DRIVER_ADDRESS 0b00

TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, DRIVER_ADDRESS);
AccelStepper stepper(AccelStepper::DRIVER, PIN_TMC2209_STEP, PIN_TMC2209_DIR);
Adafruit_NeoPixel pixel(1, PIN_ONBOARD_LED);

// State machine states
enum State {
  STATE_INIT,
  STATE_LED_FADE_ON,
  STATE_LED_FADE_OFF,
  STATE_CALIBRATING,
  STATE_IDLE,
  STATE_CYCLING_DOWN,
  STATE_CYCLING_UP,
  STATE_ERROR
};

State currentState = STATE_INIT;
unsigned long stateStartTime = 0;
unsigned long calibrationStartTime = 0;
long cycleTargetPosition = 0;

// Random cycling state
long currentDistance = 0;
long currentSpeed = 0;
long currentAccel = 0;

// Function prototypes
void initializeHardware();
void initializeTMC2209();
void performCalibration();
void fadeLED(bool fadeIn);
void updateStateMachine();
void showLED(uint8_t r, uint8_t g, uint8_t b);
long mmToSteps(long mm);
void generateRandomCycleParameters();

void setup() {
  Serial.begin(115200);

  Serial.println("\n========================================");
  Serial.println("FU Winch v2 - Continuous Cycle Test");
  Serial.println("Waveshare RP2040-Zero");
  Serial.println("========================================\n");

  initializeHardware();

  Serial.println("Setup complete. Starting operation...\n");
  stateStartTime = millis();
  currentState = STATE_INIT;
}

void loop() {
  updateStateMachine();
}

void initializeHardware() {
  // Pin initialization
  pinMode(PIN_TMC2209_EN, OUTPUT);
  pinMode(PIN_TMC2209_STEP, OUTPUT);
  pinMode(PIN_TMC2209_DIR, OUTPUT);
  pinMode(PIN_LED_PWM, OUTPUT);
  pinMode(PIN_SENSOR, INPUT_PULLUP);  // Digital input with internal pullup
  pinMode(PIN_SOLENOID, OUTPUT);

  // Onboard LED initialization
  pixel.begin();
  showLED(0, 0, 0);

  // LED PWM initialization
  analogWriteFreq(20000);
  analogWriteResolution(8);
  analogWrite(PIN_LED_PWM, 0);

  // CRITICAL: Brake locked at startup
  digitalWrite(PIN_SOLENOID, HIGH);
  Serial.println("SOLENOID: Brake LOCKED (HIGH)");

  // TMC2209 initialization
  initializeTMC2209();
}

void initializeTMC2209() {
  digitalWrite(PIN_TMC2209_EN, HIGH);  // Initially disabled

  // UART initialization
  TMC_SERIAL.begin(115200);

  // TMC2209 configuration
  driver.begin();
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.rms_current(800, 1.0f);
  driver.microsteps(16);
  driver.pwm_autoscale(true);
  driver.en_spreadCycle(false);  // StealthChop mode for quiet operation at low speeds
  driver.TPWMTHRS(200);
  driver.semin(5);
  driver.semax(2);
  driver.seup(2);
  driver.sedn(1);

  // AccelStepper configuration
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(MAX_ACCL);

  // Verify TMC2209 communication
  uint32_t drvVersion = driver.version();
  if (drvVersion == 0xFFFFFFFF) {
    Serial.println("ERROR: TMC2209 communication failed!");
    currentState = STATE_ERROR;
  } else {
    Serial.print("TMC2209 version: 0x");
    Serial.println(drvVersion, HEX);
    Serial.print("Microsteps: ");
    Serial.println(driver.microsteps());
    Serial.print("RMS Current: ");
    Serial.print(driver.rms_current());
    Serial.println(" mA");
  }
}

void fadeLED(bool fadeIn) {
  unsigned long elapsed = millis() - stateStartTime;
  float progress = (float)elapsed / (float)LED_FADE_DURATION_MS;
  progress = constrain(progress, 0.0f, 1.0f);

  uint8_t brightness;
  if (fadeIn) {
    brightness = (uint8_t)(255.0f * progress);
  } else {
    brightness = (uint8_t)(255.0f * (1.0f - progress));
  }

  analogWrite(PIN_LED_PWM, brightness);

  // Fade complete check
  if (progress >= 1.0f) {
    if (fadeIn) {
      Serial.println("LED fade-in complete");
      currentState = STATE_LED_FADE_OFF;
      stateStartTime = millis();
    } else {
      Serial.println("LED fade-out complete");
      currentState = STATE_CALIBRATING;
      calibrationStartTime = millis();
      stateStartTime = millis();
    }
  }
}

void performCalibration() {
  static bool calibrationStarted = false;

  if (!calibrationStarted) {
    Serial.println("\n=== CALIBRATION START ===");
    Serial.println("Moving toward end switch...");

    // Enable motor FIRST (holding torque active)
    digitalWrite(PIN_TMC2209_EN, LOW);
    Serial.println("Motor ENABLED (holding torque active)");
    delay(100);  // Wait for driver to stabilize

    // THEN unlock brake (motor is already holding)
    digitalWrite(PIN_SOLENOID, LOW);
    Serial.println("SOLENOID: Brake UNLOCKED for calibration");
    delay(200);  // Wait for brake to fully release

    // Setup AccelStepper for calibration with SMOOTH ACCELERATION
    stepper.setMaxSpeed(HOMING_SPEED);
    stepper.setAcceleration(1000);  // Smooth acceleration (1000 steps/sec²)

    // Use moveTo() for smooth acceleration from 0 to HOMING_SPEED
    stepper.moveTo(1000000);  // Large positive number = move toward end switch indefinitely

    Serial.print("Max Speed: ");
    Serial.print(HOMING_SPEED);
    Serial.println(" steps/sec");
    Serial.print("Acceleration: 1000 steps/sec²");
    Serial.println("Direction: Toward end switch (positive with smooth acceleration)");

    calibrationStarted = true;
  }

  // Check timeout
  unsigned long calibrationElapsed = millis() - calibrationStartTime;
  if (calibrationElapsed > CALIBRATION_TIMEOUT_MS) {
    Serial.println("ERROR: Calibration timeout (2 minutes)");

    // CRITICAL SAFETY SEQUENCE for stopping:
    digitalWrite(PIN_SOLENOID, HIGH);  // Lock brake
    Serial.println("SOLENOID: Brake LOCKED");
    delay(200);
    digitalWrite(PIN_TMC2209_EN, HIGH);  // Disable motor
    Serial.println("Motor DISABLED");

    currentState = STATE_ERROR;
    return;
  }

  // Check end switch (HIGH = not triggered, LOW = triggered with INPUT_PULLUP)
  bool endSwitchTriggered = (digitalRead(PIN_SENSOR) == LOW);

  if (endSwitchTriggered) {
    Serial.println("SENSOR DETECTED!");
    Serial.print("Calibration time: ");
    Serial.print(calibrationElapsed / 1000);
    Serial.println(" seconds");

    // Stop motor immediately
    stepper.stop();

    // Set current position as zero (home)
    stepper.setCurrentPosition(0);

    Serial.println("=== CALIBRATION COMPLETE ===\n");
    Serial.println("Waiting 3 seconds before starting random cycle...\n");

    // 3 second delay before starting cycle
    delay(3000);

    Serial.println("Starting random distance/speed cycling...\n");

    // CRITICAL: Brake remains unlocked for cycling (already LOW)
    Serial.println("SOLENOID: Brake UNLOCKED for cycling");

    // Set LED to indicate cycling (Green)
    showLED(0, 100, 0);

    // Generate first random cycle parameters
    generateRandomCycleParameters();

    // Start cycling with smooth acceleration
    currentState = STATE_CYCLING_DOWN;
    stateStartTime = millis();
  } else {
    // Continue moving toward end switch with smooth acceleration
    stepper.run();

    // Status update every 2 seconds
    static unsigned long lastStatusTime = 0;
    if (millis() - lastStatusTime > 2000) {
      lastStatusTime = millis();
      Serial.print("Moving... (");
      Serial.print(calibrationElapsed / 1000);
      Serial.print("s elapsed, sensor: ");
      Serial.print(digitalRead(PIN_SENSOR) ? "HIGH" : "LOW");
      Serial.print(", current speed: ");
      Serial.print(stepper.speed());
      Serial.println(" steps/sec)");
    }
  }
}

long mmToSteps(long mm) {
  float revolutions = (float)mm / (2.0f * PI * WINCH_RADIUS);
  return (long)(revolutions * (float)STEPS_PER_REV);
}

void updateStateMachine() {
  switch (currentState) {

    case STATE_INIT:
      Serial.println("State: INIT");
      Serial.println("Starting LED fade-in...");
      currentState = STATE_LED_FADE_ON;
      stateStartTime = millis();
      showLED(100, 100, 0);  // Yellow = initializing
      break;

    case STATE_LED_FADE_ON:
      fadeLED(true);  // Fade in
      break;

    case STATE_LED_FADE_OFF:
      fadeLED(false);  // Fade out
      break;

    case STATE_CALIBRATING:
      performCalibration();
      break;

    case STATE_CYCLING_DOWN:
      // Use runToNewPosition() for blocking smooth motion
      Serial.print("Moving down ");
      Serial.print(currentDistance);
      Serial.print("mm @ ");
      Serial.print(currentSpeed);
      Serial.println(" steps/sec...");
      stepper.runToNewPosition(cycleTargetPosition);
      Serial.println("Reached bottom, reversing...");

      // Immediately switch to up direction
      currentState = STATE_CYCLING_UP;
      stateStartTime = millis();
      break;

    case STATE_CYCLING_UP:
      // Use runToNewPosition() for blocking smooth motion
      Serial.println("Moving up to home...");
      stepper.runToNewPosition(0);
      Serial.println("Reached top, generating new random parameters...");

      // Generate new random parameters for next cycle
      generateRandomCycleParameters();

      // Immediately switch to down direction
      currentState = STATE_CYCLING_DOWN;
      stateStartTime = millis();
      break;

    case STATE_ERROR:
      // Error state - stop everything
      digitalWrite(PIN_TMC2209_EN, HIGH);  // Disable motor
      digitalWrite(PIN_SOLENOID, HIGH);    // Lock brake
      analogWrite(PIN_LED_PWM, 0);         // LED off
      showLED(100, 0, 0);                  // Red = error

      static unsigned long lastErrorTime = 0;
      if (millis() - lastErrorTime > 1000) {
        lastErrorTime = millis();
        Serial.println("ERROR STATE - System halted");
      }
      break;
  }
}

void showLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

// Generate random cycle parameters (distance, speed, acceleration)
void generateRandomCycleParameters() {
  // Seed random with analog noise from unused pin (A0)
  uint32_t seed = analogRead(A0);
  seed = (seed << 16) | analogRead(A0);  // Use two readings for more entropy
  randomSeed(seed);

  // Random distance: 300mm (30cm) to 2000mm (2m)
  currentDistance = random(MIN_DISTANCE_MM, MAX_DISTANCE_MM + 1);

  // Random speed: 500 to 5000 steps/sec
  currentSpeed = random(MIN_SPEED, MAX_SPEED + 1);

  // Calculate appropriate acceleration based on speed
  // Lower speeds → lower acceleration, higher speeds → higher acceleration
  float speedRatio = (float)(currentSpeed - MIN_SPEED) / (float)(MAX_SPEED - MIN_SPEED);
  currentAccel = MIN_ACCL + (long)(speedRatio * (float)(MAX_ACCL - MIN_ACCL));

  // Calculate target position (negative = downward from home)
  cycleTargetPosition = -mmToSteps(currentDistance);

  // Apply new speed and acceleration settings
  stepper.setMaxSpeed(currentSpeed);
  stepper.setAcceleration(currentAccel);

  // Debug output
  Serial.print(">>> NEW CYCLE: Distance=");
  Serial.print(currentDistance);
  Serial.print("mm, Speed=");
  Serial.print(currentSpeed);
  Serial.print(" steps/sec, Accel=");
  Serial.print(currentAccel);
  Serial.println(" steps/sec²");
}
