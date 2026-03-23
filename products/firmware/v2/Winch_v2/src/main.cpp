/* ============================================================================
 *  _      ___          __
 * | | /| / (_)__  ____/ /
 * | |/ |/ / / _ \/ __/ _ \
 * |__/|__/_/_//_/\__/_//_/
 *
 *  FU Product Winch v2
 * ============================================================================
 * Role:
 * 1. Controls Winch Motor (Stepper + TMC2209)
 * 2. Communicates with Tube Unit 1 (UART) via Slip Ring
 * 3. Handles MQTT Command & Control
 * 4. Master controller for the entire kinetic unit
 * ============================================================================
 */

#include <AccelStepper.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <TMCStepper.h>
#include <Update.h>
#include <WiFi.h>

#include "driver/temperature_sensor.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"

#define FIRMWARE_VERSION "2.4.4"
  
/*
 * CHANGELOG
 * ============================================================================
 * v2.4.4 | 2026-02-11 | Optimization
 *   - [OPT] Increased motor voltage.
 *
 * v2.4.3 | 2026-02-09 | Optimization
 *   - [OPT] Removed RX_RAW debug monitor to improve performance/stability.
 *
 * v2.4.2 | 2026-02-09 | MQTT Task Separation
 *   - [NEW] Implemented dedicated FreeRTOS task (Core 0) for MQTT/WiFi.
 *   - [FIX] Removed blocking MQTT calls from main loop (Core 1).
 *   - [NEW] Added MqttMessage queue for thread-safe communication.
 *   - [FIX] Initialization order in setup() to prevent race conditions.
 *
 * v2.4.1 | 2026-02-09 | Calibration Tweak
 *   - [MOD] Increased CALIBRATION_OFFSET_STEPS to -1600 (~200mm).
 * ============================================================================
 */

#ifndef ENABLE_DEBUG_SERIAL
#define ENABLE_DEBUG_SERIAL // Enabled for debugging homing
#endif

#ifdef ENABLE_DEBUG_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DEBUG_BEGIN(x) Serial.begin(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#define DEBUG_BEGIN(x)
#endif

// =============================================================================
// Pin Definitions
// =============================================================================
#define PIN_TUBE_TX 9      // UART TX to TUBE
#define PIN_TUBE_RX 8      // UART RX from TUBE
#define PIN_ONBOARD_LED 48 // ESP32-S3 onboard RGB LED (WS2812)
#define PIN_STATUS_LED 16  // Status LED strip (4x WS2812B)
#define PIN_LED_PWM 15     // 3W LED PWM control

// TMC2209 Stepper Motor Pins (from winch_esp32s3_dev_test)
#define PIN_TMC2209_EN 10   // Enable (active low)
#define PIN_TMC2209_TX 17   // UART TX
#define PIN_TMC2209_RX 18   // UART RX
#define PIN_TMC2209_STEP 13 // Step
#define PIN_TMC2209_DIR 14  // Direction

// Electromagnetic Brake / Solenoid (from winch_esp32s3_dev_test)
#define PIN_SOLENOID 3 // Brake control (active LOW: HIGH=unlock, LOW=lock)

// End Sensor (from winch_esp32s3_dev_test)
#define PIN_SENSOR 46    // End switch sensor (INPUT_PULLUP, LOW=triggered)
#define PIN_SENSOR_LED 7 // Endswitch detect LED

// PWM settings for 3W LED (Converted to 0.2W LED support)
// TPS92200D2 Driver requires 20kHz-200kHz PWM for analog dimming mode
#define PWM_FREQ 20000   // 20kHz
#define PWM_RESOLUTION 8 // 8-bit (0-255)

// Motor parameters (from v1 FU_WINCH_RP2040_U2)
#define WINCH_RADIUS                                                           \
  50 // mm (spiral winding effective radius - re-calibrated: 1800mm@77mm →
     // 2800mm@50mm)
#define STEPS_PER_REV 6400   // 200 steps/rev × 32 microsteps
#define MOTOR_MAX_SPEED 5000 // steps/sec (doubled from 5000)
#define MOTOR_MAX_ACCEL 5000 // steps/sec² (increased for faster response)
#define MAX_HEIGHT_MM                                                          \
  2800                // Maximum height in mm (calibration home position limit)
#define R_SENSE 0.11f // TMC2209 sense resistor
#define DRIVER_ADDRESS 0b00 // TMC2209 address

// Calibration parameters (from winch_rp2040_eval)
#define HOMING_SPEED 1000             // steps/sec (slow for safety)
#define CALIBRATION_TIMEOUT_MS 120000 // 2 minutes timeout
#define CALIBRATION_OFFSET_STEPS                                               \
  -1600 // 150mm offset after sensor detection (negative = away from sensor)

// StallGuard parameters (DISABLED - EndSwitch only homing)
// #define STALLGUARD_THRESHOLD 100
// #define STALLGUARD_TCOOLTHRS 0xFFFFF
// #define STALLGUARD_IGNORE_DURATION_MS 500

// Standalone mode parameters (WiFi接続失敗時のフォールバック動作)
#define STANDALONE_MODE_ENABLED                                                \
  false // スタンドアロンモード有効/無効 (true=有効, false=無効)
#define STANDALONE_MIN_HEIGHT_MM 500  // ランダム動作の最小高さ (mm)
#define STANDALONE_MAX_HEIGHT_MM 2500 // ランダム動作の最大高さ (mm)
#define STANDALONE_MIN_WAIT_MS 3000   // 到達後の最小待機時間 (ms)
#define STANDALONE_MAX_WAIT_MS 10000  // 到達後の最大待機時間 (ms)

// =============================================================================
// Configuration
// =============================================================================
const char *ssid = "FU";                 // WiFi SSID (venue/開発共通)
const char *password = "Spark!!!!!";     // WiFi Password
const char *mqtt_server = "192.168.1.2"; // Mac IP (Docker nanomq)
const int mqtt_port = 1883;

// TUBE UART
#define TUBE_SERIAL Serial1

// =============================================================================
// Global Objects
// =============================================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_NeoPixel pixel(1, PIN_ONBOARD_LED, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel statusLed(4, PIN_STATUS_LED,
                            NEO_GRB + NEO_KHZ800); // 4x status LEDs

// TMC2209 and AccelStepper (using Serial2 for ESP32-S3)
HardwareSerial TMC_SERIAL(2); // Use Serial2 for TMC2209
TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, DRIVER_ADDRESS);
AccelStepper stepper(AccelStepper::DRIVER, PIN_TMC2209_STEP, PIN_TMC2209_DIR);

// =============================================================================
// OTA Variables (V1 implementation - simple and proven)
// =============================================================================
bool OTAprogress = false;
String otaHost = "comone-fragmented-unity-fw.s3.ap-northeast-1.amazonaws.com";
int otaPort = 80;
String otaBin = "/winch_v2/winch_v2_product.bin"; // S3 path to latest firmware
long contentLength = 0;
bool isValidContentType = false;
int retryCount = 0;
const int maxRetry = 20;

char macWithoutColons[13];
char deviceId[4] = "";
uint8_t deviceIdNumber = 0;
bool idAcquired = false;

// --- Forward Declarations ---
void processTubeLine(char *line);
void updateStatusLed();
void execOTA();
void performCalibration();
// ----------------------------

// Statistics
unsigned long connectTime = 0;
unsigned long messageCount = 0;
unsigned long lastStatusTime = 0;
unsigned long psMessageCount = 0;
unsigned long clMessageCount = 0;
unsigned long dlMessageCount = 0;

// Latest received values
float lastZ = 0.0f;
float lastPitch = 0.0f;
float lastYaw = 0.0f;
uint8_t lastR = 0, lastG = 0, lastB = 0;
uint8_t lastDL = 0; // 3W LED PWM level

// Motor control variables
volatile long targetPosition = 0;       // Target position in steps
volatile uint16_t targetZ_mm = 0;       // Target height in mm
volatile bool motorEnabled = false;     // Motor enable state
volatile bool newTargetArrived = false; // New target flag for motor
volatile bool brakeUnlocked = false;    // Brake state tracking

float pendingPitch = 0.0f;
float pendingYaw = 0.0f;
bool posDirty = false;

// Calibration state variables
volatile bool isCalibrating = false;        // Calibration in progress
volatile bool isCalibrated = false;         // Calibration completed flag
volatile bool calibrationRequested = false; // Request from MQTT
unsigned long calibrationStartTime = 0;     // For timeout detection

// Sensor LED state tracking
bool lastSensorState = false; // Previous sensor state for change detection

// TMC2209 communication state
bool tmcCommunicationOK =
    false; // TMC2209 communication verified (version 0x21)

// Heartbeat timing (moved from FreeRTOS task to loop() for thread safety)
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL_MS = 30000; // 30 seconds

