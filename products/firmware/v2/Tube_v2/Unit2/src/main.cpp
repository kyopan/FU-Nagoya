/* ============================================================================
 *  ______     __         __  __     _ __    ___
 * /_  __/_ __/ /  ___   / / / /__  (_) /_  |_  |
 *  / / / // / _ \/ -_) / /_/ / _ \/ / __/ / __/
 * /_/  \_,_/_.__/\__/  \____/_//_/_/\__/ /____/
 *
 *  FU Product Tube Unit 2 (Motor Master) - Dual Core Edition
 * ============================================================================
 * Core 1 (Arduino Loop): Pitch Control, Comm, State Management
 * Core 0 (Yaw Task): Yaw Motor Control (High Frequency FOC Loop)
 * ============================================================================
 */

#include "YawController.h"
#include <Arduino.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <SimpleFOC.h>
#include <Update.h>
#include <WiFi.h>
#include <Wire.h>

#ifdef FIRMWARE_VERSION
#undef FIRMWARE_VERSION
#endif
#define FIRMWARE_VERSION "2.2.94"

// --- OTA Configuration ---
const char *ota_ssid = "FU";
const char *ota_password = "Spark!!!!!";
const char *ota_host =
    "comone-fragmented-unity-fw.s3.ap-northeast-1.amazonaws.com";
const int ota_port = 80;
const char *ota_bin = "/tube_v2_unit2_product.bin";

