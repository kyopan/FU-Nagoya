/***********************************************************************
 *  _       _______   __________  __   __  _____
 * | |     / /  _/ | / / ____/ / / /  / / / /__ \
 * | | /| / // //  |/ / /   / /_/ /  / / / /__/ /
 * | |/ |/ // // /|  / /___/ __  /  / /_/ // __/
 * |__/|__/___/_/ |_/\____/_/ /_/   \____//____/
 *
 * FU Winch v2 - RP2040 Eval Hardware Test
 *
 * Author: Kyopalab. LLC
 * Creation Date: 2025/11/17
 * Version: 0.1-test
 * License: Proprietary License
 * MCU: Waveshare RP2040-Zero
 *
 * Test Items:
 *   1. Onboard LED (NeoPixel GPIO 16)
 *   2. LED PWM (GPIO 14)
 *   3. Electromagnetic Brake / Solenoid (GPIO 26)
 *   4. Stepper Motor (TMC2209 via GPIO 0,1,2,7,8)
 *   5. End Switch / Homing Sensor (GPIO 27)
 *
 * Serial Commands:
 *   1           - Test Onboard LED (RGB cycle)
 *   2           - Test LED PWM (brightness sweep)
 *   3           - Test Electromagnetic Brake (lock/unlock)
 *   4 <steps>   - Test Stepper Motor (move steps, default 1000)
 *   5           - Test End Switch (continuous read)
 *   h           - Show help
 *   i           - Show TMC2209 driver info
 *   r           - Reset all outputs
 ************************************************************************/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

// Pin definitions (from winch_rp2040_eval)
#define PIN_ONBOARD_LED   16
#define PIN_LED_PWM       14
#define PIN_SENSOR        15  // End switch (DIGITAL INPUT_PULLUP)
#define PIN_SOLENOID      26
#define PIN_TMC2209_EN    2
#define PIN_TMC2209_TX    0
#define PIN_TMC2209_RX    1
#define PIN_TMC2209_STEP  7
#define PIN_TMC2209_DIR   8
#define PIN_U2_SDA        4
#define PIN_U2_SCL        5

// I2C configuration
#define U2_SLAVE_ADDRESS  0x08

// TMC2209 configuration
#define R_SENSE 0.11f  // Sense resistor value
HardwareSerial &TMC_SERIAL = Serial1;
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, 0b00);

// AccelStepper
AccelStepper stepper(AccelStepper::DRIVER, PIN_TMC2209_STEP, PIN_TMC2209_DIR);

// NeoPixel (Onboard LED)
Adafruit_NeoPixel pixel(1, PIN_ONBOARD_LED, NEO_GRB + NEO_KHZ800);

// PWM settings
const uint32_t PWM_FREQ = 1000;  // 1kHz

// Test states
enum TestState {
    TEST_IDLE,
    TEST_ONBOARD_LED,
    TEST_LED_PWM,
    TEST_BRAKE,
    TEST_STEPPER,
    TEST_SENSOR,
    TEST_I2C,
    TEST_CALIBRATION
};

TestState currentTest = TEST_IDLE;
unsigned long testStartTime = 0;
String inputString = "";

// I2C communication variables
volatile bool i2cDataReceived = false;
volatile uint8_t i2cRxBuffer[32];
volatile uint8_t i2cRxLength = 0;
uint8_t i2cTxData = 0x00;

// Function prototypes
void showLED(uint8_t r, uint8_t g, uint8_t b);
void testOnboardLED();
void testLEDPWM();
void testBrake();
void testStepper(long steps);
void testSensor();
void testI2C();
void testCalibration();
void resetAll();
void printHelp();
void readDriverInfo();
bool initTMC2209();
void processCommand(String cmd);

// I2C callback prototypes
void receiveEvent(int howMany);
void requestEvent();