// FreeRTOS task handles
// FreeRTOS task handles
TaskHandle_t motorTaskHandle = NULL; // Motor control on Core 1
TaskHandle_t mqttTaskHandle = NULL;  // MQTT control on Core 0
QueueHandle_t mqttQueue = NULL;      // Queue for outgoing MQTT messages

struct MqttMessage {
  char topic[64];
  char payload[256];
  bool retained;
};

// Temperature sensor handle (ESP32-S3)
temperature_sensor_handle_t temp_sensor = NULL;

// Status LED indices and states
// LED 0: WiFi    - Red: No connection, Yellow: Connecting, Blue: Connected
// (Ready=Black when all OK) LED 1: MQTT    - Red: No connection, Yellow:
// Connecting, Blue: Connected (Ready=Black when all OK) LED 2: Motor   - Red:
// TMC init fail, Yellow: Calibrating, Blue: Calibrated (Ready=Black when all
// OK) LED 3: Tube    - Red: No connection, Yellow: Connecting, Blue: Connected
// (Ready=Black when all OK)
enum StatusLedIndex { LED_WIFI = 0, LED_MQTT = 1, LED_MOTOR = 2, LED_TUBE = 3 };

enum WiFiStatus {
  APP_WIFI_NO_CONNECTION,
  APP_WIFI_CONNECTING,
  APP_WIFI_CONNECTED,
  APP_WIFI_OTA
};

enum MqttStatus {
  APP_MQTT_NO_CONNECTION,
  APP_MQTT_CONNECTING,
  APP_MQTT_CONNECTED,
  APP_MQTT_OTA
};

enum MotorStatus {
  APP_MOTOR_INIT_FAIL,
  APP_MOTOR_CALIBRATING,
  APP_MOTOR_CALIBRATED,
  APP_MOTOR_OTA
};

enum TubeStatus {
  APP_TUBE_NO_CONNECTION,
  APP_TUBE_CONNECTING,
  APP_TUBE_CONNECTED,
  APP_TUBE_OTA
};

WiFiStatus wifiStatus = APP_WIFI_NO_CONNECTION;
MqttStatus mqttStatus = APP_MQTT_NO_CONNECTION;
MotorStatus motorStatus = APP_MOTOR_INIT_FAIL;
TubeStatus tubeStatus = APP_TUBE_NO_CONNECTION;
bool isOtaMode = false;

unsigned long lastTubeRxTime = 0; // For TUBE connection detection
const unsigned long TUBE_TIMEOUT_MS =
    5000; // TUBE considered disconnected after 5s

// TUBE UART buffering (send from loop(), not callback)
unsigned long lastTubeSendTime = 0;
const unsigned long TUBE_SEND_INTERVAL =
    50; // ms (20Hz max) - reduced to prevent congestion
uint8_t pendingR = 0, pendingG = 0, pendingB = 0;
bool rgbDirty = false;

// Retry timers for non-blocking connection
unsigned long lastWiFiRetryTime = 0;
const unsigned long WIFI_RETRY_INTERVAL = 5000;
unsigned long lastMqttRetryTime = 0;
const unsigned long MQTT_RETRY_INTERVAL = 2000;

// TUBE RX buffer (non-blocking read)
char tubeRxBuffer[64];
int tubeRxIndex = 0;

// =============================================================================
// LED Control Functions
// =============================================================================
void showLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void setSpotlightPWM(uint8_t level) {
  // SAFETY LIMIT for 0.2W LED (60mA rating) with 1.5A Driver
  // Driver Max: 1500mA, LED Max: 60mA -> Safe Duty: 4%
  // We use 3% limit (approx 45mA) -> Max PWM Value = 7
  uint8_t safeLevel = map(level, 0, 255, 0, 7);
  ledcWrite(PIN_LED_PWM, safeLevel);
}

// Spotlight LED fade in/out (startup notification)
void spotlightFadeInOut(uint16_t duration_ms) {
  DEBUG_PRINTLN("[SPOTLIGHT] Fade in/out sequence starting...");

  // Fade in (0 -> 100)
  for (int i = 0; i <= 100; i++) {
    setSpotlightPWM(i);
    delay(duration_ms / 200); // 200 steps total (100 up + 100 down)
  }

  // Fade out (100 -> 0)
  for (int i = 100; i >= 0; i--) {
    setSpotlightPWM(i);
    delay(duration_ms / 200);
  }

  DEBUG_PRINTLN("[SPOTLIGHT] Fade in/out sequence complete");
}

// Spotlight LED flash (OTA notification)
void spotlightFlash(uint8_t count, uint16_t interval_ms) {
  DEBUG_PRINTF("[SPOTLIGHT] Flashing %d times (interval=%dms)...\n", count,
               interval_ms);

  for (uint8_t i = 0; i < count; i++) {
    setSpotlightPWM(100); // Full brightness (30% actual)
    delay(interval_ms);
    setSpotlightPWM(0); // Off
    delay(interval_ms);
  }

  DEBUG_PRINTLN("[SPOTLIGHT] Flash sequence complete");
}

// =============================================================================
// Status LED Functions (4x WS2812B on GPIO16)
// Defines per user chart:
// - Red: Error / No Connection
// - Yellow: Connecting / Calibrating
// - Blue: Connected / Calibrated
// - Black (OFF): Alert Logic -> If *ALL* are Blue, turn *ALL* off (Ready
// state).
// =============================================================================
// Update Status LEDs (4x WS2812B)
// Update Status LEDs (4x WS2812B)
void updateStatusLed() {
  if (isOtaMode) {
    // OTA Mode Override: All Blue
    for (int i = 0; i < 4; i++)
      statusLed.setPixelColor(i, 0, 0, 50);
    statusLed.show();
    return;
  }

  // Check if ALL statuses are in "OK" state (Green)
  // If so, turn OFF all LEDs (Ready state)
  bool allOk = (wifiStatus == APP_WIFI_CONNECTED) &&
               (mqttStatus == APP_MQTT_CONNECTED) &&
               (motorStatus == APP_MOTOR_CALIBRATED) &&
               (tubeStatus == APP_TUBE_CONNECTED);

  if (allOk) {
    // Ready Mode: All Off (Black)
    for (int i = 0; i < 4; i++)
      statusLed.setPixelColor(i, 0, 0, 0);
  } else {
    // LED 0: WiFi
    switch (wifiStatus) {
    case APP_WIFI_NO_CONNECTION:
      statusLed.setPixelColor(LED_WIFI, 50, 0, 0);
      break; // Red
    case APP_WIFI_CONNECTING:
      statusLed.setPixelColor(LED_WIFI, 50, 30, 0);
      break; // Orange
    case APP_WIFI_CONNECTED:
      statusLed.setPixelColor(LED_WIFI, 0, 50, 0);
      break; // Green
    }

    // LED 1: MQTT
    switch (mqttStatus) {
    case APP_MQTT_NO_CONNECTION:
      statusLed.setPixelColor(LED_MQTT, 50, 0, 0);
      break; // Red
    case APP_MQTT_CONNECTING:
      statusLed.setPixelColor(LED_MQTT, 50, 30, 0);
      break; // Orange
    case APP_MQTT_CONNECTED:
      statusLed.setPixelColor(LED_MQTT, 0, 50, 0);
      break; // Green
    }

    // LED 2: Motor
    switch (motorStatus) {
    case APP_MOTOR_INIT_FAIL:
      statusLed.setPixelColor(LED_MOTOR, 50, 0, 0);
      break; // Red
    case APP_MOTOR_CALIBRATING:
      statusLed.setPixelColor(LED_MOTOR, 50, 30, 0);
      break; // Orange
    case APP_MOTOR_CALIBRATED:
      statusLed.setPixelColor(LED_MOTOR, 0, 50, 0);
      break; // Green
    }

    // LED 3: Tube
    switch (tubeStatus) {
    case APP_TUBE_NO_CONNECTION:
      statusLed.setPixelColor(LED_TUBE, 50, 0, 0);
      break; // Red
    case APP_TUBE_CONNECTING:
      statusLed.setPixelColor(LED_TUBE, 50, 30, 0);
      break; // Orange
    case APP_TUBE_CONNECTED:
      statusLed.setPixelColor(LED_TUBE, 0, 50, 0);
      break; // Green
    }
  }
  statusLed.show();
}

// =============================================================================
// Motor Control Functions (from v1 FU_WINCH_RP2040_U2)
// =============================================================================

// Convert height (mm) to steps
// Returns step position for given height (negative = away from sensor/down)
long heightToSteps(uint16_t height_mm) {
  int32_t distance_from_home = (int32_t)MAX_HEIGHT_MM - (int32_t)height_mm;
  float rev = (float)distance_from_home / (2.0f * PI * WINCH_RADIUS);
  return -(long)(rev *
                 (float)STEPS_PER_REV); // Negative = away from sensor (down)
}