/*
 * v2.2.91 Changelog:
 * - Tuning (Stability): Removed LPF (alpha=1.0) to eliminate Phase Lag.
 * - Tuning (PID): Reduced Gains (P=1.0, I=0.0, D=0.05) to prevent
 * Overshoot/Saturation.
 * - Logic: Kept v2.2.90 Polarity (`+target_vel`) as osc amplitude decreased.
 *
 * v2.2.90 Changelog:
 * - Fix (Yaw Oscillation): Added LPF (alpha=0.1) and Deadband (0.2) to Heading
 * input.
 * - Revert (Yaw Polarity): Restored Inverted Polarity (`-target_vel`) per user.
 * - Tuning (Calibration): Slowed down Yaw Cal (8.0) and extended duration
 * (5000ms).
 * - Reliability: Increased `initFOC` retries to 10 for startup stability.
 *
 * v2.2.84 Changelog:
 * - Reliability: Set Serial Timeout to 5ms (was 1000ms) to prevent blocking on
 * noise.
 * - Reliability (Limit): Added strict clamping (-60~60) to avoid "Runaway 90"
 * risk.
 * - Reliability (Init): Added I2C Timeout (1000ms) to prevent Yaw Task
 * deadlock.
 * - Safety: Added `initFOC` success check & retry for Pitch Motor.
 * - Safety: Ignored `P` (Tracking) commands if Pitch is UNCALIBRATED (prevent
 * flywheel spin). v2.2.82 Changelog:
 * - Fix (Command): Corrected `P` command parsing. It was accidentally nested
 * inside `CAL,YAW` block. This fixes the "Pitch not reacting" issue. v2.2.76
 * Changelog:
 * - Fix (Tracking): Added ±60 deg limit check to `P` command handler to prevent
 * collisions. v2.2.75 Changelog:
 * - Tuning: Adjusted Gain for Tracking (Sync with Unit 1).
 * v2.2.74 Changelog:
 * - Feature: Re-enabled `Y` command for GridEye Tracking (interprets as
 * Incremental Yaw). v2.2.73 Changelog:
 * - Fix: Randomized Yaw startup delay (200-1000ms) to prevent simultaneous
 * inrush. v2.2.72 Changelog:
 * - Fix: Added retry logic (3 attempts) for Yaw Motor `initFOC` failure.
 * - Fix: Increased startup delay to 1000ms to prevent power sag collision with
 * Pitch Motor. v2.2.71 Changelog:
 * - Tuning: Damping (Inner P=0.5, Ki=0.15, Leak=0.95) to reduce oscillation.
 * v2.2.70 Changelog:
 * - Tuning: Gain Scheduling (Boost P=3.0 / Stable P=1.5).
 * - Unit 1 Fix: Increased Stack Size to prevent visual task crash.
 * v2.2.69 Changelog:
 * - Tuning: Boosted Acceleration (Outer P=2.5, Inner P=1.0, Ki=0.20) to fight
 * external drift.
 * - Tuning: Fixed "No Return" Drift by removing I-term Reset logic and widening
 * Error Clamp (45 deg). v2.2.67 Changelog:
 * - Tuning: Fixed Drift by Blocking Leak (0.96) and Boosting I-term (0.10).
 * - Target: Holds position against cable torque.
 * v2.2.66 Changelog:
 * - Tuning: Increased Virtual Friction (Leak=0.90) to prevent Windup during
 * slow moves. v2.2.65 Changelog:
 * - PID Tuning: Reduced P to 1.5 for Stability (Anti-Oscillation).
 * - Limits: Set Saturation to 150.0f (Physical Reality) to enable Unwind Logic.
 * v2.2.64 Changelog:
 * - Modified: OTA Source path.
 * v2.2.62 Changelog:
 * - PID Tuning: Increased Gains for Snappy Response (Inner P=1.0, Outer
 * P=4.0/D=0.15). v2.2.61 Changelog:
 * - Added 200ms delay to Yaw Task (Core 0) to stagger initialization (Power Sag
 * Fix).
 * - Removed ALL Serial prints (Setup, Loop, Cmd) to prevent Core 1 blocking.
 * v2.2.59 Changelog:
 * - Removed unauthorized delays. Added Heartbeat Diagnostic to detect Core 0
 * Hang.
 * - Removed Serial prints from Core 0 Yaw Task to prevent startup hang.
 * v2.2.56 Changelog:
 * - Refactoring: Moved Yaw Parsing outside Pitch Angle check (Fix for limp
 * motor).
 * - Refactoring: Enabled Yaw parsing from POS, Disabled separate Y command.
 * v2.2.54 Changelog:
 * - Removed Yaw Debug Prints to fix blocking issue when USB disconnected.
 * v2.2.53 Changelog:
 * - Added Yaw Debug Prints (Target, Current, Output).
 * v2.2.48 Changelog:
 * - Reverted YawController limits to 750 (safe value) to diagnose stopped motor
 * issue. v2.2.46 Changelog:
 * - Corrected YawController limits in setup to match library defaults
 * (Threshold=150). v2.2.37 Changelog:
 * - Added debug print for Y command verification.
 * v2.2.36 Changelog:
 * - Disabled Yaw update from POS command to prevent overwrite by upper program.
 * v2.2.35 Changelog:
 * - Improved Saturation Unwind Logic: Threshold=150, Decel=500, Leak=0.980.
 * v2.2.34 Changelog:
 * - Increased Auto-Cal speed to 14.0 rad/s.
 * v2.2.33 Changelog:
 * - Inverted Yaw Control Polarity (again) to fix stability at 180 deg.
 * v2.2.32 Changelog:
 * - Parallel Initialization: Yaw and Pitch motors calibrate simultaneously on
 * separate cores. v2.2.31 Changelog:
 * - Fixed Yaw Control Polarity: Inverted output for Reaction Wheel physics.
 * - Doubled Yaw Speed/Accel Limits (900rad/s, 750rad/s^2).
 * v2.2.30 Changelog:
 * - Fixed Yaw Control: Removed dependency on Pitch Calibration (Always Active).
 * v2.2.29 Changelog:
 * - Updated Yaw PID gains (P=1.0, I=0.02, D=0.05).
 * - Adjusted Auto-Cal sequence: 4s duration, 7.0 rad/s speed.
 * - Implemented 'SET_HORIZON' and 'CLEAR_CAL' for Pitch offset.
 * - Enforced sequential initialization (Core 1 -> Cmd -> Core 0).
 */

// --- Preferences ---

Preferences preferences;

