/**
 * ESP32-S3 RC Servo Test with Potentiometer + NeoPixel Visual Feedback
 *
 * Hardware Connection:
 * - Servo Signal: GPIO 13
 * - Servo Power: 5V
 * - Servo GND: GND
 * - Potentiometer VCC: 3.3V
 * - Potentiometer GND: GND
 * - Potentiometer Signal: GPIO 4 (ADC1_CH3)
 * - NeoPixel Data: GPIO 48 (ESP32-S3 onboard RGB LED)
 * - NeoPixel Power: 3.3V (onboard)
 *
 * Functionality:
 * - Reads 10K potentiometer value via ADC
 * - Maps potentiometer position (0-4095) to servo angle (0-180 degrees)
 * - Updates servo position in real-time
 * - NeoPixel color changes based on angle (0°=Red → 90°=Green → 180°=Blue)
 * - Serial output for monitoring
 */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>

// Pin Definitions
constexpr uint8_t SERVO_PIN = 13;     // GPIO 13 for servo PWM output
constexpr uint8_t POT_PIN = 4;        // GPIO 4 for potentiometer ADC input (ADC1_CH3)
constexpr uint8_t NEOPIXEL_PIN = 48;  // GPIO 48 for ESP32-S3 onboard RGB LED

// Servo configuration
constexpr int SERVO_MIN_US = 500;   // Minimum pulse width in microseconds
constexpr int SERVO_MAX_US = 2500;  // Maximum pulse width in microseconds
constexpr int SERVO_MIN_ANGLE = 0;  // Minimum servo angle (degrees)
constexpr int SERVO_MAX_ANGLE = 180; // Maximum servo angle (degrees)

// ADC configuration
constexpr int ADC_RESOLUTION = 12;   // 12-bit ADC (0-4095)
constexpr int ADC_MAX_VALUE = 4095;  // Maximum ADC reading

// Update rate
constexpr unsigned long UPDATE_INTERVAL_MS = 20; // 50Hz update rate

// Jitter reduction configuration
constexpr int DEADBAND_THRESHOLD = 2;     // Ignore angle changes within ±2 degrees
constexpr int FILTER_SAMPLES = 5;         // Moving average filter samples

// NeoPixel configuration
constexpr uint8_t NEOPIXEL_COUNT = 1;        // 1 onboard LED
constexpr uint8_t NEOPIXEL_BRIGHTNESS = 50;  // 0-255 (keep low for comfort)

// Global objects
Servo servo;
Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// State variables
unsigned long lastUpdateTime = 0;
int lastServoAngle = -1;  // Track last servo position to avoid unnecessary updates

// Moving average filter buffer
int adcBuffer[FILTER_SAMPLES] = {0};
int bufferIndex = 0;
bool bufferFilled = false;

/**
 * @brief Initialize serial communication
 */
void setupSerial() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=================================");
  Serial.println("ESP32-S3 Servo + NeoPixel Test");
  Serial.println("=================================");
  Serial.println("Pin Configuration:");
  Serial.printf("  Servo Signal: GPIO %d\n", SERVO_PIN);
  Serial.printf("  Potentiometer: GPIO %d (ADC1_CH3)\n", POT_PIN);
  Serial.printf("  NeoPixel LED: GPIO %d (onboard)\n", NEOPIXEL_PIN);
  Serial.println("=================================\n");
}

/**
 * @brief Initialize NeoPixel LED
 */
void setupNeoPixel() {
  pixel.begin();
  pixel.setBrightness(NEOPIXEL_BRIGHTNESS);
  pixel.setPixelColor(0, pixel.Color(0, 255, 0));  // Green (center position indicator)
  pixel.show();

  Serial.println("[NeoPixel] Initialized onboard RGB LED");
  Serial.printf("[NeoPixel] Brightness: %d/255\n", NEOPIXEL_BRIGHTNESS);
  Serial.println("[NeoPixel] Color mapping: 0°=Red → 90°=Green → 180°=Blue");
  Serial.printf("[Filter] Moving average: %d samples, Deadband: ±%d°\n\n",
                FILTER_SAMPLES, DEADBAND_THRESHOLD);
}

/**
 * @brief Initialize ADC for potentiometer reading
 */
void setupADC() {
  analogReadResolution(ADC_RESOLUTION);
  analogSetAttenuation(ADC_11db);  // Full range 0-3.3V
  pinMode(POT_PIN, INPUT);
  Serial.println("[ADC] Initialized 12-bit ADC (0-4095)");
}