void setup() {
    Serial.begin(115200);

    Serial.println("\n========================================");
    Serial.println("FU Winch v2 - RP2040 Eval Hardware Test");
    Serial.println("Waveshare RP2040-Zero");
    Serial.println("========================================\n");

    // Pin initialization
    pinMode(PIN_LED_PWM, OUTPUT);
    pinMode(PIN_SENSOR, INPUT_PULLUP);  // Digital input with internal pullup
    pinMode(PIN_SOLENOID, OUTPUT);
    pinMode(PIN_TMC2209_EN, OUTPUT);
    pinMode(PIN_TMC2209_STEP, OUTPUT);
    pinMode(PIN_TMC2209_DIR, OUTPUT);

    // PWM configuration
    analogWriteFreq(PWM_FREQ);
    analogWriteResolution(8);

    // NeoPixel initialization
    pixel.begin();
    showLED(0, 0, 0);

    // Initial states
    digitalWrite(PIN_TMC2209_EN, HIGH);  // Motor disabled
    analogWrite(PIN_LED_PWM, 0);          // LED OFF
    analogWrite(PIN_SOLENOID, 255);       // Brake locked

    Serial.println("Hardware initialization complete.\n");
    printHelp();
}

void loop() {
    // Serial command processing
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputString.length() > 0) {
                processCommand(inputString);
                inputString = "";
            }
        } else {
            inputString += c;
        }
    }

    // Test state machine
    switch (currentTest) {
        case TEST_ONBOARD_LED:
            testOnboardLED();
            break;
        case TEST_LED_PWM:
            testLEDPWM();
            break;
        case TEST_BRAKE:
            testBrake();
            break;
        case TEST_STEPPER:
            stepper.run();
            if (stepper.distanceToGo() == 0) {
                Serial.println("✓ Stepper movement complete");

                // CRITICAL SAFETY SEQUENCE for stopping:
                // 1. Lock brake FIRST while motor still holding
                analogWrite(PIN_SOLENOID, 255);  // Lock brake
                Serial.println("🔒 Brake LOCKED (motor still holding)");
                delay(200);  // Wait for brake to engage

                // 2. THEN disable motor (brake is already holding)
                digitalWrite(PIN_TMC2209_EN, HIGH);  // Disable motor
                Serial.println("⏹️  Motor DISABLED (safe state)");

                currentTest = TEST_IDLE;
            }
            break;
        case TEST_SENSOR:
            testSensor();
            break;
        case TEST_I2C:
            testI2C();
            break;
        case TEST_CALIBRATION:
            testCalibration();
            break;
        default:
            break;
    }
}

void processCommand(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;

    char command = cmd.charAt(0);

    switch (command) {
        case '1':
            Serial.println("\n=== Test 1: Onboard LED (NeoPixel) ===");
            Serial.println("RGB color cycling for 5 seconds...");
            currentTest = TEST_ONBOARD_LED;
            testStartTime = millis();
            break;

        case '2':
            Serial.println("\n=== Test 2: LED PWM ===");
            Serial.println("Brightness sweep for 5 seconds...");
            currentTest = TEST_LED_PWM;
            testStartTime = millis();
            break;

        case '3':
            Serial.println("\n=== Test 3: Electromagnetic Brake ===");
            Serial.println("Lock/Unlock cycle for 5 seconds...");
            currentTest = TEST_BRAKE;
            testStartTime = millis();
            break;

        case '4': {
            Serial.println("\n=== Test 4: Stepper Motor ===");
            long steps = 1000;  // Default
            if (cmd.length() > 2) {
                steps = cmd.substring(2).toInt();
            }
            Serial.print("Moving ");
            Serial.print(steps);
            Serial.println(" steps...");
            testStepper(steps);
            break;
        }

        case '5':
            Serial.println("\n=== Test 5: End Switch / Sensor ===");
            Serial.println("Continuous reading (press any key to stop)...");
            currentTest = TEST_SENSOR;
            break;

        case '6':
            Serial.println("\n=== Test 6: I2C Communication ===");
            Serial.println("Waiting for I2C messages (press any key to stop)...");
            currentTest = TEST_I2C;
            testStartTime = millis();
            break;

        case 'c':
        case 'C':
            Serial.println("\n=== Calibration / Homing ===");
            Serial.println("Starting calibration sequence...");
            Serial.println("Moving toward end switch...");
            currentTest = TEST_CALIBRATION;
            testStartTime = millis();
            break;

        case 'h':
        case 'H':
            printHelp();
            break;

        case 'i':
        case 'I':
            Serial.println("\n=== TMC2209 Driver Info ===");
            if (initTMC2209()) {
                readDriverInfo();
            } else {
                Serial.println("✗ TMC2209 initialization failed");
            }
            break;

        case 'r':
        case 'R':
            Serial.println("\n=== Reset All Outputs ===");
            resetAll();
            Serial.println("✓ All outputs reset");
            break;

        default:
            Serial.print("Unknown command: ");
            Serial.println(cmd);
            Serial.println("Type 'h' for help");
            break;
    }
}