// --- Pin Definitions (Unit 2) ---
#define PIN_RX D6 // UART1 RX from Unit 1
// Yaw Motor
#define YAW_U D4
#define YAW_V D5
#define YAW_W D3
#define YAW_SDA D9
#define YAW_SCL D10
// Pitch Motor
#define PITCH_PIN_A D2
#define PITCH_PIN_B D1
#define PITCH_PIN_C D0
#define PITCH_SDA D7
#define PITCH_SCL D8

// --- Objects ---
BLDCMotor motorYaw = BLDCMotor(7, 6.5f, 330.0f);
BLDCDriver3PWM driverYaw = BLDCDriver3PWM(YAW_U, YAW_V, YAW_W);
MagneticSensorI2C sensorYaw = MagneticSensorI2C(AS5600_I2C);

BLDCMotor motorPitch = BLDCMotor(7, 6.5f, 330.0f);
BLDCDriver3PWM driverPitch = BLDCDriver3PWM(PITCH_PIN_B, PITCH_PIN_A,
                                            PITCH_PIN_C); // Swapped A/B
TwoWire I2C_PITCH = TwoWire(1);
MagneticSensorI2C sensorPitch = MagneticSensorI2C(AS5600_I2C);

YawController yawController;

TaskHandle_t yawTaskHandle;

// --- Shared Globals (Volatile for Sync) ---
volatile float g_target_heading = 0.0f;
volatile float g_current_heading = 0.0f;
volatile bool g_imu_active = false;
volatile bool g_is_calibrated = false;

// Auto-Calibration Shared States
volatile int g_yaw_cal_state = 0; // 0:Idle, 1-4:Moves
volatile unsigned long g_yaw_cal_timer = 0;
volatile uint32_t g_yaw_heartbeat = 0;     // Diagnostic Heartbeat
volatile bool g_yaw_velocity_mode = false; // v2.2.86: Direct Velocity Control
volatile float g_yaw_target_velocity = 0.0f;

float g_pitch_offset = 0.0f; // Only accessed by Core 1

// --- Debug Global ---
bool g_debug_stream = false;
unsigned long g_last_debug = 0;

// --- Helper Functions ---
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