// Convert steps to height (mm)
// Returns height in mm for given step position
uint16_t stepsToHeight(long steps) {
  // steps = -(rev * STEPS_PER_REV)
  // rev = -steps / STEPS_PER_REV
  float rev = -(float)steps / (float)STEPS_PER_REV;
  float distance_from_home = rev * (2.0f * PI * WINCH_RADIUS);
  int32_t height_mm = (int32_t)MAX_HEIGHT_MM - (int32_t)distance_from_home;

  // Clamp to valid range [0, MAX_HEIGHT_MM]
  if (height_mm < 0)
    height_mm = 0;
  if (height_mm > MAX_HEIGHT_MM)
    height_mm = MAX_HEIGHT_MM;

  return (uint16_t)height_mm;
}

// Calculate target position in steps from height in mm
// Coordinate system: Home (calibration position at sensor) = 0 steps = z=2800mm
// (top) z=2800mm -> 0 steps (stay at home/top), z=0mm -> -X steps (move to
// bottom)
void calcTargetPosition(uint16_t height_mm) {
  // Invert: z=2800 -> 0 steps, z=0 -> -2800mm worth of steps
  int32_t distance_from_home = (int32_t)MAX_HEIGHT_MM - (int32_t)height_mm;
  float rev = (float)distance_from_home / (2.0f * PI * WINCH_RADIUS);
  targetPosition =
      -(long)(rev * (float)STEPS_PER_REV); // Negative = away from sensor (down)
}

// =============================================================================
// Temperature Sensor Functions
// =============================================================================

// Initialize ESP32-S3 temperature sensor
bool initTemperatureSensor() {
  temperature_sensor_config_t temp_sensor_config =
      TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  esp_err_t err = temperature_sensor_install(&temp_sensor_config, &temp_sensor);

  if (err != ESP_OK) {
    DEBUG_PRINTF("[TEMP] Failed to install temperature sensor: %d\n", err);
    return false;
  }

  err = temperature_sensor_enable(temp_sensor);
  if (err != ESP_OK) {
    DEBUG_PRINTF("[TEMP] Failed to enable temperature sensor: %d\n", err);
    return false;
  }

  DEBUG_PRINTLN("[TEMP] Temperature sensor initialized");
  return true;
}

// Read chip temperature in Celsius
float readChipTemperature() {
  if (temp_sensor == NULL) {
    return 0.0f;
  }

  float tsens_out;
  esp_err_t err = temperature_sensor_get_celsius(temp_sensor, &tsens_out);

  if (err != ESP_OK) {
    DEBUG_PRINTF("[TEMP] Failed to read temperature: %d\n", err);
    return 0.0f;
  }

  return tsens_out;
}

// =============================================================================
// OTA Update Functions (V1 implementation)
// =============================================================================

// HTTP header parsing helper
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

// Execute OTA update from S3
void execOTA() {
  DEBUG_PRINTLN("[OTA] Connecting to: " + otaHost);
  OTAprogress = true;

  // Stagger connection by device ID to avoid network overload (60 devices)
  unsigned long delayMs = deviceIdNumber * 2000;
  DEBUG_PRINTF("[OTA] Waiting %lu ms before starting...\n", delayMs);
  delay(delayMs);

  WiFiClient http;
  if (http.connect(otaHost.c_str(), otaPort)) {
    DEBUG_PRINTLN("[OTA] Fetching: " + otaBin);
    http.print(String("GET ") + otaBin + " HTTP/1.1\r\n" + "Host: " + otaHost +
               "\r\n" + "Cache-Control: no-cache\r\n" +
               "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (http.available() == 0) {
      if (millis() - timeout > 10000) {
        DEBUG_PRINTLN("[OTA] HTTP Timeout!");
        http.stop();
        OTAprogress = false;
        return;
      }
    }

    // Parse HTTP headers
    while (http.available()) {
      String line = http.readStringUntil('\n');
      line.trim();
      if (!line.length()) {
        break; // Headers end
      }
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          DEBUG_PRINTLN("[OTA] Non-200 status code. Exiting.");
          http.stop();
          OTAprogress = false;
          return;
        }
      }
      if (line.startsWith("Content-Length: ")) {
        contentLength =
            atol((getHeaderValue(line, "Content-Length: ")).c_str());
        DEBUG_PRINTF("[OTA] Content-Length: %ld bytes\n", contentLength);
      }
      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        DEBUG_PRINTLN("[OTA] Content-Type: " + contentType);
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    }
  } else {
    DEBUG_PRINTLN("[OTA] Connection to " + otaHost + " failed.");
    if (retryCount < maxRetry) {
      retryCount++;
      DEBUG_PRINTF("[OTA] Retry %d/%d\n", retryCount, maxRetry);
      execOTA();
    } else {
      DEBUG_PRINTLN("[OTA] Max retries reached. Restarting...");
      ESP.restart();
    }
    return;
  }

  DEBUG_PRINTF("[OTA] Content valid: %d, Length: %ld\n", isValidContentType,
               contentLength);

  // Flash firmware
  if (contentLength && isValidContentType) {
    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      DEBUG_PRINTLN("[OTA] Starting update. This may take 2-3 minutes...");
      size_t written = Update.writeStream(http);

      if (written == contentLength) {
        DEBUG_PRINTF("[OTA] Written %d bytes successfully\n", written);
      } else {
        DEBUG_PRINTF("[OTA] Written only %d/%ld bytes\n", written,
                     contentLength);
      }

      if (Update.end()) {
        DEBUG_PRINTLN("[OTA] Update completed!");
        if (Update.isFinished()) {
          DEBUG_PRINTLN("[OTA] Success! Rebooting...");
          delay(1000);
          ESP.restart();
        } else {
          DEBUG_PRINTLN("[OTA] Update not finished. Restarting...");
          ESP.restart();
        }
      } else {
        DEBUG_PRINTF("[OTA] Error #%d\n", Update.getError());
        ESP.restart();
      }
    } else {
      DEBUG_PRINTLN("[OTA] Not enough space for update");
      http.stop();
      OTAprogress = false;
    }
  } else {
    DEBUG_PRINTLN("[OTA] Invalid content or empty response");
    http.stop();
    OTAprogress = false;
  }
}

// =============================================================================
// Motor Control Functions (from v1 FU_WINCH_RP2040_U2)
// =============================================================================

// Initialize TMC2209 driver
bool initTMC2209() {
  DEBUG_PRINTLN("[MOTOR] Initializing TMC2209...");

  // Pin initialization
  pinMode(PIN_TMC2209_EN, OUTPUT);
  pinMode(PIN_TMC2209_STEP, OUTPUT);
  pinMode(PIN_TMC2209_DIR, OUTPUT);

  // Disable motor initially
  digitalWrite(PIN_TMC2209_EN, HIGH);

  // Initialize TMC2209 UART
  TMC_SERIAL.begin(115200, SERIAL_8N1, PIN_TMC2209_RX, PIN_TMC2209_TX);

  // TMC2209 driver configuration
  driver.begin();
  driver.pdn_disable(true);       // Use UART control
  driver.I_scale_analog(false);   // Use UART current setting
  driver.rms_current(1200, 1.0f); // 1000mA RMS current
  driver.microsteps(32);          // 32 microsteps

  // StealthChop optimization for quiet operation (anti-humming)
  driver.pwm_autoscale(true);   // Enable automatic current scaling
  driver.pwm_autograd(true);    // Enable automatic gradient adaptation
  driver.en_spreadCycle(false); // StealthChop mode (quiet)

  // PWM configuration for reduced audible noise
  driver.pwm_freq(0b01); // PWM frequency: 35.1kHz (above audible range)
  driver.pwm_reg(4);     // PWM regulation strength
  driver.pwm_ofs(36);    // PWM offset for smooth standstill
  driver.pwm_grad(14);   // PWM gradient for torque regulation

  // TPWMTHRS: StealthChop upper velocity threshold
  // Higher value = StealthChop active at higher speeds (quieter operation)
  driver.TPWMTHRS(
      0); // 0 = StealthChop always active (no SpreadCycle transition)

  // Disable CoolStep (not needed for StealthChop-only mode)
  driver.semin(0); // Disable CoolStep

  // AccelStepper configuration
  stepper.setMaxSpeed(MOTOR_MAX_SPEED);
  stepper.setAcceleration(MOTOR_MAX_ACCEL);

  // Verify communication - MUST be version 0x21 for valid TMC2209
  uint32_t version = driver.version();
  DEBUG_PRINTF("[MOTOR] TMC2209 version: 0x%02X\n", version);

  if (version == 0xFFFFFFFF) {
    DEBUG_PRINTLN("[MOTOR] TMC2209 communication error! (no response)");
    tmcCommunicationOK = false;
    return false;
  }

  if (version != 0x21) {
    DEBUG_PRINTF("[MOTOR] TMC2209 unexpected version: 0x%02X (expected 0x21)\n",
                 version);
    tmcCommunicationOK = false;
    return false;
  }

  // Version 0x21 confirmed - communication OK
  DEBUG_PRINTLN("[MOTOR] TMC2209 communication OK (version 0x21 confirmed)");
  tmcCommunicationOK = true;

  DEBUG_PRINT("[MOTOR] Microsteps: ");
  DEBUG_PRINTLN(driver.microsteps());
  DEBUG_PRINT("[MOTOR] RMS Current: ");
  DEBUG_PRINT(driver.rms_current());
  DEBUG_PRINTLN(" mA");
  DEBUG_PRINTLN("[MOTOR] StealthChop mode (quiet operation)");

  return true;
}