void testOnboardLED() {
    unsigned long elapsed = millis() - testStartTime;

    if (elapsed > 5000) {
        showLED(0, 0, 0);
        Serial.println("✓ Onboard LED test complete");
        currentTest = TEST_IDLE;
        return;
    }

    // RGB color cycle (Red -> Green -> Blue)
    int phase = (elapsed / 1666) % 3;  // 5000ms / 3 colors
    int brightness = 100;

    switch (phase) {
        case 0:
            showLED(brightness, 0, 0);  // Red
            break;
        case 1:
            showLED(0, brightness, 0);  // Green
            break;
        case 2:
            showLED(0, 0, brightness);  // Blue
            break;
    }
}

void testLEDPWM() {
    unsigned long elapsed = millis() - testStartTime;

    if (elapsed > 5000) {
        analogWrite(PIN_LED_PWM, 0);
        Serial.println("✓ LED PWM test complete");
        currentTest = TEST_IDLE;
        return;
    }

    // Brightness sweep (0 -> 255 -> 0)
    int brightness;
    if (elapsed < 2500) {
        brightness = map(elapsed, 0, 2500, 0, 255);
    } else {
        brightness = map(elapsed, 2500, 5000, 255, 0);
    }

    analogWrite(PIN_LED_PWM, brightness);
}

void testBrake() {
    unsigned long elapsed = millis() - testStartTime;

    if (elapsed > 5000) {
        analogWrite(PIN_SOLENOID, 255);  // Lock
        Serial.println("✓ Brake test complete (locked)");
        currentTest = TEST_IDLE;
        return;
    }

    // Lock/Unlock cycle every 1 second
    if ((elapsed / 1000) % 2 == 0) {
        analogWrite(PIN_SOLENOID, 0);    // Unlock
        Serial.println("  Brake: UNLOCKED");
    } else {
        analogWrite(PIN_SOLENOID, 255);  // Lock
        Serial.println("  Brake: LOCKED");
    }

    delay(1000);
}

void testStepper(long steps) {
    // Initialize TMC2209
    if (!initTMC2209()) {
        Serial.println("✗ TMC2209 initialization failed");
        return;
    }

    // CRITICAL SAFETY SEQUENCE for winch (gravity load):
    // 1. Enable motor FIRST to hold position
    digitalWrite(PIN_TMC2209_EN, LOW);
    Serial.println("⚡ Motor ENABLED (holding torque active)");
    delay(200);  // Wait for motor current to stabilize

    // 2. THEN unlock brake (motor is already holding)
    analogWrite(PIN_SOLENOID, 0);  // Unlock brake
    Serial.println("⚠️  Brake UNLOCKED (motor holding load)");
    delay(100);  // Wait for brake to release

    // 3. Configure movement
    stepper.setMaxSpeed(1000);
    stepper.setAcceleration(500);
    stepper.setCurrentPosition(0);
    stepper.moveTo(steps);

    currentTest = TEST_STEPPER;
    Serial.println("🔄 Movement started...");
}

void testSensor() {
    // Check for any key press to stop
    if (Serial.available()) {
        while (Serial.available()) Serial.read();  // Clear buffer
        Serial.println("✓ Sensor test stopped");
        currentTest = TEST_IDLE;
        return;
    }

    // Read sensor value (DIGITAL input - INPUT_PULLUP)
    int sensorValue = digitalRead(PIN_SENSOR);
    Serial.print("Sensor state: ");
    Serial.print(sensorValue);

    if (sensorValue == LOW) {
        Serial.println("  [TRIGGERED]");  // LOW = switch pressed
    } else {
        Serial.println("  [OPEN]");       // HIGH = switch released
    }

    delay(100);
}