// --- CORE 0: Yaw Motor Task ---
void yawMotorTask(void *pvParameters) {
  // Wait for stability (Replace Serial Print delay)
  // v2.2.90: Staggered Startup (1000ms base + 0-1000ms random)
  // Ensures Pitch Motor (Core 1) initializes FIRST to avoid inrush collision.
  unsigned long delay_ms = 1000 + random(0, 1001);
  vTaskDelay(delay_ms / portTICK_PERIOD_MS);

  // Serial.println("Yaw Task Started on Core 0. Initializing Yaw Motor...");

  // --- Yaw Motor Init (Core 0 Local) ---
  Wire.begin(YAW_SDA, YAW_SCL);
  Wire.setClock(100000); // v2.2.86: Slow I2C for Reliability (Fixed 1/5 fail?)
  Wire.setTimeOut(1000); // v2.2.85: Prevent Deadlock
  sensorYaw.init(&Wire);
  motorYaw.linkSensor(&sensorYaw);
  driverYaw.voltage_power_supply = 5.0f;
  driverYaw.init();
  motorYaw.linkDriver(&driverYaw);
  motorYaw.controller = MotionControlType::velocity;
  motorYaw.controller = MotionControlType::velocity;
  motorYaw.PID_velocity.P = 0.5f; // Reverted to 0.5f (Stability Priority)
  motorYaw.PID_velocity.I =
      0.0f; // v2.2.88: Disable I-term to prevent drift at 0 speed
  motorYaw.init();

  // v2.3.10: Enhanced Retry Logic (Fix 1/5 Stuck Issue)
  int retries = 0;
  bool yaw_ok = false;
  while (retries < 10) {
    if (motorYaw.initFOC()) {
      motorYaw.enable();
      yaw_ok = true;
      break;
    }
    retries++;
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  if (yaw_ok) {
    // Serial.println("Yaw Motor Ready.");
  } else {
    // Serial.println("Yaw Motor FAILED.");
  }

  // v2.2.86: Startup Diagnostic Check
  float check_angle = sensorYaw.getAngle();
  if (check_angle == 0.0f && sensorYaw.getVelocity() == 0.0f) {
    // Possible I2C failure (Stuck at 0?)
    // Try soft reset? Or just flag it. The LED will blink.
  }

  // Yaw Controller Settings (High Level)
  // v2.2.91: PID Tuning for Stability (No I-term, Soft P)
  yawController.setPID(1.0f, 0.0f, 0.05f); // Base P=1.0, Ki=0.0
  yawController.setLimits(200.0f, 100.0f);

  // Serial.println("Yaw Motor Ready (Core 0). Starting Loop...");

  for (;;) {
    // 0. Update Heartbeat
    g_yaw_heartbeat++;

    // 1. FOC Loop (High Frequency)
    motorYaw.loopFOC();

    // 2. Control Logic
    // Allow Yaw control independent of Pitch Calibration

    // Auto-Calibration Sequence (Overrides Normal Control)
    if (g_yaw_cal_state != 0) {
      unsigned long now = millis();
      unsigned long elapsed = now - g_yaw_cal_timer;
      // v2.3.11: Reduced Speed (was 14.0) and Increased Duration (was 4000)
      float cal_speed = 8.0f;
      unsigned long step_dur = 5000;

      if (g_yaw_cal_state == 1) {
        motorYaw.target = cal_speed;
        if (elapsed > step_dur) {
          g_yaw_cal_state = 2;
          g_yaw_cal_timer = now;
        }
      } else if (g_yaw_cal_state == 2) {
        motorYaw.target = -cal_speed;
        if (elapsed > step_dur) {
          g_yaw_cal_state = 3;
          g_yaw_cal_timer = now;
        }
      } else if (g_yaw_cal_state == 3) {
        motorYaw.target = cal_speed;
        if (elapsed > step_dur) {
          g_yaw_cal_state = 4;
          g_yaw_cal_timer = now;
        }
      } else if (g_yaw_cal_state == 4) {
        motorYaw.target = -cal_speed;
        if (elapsed > step_dur) {
          g_yaw_cal_state = 0;                  // Done
          g_target_heading = g_current_heading; // Prevent jump
        }
      }
    }
    // Normal Control Mode (State 0)
    else {
      if (g_yaw_velocity_mode) {
        // v2.2.88: Reverted Polarity (Negative Target) based on Log Analysis
        // v2.2.87 (+target) caused Runaway to Right on Left Command.
        motorYaw.target = -g_yaw_target_velocity;
      } else {
        // Normal Position Control (Always Active)
        static unsigned long last_yaw = 0;
        unsigned long now = millis();
        if (now - last_yaw > 20) { // 50Hz
          last_yaw = now;
          float current_vel = motorYaw.shaft_velocity;

          // v2.3.10: Filters to fix Oscillation
          static float filtered_heading = 0;
          // v2.2.91: Removed LPF (set to 1.0) to eliminate Phase Lag
          if (filtered_heading == 0)
            filtered_heading = g_current_heading; // Init
          filtered_heading = 1.0f * filtered_heading +
                             0.0f * g_current_heading; // Pass-through

          // Or simpler: just use g_current_heading. But I'll keep structure.
          filtered_heading = g_current_heading;

          // Use local copies of volatiles
          float target_vel = yawController.update(
              filtered_heading, g_target_heading, current_vel);

          // Deadband on Velocity Output (Stop Micro-Oscillations)
          if (fabs(target_vel) < 0.2f)
            target_vel = 0.0f; // Tuned for reliability

          // v2.2.90: Fix 180 Oscillation -> FORCE REMOVED INVERSION
          motorYaw.target = target_vel;
        }
      }
    }

    // 3. Move (ALWAYS EXECUTE)
    motorYaw.move();

    // Critical: Yield to IDLE task to feed WDT on Core 0
    vTaskDelay(1);
    // No delay to maximize performance
    // ESP32 Watchdog might trigger if loop is too tight, but loopFOC has some
    // overhead? If needed: vTaskDelay(1); but that kills FOC performance.
    // Ideally, SimpleFOC loop is fine.
  }
}

// --- Command Handler (Runs on Core 1) ---
// --- OTA Implementation ---
void performOTA() {
  Serial.println("[OTA] Starting Update Process (v2.3.23 Method)...");

  // 1. Safety: Stop Motors
  motorYaw.disable();
  motorPitch.disable();
  // Suspend Yaw Task if running to prevent I2C conflicts during high-load WiFi
  if (yawTaskHandle != NULL) {
    vTaskSuspend(yawTaskHandle); // Ensure 'yawTaskHandle' is visible or use
                                 // 'extern' if needed
  }

  // 2. Connect to WiFi
  WiFi.begin(ota_ssid, ota_password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - start > 15000)
      return; // Timeout
  }

  // 3. Start Update
  WiFiClient client;
  httpUpdate.setLedPin(LED_BUILTIN, LOW);
  t_httpUpdate_return ret =
      httpUpdate.update(client, ota_host, ota_port, ota_bin);
  // If success, it reboots automatically.

  // If failed:
  Serial.printf("[OTA] Failed: %s\n", httpUpdate.getLastErrorString().c_str());
  // Resume motor task if failed?
  if (yawTaskHandle != NULL) {
    vTaskResume(yawTaskHandle);
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0)
    return;

  // --- OTA Handling ---
  if (cmd.equals("OTA")) {
    performOTA();
    return;
  }

  if (cmd.equals("j")) {
    g_debug_stream = true;
  } else if (cmd.equals("s")) {
    g_debug_stream = false;
  } else if (cmd.equalsIgnoreCase("OTA,START")) {
    preferences.begin("system", false);
    preferences.putBool("ota_mode", true);
    preferences.end();
    ESP.restart();
  } else if (cmd.startsWith("PV,")) {
    float v = cmd.substring(3).toFloat();
    if (motorPitch.controller == MotionControlType::velocity)
      motorPitch.target = v;
  } else if (cmd.equalsIgnoreCase("CLEAR_CAL")) {
    g_pitch_offset = 0.0f;
    g_is_calibrated = false;
    preferences.begin("tube_v2", false);
    preferences.putFloat("p_off", 0.0f);
    preferences.end();
    motorPitch.controller = MotionControlType::velocity;
    motorPitch.target = 0.0f;
    Serial.println("CAL_CLEARED");
  } else if (cmd.equalsIgnoreCase("SET_HORIZON")) {
    float raw = sensorPitch.getAngle();
    g_pitch_offset = -raw; // (raw + off) = 0 -> off = -raw
    preferences.begin("tube_v2", false);
    preferences.putFloat("p_off", g_pitch_offset);
    preferences.end();
    g_is_calibrated = true;
    // Serial.println("CAL_OK_HORIZON. Offset=" + String(g_pitch_offset));
    motorPitch.target = motorPitch.shaft_angle;
    motorPitch.controller = MotionControlType::angle;
  } else if (cmd.startsWith("I,")) {
    g_imu_active = true;
    int p1 = cmd.indexOf(',');
    int p2 = cmd.indexOf(',', p1 + 1);
    int p3 = cmd.lastIndexOf(',');
    if (p1 > 0 && p2 > p1 && p3 > p2) {
      g_current_heading = cmd.substring(p1 + 1, p2).toFloat();
    }
  } else if (cmd.startsWith("POS,")) {
    String params = cmd.substring(4);
    int p1 = params.indexOf(',');
    if (p1 < 0)
      p1 = params.length();

    // 1. Yaw Control (Independent)
    // POS,Pitch,Yaw
    if (p1 < params.length()) {
      String yaw_str = params.substring(p1 + 1);
      if (yaw_str.length() > 0) {
        g_target_heading = yaw_str.toFloat();
        // v2.2.86: Any POS command restores Position Control
        g_yaw_velocity_mode = false;
      }
    }

    // 2. Pitch Control (Dependent on Calibration)
    if (g_is_calibrated)
      motorPitch.controller = MotionControlType::angle;

    if (motorPitch.controller == MotionControlType::angle) {
      float pitch_deg = params.substring(0, p1).toFloat();

      // v2.3.8: Avoid Clamping Trigger Values (e.g. 90) to Limit (60).
      // Instead, IGNORE them completely.
      if (fabs(pitch_deg) > 60.0f) {
        // Serial.println("RX_POS_IGNORE: >60.0 Unit 2 safety.");
        return; // Do NOT update target
      }

      // If valid, convert to radians and execute
      float target_phys = pitch_deg * (PI / 180.0f);
      motorPitch.target = target_phys - g_pitch_offset;
    }
  } else if (cmd.startsWith("Y")) {
    // v2.2.74: Re-enabled for Tracking (Incremental Move)
    // Unit 1 sends Y[val]. We add this to current target.
    // v2.2.86: Switch to Velocity Mode (Direct Speed Control)
    g_yaw_velocity_mode = true;
    g_yaw_target_velocity = cmd.substring(1).toFloat();
    // Sync Heading to prevent jump on exit
    g_target_heading = g_current_heading;
  } else if (cmd.equalsIgnoreCase("CAL,YAW")) {
    g_yaw_cal_state = 1;
    g_yaw_cal_timer = millis();
  } else if (cmd.startsWith("P")) {
    // v2.2.75: Incremental Pitch Command (Tracking)
    // Unit 1 sends P[val]. We add this to current target.

    // SAFETY CHECK (v2.2.84): Ignore if Uncalibrated
    // If we are in Velocity mode (Uncalibrated), `P` would set Velocity,
    // causing spin.
    if (!g_is_calibrated)
      return;

    // v2.2.76: Add Limit Check (±60 deg) - Physical Angle
    float inc = cmd.substring(1).toFloat();
    // Convert inc (degrees) to radians
    float inc_rad = inc * (PI / 180.0f);

    // Calculate New Physical Target (rad)
    float current_phys = motorPitch.target + g_pitch_offset;
    float new_phys = current_phys + inc_rad;

    // Clamp to ±60 deg (±1.047 rad)
    float limit_rad = 60.0f * (PI / 180.0f);
    if (new_phys > limit_rad)
      new_phys = limit_rad;
    if (new_phys < -limit_rad)
      new_phys = -limit_rad;

    // Set Target
    motorPitch.target = new_phys - g_pitch_offset;
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, PIN_RX, -1);
  // v2.2.84: Set Timeout to 5ms to prevent blocking (Default 1000ms)
  Serial.setTimeout(5);
  Serial1.setTimeout(5);

  preferences.begin("system", false);
  bool ota_mode = preferences.getBool("ota_mode", false);
  if (ota_mode) {
    Serial.println("OTA Mode. Executing...");
    preferences.putBool("ota_mode", false);
    preferences.end();
    performOTA();
    ESP.restart();
  }
  preferences.end();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Serial.printf("Unit 2 Firmware v%s Booting...\n", FIRMWARE_VERSION);

  // --- 0. Start Yaw Task (Parallel Init) ---
  // Launch Core 0 task immediately so it initializes Yaw motor
  // while Core 1 initializes Pitch motor below.
  xTaskCreatePinnedToCore(yawMotorTask, "YawTask", 8192, NULL, 1,
                          &yawTaskHandle, 0); // Core 0

  // --- 1. Pitch Motor Init (Priority) ---
  I2C_PITCH.begin(PITCH_SDA, PITCH_SCL);
  I2C_PITCH.setTimeOut(1000); // v2.2.85: Prevent Deadlock
  sensorPitch.init(&I2C_PITCH);
  motorPitch.linkSensor(&sensorPitch);
  driverPitch.voltage_power_supply = 5.0f;
  driverPitch.init();
  motorPitch.linkDriver(&driverPitch);
  motorPitch.controller = MotionControlType::velocity;
  motorPitch.PID_velocity.P = 0.2f;
  motorPitch.PID_velocity.I = 2.0f;
  motorPitch.LPF_velocity.Tf = 0.02f;
  motorPitch.P_angle.P = 5.0f;
  motorPitch.velocity_limit = 20.0f;
  motorPitch.voltage_limit = 3.0f;

  preferences.begin("tube_v2", false);
  g_pitch_offset = preferences.getFloat("p_off", 0.0f);
  preferences.end();

  motorPitch.init();

  // v2.2.84: Retry Logic for Pitch Initialization
  int pitch_retries = 0;
  bool pitch_ready = false;
  while (pitch_retries < 3) {
    if (motorPitch.initFOC()) {
      pitch_ready = true;
      break;
    }
    pitch_retries++;
    // Serial.println("Pitch InitFOC Failed. Retrying...");
    delay(500);
  }

  if (!pitch_ready) {
    // Critical Failure: Disable Motor to prevent burn
    motorPitch.disable();
    // Serial.println("Pitch InitFOC FAILED. Motor Disabled.");
  }

  // motorPitch.initFOC(); // Blocking calibration (~2-3 sec)

  if (g_pitch_offset != 0.0f) {
    g_is_calibrated = true;
    // Serial.println("Pitch Calibrated.");
    motorPitch.controller = MotionControlType::angle;
    motorPitch.target = 0.0f - g_pitch_offset; // Hold Horizon
  } else {
    // Serial.println("Pitch NOT Calibrated.");
    motorPitch.target = 0.0f;
  }
}