// =============================================================================
// Brake Control Functions (from winch_esp32s3_dev_test)
// Safety sequence: Motor ENABLE first -> Brake UNLOCK
//                 Brake LOCK first -> Motor DISABLE
// =============================================================================

// Unlock brake (allow motor movement)
void brakeUnlock() {
  if (!brakeUnlocked) {
    digitalWrite(PIN_SOLENOID, HIGH); // Active LOW: HIGH = unlock
    brakeUnlocked = true;
    DEBUG_PRINTLN("[BRAKE] UNLOCKED (motor can move)");
  }
}

// Lock brake (hold position)
void brakeLock() {
  if (brakeUnlocked) {
    digitalWrite(PIN_SOLENOID, LOW); // Active LOW: LOW = lock
    brakeUnlocked = false;
    DEBUG_PRINTLN("[BRAKE] LOCKED (holding position)");
  }
}

// Enable motor with safety sequence
void setMotorEnable(bool enable) {
  if (enable && !motorEnabled) {
    // SAFETY: Enable motor FIRST, then unlock brake
    digitalWrite(PIN_TMC2209_EN, LOW);
    motorEnabled = true;
    DEBUG_PRINTLN("[MOTOR] ENABLED (holding torque active)");
    delay(100); // Wait for motor current to stabilize

    brakeUnlock();
    delay(100); // Wait for brake to release
  } else if (!enable && motorEnabled) {
    // SAFETY: Lock brake FIRST, then disable motor
    brakeLock();
    delay(200); // Wait for brake to engage

    digitalWrite(PIN_TMC2209_EN, HIGH);
    motorEnabled = false;
    DEBUG_PRINTLN("[MOTOR] DISABLED (safe state)");
  }
}

// =============================================================================
// Calibration Functions (from winch_rp2040_eval)
// Process: Move toward sensor at slow speed, detect, offset, set zero
// =============================================================================

// Check if sensor is triggered (LOW = triggered due to INPUT_PULLUP)
bool isSensorTriggered() { return digitalRead(PIN_SENSOR) == LOW; }

// Perform calibration (blocking function - call from loop when requested)
// EndSwitch only homing (StallGuard disabled for reliability)
void performCalibration() {
  if (isCalibrating)
    return; // Already calibrating

  DEBUG_PRINTLN("\n[CALIBRATION] ========== STARTING (EndSwitch only) "
                "==========");
  isCalibrating = true;
  isCalibrated = false;
  calibrationStartTime = millis();

  // Enable motor and unlock brake
  setMotorEnable(true);
  delay(200);

  // Set slow speed for homing
  stepper.setMaxSpeed(HOMING_SPEED);
  stepper.setAcceleration(MOTOR_MAX_ACCEL);

  // Move toward sensor (positive direction = toward end switch)
  DEBUG_PRINTLN("[CALIBRATION] Moving toward sensor (EndSwitch only)...");
  stepper.move(1000000); // Large positive move

  // Run until EndSwitch triggered or timeout
  while (true) {
    stepper.run();
    delayMicroseconds(50);

    // EndSwitch detection
    if (isSensorTriggered()) {
      DEBUG_PRINTLN("[CALIBRATION] EndSwitch detected!");
      break;
    }

    // Check timeout
    if (millis() - calibrationStartTime > CALIBRATION_TIMEOUT_MS) {
      DEBUG_PRINTLN("[CALIBRATION] ERROR: Timeout - no sensor detected!");
      stepper.stop();
      isCalibrating = false;
      stepper.setMaxSpeed(MOTOR_MAX_SPEED);
      return;
    }

    // Allow MQTT to stay connected
    mqttClient.loop();
    delay(1); // Yield
  }

  // Stop immediately
  stepper.stop();

  // Wait for motor to fully stop
  while (stepper.isRunning()) {
    stepper.run();
    delayMicroseconds(50);
  }
  delay(100);

  // Move offset distance away from sensor
  DEBUG_PRINTF("[CALIBRATION] Moving offset: %d steps\n",
               CALIBRATION_OFFSET_STEPS);
  stepper.move(CALIBRATION_OFFSET_STEPS);

  while (stepper.isRunning()) {
    stepper.run();
    delayMicroseconds(50);
    mqttClient.loop();
    delay(1);
  }

  // Set current position as zero (home position)
  // Home position = sensor location = MAX_HEIGHT_MM (2800mm top position)
  stepper.setCurrentPosition(0);
  targetPosition = 0;
  targetZ_mm = MAX_HEIGHT_MM; // Calibration complete at top (2800mm)

  // Restore normal speed
  stepper.setMaxSpeed(MOTOR_MAX_SPEED);

  isCalibrating = false;
  isCalibrated = true;

  // Update motor status to OK after successful calibration
  motorStatus = APP_MOTOR_CALIBRATED;
  updateStatusLed();

  unsigned long duration = millis() - calibrationStartTime;
  DEBUG_PRINTF("[CALIBRATION] ========== COMPLETE ==========\n");
  DEBUG_PRINTLN("[CALIBRATION] Method: EndSwitch");
  DEBUG_PRINTF("[CALIBRATION] Duration: %lu ms\n", duration);
  DEBUG_PRINTLN("[CALIBRATION] Position set to 0 (home)\n");
}

// =============================================================================
// Standalone Mode (WiFi接続失敗時のフォールバック動作)
// Random up/down movement for standalone operation without WiFi/MQTT
// =============================================================================
void runStandaloneMode() {
  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("[STANDALONE] WiFi connection failed!");
  DEBUG_PRINTLN("[STANDALONE] Starting random movement mode");
  DEBUG_PRINTLN("[STANDALONE] Range: " + String(STANDALONE_MIN_HEIGHT_MM) +
                "-" + String(STANDALONE_MAX_HEIGHT_MM) + "mm");
  DEBUG_PRINTLN("========================================\n");

  // Initialize random seed with current time
  randomSeed(micros());

  // Enable motor and unlock brake (SAFETY: Only if TMC2209 is OK)
  if (tmcCommunicationOK) {
    setMotorEnable(true);
    delay(200);

    // Set motor to normal speed
    stepper.setMaxSpeed(MOTOR_MAX_SPEED);
    stepper.setAcceleration(MOTOR_MAX_ACCEL);

    DEBUG_PRINTLN("[STANDALONE] Motor enabled - starting movement loop");
  } else {
    DEBUG_PRINTLN("[STANDALONE] ERROR: TMC2209 not available - cannot run "
                  "standalone mode");
    return;
  }

  // Infinite loop: random movement
  while (true) {
    // Generate random target height
    uint16_t targetHeight =
        random(STANDALONE_MIN_HEIGHT_MM, STANDALONE_MAX_HEIGHT_MM + 1);
    long targetSteps = heightToSteps(targetHeight);

    DEBUG_PRINTF("\n[STANDALONE] Moving to: %d mm (%ld steps)\n", targetHeight,
                 targetSteps);

    // Set target position
    stepper.moveTo(targetSteps);

    // Move to target (with sensor safety check)
    while (stepper.distanceToGo() != 0) {
      stepper.run();
      delayMicroseconds(50);

      // Update sensor LED state
      bool currentSensorState = isSensorTriggered();
      if (currentSensorState != lastSensorState) {
        digitalWrite(PIN_SENSOR_LED, currentSensorState ? HIGH : LOW);
        DEBUG_PRINTF("[SENSOR LED] %s\n", currentSensorState
                                              ? "ON (sensor triggered)"
                                              : "OFF (sensor open)");
        lastSensorState = currentSensorState;
      }

      // SAFETY: Stop if sensor triggered (emergency stop)
      if (isSensorTriggered()) {
        stepper.stop();
        DEBUG_PRINTLN(
            "[STANDALONE] SAFETY: Sensor triggered - movement stopped!");
        delay(2000); // Wait before continuing
        break;
      }

      // Yield to watchdog every 100 steps
      if (stepper.currentPosition() % 100 == 0) {
        delay(1);
      }
    }

    long currentPos = stepper.currentPosition();
    uint16_t currentHeight = stepsToHeight(currentPos);
    DEBUG_PRINTF("[STANDALONE] Reached: %d mm (%ld steps)\n", currentHeight,
                 currentPos);

    // Generate random wait time
    unsigned long waitTime =
        random(STANDALONE_MIN_WAIT_MS, STANDALONE_MAX_WAIT_MS + 1);
    DEBUG_PRINTF("[STANDALONE] Waiting: %lu ms\n", waitTime);

    // Wait with sensor LED updates
    unsigned long waitStart = millis();
    while (millis() - waitStart < waitTime) {
      // Update sensor LED state during wait
      bool currentSensorState = isSensorTriggered();
      if (currentSensorState != lastSensorState) {
        digitalWrite(PIN_SENSOR_LED, currentSensorState ? HIGH : LOW);
        DEBUG_PRINTF("[SENSOR LED] %s\n", currentSensorState
                                              ? "ON (sensor triggered)"
                                              : "OFF (sensor open)");
        lastSensorState = currentSensorState;
      }
      delay(100);
    }
  }
}