void testI2C() {
    // Check for any key press to stop
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        Serial.println("✓ I2C test stopped");
        currentTest = TEST_IDLE;
        return;
    }

    // Check if data received via I2C
    if (i2cDataReceived) {
        i2cDataReceived = false;

        Serial.print("📨 I2C RX [");
        Serial.print(i2cRxLength);
        Serial.print(" bytes]: ");

        for (uint8_t i = 0; i < i2cRxLength; i++) {
            Serial.print("0x");
            if (i2cRxBuffer[i] < 0x10) Serial.print("0");
            Serial.print(i2cRxBuffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();

        // Interpret common commands (matching winch_rp2040_eval)
        if (i2cRxLength >= 2) {
            uint8_t cmd = i2cRxBuffer[0];
            uint8_t data = i2cRxBuffer[1];

            switch (cmd) {
                case 'H':
                    Serial.println("  → HOMING command detected");
                    break;
                case 'M':
                    Serial.print("  → MOVE command, target: ");
                    Serial.println(data);
                    break;
                case 'S':
                    Serial.println("  → STOP command detected");
                    break;
                default:
                    Serial.print("  → Unknown command: 0x");
                    Serial.println(cmd, HEX);
                    break;
            }
        }
    }

    delay(100);
}

// I2C receive callback
void receiveEvent(int howMany) {
    i2cRxLength = 0;

    while (Wire.available() && i2cRxLength < 32) {
        i2cRxBuffer[i2cRxLength++] = Wire.read();
    }

    if (i2cRxLength > 0) {
        i2cDataReceived = true;
    }
}

// I2C request callback
void requestEvent() {
    // Send status byte (0x00 = idle, 0x01 = homing, 0x02 = moving)
    Wire.write(i2cTxData);
}

void testCalibration() {
    static bool calibrationInitialized = false;
    static bool motorStarted = false;
    const long CALIBRATION_SPEED = 500;  // Low speed for safety (steps/sec)

    // Initialization phase
    if (!calibrationInitialized) {
        // Initialize TMC2209
        if (!initTMC2209()) {
            Serial.println("✗ Calibration failed: TMC2209 initialization error");
            currentTest = TEST_IDLE;
            return;
        }

        // CRITICAL SAFETY SEQUENCE for starting:
        // 1. Enable motor FIRST (holding torque active)
        digitalWrite(PIN_TMC2209_EN, LOW);  // Enable motor
        Serial.println("🔓 Motor ENABLED (holding torque active)");
        delay(100);  // Wait for driver to stabilize

        // 2. THEN unlock brake (motor is already holding)
        analogWrite(PIN_SOLENOID, 0);  // Unlock brake
        Serial.println("🔓 Brake UNLOCKED (motor is holding)");
        delay(200);  // Wait for brake to fully release

        // Setup AccelStepper for calibration
        stepper.setMaxSpeed(CALIBRATION_SPEED);
        stepper.setSpeed(CALIBRATION_SPEED);  // Positive = toward end switch

        Serial.println("✓ Calibration initialized");
        Serial.print("  Speed: ");
        Serial.print(CALIBRATION_SPEED);
        Serial.println(" steps/sec");
        Serial.println("  Direction: Toward end switch (positive)");

        calibrationInitialized = true;
        motorStarted = false;
        testStartTime = millis();
    }

    // Check for timeout (30 seconds)
    if (millis() - testStartTime > 30000) {
        Serial.println("✗ Calibration timeout (30s exceeded)");

        // CRITICAL SAFETY SEQUENCE for stopping:
        analogWrite(PIN_SOLENOID, 255);  // Lock brake
        Serial.println("🔒 Brake LOCKED");
        delay(200);
        digitalWrite(PIN_TMC2209_EN, HIGH);  // Disable motor
        Serial.println("⏹️  Motor DISABLED");

        calibrationInitialized = false;
        currentTest = TEST_IDLE;
        return;
    }

    // Check end switch (HIGH = not triggered, LOW = triggered with INPUT_PULLUP)
    bool endSwitchTriggered = (digitalRead(PIN_SENSOR) == LOW);

    if (endSwitchTriggered) {
        Serial.println("✓ End switch detected!");
        Serial.println("🎯 Calibration complete");

        // Stop motor immediately
        stepper.stop();
        stepper.setCurrentPosition(0);  // Set current position as home (0)
        Serial.println("  Current position set to 0 (home)");

        // CRITICAL SAFETY SEQUENCE for stopping:
        analogWrite(PIN_SOLENOID, 255);  // Lock brake
        Serial.println("🔒 Brake LOCKED");
        delay(200);
        digitalWrite(PIN_TMC2209_EN, HIGH);  // Disable motor
        Serial.println("⏹️  Motor DISABLED");

        calibrationInitialized = false;
        currentTest = TEST_IDLE;
        return;
    }

    // Continue moving toward end switch
    stepper.runSpeed();

    // Status update every 2 seconds
    static unsigned long lastStatusTime = 0;
    if (millis() - lastStatusTime > 2000) {
        Serial.print("  Moving... (");
        Serial.print((millis() - testStartTime) / 1000);
        Serial.print("s elapsed, sensor: ");
        Serial.print(digitalRead(PIN_SENSOR) ? "HIGH" : "LOW");
        Serial.println(")");
        lastStatusTime = millis();
    }
}

void resetAll() {
    currentTest = TEST_IDLE;
    showLED(0, 0, 0);
    analogWrite(PIN_LED_PWM, 0);
    analogWrite(PIN_SOLENOID, 255);  // Lock
    digitalWrite(PIN_TMC2209_EN, HIGH);  // Disable motor
}

void showLED(uint8_t r, uint8_t g, uint8_t b) {
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

bool initTMC2209() {
    // UART initialization
    TMC_SERIAL.begin(115200);
    delay(100);

    // TMC2209 initialization
    driver.begin();
    driver.pdn_disable(true);         // PDN/UART pin for UART control
    driver.I_scale_analog(false);     // Current control via UART, not VREF
    driver.rms_current(800, 1.0f);    // RMS current: 800mA (run & hold)
    driver.microsteps(16);            // Microstep setting: 1/16
    driver.pwm_autoscale(true);       // StealthChop auto current adjustment

    // StealthChop mode (quiet operation)
    driver.en_spreadCycle(false);
    driver.pdn_disable(true);
    driver.I_scale_analog(false);

    // CoolStep configuration (active when SpreadCycle transitions)
    driver.TPWMTHRS(200);  // StealthChop speed upper limit
    driver.semin(5);
    driver.semax(2);
    driver.seup(2);
    driver.sedn(1);

    delay(100);

    // Verify UART communication
    uint32_t drvVersion = driver.version();
    if (drvVersion == 0xFFFFFFFF || drvVersion == 0x00000000) {
        Serial.println("✗ TMC2209 UART communication error!");
        Serial.println("  Check wiring: TX(GPIO0), RX(GPIO1)");
        Serial.println("  Check slave address: 0b00");
        return false;
    }

    // Verify microstep setting
    uint16_t microsteps = driver.microsteps();
    if (microsteps != 16) {
        Serial.print("✗ TMC2209 microstep read failed: ");
        Serial.println(microsteps);
        return false;
    }

    Serial.println("✓ TMC2209 initialized successfully");
    return true;
}

void readDriverInfo() {
    uint32_t drvVersion = driver.version();
    if (drvVersion == 0xFFFFFFFF) {
        Serial.println("  Version: Communication error");
    } else {
        Serial.print("  Version: 0x");
        Serial.println(drvVersion, HEX);
    }

    Serial.print("  Microsteps: ");
    Serial.println(driver.microsteps());

    Serial.print("  RMS Current: ");
    Serial.print(driver.rms_current());
    Serial.println(" mA");
}

void printHelp() {
    Serial.println("--- Available Test Commands ---");
    Serial.println("1           : Test Onboard LED (RGB cycle)");
    Serial.println("2           : Test LED PWM (brightness sweep)");
    Serial.println("3           : Test Electromagnetic Brake (lock/unlock)");
    Serial.println("4 <steps>   : Test Stepper Motor (default 1000 steps)");
    Serial.println("5           : Test End Switch (continuous read)");
    Serial.println("6           : Test I2C Communication (slave mode)");
    Serial.println("c           : Calibration / Homing (move to end switch)");
    Serial.println("h           : Show this help");
    Serial.println("i           : Show TMC2209 driver info");
    Serial.println("r           : Reset all outputs");
    Serial.println("--------------------------------\n");
}