/**
 * @brief Initialize servo
 */
void setupServo() {
  // Attach servo with custom pulse width range
  servo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);

  // Move to center position
  servo.write(90);

  Serial.println("[Servo] Attached to GPIO 13");
  Serial.printf("[Servo] Pulse width: %d-%d us\n", SERVO_MIN_US, SERVO_MAX_US);
  Serial.printf("[Servo] Angle range: %d-%d degrees\n", SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  Serial.println("[Servo] Initial position: 90 degrees (center)\n");

  delay(500);  // Allow servo to reach center position
}

/**
 * @brief Read potentiometer value and convert to servo angle with noise filtering
 * @return Servo angle (0-180 degrees)
 */
int readPotentiometerAngle() {
  // Read ADC value and add to moving average buffer
  adcBuffer[bufferIndex] = analogRead(POT_PIN);
  bufferIndex = (bufferIndex + 1) % FILTER_SAMPLES;

  // Mark buffer as filled after first full cycle
  if (bufferIndex == 0 && !bufferFilled) {
    bufferFilled = true;
  }

  // Calculate moving average
  int sum = 0;
  int samples = bufferFilled ? FILTER_SAMPLES : (bufferIndex + 1);
  for (int i = 0; i < samples; i++) {
    sum += adcBuffer[i];
  }
  int avgValue = sum / samples;

  // Map averaged ADC value to servo angle (0-180)
  int angle = map(avgValue, 0, ADC_MAX_VALUE, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);

  // Constrain to valid range
  angle = constrain(angle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);

  return angle;
}

/**
 * @brief Update NeoPixel color based on servo angle
 * @param angle Servo angle (0-180 degrees)
 *
 * Color mapping:
 *   0° - 60°   : Red → Yellow (R=255, G: 0→255, B=0)
 *   60° - 120° : Yellow → Green (R: 255→0, G=255, B=0)
 *   120° - 180°: Green → Blue (R=0, G: 255→0, B: 0→255)
 */
void updateNeoPixelColor(int angle) {
  uint8_t r, g, b;

  if (angle < 60) {
    // 0° → 60°: Red → Yellow (add green)
    r = 255;
    g = map(angle, 0, 60, 0, 255);
    b = 0;
  } else if (angle < 120) {
    // 60° → 120°: Yellow → Green (remove red)
    r = map(angle, 60, 120, 255, 0);
    g = 255;
    b = 0;
  } else {
    // 120° → 180°: Green → Blue (remove green, add blue)
    r = 0;
    g = map(angle, 120, 180, 255, 0);
    b = map(angle, 120, 180, 0, 255);
  }

  // Apply gamma correction for smoother color transitions
  r = pixel.gamma8(r);
  g = pixel.gamma8(g);
  b = pixel.gamma8(b);

  // Update LED
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

/**
 * @brief Update servo position and NeoPixel color based on potentiometer
 */
void updateServoAndLED() {
  unsigned long currentTime = millis();

  // Check if update interval has passed
  if (currentTime - lastUpdateTime < UPDATE_INTERVAL_MS) {
    return;
  }

  lastUpdateTime = currentTime;

  // Read potentiometer and calculate target angle (with moving average filter)
  int targetAngle = readPotentiometerAngle();

  // Apply deadband: only update if angle change exceeds threshold
  if (abs(targetAngle - lastServoAngle) > DEADBAND_THRESHOLD) {
    // Convert angle to pulse width for high-resolution control
    int pulseWidth = map(targetAngle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE, SERVO_MIN_US, SERVO_MAX_US);

    // Update servo position with microsecond precision (11x better resolution than write())
    servo.writeMicroseconds(pulseWidth);

    // Update NeoPixel color
    updateNeoPixelColor(targetAngle);

    // Debug output (show filtered angle and pulse width)
    Serial.printf("[Update] Filtered Angle: %3d° → %4d μs (change: %+d°)\n",
                  targetAngle, pulseWidth, targetAngle - lastServoAngle);

    lastServoAngle = targetAngle;
  }
}

/**
 * @brief Arduino setup function
 */
void setup() {
  setupSerial();
  setupNeoPixel();
  setupADC();
  setupServo();

  Serial.println("Setup complete. Starting servo + LED control...\n");
}

/**
 * @brief Arduino loop function
 */
void loop() {
  updateServoAndLED();

  // Small delay to prevent watchdog issues
  delay(1);
}