// Set target height (called from MQTT callback)
void setTargetHeight(uint16_t height_mm) {
  // SAFETY: Reject values above calibration home position (2800mm)
  // This prevents the winch from moving above the end sensor position
  if (height_mm > MAX_HEIGHT_MM) {
    DEBUG_PRINTF(
        "[MOTOR] WARNING: Height %d mm exceeds limit %d mm - IGNORED\n",
        height_mm, MAX_HEIGHT_MM);
    return; // Ignore the command entirely
  }

  targetZ_mm = height_mm;
  calcTargetPosition(height_mm);
  newTargetArrived = true;
  DEBUG_PRINTF("[MOTOR] Target height: %d mm -> %ld steps\n", height_mm,
               targetPosition);
}

// Motor control task (called from loop) - DEPRECATED: Now using FreeRTOS task
// on Core 1
void motorControlTask() {
  if (!motorEnabled)
    return;

  // Update stepper target if new position arrived
  if (newTargetArrived) {
    stepper.moveTo(targetPosition);
    newTargetArrived = false;
  }

  // Run stepper (non-blocking)
  stepper.run();
}

// =============================================================================
// FreeRTOS Motor Task - Runs on Core 1 with high-frequency stepper.run() calls
// Balances motor performance with system stability (MQTT on Core 0)
// =============================================================================
void motorTaskCore1(void *parameter) {
  unsigned long lastYieldTime = millis();
  const unsigned long YIELD_INTERVAL_MS = 10; // 10ms毎にWDTへyield

  for (;;) {
    if (!isCalibrating && motorEnabled) {
      // 新しい目標位置があれば設定
      if (newTargetArrived) {
        stepper.moveTo(targetPosition);
        newTargetArrived = false;
      }

      // 10ms間、50μs毎にstepper.run()を呼び出す
      unsigned long startMicros = micros();
      while ((micros() - startMicros) < 10000) { // 10ms = 10,000μs
        stepper.run();                           // AccelStepperの時間ベース処理
                                                 // delayMicroseconds(10);
      }
    }
    vTaskDelay(1); // WDTフィード（1tick = 1ms）
  }
}

// =============================================================================
// Helper: Queue MQTT Message (Non-blocking)
// =============================================================================
void queueMqttMessage(const char *topic, const char *payload, bool retained) {
  if (mqttQueue == NULL)
    return;

  MqttMessage msg;
  strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
  msg.topic[sizeof(msg.topic) - 1] = '\0';
  strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
  msg.payload[sizeof(msg.payload) - 1] = '\0';
  msg.retained = retained;

  // Send to queue (dont wait if full to avoid blocking main loop)
  if (xQueueSend(mqttQueue, &msg, 0) != pdTRUE) {
    // Queue full - drop packet (better than blocking motor)
    // DEBUG_PRINTLN("[MQTT] Queue Full! Dropping packet.");
  }
}

// =============================================================================
// FreeRTOS MQTT Task - Runs on Core 0 (Network Core)
// Handles WiFi connection, MQTT connection, and Publishing to avoid blocking
// =============================================================================
bool mqttReconnect(); // Forward declaration

void mqttTask(void *parameter) {
  // const unsigned long YIELD_INTERVAL_MS = 10;

  for (;;) {
    unsigned long now = millis();

    // 1. WiFi Handling
    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWiFiRetryTime > WIFI_RETRY_INTERVAL) {
        DEBUG_PRINTLN("[MQTT_TASK] WiFi disconnected, reconnecting...");
        wifiStatus = APP_WIFI_NO_CONNECTION;
        mqttStatus = APP_MQTT_NO_CONNECTION;
        updateStatusLed();
        WiFi.reconnect();
        lastWiFiRetryTime = now;
      }
    }
    // 2. MQTT Handling
    else {
      if (!mqttClient.connected()) {
        if (wifiStatus == APP_WIFI_CONNECTED) {
          mqttStatus = APP_MQTT_CONNECTING;
          updateStatusLed();
        }

        if (now - lastMqttRetryTime > MQTT_RETRY_INTERVAL) {
          mqttReconnect(); // This function accesses mqttClient directly (Safe
                           // here)
          lastMqttRetryTime = now;
        }
      } else {
        // Connected
        mqttClient.loop(); // Process incoming messages

        // Process Outgoing Queue
        MqttMessage msg;
        while (xQueueReceive(mqttQueue, &msg, 0) == pdTRUE) {
          mqttClient.publish(msg.topic, msg.payload, msg.retained);
          // Small yield between publishes if needed
        }
      }
    }

    vTaskDelay(10); // Yield to IDLE task
  }
}

// =============================================================================
// Initialize MAC Address
// =============================================================================
void initMacAddress() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  sprintf(macWithoutColons, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);
  DEBUG_PRINTLN("[MQTT+TUBE] eFuse MAC: " + String(macWithoutColons));

  uint8_t wifiMac[6];
  WiFi.macAddress(wifiMac);
  char wifiMacStr[18];
  sprintf(wifiMacStr, "%02X:%02X:%02X:%02X:%02X:%02X", wifiMac[0], wifiMac[1],
          wifiMac[2], wifiMac[3], wifiMac[4], wifiMac[5]);
  DEBUG_PRINTLN("[MQTT+TUBE] WiFi MAC: " + String(wifiMacStr));
}

// =============================================================================
// WiFi Setup
// =============================================================================
void setupWiFi() {
  DEBUG_PRINTLN("[MQTT+TUBE] Connecting to WiFi: " + String(ssid));
  // Explicitly disconnect and reset WiFi to clear any stale state
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  wifiStatus = APP_WIFI_CONNECTING;
  updateStatusLed();

  DEBUG_PRINTLN("[MQTT+TUBE] Connecting to WiFi (Fresh Start)...");
  WiFi.begin(ssid, password);

  // Extend timeout to 30 seconds (300 * 100ms)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 300) {
    delay(100);
    if (attempts % 10 == 0)
      DEBUG_PRINT(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("\n[MQTT+TUBE] WiFi connected!");
    DEBUG_PRINTLN("[MQTT+TUBE] IP: " + WiFi.localIP().toString());
    wifiStatus = APP_WIFI_CONNECTED;
    // MQTT not connected yet
    mqttStatus = APP_MQTT_NO_CONNECTION;
    updateStatusLed();
  } else {
    DEBUG_PRINTLN("\n[MQTT+TUBE] WiFi FAILED!");
    wifiStatus = APP_WIFI_NO_CONNECTION;
    mqttStatus = APP_MQTT_NO_CONNECTION;
    updateStatusLed();
  }
}

// =============================================================================
// Queue POS value for TUBE (called from MQTT callback - non-blocking)
// =============================================================================
void queuePosForTube(float pitch, float yaw) {
  pendingPitch = pitch;
  pendingYaw = yaw;
  posDirty = true;
}

// =============================================================================
// Queue RGB value for TUBE (called from MQTT callback - non-blocking)
// =============================================================================
void queueRgbForTube(uint8_t r, uint8_t g, uint8_t b) {
  pendingR = r;
  pendingG = g;
  pendingB = b;
  rgbDirty = true;
}

// =============================================================================
// Process TUBE send queue (called from loop() - rate limited)
// =============================================================================
void processTubeQueue() {
  unsigned long now = millis();
  if (now - lastTubeSendTime < TUBE_SEND_INTERVAL) {
    return; // Too soon
  }

  char cmd[32]; // Fixed buffer - no heap allocation

  // Send pending POS if dirty
  if (posDirty) {
    lastTubeSendTime = now;
    snprintf(cmd, sizeof(cmd), "POS,%.2f,%.2f", pendingPitch, pendingYaw);
    TUBE_SERIAL.println(cmd);
    posDirty = false;
    return; // One command per interval
  }

  // Send pending RGB if dirty
  if (rgbDirty) {
    lastTubeSendTime = now;
    snprintf(cmd, sizeof(cmd), "RGB,%d,%d,%d", pendingR, pendingG, pendingB);
    TUBE_SERIAL.println(cmd);
    rgbDirty = false;
  }
}