void loop() {
  // --- Core 1: Pitch Control & Comm ---
  motorPitch.loopFOC();
  motorPitch.move();

  // LED Priority:
  // 1. Yaw Motor Disabled (Init Fail) -> FAST BLINK (50ms)
  // 2. Yaw Task Stuck (Heartbeat Fail) -> MEDIUM BLINK (100ms)
  // 3. Pitch Uncalibrated -> SLOW BLINK (200ms)
  // 4. Normal -> SOLID OFF (High)

  static uint32_t last_heartbeat_val = 0;
  static unsigned long last_heartbeat_check = 0;
  static bool yaw_stuck = false;

  if (millis() - last_heartbeat_check > 500) {
    if (g_yaw_heartbeat == last_heartbeat_val) {
      yaw_stuck = true;
    } else {
      yaw_stuck = false;
      last_heartbeat_val = g_yaw_heartbeat;
    }
    last_heartbeat_check = millis();
  }

  // Simplified Check
  if (!motorYaw.enabled) {
    static unsigned long last_fast_blink = 0;
    if (millis() - last_fast_blink > 50) {
      last_fast_blink = millis();
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  } else if (yaw_stuck) {
    static unsigned long last_med_blink = 0;
    if (millis() - last_med_blink > 100) {
      last_med_blink = millis();
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  } else if (!g_is_calibrated) {
    static unsigned long last_blink = 0;
    if (millis() - last_blink > 200) {
      last_blink = millis();
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }

  // Handle Communication
  if (Serial1.available())
    handleCommand(Serial1.readStringUntil('\n'));
  if (Serial.available())
    handleCommand(Serial.readStringUntil('\n'));

  // Debug Stream
  if (g_debug_stream && (millis() - g_last_debug > 100)) {
    g_last_debug = millis();
    float raw_rad = sensorPitch.getAngle();
    float raw_deg = raw_rad * 180.0f / PI;
    float cal_deg = (raw_rad + g_pitch_offset) * 180.0f / PI;
    // Serial.printf("D:%.2f,%.2f,%.1f,%.1f,%.1f\n", raw_deg, cal_deg,
    //               g_target_heading, g_current_heading,
    //               motorYaw.shaft_velocity);
  }
}