// =============================================================================
// MQTT Callback
// =============================================================================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  messageCount++;

  DEBUG_PRINTF("[MQTT] Topic: %s, Length: %d\n", topic,
               length); // Re-enabled for OTA debugging

  // Handle ps/<deviceId> - position/orientation (6 bytes)
  if (strncmp(topic, "ps/", 3) == 0 && length == 6) {
    psMessageCount++;
    uint16_t z_mm;
    int16_t pitch_centideg;
    uint16_t yaw_centideg;
    memcpy(&z_mm, payload, 2);
    memcpy(&pitch_centideg, payload + 2, 2);
    memcpy(&yaw_centideg, payload + 4, 2);

    float newZ = z_mm / 1000.0f;
    float newPitch = pitch_centideg / 100.0f;
    float newYaw = yaw_centideg / 100.0f;

    if (newZ != lastZ || newPitch != lastPitch || newYaw != lastYaw) {
      lastZ = newZ;
      lastPitch = newPitch;
      lastYaw = newYaw;
      // DEBUG_PRINTF("[ps] z=%.3fm pitch=%.2f° yaw=%.2f°\n", lastZ, lastPitch,
      // lastYaw);  // Temporarily disabled

      // Convert z (meters) to motor position (mm)
      // z_mm is already in millimeters from binary protocol
      setTargetHeight(z_mm);

      // Queue POS for TUBE (non-blocking)
      queuePosForTube(newPitch, newYaw);
    }
    return;
  }

  // Handle cl/<deviceId> - color (3 bytes: R, G, B)
  if (strncmp(topic, "cl/", 3) == 0 && length == 3) {
    clMessageCount++;
    uint8_t newR = payload[0];
    uint8_t newG = payload[1];
    uint8_t newB = payload[2];

    if (newR != lastR || newG != lastG || newB != lastB) {
      lastR = newR;
      lastG = newG;
      lastB = newB;
      // DEBUG_PRINTF("[cl] R=%d G=%d B=%d\n", lastR, lastG, lastB);  //
      // Temporarily disabled

      // Set local onboard LED
      showLED(lastR, lastG, lastB);

      // Queue RGB for TUBE (non-blocking)
      queueRgbForTube(lastR, lastG, lastB);
    }
    return;
  }

  // Handle dl/<deviceId> - 3W LED PWM (1 byte: 0-255)
  if (strncmp(topic, "dl/", 3) == 0 && length == 1) {
    dlMessageCount++;
    uint8_t newDL = payload[0];

    if (newDL != lastDL) {
      lastDL = newDL;
      // DEBUG_PRINTF("[dl] PWM=%d\n", lastDL);  // Temporarily disabled

      // Set 3W LED PWM
      setSpotlightPWM(lastDL);
    }
    return;
  }

  // Handle rb/<deviceId> - Reboot/Calibration command
  if (strncmp(topic, "rb/", 3) == 0) {
    DEBUG_PRINTLN("[rb] Calibration requested via MQTT");
    calibrationRequested = true;
    return;
  }

  // Handle "ota" - OTA firmware update command (V1 implementation)
  DEBUG_PRINTF("[MQTT] Checking OTA topic: strcmp('%s', 'ota') = %d\n", topic,
               strcmp(topic, "ota"));
  if (strcmp(topic, "ota") == 0) {
    DEBUG_PRINTLN("[MQTT] ✅ OTA command MATCHED! Starting OTA update...");

    // Flash spotlight LED to indicate OTA start (5 flashes, 100ms interval)
    spotlightFlash(5, 100);

    DEBUG_PRINTLN("[MQTT] Calling execOTA()...");
    execOTA();
    execOTA();
    return;
  }

  // Handle "ota_tube" - Trigger OTA for Tube Units (Unit 1 & Unit 2)
  if (strcmp(topic, "ota_tube") == 0) {
    DEBUG_PRINTLN("[MQTT] 🚀 OTA for TUBE triggered via MQTT!");
    // Status LED notification (Purple flash)
    for (int i = 0; i < 3; i++) {
      showLED(50, 0, 50);
      delay(100);
      showLED(0, 0, 0);
      delay(100);
    }
    TUBE_SERIAL.println("OTA,START");
    return;
  }

  // Handle "ota1" - Trigger OTA for Unit 1 Only
  if (strcmp(topic, "ota1") == 0) {
    DEBUG_PRINTLN("[MQTT] 🚀 OTA for UNIT 1 triggered via MQTT!");
    // Status LED notification (Cyan flash)
    for (int i = 0; i < 3; i++) {
      showLED(0, 50, 50);
      delay(100);
      showLED(0, 0, 0);
      delay(100);
    }
    TUBE_SERIAL.println("OTA,UNIT1");
    return;
  }

  // Handle "ota2" - Trigger OTA for Unit 2 Only
  if (strcmp(topic, "ota2") == 0) {
    DEBUG_PRINTLN("[MQTT] 🚀 OTA for UNIT 2 triggered via MQTT!");
    // Status LED notification (Magenta flash)
    for (int i = 0; i < 3; i++) {
      showLED(50, 0, 50);
      delay(100);
      showLED(0, 0, 0);
      delay(100);
    }
    TUBE_SERIAL.println("OTA,UNIT2");
    return;
  }

  // Handle idxy topic (device ID assignment)
  char idxyTopic[32];
  snprintf(idxyTopic, sizeof(idxyTopic), "%s/idxy", macWithoutColons);
  if (strcmp(topic, idxyTopic) == 0) {
    char message[64];
    if (length >= sizeof(message))
      length = sizeof(message) - 1;
    strncpy(message, (char *)payload, length);
    message[length] = '\0';

    char *token = strtok(message, ",");
    if (token != NULL) {
      strncpy(deviceId, token, sizeof(deviceId) - 1);
      deviceIdNumber = (uint8_t)atoi(token);
      DEBUG_PRINTLN("[MQTT+TUBE] Device ID: " + String(deviceId));
    }
    idAcquired = true;

    // Subscribe to ps/<deviceId>, cl/<deviceId>, dl/<deviceId>, rb/<deviceId>
    char psTopic[64];
    snprintf(psTopic, sizeof(psTopic), "ps/%s", deviceId);
    mqttClient.subscribe(psTopic, 0);
    DEBUG_PRINTLN("[MQTT+TUBE] Subscribed: " + String(psTopic));

    char clTopic[64];
    snprintf(clTopic, sizeof(clTopic), "cl/%s", deviceId);
    mqttClient.subscribe(clTopic, 0);
    DEBUG_PRINTLN("[MQTT+TUBE] Subscribed: " + String(clTopic));

    char dlTopic[64];
    snprintf(dlTopic, sizeof(dlTopic), "dl/%s", deviceId);
    mqttClient.subscribe(dlTopic, 0);
    DEBUG_PRINTLN("[MQTT+TUBE] Subscribed: " + String(dlTopic));

    char rbTopic[64];
    snprintf(rbTopic, sizeof(rbTopic), "rb/%s", deviceId);
    mqttClient.subscribe(rbTopic, 0);
    DEBUG_PRINTLN("[MQTT+TUBE] Subscribed: " + String(rbTopic));
    return;
  }

  // Log other messages
  char message[64];
  if (length >= sizeof(message))
    length = sizeof(message) - 1;
  strncpy(message, (char *)payload, length);
  message[length] = '\0';
  DEBUG_PRINTLN("[MQTT+TUBE] " + String(topic) + " -> " + String(message));
}
// MQTT Reconnect (Non-blocking attempt)
// Returns true if connected, false if failed
// =============================================================================
bool mqttReconnect() {
  DEBUG_PRINTLN("[MQTT+TUBE] Attempting MQTT connection...");
  String clientId = "ESP32S3-TUBE-" + String(random(0xffff), HEX);

  if (mqttClient.connect(clientId.c_str())) {
    DEBUG_PRINTLN("[MQTT+TUBE] MQTT connected!");
    // Keep WiFi status as CONNECTED, set MQTT to CONNECTED
    wifiStatus = APP_WIFI_CONNECTED;
    mqttStatus = APP_MQTT_CONNECTED;
    updateStatusLed();
    connectTime = millis();
    messageCount = 0;
    psMessageCount = 0;
    clMessageCount = 0;
    dlMessageCount = 0;

    // Subscribe to MAC/idxy topic
    char idxyTopic[32];
    snprintf(idxyTopic, sizeof(idxyTopic), "%s/idxy", macWithoutColons);
    mqttClient.subscribe(idxyTopic, 1);
    DEBUG_PRINTLN("[MQTT+TUBE] Subscribed: " + String(idxyTopic));

    // Subscribe to "ota" for OTA updates (V1 implementation)
    bool otaSubResult = mqttClient.subscribe("ota", 0);
    DEBUG_PRINTF("[MQTT+TUBE] Subscribe 'ota' result: %s\n",
                 otaSubResult ? "SUCCESS" : "FAILED");

    // Subscribe to "ota1" for Unit 1
    bool ota1SubResult = mqttClient.subscribe("ota1", 0);
    DEBUG_PRINTF("[MQTT+TUBE] Subscribe 'ota1' result: %s\n",
                 ota1SubResult ? "SUCCESS" : "FAILED");

    // Subscribe to "ota2" for Unit 2
    bool ota2SubResult = mqttClient.subscribe("ota2", 0);
    DEBUG_PRINTF("[MQTT+TUBE] Subscribe 'ota2' result: %s\n",
                 ota2SubResult ? "SUCCESS" : "FAILED");

    // Publish to ini/<MAC> to request device ID
    char iniTopic[32];
    snprintf(iniTopic, sizeof(iniTopic), "ini/%s", macWithoutColons);
    mqttClient.publish(iniTopic, "init", false);
    DEBUG_PRINTLN("[MQTT+TUBE] Published: " + String(iniTopic));

    // Re-subscribe if ID already acquired
    if (idAcquired && deviceId[0] != '\0') {
      char psTopic[64];
      snprintf(psTopic, sizeof(psTopic), "ps/%s", deviceId);
      mqttClient.subscribe(psTopic, 0);
      DEBUG_PRINTLN("[MQTT+TUBE] Re-subscribed: " + String(psTopic));

      char clTopic[64];
      snprintf(clTopic, sizeof(clTopic), "cl/%s", deviceId);
      mqttClient.subscribe(clTopic, 0);
      DEBUG_PRINTLN("[MQTT+TUBE] Re-subscribed: " + String(clTopic));

      char dlTopic[64];
      snprintf(dlTopic, sizeof(dlTopic), "dl/%s", deviceId);
      mqttClient.subscribe(dlTopic, 0);
      DEBUG_PRINTLN("[MQTT+TUBE] Re-subscribed: " + String(dlTopic));

      char rbTopic[64];
      snprintf(rbTopic, sizeof(rbTopic), "rb/%s", deviceId);
      mqttClient.subscribe(rbTopic, 0);
      DEBUG_PRINTLN("[MQTT+TUBE] Re-subscribed: " + String(rbTopic));
    }
    return true;
  } else {
    DEBUG_PRINTLN("[MQTT+TUBE] MQTT failed, rc=" + String(mqttClient.state()));
    return false;
  }
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
  // SAFETY: Initialize brake FIRST (locked state)
  pinMode(PIN_SOLENOID, OUTPUT);
  digitalWrite(PIN_SOLENOID, LOW); // Lock brake (active LOW)

  DEBUG_BEGIN(115200);
  delay(500);

  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("MQTT + TUBE UART + Motor + Brake + Calibration");
  DEBUG_PRINTLN("Purpose: MQTT -> TUBE LED + Motor + Brake + Homing");
  DEBUG_PRINTLN("========================================\n");
  DEBUG_PRINTLN("[BRAKE] Initialized in LOCKED state (safety)");

  // End sensor initialization
  pinMode(PIN_SENSOR, INPUT_PULLUP);
  pinMode(PIN_SENSOR_LED, OUTPUT);

  // Set initial LED state based on current sensor state
  lastSensorState = isSensorTriggered();
  digitalWrite(PIN_SENSOR_LED, lastSensorState ? HIGH : LOW);

  DEBUG_PRINT("[SENSOR] Initialized on GPIO ");
  DEBUG_PRINT(PIN_SENSOR);
  DEBUG_PRINT(" - Current state: ");
  DEBUG_PRINTLN(lastSensorState ? "TRIGGERED" : "OPEN");
  DEBUG_PRINT("[SENSOR LED] Initialized on GPIO ");
  DEBUG_PRINT(PIN_SENSOR_LED);
  DEBUG_PRINT(" - Initial state: ");
  DEBUG_PRINTLN(lastSensorState ? "ON" : "OFF");

  // TUBE UART initialization
  // TUBE UART initialization
  TUBE_SERIAL.begin(38400, SERIAL_8N1, PIN_TUBE_RX, PIN_TUBE_TX);
  DEBUG_PRINTLN("[MQTT+TUBE] TUBE UART enabled @ 38400");

  // LED initialization (Onboard LED unused)
  // pixel.begin();
  // showLED(0, 0, 0);
  // DEBUG_PRINTLN("[MQTT+TUBE] Onboard NeoPixel disabled (unused)");

  // Status LED initialization (4x WS2812B on GPIO16)
  // Status LED initialization (4x WS2812B on GPIO16)
  statusLed.begin();
  statusLed.setBrightness(50); // Reduce brightness to avoid blinding

  // Initial States
  wifiStatus = APP_WIFI_NO_CONNECTION;
  mqttStatus = APP_MQTT_NO_CONNECTION;
  motorStatus = APP_MOTOR_INIT_FAIL;
  tubeStatus = APP_TUBE_NO_CONNECTION;

  updateStatusLed();
  DEBUG_PRINTLN("[STATUS LED] 4x WS2812B initialized on GPIO16");

  // 3W LED PWM initialization
  ledcAttach(PIN_LED_PWM, PWM_FREQ, PWM_RESOLUTION);
  setSpotlightPWM(0); // Start with spotlight off
  DEBUG_PRINTLN("[MQTT+TUBE] 3W LED PWM initialized");

  // Temperature sensor initialization
  initTemperatureSensor();
  float initialTemp = readChipTemperature();
  DEBUG_PRINTF("[TEMP] Initial chip temperature: %.1f°C\n", initialTemp);

  // Motor initialization
  if (initTMC2209()) {
    DEBUG_PRINTLN("[MOTOR] TMC2209 initialized successfully");
    setMotorEnable(true);                // Enable motor after initialization
    motorStatus = APP_MOTOR_CALIBRATING; // Ready for calibration (Yellow)
  } else {
    DEBUG_PRINTLN("[MOTOR] TMC2209 initialization failed!");
    motorStatus = APP_MOTOR_INIT_FAIL; // Red
  }
  updateStatusLed();

  setupWiFi();
  initMacAddress();

  // V1 OTA: No initialization needed - execOTA() is called directly from MQTT
  // callback

  // Check WiFi connection status
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("\n[SETUP] WiFi connection FAILED");

#if STANDALONE_MODE_ENABLED
    DEBUG_PRINTLN("[SETUP] STANDALONE_MODE_ENABLED = true - switching to "
                  "STANDALONE mode");

    // Auto-calibration before standalone mode (if TMC2209 is OK)
    if (tmcCommunicationOK) {
      DEBUG_PRINTLN("\n[CALIBRATION] TMC2209 version 0x21 confirmed - starting "
                    "auto-calibration...");
      performCalibration();
    } else {
      DEBUG_PRINTLN("\n[CALIBRATION] SKIPPED - TMC2209 communication not "
                    "established (version 0x21 not confirmed)");
    }

    // Enter standalone mode (infinite loop - never returns)
    runStandaloneMode();

    // This point will never be reached
    while (1) {
      delay(1000);
    }
#else
    DEBUG_PRINTLN("[SETUP] STANDALONE_MODE_ENABLED = false - halting system");
    DEBUG_PRINTLN("[SETUP] Please check WiFi configuration and reboot");

    // Set WiFi status LED to error state
    wifiStatus = APP_WIFI_NO_CONNECTION;
    motorStatus = APP_MOTOR_INIT_FAIL;
    updateStatusLed();

    // Halt the system (infinite loop - manual reboot required)
    while (1) {
      delay(1000);
      DEBUG_PRINTLN(
          "[SETUP] System halted - WiFi connection required but failed");
    }
#endif
  }

  // WiFi connected - continue with normal MQTT operation
  DEBUG_PRINTLN("\n[SETUP] WiFi connected - continuing with MQTT setup");

  // Auto-calibration ONLY if TMC2209 communication confirmed (version 0x21)
  if (tmcCommunicationOK) {
    DEBUG_PRINTLN("\n[CALIBRATION] TMC2209 version 0x21 confirmed - starting "
                  "auto-calibration...");
    performCalibration();
  } else {
    DEBUG_PRINTLN("\n[CALIBRATION] SKIPPED - TMC2209 communication not "
                  "established (version 0x21 not confirmed)");
  }

  // Create FreeRTOS task for motor control on Core 1
  // Uses micros()-based tight loop for high-frequency stepper calls (~20kHz)
  // while periodically yielding to feed WDT
  xTaskCreatePinnedToCore(
      motorTaskCore1, // Task function
      "MotorTask",    // Task name
      4096,           // Stack size (bytes)
      NULL,           // Task parameters
      2,              // Priority (higher than default Arduino loop which is 1)
      &motorTaskHandle, // Task handle
      1                 // Core 1 (Arduino loop runs on Core 0 for ESP32-S3)
  );
  DEBUG_PRINTLN("[MOTOR TASK] Created on Core 1 with micros()-based "
                "high-frequency timing");

  // MQTT setup
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(30);

  // Create MQTT Task on Core 0
  mqttQueue = xQueueCreate(20, sizeof(MqttMessage)); // Buffer 20 messages
  xTaskCreatePinnedToCore(mqttTask, "MQTT", 4096, NULL, 1, &mqttTaskHandle, 0);
  DEBUG_PRINTLN("[MQTT TASK] Created on Core 0");

  DEBUG_PRINTLN("[MQTT+TUBE] Setup complete. Starting main loop...\n");

  // Send initial command to TUBE - DISABLED FOR DEBUGGING
  // Send initial command to TUBE - DISABLED FOR DEBUGGING
  // TUBE_SERIAL.println("POS,0,0");
  DEBUG_PRINTLN("[MQTT+TUBE] Initial POS command DISABLED for debugging");

  // Startup notification: Fade in/out spotlight LED (3 seconds)
  DEBUG_PRINTLN("\n[STARTUP] Winch ready - spotlight fade in/out...");
  spotlightFadeInOut(3000); // 3 seconds fade in/out
  DEBUG_PRINTLN("[STARTUP] Spotlight notification complete\n");
}

// =============================================================================
// Main Loop
// =============================================================================
// --- Helper: Process Tube Line ---
void processTubeLine(char *line) {
  // 1. Version Report: "VER,..."
  if (String(line).startsWith("VER,")) {
    static unsigned long last_mqtt_ver = 0;
    if (millis() - last_mqtt_ver > 5000) {
      last_mqtt_ver = millis();
      char topic[64];
      snprintf(topic, sizeof(topic), "fu/winch/version/tube");
      queueMqttMessage(topic, line, true); // Retained
      DEBUG_PRINTLN("[MQTT] Published Tube Versions: " + String(line));
    }
  }
  // 2. IMU Data: "Q,w,x,y,z"
  else if (line[0] == 'Q') {
    if (mqttStatus == APP_MQTT_CONNECTED) {
      float w, x, y, z;
      if (sscanf(line, "Q,%f,%f,%f,%f", &w, &x, &y, &z) == 4) {
        // Construct Full Heartbeat Payload with IMU
        char heartbeatTopic[64];
        snprintf(heartbeatTopic, sizeof(heartbeatTopic),
                 "fu/device/%s/heartbeat", macWithoutColons);

        float chipTemp = readChipTemperature();
        int8_t rssi = WiFi.RSSI();
        unsigned long uptime = (millis() - connectTime) / 1000;

        // JSON with "q" array
        char payload[256];
        snprintf(payload, sizeof(payload),
                 "{\"id\":%d,\"fw\":\"%s\",\"cal\":%d,\"temp\":%.1f,\"rssi\":%"
                 "d,\"up\":%lu,\"q\":[%.4f,%.4f,%.4f,%.4f]}",
                 deviceIdNumber, FIRMWARE_VERSION, isCalibrated ? 1 : 0,
                 chipTemp, rssi, uptime, w, x, y, z);

        queueMqttMessage(heartbeatTopic, payload, false);
        lastHeartbeatTime =
            millis(); // Reset timer to prevent fallback triggering
      }
    }
  }
}

void loop() {
  unsigned long now = millis();

  // WiFi and MQTT handling moved to FreeRTOS task (mqttTask)
  // Reconnection logic removed from loop() to prevent blocking

  // Heartbeat / Status Logic
  // Strategy:
  // 1. If Tube sends IMU data (10Hz), we wrap it into the Heartbeat and send
  // immediately.
  // 2. If no IMU data for >1000ms (Tube dead/disconnected), we send a
  // fallback Heartbeat (1Hz) without IMU data.

  // Fallback Heartbeat (1Hz) if no IMU triggered recently
  if (now - lastHeartbeatTime >= 1000 && mqttStatus == APP_MQTT_CONNECTED) {
    // Check if we haven't sent a heartbeat (via IMU trigger) recently
    // Ideally lastHeartbeatTime is updated by BOTH triggers.
    // So this condition handles the "Silence" case.

    char heartbeatTopic[64];
    snprintf(heartbeatTopic, sizeof(heartbeatTopic), "fu/device/%s/heartbeat",
             macWithoutColons);

    float chipTemp = readChipTemperature();
    int8_t rssi = WiFi.RSSI();
    unsigned long uptime = (millis() - connectTime) / 1000;

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"id\":%d,\"fw\":\"%s\",\"cal\":%d,\"temp\":%.1f,\"rssi\":%d,"
             "\"up\":%lu}",
             deviceIdNumber, FIRMWARE_VERSION, isCalibrated ? 1 : 0, chipTemp,
             rssi, uptime);

    queueMqttMessage(heartbeatTopic, payload, false);
    lastHeartbeatTime = now;
  }

  // Handle calibration request (blocking - runs full calibration sequence)
  if (calibrationRequested && !isCalibrating) {
    calibrationRequested = false;
    performCalibration();
  }

  // V1 OTA: No process() needed - execOTA() runs synchronously when triggered
  // by MQTT

  // Process TUBE send queue (rate-limited, non-blocking)
  processTubeQueue();

  // --- STANDARD TUBE RX (Without Debug Hex Dump) ---
  while (TUBE_SERIAL.available()) {
    uint8_t b = TUBE_SERIAL.read();
    lastTubeRxTime = millis(); // Keep connection Alive!

    if (b == '\n' || b == '\r') {
      if (tubeRxIndex > 0) {
        tubeRxBuffer[tubeRxIndex] = '\0';
        processTubeLine(tubeRxBuffer);
        tubeRxIndex = 0;
      }
    } else if (tubeRxIndex < (int)sizeof(tubeRxBuffer) - 1) {
      if (b >= 32)
        tubeRxBuffer[tubeRxIndex++] = (char)b;
    }
  }

  // Update TUBE connection status based on lastTubeRxTime
  // Red: No data received yet (lastTubeRxTime == 0) - TUBE_NO_CONNECTION
  // Yellow: Data received but older than TUBE_TIMEOUT_MS (unstable) -
  // TUBE_CONNECTING Blue: Recent data received - TUBE_CONNECTED
  TubeStatus newTubeStatus;
  if (lastTubeRxTime == 0) {
    newTubeStatus = APP_TUBE_NO_CONNECTION;
  } else if (millis() - lastTubeRxTime > TUBE_TIMEOUT_MS) {
    newTubeStatus = APP_TUBE_CONNECTING; // Treat unstable as connecting/waiting
  } else {
    newTubeStatus = APP_TUBE_CONNECTED;
  }
  if (newTubeStatus != tubeStatus) {
    tubeStatus = newTubeStatus;
    updateStatusLed();
  }

  // Update sensor LED only when state changes
  bool currentSensorState = isSensorTriggered();
  if (currentSensorState != lastSensorState) {
    digitalWrite(PIN_SENSOR_LED, currentSensorState ? HIGH : LOW);
    DEBUG_PRINTF("[SENSOR LED] %s\n", currentSensorState
                                          ? "ON (sensor triggered)"
                                          : "OFF (sensor open)");
    lastSensorState = currentSensorState;
  }

  // Status output every 5 seconds
  if (millis() - lastStatusTime >= 5000) {
    unsigned long uptime = (millis() - connectTime) / 1000;
    DEBUG_PRINTF("[MQTT+TUBE] CONNECTED %lus | Total:%lu | ps:%lu | cl:%lu | "
                 "dl:%lu | rc:%d\n",
                 uptime, messageCount, psMessageCount, clMessageCount,
                 dlMessageCount, mqttClient.state());
    if (psMessageCount > 0) {
      DEBUG_PRINTF("  ps: z=%.2f pitch=%.2f yaw=%.2f\n", lastZ, lastPitch,
                   lastYaw);
    }
    if (clMessageCount > 0) {
      DEBUG_PRINTF("  cl: R=%d G=%d B=%d\n", lastR, lastG, lastB);
    }
    if (dlMessageCount > 0) {
      DEBUG_PRINTF("  dl: PWM=%d\n", lastDL);
    }
    // Motor, brake, and calibration status
    if (motorEnabled) {
      DEBUG_PRINTF(
          "  motor: target=%ldsteps pos=%ldsteps dist=%ldsteps brake=%s "
          "cal=%s "
          "sensor=%s\n",
          targetPosition, stepper.currentPosition(), stepper.distanceToGo(),
          brakeUnlocked ? "UNLOCKED" : "LOCKED",
          isCalibrated ? "OK" : (isCalibrating ? "IN_PROGRESS" : "NOT_DONE"),
          isSensorTriggered() ? "TRIGGERED" : "OPEN");
    }
    lastStatusTime = millis();
  }
}
