/* ============================================================================
 *  ______     __         __  __     _ __    ___
 * /_  __/_ __/ /  ___   / / / /__  (_) /_  <  /
 *  / / / // / _ \/ -_) / /_/ / _ \/ / __/  / /
 * /_/  \_,_/_.__/\__/  \____/_//_/_/\__/  /_/
 *
 *  FU Product Tube Unit v1 (Sensor Master) - Single Core Edition
 * ============================================================================
 * Core 1 (Main Loop): Logic, Sensors, LEDs, Comm (Consolidated)
 * ============================================================================
 */

#include <Adafruit_AMG88xx.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>

// Early Neopixel for Debug
// Adafruit_NeoPixel strip_debug(100, D4, NEO_GRB + NEO_KHZ800); // Removed for
// RMT Conflict
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <Wire.h>

#define FIRMWARE_VERSION "2.3.23"

// --- OTA Configuration ---
const char *ota_ssid = "FU";
const char *ota_password = "Spark!!!!!";
const char *ota_host =
    "comone-fragmented-unity-fw.s3.ap-northeast-1.amazonaws.com";
const int ota_port = 80;
const char *ota_bin = "/tube_v2_unit1_product.bin";

void performCalibrationSequence(); // Forward Declaration
void performOTA();                 // Forward Declaration

/*
 * v2.3.22 Changelog:
 * - Tuning (Sensitivity): Lowered GridEye Threshold.
 *   - Problem: "Must be very close to detect".
 *   - Cause: Threshold 26.0C is too high for distant signals.
 *   - Fix: Lowered Threshold `26.0f -> 23.0f`.
 * v2.3.21 Changelog:
 * - Tuning (Optimization): High Gain + High Leak (Agile & Stable).
 *   - Strategy: Move closer to P-Control (Vel proportional to Error).
 *   - Fix: Gain `1.0 -> 3.0` (High Torque for instant start).
 *   - Fix: Leak `0.95 -> 0.85` (High Friction for instant stop).
 *   - Result: Snappy response to error, but brakes immediately when error
 * reduces. v2.3.20 Changelog:
 * - Tuning (Stability): Reduced Gain and Added Damping.
 *   - Problem: "Slow/Unable to Track" even with valid commands.
 *   - Cause: Max Limit `3.0 rad/s` (~30 RPM) is too low for Reaction Wheel
 * torque.
 *   - Fix: Increased Limit to `50.0 rad/s` (~477 RPM).
 *   - Fix: Increased Gain `0.2 -> 0.5` for faster reaction.
 * v2.3.17 Changelog:
 * - Fix (Communication): Removed Comma from `Y` Command.
 *   - CRITICAL BUG FOUND: U1 sent `Y,3.00`. U2 parsed `substring(1)` ->
 * `,3.00`.
 *   - Problem: Logs showed `Valid=0` caused Velocity to drop 0.38->0.00 in
 * <500ms.
 *   - Reason: Decay `0.95` at 50Hz is ~93% loss per second. Too fast.
 *   - Fix: Changed Decay to `0.995` (Holds momentum for seconds).
 *   - Gain: Increased to 0.2 for stronger response.
 * v2.3.15 Changelog:
 * - Fix (Responsiveness): Enabled Yaw Tracking DURING Pitch Alignment Phase.
 *   - Issue: Entering Tracking Mode (`POS,90`) caused 2-second Yaw Freeze
 * (`is_aligning`).
 *   - Fix: Moved Yaw Control logic OUT of `else if` so it runs concurrently
 * with Alignment.
 *   - Debug: Re-enabled Tracking Debug Logs (CX, CY, VEL) to diagnose
 * "Stoppage". v2.3.14 Changelog:
 * - Fix (Control Physics): Adopted "Bang-Coast" Logic for Reaction Wheel.
 *   - Problem: "Leaky Integrator" (v2.3.11-13) caused Constant Velocity -> Zero
 * Torque (Braking).
 *   - Fix: REMOVED steady-state Leak.
 *     - When Error > Deadband: Accumulate Velocity (Accelerate) -> Torque.
 *     - When Error < Deadband: Decay Velocity (Coast/Brake) -> Stop.
 *   - Gain: Increased to 0.1 (from 0.02) for responsiveness.
 * v2.3.13 Changelog:
 * - Fix (Stability): Protect Integral Momentum from Sensor Dropouts.
 *   - Issue: Intermittent GridEye "No Heat" caused hard reset of Velocity to
 * 0.0.
 *   - Fix: Changed `else` block to DECAY velocity (0.95) instead of reset.
 *   - Result: Wheel "coasts" through sensor noise instead of "Braking".
 * v2.3.12 Changelog:
 * - Fix (Safety): Drastically reduced Integral Gain (0.02) and Max Velocity
 * (3.0).
 *   - Rationale: v2.3.9/11 caused Driver Fault (Overcurrent) due to excessive
 * Accel/Velocity output.
 *   - Target Speed: ~30 RPM max (3.0 rad/s).
 *   - Soft Ramp: Accel limited by low integral gain.
 * v2.3.11 Changelog:
 * - Fix (Control Physics): Changed Yaw Logic to INTEGRAL VELOCITY (`Vel +=
 * Err`).
 *   - Rationale: Reaction Wheel torque comes from Acceleration, not Velocity.
 *   - Proportional Vel (v2.3.9) resulted in "Kick & Stop" (D-term behavior).
 *   - Integral Vel gives `Accel ~ Error`, providing correct restoring torque.
 *   - Added Leak Factor (0.95) to prevent saturation/drift.
 * v2.3.10 Changelog:
 * - Fix (Stability): Send `POS,0,0` on Mode Switch to Normal to force Unit 2
 * out of Velocity Mode.
 *   - Prevents Unit 2 from getting stuck in Velocity Mode when acting as Normal
 * Slave. v2.3.9 Changelog:
 * - Tuning (Tracking): Reduced Yaw Velocity Gain to P=5.0 (was 15.0).
 *   - Rationale: High Gain caused "D5m Y_FAULT" (Overcurrent/Acceleration
 * Trip).
 *   - Use Proportional Gain (P=15.0) for direct response.
 * v2.3.7 Changelog:
 * - Fix (Stability): Normalized `trk_yaw_position` to 0-360 range to prevent
 * "Spin/Runaway".
 * - Tuning (Tracking): Increased YAW_P to 7.0 (Balance between 5.0 and 10.0).
 * v2.3.6 Changelog:
 * - Fix (Tracking): Reverted Yaw Logic to `-=` (Inverse).
 *   - Rationale: v2.3.5 (`+=`) confirmed "Escaping". Physics requires `Target <
 * Current` for Right Turn.
 * - Tuning: Kept P=5.0, Deadband 0.2.
 * v2.3.5 Changelog:
 * - Tuning (Tracking): Reduced YAW_P to 5.0 (was 10.0) to stop
 * spin/instability.
 *   - Logic preserved as `+=` (Standard) since `-=` was confirmed Reverse.
 * - Tuning: Deadband 0.2 (High Sensitivity).
 * v2.3.4 Changelog:
 * - Fix (Tracking): Reverted Yaw Logic to `+=` (Standard) based on user test.
 * v2.3.3 Changelog:
 * - Tuning (Tracking): Aggressively Boosted Gains (Yaw x2.5, Pitch x3).
 * v2.3.1 Changelog:
 * - Feature (Calibration): Added `performCalibrationSequence()` to `setup()`.
 *   - Sequence: Wait 5s (Unit 2 Init) -> Spin Y+20 (4s) -> Spin Y-20 (4s) ->
 * Stop.
 * - Fix (Boot): Consolidated VisualTask to Single Core (Main Loop).
 * v2.3.0 Changelog:
 * - Fix (LED): Consolidated VisualTask to Single Core (Main Loop) to fix Race
 * Condition.
 * - Fix (Boot): Implemented "Sakura Dim (10%)" startup color for polite boot.
 * - Fix (Init): Restored global brightness 128 (50%) for full dynamic range.
 * - Fix (Hardware): Confirmed D4 Pin & Wiring integrity via Boot Test.
 * v2.2.97 Changelog (Restored):
 * - Reverted to Monolithic architecture (Stable v2.2 baseline).
 * - Preserved Critical Fixes: Serial1 D8 Pin, Pitch Jump Init.
 */
// --- Global Constants ---
#define PIN_NEOPIXEL D4
#define NUM_PIXELS 100
#define PIN_GRIDEYE_SDA D0
#define PIN_GRIDEYE_SCL D1
#define PIN_BNO_SDA D2
#define PIN_BNO_SCL D3

// LED Indices
#define LED_STAT_MOT 96
#define LED_STAT_SENS 97
#define LED_STAT_CONN 98
#define LED_STAT_PWR 99
#define GROUP_GR_FRONT_START 0
#define GROUP_GR_BACK_START 24
#define GROUP_CR_FRONT_START 48
#define GROUP_CR_BACK_START 72
#define GROUP_SIZE 24

// --- Global State ---
struct SystemConfig {
  bool heading_mode; // true: Normal, false: Winch Color Override
};
volatile SystemConfig g_system_config = {
    false}; // Default: Winch Control (Direct RGB)

struct SensorData {
  float euler_x, euler_y, euler_z;
  float quat_w, quat_x, quat_y, quat_z;
  float centroid_x, centroid_y;
  uint8_t mag_cal;
  float max_temp;
};
volatile SensorData g_sensor_data = {0};

volatile int g_tracking_state = 0; // 0:Idle, 1:Align, 2:Track
volatile bool g_tracking_active = false;
volatile bool g_grideye_ok = false;
volatile bool g_bno_ok = false;
volatile bool g_cmd_tracking_mode = false;

// Anti-Jump & Parsing State
float g_last_pitch_cmd = 0.0f;
float g_align_start_pitch = 0.0f;
unsigned long g_align_start_time = 0;

// Output Speeds
float g_tracking_yaw_speed = 0.0f;
float g_tracking_pitch_speed = 0.0f;

// Debug & Diagnostics
bool g_debug_stream_enabled = false;
unsigned long g_last_debug_time = 0;
volatile bool g_stream_grideye = false;
volatile bool g_winch_connected = false;
volatile float g_target_yaw_debug = 0.0f;

// LED Control
volatile uint32_t g_target_rgb = 0;
volatile bool g_cmd_ota_visual_u2 = false;

// Winch Override
bool g_winch_color_override = false;
uint8_t g_winch_color_r = 0;
uint8_t g_winch_color_g = 0;
uint8_t g_winch_color_b = 0;

// --- Task Handles ---
TaskHandle_t visualTaskHandle;

// --- Forward Declarations ---
void visualTaskFunction(void *pvParameters);
void handleUsbCommand(String cmd);
void handleWinchCommand(String cmd);
void sendMotorCommand(String cmd);
void sendTelem();

// --- Tracking Controller Logic ---
// --- Motion Manager Logic --- (Refactored from TrackingController)
enum MotionMode { MODE_NORMAL, MODE_TRACKING };

class MotionManager {
public:
  MotionMode current_mode = MODE_NORMAL;

  // Normal Mode State
  float target_pitch_normal = 0.0f;
  float target_yaw_normal = 0.0f;

  // Tracking Parameters
  const float ALIGN_DURATION = 2000.0f;
  const float PITCH_DEADBAND = 0.3f;
  const float YAW_DEADBAND = 0.2f; // High Sensitivity
  const float YAW_P =
      7.0f; // v2.3.7: Tuned to 7.0 (Faster than 5.0, Safer than 10.0)
  const float PITCH_P1 = 1.5f; // Keep Pitch Boost
  const float PITCH_P2 = 0.5f;

  // Tracking Limits
  const float PITCH_MIN = -60.0f; // Revert to Standard (0=Horizon)
  const float PITCH_MAX = 60.0f;  // Revert to Standard

  unsigned long last_debug_print = 0;
  unsigned long last_imu_stream = 0;

  // Align State
  unsigned long align_start_time = 0;
  bool is_aligning = false;
  float align_start_pitch = 0.0f;

  // Current Tracking Command
  float trk_pitch_cmd = 0.0f; // Position (0=Horizon)
  // float trk_yaw_vel = 0.0f;   // Velocity (Deprecated due to conflict)
  float trk_yaw_position = 0.0f; // Consolidated Yaw Position Integrator
  float trk_yaw_velocity = 0.0f; // v2.3.8

  void init() {
    current_mode = MODE_NORMAL;
    target_pitch_normal = 0.0f;
    target_yaw_normal = 0.0f;
    trk_pitch_cmd = 0.0f;
    trk_yaw_position = 0.0f;
    trk_yaw_velocity = 0.0f; // v2.3.8
    is_aligning = false;
  }

  void setMode(MotionMode mode) {
    if (current_mode == mode)
      return;

    current_mode = mode;
    // 1. Critical State Sync (Fix for LED & Legacy)
    g_tracking_active = (mode == MODE_TRACKING);
    g_tracking_state = (mode == MODE_TRACKING) ? 1 : 0; // 1=Align, 2=Track
    g_cmd_tracking_mode = (mode == MODE_TRACKING);

    if (mode == MODE_TRACKING) {
      Serial.println("MODE: TRACKING_START (POS,0)");
      is_aligning = true;
      align_start_time = millis();
      align_start_pitch = g_last_pitch_cmd;
      trk_yaw_velocity = 0.0f; // Init
      // v2.3.6: Clamp Alignment Start to Horizon if currently Stow/Trigger
      // (e.g. 90)
      if (fabs(align_start_pitch) > 60.0f) {
        align_start_pitch = 0.0f;
      }

      // Initialize Yaw Position to CURRENT INVERTED HEADING to prevent jump
      float current_h = g_sensor_data.euler_x;
      float inv_h = 360.0f - current_h;
      if (inv_h >= 360.0f)
        inv_h -= 360.0f;
      trk_yaw_position = inv_h;

    } else {
      Serial.println("MODE: NORMAL_ESTABLISHED");
      is_aligning = false;

      // v2.3.10: CRITICAL - Force Unit 2 back to Position Mode
      // If we leave Unit 2 in Velocity Mode (from Tracking), it will ignore IMU
      // packets. Sending "POS..." resets Unit 2's `g_yaw_velocity_mode` to
      // false.
      Serial1.println("POS,0,0"); // Args don't matter much, just the POS header
                                  // resets mode.
    }
  }

  // Called from main loop or specific task (e.g., 50Hz)
  // Added `valid_thermal` to handle "No Heat" case without stopping Normal Mode
  // loop
  void update(float cx, float cy, float current_pitch, bool valid_thermal) {
    unsigned long now = millis();

    // 1. Calculate Common Data (Inverted Heading)
    float heading_deg = g_sensor_data.euler_x;
    float inverted_heading = 360.0f - heading_deg;
    if (inverted_heading >= 360.0f)
      inverted_heading -= 360.0f;

    // --- NORMAL MODE Logic ---
    if (current_mode == MODE_NORMAL) {
      // Normal mode has no specific internal logic update here,
      // it purely relies on streaming the IMU data below.
    }

    // --- TRACKING MODE Logic ---
    else if (current_mode == MODE_TRACKING) {
      // 3. Sensor Guard Removal (Allow LED updates "Blind")
      // if (!g_grideye_ok) return; // REMOVED

      // State Update
      g_tracking_state = is_aligning ? 1 : 2;

      // --- YAW CONTROL (Run Always if Heat Valid) ---
      // v2.3.15: Moved outside of `else` to run during `is_aligning` phase.
      // Prevents "2 second freeze" on mode entry.
      // v2.3.16: "Blind Momentum". Keep spinning if sensor lost.
      if (g_grideye_ok && valid_thermal) {
        float err_x = cx - 3.5f;

        // Bang-Coast Logic (v2.3.14) -> Modified to Leaky Integrator (v2.3.20)
        // v2.3.21: High Gain (3.0) + High Leak (0.85)
        // Approaches P-Control: Velocity ~ Error * 20.
        // Fast Start, Fast Stop.
        if (fabs(err_x) > YAW_DEADBAND) {
          trk_yaw_velocity *= 0.85f; // Strong Damping (15% loss/tick)
          trk_yaw_velocity -= (err_x * 3.0f);
        } else {
          // Target Reached: Decay Momentum (Brake slowly)
          // 0.98 -> Slow brake when on target
          trk_yaw_velocity *= 0.90f; // Increase braking when on target too
          if (fabs(trk_yaw_velocity) < 0.1f)
            trk_yaw_velocity = 0.0f;
        }

        // Clamp
        // v2.3.18: Boost Limit to 50.0 (~477 RPM)
        if (trk_yaw_velocity > 50.0f)
          trk_yaw_velocity = 50.0f;
        if (trk_yaw_velocity < -50.0f)
          trk_yaw_velocity = -50.0f;

        // NaN Check
        if (isnan(trk_yaw_velocity) || isinf(trk_yaw_velocity)) {
          Serial.println("ERR: VELOCITY NAN/INF RESET");
          trk_yaw_velocity = 0.0f;
        }

      } else {
        // No Heat: Decay Yaw Momentum (Momentum Protection)
        // v2.3.16: Decay 0.995. (0.95 killed it instantly).
        // 0.995^50 (1sec) = 0.77. Retains 77% speed after 1 sec blind.
        trk_yaw_velocity *= 0.995f;

        if (fabs(trk_yaw_velocity) < 0.1f)
          trk_yaw_velocity = 0.0f;
      }

      // --- PITCH CONTROL (Mode Dependent) ---
      if (is_aligning) {
        // 1. Alignment Phase (Horizon <-> 30-150 range?)
        // Actually we just want to move to target pitch smoothly?
        // v2.3.15: Keep existing logic
        unsigned long elapsed = now - align_start_time;
        if (elapsed < ALIGN_DURATION) {
          float progress = (float)elapsed / ALIGN_DURATION;
          // Interpolate? Actually existing logic was:
          // target = align_start_pitch * (1.0 - progress); -> Goes to 0.
          float target = align_start_pitch * (1.0f - progress);
          trk_pitch_cmd = target;
        } else {
          is_aligning = false;
          trk_pitch_cmd = 0.0f;
          Serial.println("TRK: ALIGN_DONE");
        }
      } else if (g_grideye_ok && valid_thermal) {
        // 2. Active Pitch Tracking
        float err_y = cy - 3.5f;
        float pitch_step = 0.0f;
        if (fabs(err_y) > PITCH_DEADBAND) {
          float base_speed = (err_y > 0) ? PITCH_P1 : -PITCH_P1;
          pitch_step = base_speed + (err_y * PITCH_P2);
        }
        trk_pitch_cmd += pitch_step; // += for Position Control

        // Clamp
        if (trk_pitch_cmd > PITCH_MAX)
          trk_pitch_cmd = PITCH_MAX;
        if (trk_pitch_cmd < PITCH_MIN)
          trk_pitch_cmd = PITCH_MIN;
      }

      // v2.3.15: DEBUG OUTPUT
      static unsigned long last_debug = 0;
      if (now - last_debug > 200) {
        last_debug = now;
        Serial.printf("TRK: CX=%.2f CY=%.2f Valid=%d Vel=%.2f Pitch=%.2f\n", cx,
                      cy, valid_thermal, trk_yaw_velocity, trk_pitch_cmd);
      }
    }

    // --- COMMON COMMUNICATION (Fixed: Run for BOTH modes) ---
    // Stream rate: 50Hz (20ms)
    if (now - last_imu_stream > 20) {
      last_imu_stream = now;

      // 1. Always Send IMU (Critical for Unit 2 Yaw Control)
      Serial1.printf("I,%.2f,%.2f,%.2f\n", inverted_heading,
                     g_sensor_data.euler_y, g_sensor_data.euler_z);

      // 2. Send Control Commands if in Tracking Mode
      if (current_mode == MODE_TRACKING) {
        // v2.3.8: Dual Mode Control
        // Pitch: Position (POS,pitch) - One arg doesn't touch Yaw
        Serial1.printf("POS,%.2f\n", trk_pitch_cmd);
        // Yaw: Velocity (Y[val])
        // v2.3.17: CRITICAL FIX - Removed Comma.
        // U1 `Y,3.0` -> U2 `atof(",3.0")` = 0.0.
        // New: `Y3.0` -> U2 `atof("3.0")` = 3.0.
        Serial1.printf("Y%.2f\n", trk_yaw_velocity);
      }
    }
  }
};
MotionManager motionManager;

// --- Visual Task Logic ---
// LED Fader Class (Nested-like logic)
struct LedFader {
  int start_index, count;
  uint32_t current_color, target_color;
  float r, g, b, tr, tg, tb;
  float speed_coeff, breath_phase, breath_speed;
  bool breathing_enabled;

  LedFader(int s, int c)
      : start_index(s), count(c), current_color(0), target_color(0), r(0), g(0),
        b(0), tr(0), tg(0), tb(0), speed_coeff(0.1f), breathing_enabled(false),
        breath_phase(0), breath_speed(0.1f) {}

  void setTarget(uint32_t color, float speed = 0.1f) {
    target_color = color;
    speed_coeff = speed;
    tr = (float)((color >> 16) & 0xFF);
    tg = (float)((color >> 8) & 0xFF);
    tb = (float)(color & 0xFF);
  }

  void update(Adafruit_NeoPixel &strip) {
    float diff_r = tr - r;
    float diff_g = tg - g;
    float diff_b = tb - b;
    if (abs(diff_r) > 0.5f || abs(diff_g) > 0.5f || abs(diff_b) > 0.5f) {
      r += diff_r * speed_coeff;
      g += diff_g * speed_coeff;
      b += diff_b * speed_coeff;
    } else {
      r = tr;
      g = tg;
      b = tb;
    }

    float out_r = r, out_g = g, out_b = b;
    if (breathing_enabled) {
      breath_phase += breath_speed;
      if (breath_phase > 6.28f)
        breath_phase -= 6.28f;
      float mult = 1.0f + 0.5f * (0.5f * (1.0f + sin(breath_phase)));
      out_r *= mult;
      out_g *= mult;
      out_b *= mult;
      if (out_r > 255)
        out_r = 255;
      if (out_g > 255)
        out_g = 255;
      if (out_b > 255)
        out_b = 255;
    }

    uint32_t new_color =
        strip.Color((uint8_t)out_r, (uint8_t)out_g, (uint8_t)out_b);
    // v2.3.23 Fix: Removed optimization check (new_color != current_color).
    // Reason: External modes (Tracking) modify strip buffer directly.
    // If we skip update, the strip retains the Tracking Color (Orange)
    // because Fader thinks it is still on the old Normal Color.
    current_color = new_color;
    for (int i = 0; i < count; i++)
      strip.setPixelColor(start_index + i, current_color);
  }
};

class VisualTask {
public:
  Adafruit_NeoPixel strip;
  Adafruit_AMG88xx amg;
  Adafruit_BNO055 *bno;

  // Constructor with Member Init List for Strip
  VisualTask() : strip(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800) {}

  float pixels[64];
  unsigned long last_sensor_poll = 0, last_grideye_tx = 0;
  bool tare_done = false;
  imu::Quaternion quat_tare_inv = imu::Quaternion(1, 0, 0, 0);

  LedFader *faderGrFront, *faderGrBack, *faderCrFront, *faderCrBack;

  void setup() {
    // LEDs (Explicit Init verified by Constructor)
    // strip.setPin(PIN_NEOPIXEL); // Redundant with Constructor
    // strip.updateLength(NUM_PIXELS);
    // strip.updateType(NEO_GRB + NEO_KHZ800);
    strip.begin();
    strip.setBrightness(128);
    strip.clear();
    strip.show();

    // Sensors
    Wire.begin(PIN_GRIDEYE_SDA, PIN_GRIDEYE_SCL);
    if (amg.begin())
      g_grideye_ok = true;

    Wire1.begin(PIN_BNO_SDA, PIN_BNO_SCL);
    bno = new Adafruit_BNO055(55, 0x28, &Wire1);
    if (bno->begin()) {
      g_bno_ok = true;
      bno->setExtCrystalUse(true);
    }

    // Faders
    faderGrFront = new LedFader(GROUP_GR_FRONT_START, GROUP_SIZE);
    faderGrBack = new LedFader(GROUP_GR_BACK_START, GROUP_SIZE);
    faderCrFront = new LedFader(GROUP_CR_FRONT_START, GROUP_SIZE);
    faderCrBack = new LedFader(GROUP_CR_BACK_START, GROUP_SIZE);
  }

  void update() { // Replaces loop(), Non-Blocking
    unsigned long now = millis();
    updateSensors(now);
    updateLeds(now);
    updateStatusLeds(now);
    strip.show();
  }

  void updateSensors(unsigned long now) {
    if (now - last_sensor_poll > 10) {
      last_sensor_poll = now;

      // GridEye
      bool need_read = g_cmd_tracking_mode || g_stream_grideye;
      float cx = 0.0f, cy = 0.0f; // Default if no heat
      bool heat_detected = false;

      if (need_read && g_grideye_ok && (now - last_grideye_tx > 100)) {
        last_grideye_tx = now;
        amg.readPixels(pixels);
        float sum_val = 0, sum_x = 0, sum_y = 0;
        for (int i = 0; i < 64; i++) {
          if (pixels[i] > 20.0f) {         // v2.3.22: Lowered from 26.0f
            float val = pixels[i] - 20.0f; // v2.3.22: Lowered from 26.0f
            sum_val += val;
            sum_x += (i % 8) * val;
            sum_y += (i / 8) * val;
          }
        }
        if (sum_val > 1.0f) {
          cx = sum_x / sum_val;
          cy = sum_y / sum_val;
          heat_detected = true;
          // motionManager.update(cx, cy, g_sensor_data.euler_y); // MOVED OUT
          g_sensor_data.centroid_x = cx;
          g_sensor_data.centroid_y = cy;
          // g_sensor_data.centroid_y = cy; // Duplicate removed
        }
      }

      // CRITICAL FIX: Always run MotionManager regardless of Heat Detection
      // Normal Mode relies on this loop. Tracking Mode handles cx/cy
      // internally. If heat_detected is false, cx/cy are 0.0f.
      // MotionManager.update() handles "Blind" logic or "Search" logic if
      // needed, or simply pauses integration if error is 0 (Center - 3.5 =
      // -3.5). Wait, if cx=0, Error = -3.5. We don't want to track to 0 if
      // blind. We should pass validity flag or let MM handle it? Since MM
      // checks g_grideye_ok, but here we want to know if specific frame had
      // heat. Let's pass cx/cy as is. But if cx=0, is it valid? If sum_val
      // <= 1.0, cx=0. Actually, MotionManager uses (cx - 3.5). If cx=0, it
      // thinks target is at pure Left. We must tell MM if heat is valid. BUT
      // current MM signature is `update(cx, cy, pitch)`. Normal Mode ignores
      // cx, cy. So it works for Normal. Tracking Mode: If cx=0 (No Heat), it
      // turns Left. BAD. We need to pass the heat_detected flag or equivalent.
      // Let's modify MM to check `sum_val`? No, MM is decoupled.
      // Quickest fix: Only update Tracking Integrators IF heat_detected.
      // But we MUST call update() for Normal Mode.
      // Compromise: Pass `heat_detected` as `current_pitch` arg? No, that's
      // ugly. Correct: Add `bool valid` to `update`. However, I can't change
      // signature without header/class change. Wait, `MotionManager` is defined
      // effectively inline in main.cpp. I CAN change signature.
      motionManager.update(cx, cy, g_sensor_data.euler_y, heat_detected);

      // BNO
      if (g_bno_ok) {
        imu::Quaternion q = bno->getQuat();
        if (!tare_done && now > 30000) {
          imu::Vector<3> e = q.toEuler();
          double halfYaw = e.x() * 0.5;
          quat_tare_inv =
              imu::Quaternion(cos(halfYaw), 0, 0, sin(halfYaw)) * q.conjugate();
          tare_done = true;
          g_target_rgb = strip.Color(64, 24, 32); // Sakura
        }
        imu::Quaternion q_corr = quat_tare_inv * q;
        q_corr.normalize();
        imu::Vector<3> eu = q_corr.toEuler();
        g_sensor_data.euler_x = eu.x() * 180.0 / M_PI;
        g_sensor_data.euler_y = eu.y() * 180.0 / M_PI;
        g_sensor_data.euler_z = eu.z() * 180.0 / M_PI;
        g_sensor_data.quat_w = q_corr.w();
        g_sensor_data.quat_x = q_corr.x();
        g_sensor_data.quat_y = q_corr.y();
        g_sensor_data.quat_z = q_corr.z();
        uint8_t s, g_cal, a, m;
        bno->getCalibration(&s, &g_cal, &a, &m);
        g_sensor_data.mag_cal = m;
      }
    }
  }

  void updateLeds(unsigned long now) {
    if (g_cmd_ota_visual_u2) {
      bool blink = (now % 500) < 250;
      uint32_t c = blink ? strip.Color(255, 0, 0) : 0;
      for (int i = 0; i < NUM_PIXELS; i++)
        strip.setPixelColor(i, c);
    } else if (g_tracking_active) {
      // Orange Feedback
      float cx = g_sensor_data.centroid_x, cy = g_sensor_data.centroid_y;
      float d = sqrt(pow(cx - 3.5, 2) + pow(cy - 3.5, 2));
      float boost = (d < 2.5) ? pow((2.5 - d) / 2.5, 2) : 0;
      uint32_t col = strip.Color((uint8_t)(32 + 223 * boost),
                                 (uint8_t)(12 + 128 * boost), 0);
      for (int i = 0; i < 96; i++) // Update all body LEDs
        strip.setPixelColor(i, col);
    } else {
      faderGrFront->setTarget(g_target_rgb);
      faderCrFront->setTarget(g_target_rgb);
      faderGrBack->setTarget(g_target_rgb);
      faderCrBack->setTarget(g_target_rgb);
      if (g_system_config.heading_mode) {
        faderCrFront->update(strip);
        faderGrBack->update(strip);
        float h = g_sensor_data.euler_x;
        if (h < 0)
          h += 360;
        int lit = (int)((h / 360.0) * 24.0);
        if (lit > 24)
          lit = 24;
        for (int i = 0; i <= lit; i++) {
          strip.setPixelColor(GROUP_GR_FRONT_START + i, 0, 0, 32);
          strip.setPixelColor(GROUP_CR_BACK_START + i, 32, 0, 0);
        }
        for (int i = lit + 1; i < 24; i++) {
          strip.setPixelColor(GROUP_GR_FRONT_START + i, 0);
          strip.setPixelColor(GROUP_CR_BACK_START + i, 0);
        }
      } else {
        faderGrFront->update(strip);
        faderCrFront->update(strip);
        faderGrBack->update(strip);
        faderCrBack->update(strip);
      }
    }
  }

  void updateStatusLeds(unsigned long now) {
    strip.setPixelColor(LED_STAT_PWR, 0, 0, 10);
    strip.setPixelColor(LED_STAT_CONN, g_winch_connected
                                           ? strip.Color(0, 0, 10)
                                           : strip.Color(10, 0, 0));
    strip.setPixelColor(LED_STAT_SENS, g_grideye_ok ? strip.Color(0, 0, 10)
                                                    : strip.Color(10, 0, 0));
    if (g_bno_ok) {
      if (g_sensor_data.mag_cal >= 3 || (now / 250) % 2 == 0)
        strip.setPixelColor(LED_STAT_MOT, 0, 0, 10);
      else
        strip.setPixelColor(LED_STAT_MOT, 0);
    } else
      strip.setPixelColor(LED_STAT_MOT, 10, 0, 0);
  }
};
VisualTask visualTaskModule;

void visualTaskFunction(void *pvParameters) {
  visualTaskModule.setup();
  // visualTaskModule.loop(); // Renamed to update(), run in main loop now
  while (1)
    delay(1000); // Idle if task started by mistake
}

// --- Main Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000); // Allow Serial to stabilize
  Serial.println(
      "\n\n=== UNIT 1 FIRMWARE MONOLITHIC RESTORED (DEBUG MODE) ===");
  Serial.println("BOOT: Waiting 3000ms safety delay...");
  delay(3000); // Safety Delay
  Serial.println("BOOT: Safety delay complete.");

  pinMode(D7, INPUT_PULLUP); // Restored Pullup (Safety for 38400)
  Serial2.begin(38400, SERIAL_8N1, D7,
                D6); // Revert: RX=D7, TX=D6 @ 38400 (Winch Source)
  Serial.println("BOOT: Serial2 (Winch) OK [D7(RX), D6(TX)] @ 38400 (Matches "
                 "Winch Source)");

  /* Removed to prevent RMT Channel Conflict with VisualTask
  // Force LEDs ON for Diagnostics
  strip_debug.begin();
  strip_debug.setBrightness(50);
  strip_debug.fill(strip_debug.Color(0, 0, 255)); // BLUE = Boot Success
  strip_debug.show();
  Serial.println("BOOT: LEDs set to BLUE (Hardware Check)");
  */

  // CRITICAL FIX: Unit 2 uses D8
  Serial1.begin(115200, SERIAL_8N1, -1, D8);
  Serial.println("BOOT: Serial1 (Unit 2) OK [D8]");

  motionManager.init();
  Serial.println("BOOT: MotionManager OK");

  /* SINGLE CORE DEBUG: Run Setup Here, Disable Task Loop
  Serial.println("BOOT: Starting VisualTask on Core 0...");
  xTaskCreatePinnedToCore(visualTaskFunction, "VisualTask", 8192, NULL, 1,
                          &visualTaskHandle,
                          0); // Core 0 to free up Loop (Core 1)
  */
  visualTaskModule.setup(); // Run setup synchronously
  Serial.println(
      "BOOT: VisualTask Setup (Sensors/LEDs) Complete (Task Loop Disabled)");

  // --- REFINED BOOT: Sakura Fade (10% Brightness) ---
  Serial.println("BOOT_TEST: Setting Initial Target to Sakura (10% Intensity, "
                 "Normal Brightness)...");
  visualTaskModule.strip.setBrightness(128); // Restore Normal Brightness
  g_target_rgb =
      visualTaskModule.strip.Color(7, 3, 4); // Sakura Dim (approx 10%)

  // Flash White briefly to confirm boot logic (Non-blocking visual check) or
  // just let fade handle it. User requested "Check immediately", a fade from
  // black satisfies this.

  Serial.printf("=== UNIT 1 FIRMWARE v%s BOOT COMPLETE ===\n",
                FIRMWARE_VERSION);

  performCalibrationSequence();
}

void performCalibrationSequence() {
  Serial.println("BOOT_CAL: Waiting 5s for Unit 2 InitFOC...");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    visualTaskModule.update();
    delay(10);
  }

  Serial.println("BOOT_CAL: Starting Spin Y+20.0 (4s)...");
  sendMotorCommand("Y20.0");
  start = millis();
  while (millis() - start < 4000) {
    visualTaskModule.update();
    delay(10);
  }

  Serial.println("BOOT_CAL: Reversing Spin Y-20.0 (4s)...");
  sendMotorCommand("Y-20.0");
  start = millis();
  while (millis() - start < 4000) {
    visualTaskModule.update();
    delay(10);
  }

  Serial.println("BOOT_CAL: Stopping (Y0.0)...");
  sendMotorCommand("Y0.0");
  delay(1000);
  Serial.println("BOOT_CAL: Sequence Complete. Engaging Winch Logic.");
}

// --- Main Loop & Comm Logic ---
void loop() {
  // Comm Task Logic (Polled)
  if (Serial.available()) {
    handleUsbCommand(Serial.readStringUntil('\n'));
  }
  if (Serial2.available()) {
    handleWinchCommand(Serial2.readStringUntil('\n'));
  }

  static unsigned long last_telem = 0;
  if (millis() - last_telem > 100) {
    last_telem = millis();
    sendTelem();
    // Debug Hearthbeat (1s)
    static unsigned long last_hb = 0;
    if (millis() - last_hb > 1000) {
      last_hb = millis();
      Serial.println("Global Loop Alive");
    }
  }

  // Visual Task Update (Non-Blocking, ~100Hz)
  static unsigned long last_visual_update = 0;
  if (millis() - last_visual_update > 10) {
    last_visual_update = millis();
    visualTaskModule.update();
  }
}

// --- OTA Implementation ---
void performOTA() {
  Serial.println("OTA: Starting sequence...");

  // 1. Connect to WiFi
  WiFi.begin(ota_ssid, ota_password);
  Serial.print("OTA: Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 15000) { // 15s Timeout
      Serial.println("\nOTA: WiFi Timeout!");
      return;
    }
  }
  Serial.println("\nOTA: WiFi Connected!");

  // 2. Start Update
  WiFiClient client;
  // Initialize httpUpdate
  httpUpdate.setLedPin(PIN_NEOPIXEL, LOW); // Optional: Blink LED

  // Add headers if needed (S3 often needs Host)
  // httpUpdate.rebootOnUpdate(true); // Default is true

  Serial.println("OTA: Downloading from " + String(ota_host) + String(ota_bin));
  t_httpUpdate_return ret =
      httpUpdate.update(client, ota_host, ota_port, ota_bin);

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    Serial.printf("OTA: HTTP_UPDATE_FAILD Error (%d): %s\n",
                  httpUpdate.getLastError(),
                  httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("OTA: HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("OTA: HTTP_UPDATE_OK");
    break;
  }
}

void handleUsbCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0)
    return;
  if (cmd.equals("j"))
    g_debug_stream_enabled = true;
  else if (cmd.equals("s"))
    g_debug_stream_enabled = false;
  else if (cmd.equals("g")) {
    g_stream_grideye = !g_stream_grideye;
    Serial.println(g_stream_grideye ? "GRIDEYE_ON" : "GRIDEYE_OFF");
  } else if (cmd.startsWith("HEAD,")) {
    int val = cmd.substring(5).toInt();
    g_system_config.heading_mode = (val == 1);
    Serial.println("HEAD_MODE=" + String(val));
    if (g_system_config.heading_mode) {
      Serial.println("LED: HEADING MODE (Winch Override DISABLED)");
    } else {
      Serial.println("LED: WINCH MODE (Use RGB command)");
    }
  } else if (cmd.startsWith("POS,")) {
    // v2.3.6: Unified Handling for USB (Test Consistency)
    handleWinchCommand(cmd);
  } else {
    sendMotorCommand(cmd);
  }
}

void handleWinchCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0)
    return;

  // Debug: Print received command (Clean)
  Serial.println("RX_WINCH: " + cmd);

  // --- OTA Handling ---
  if (cmd.equals("OTA,START")) {
    Serial.println("OTA: Global Trigger Received");
    // 1. Trigger Unit 2
    Serial1.println("OTA");
    delay(500); // Give Unit 2 time to process/block
    // 2. Perform Self OTA
    performOTA();
    return;
  } else if (cmd.equals("OTA,UNIT1")) {
    Serial.println("OTA: Unit 1 Trigger Received");
    performOTA();
    return;
  } else if (cmd.equals("OTA,UNIT2")) {
    Serial.println("OTA: Unit 2 Trigger Received");
    Serial1.println("OTA");
    return;
  }

  if (cmd.startsWith("POS,")) {
    g_winch_connected = true; // Heartbeat
    String content = cmd.substring(4);
    int commaIndex = content.indexOf(',');
    // Extract Pitch (First value)
    float pitch_val = (commaIndex > 0)
                          ? content.substring(0, commaIndex).toFloat()
                          : content.toFloat();

    // Check for Tracking Trigger (Approx 90 deg)
    // v2.3.6: Widen Tolerance for Trigger (85.0 to 95.0) to prevent Noise Jump
    if (fabs(pitch_val - 90.0f) < 5.0f) {
      if (motionManager.current_mode != MODE_TRACKING) {
        Serial.println("TRACKING_REQ: POS,90 found");
        motionManager.setMode(MODE_TRACKING);
      }
    } else {
      if (motionManager.current_mode == MODE_TRACKING) {
        Serial.println("TRACKING_EXIT: " + cmd);
        motionManager.setMode(MODE_NORMAL);
      }

      // NORMAL MODE HANDLING
      if (motionManager.current_mode == MODE_NORMAL) {
        // v2.3.7: STRICT LIMIT CHECK
        // Only allow positions within valid range (+/- 60)
        // Anything else (e.g. 70, 80, 90, 100) is ignored to prevent jumps.
        if (fabs(pitch_val) <= 60.0f) {
          g_last_pitch_cmd = pitch_val;

          // 1. Forward Command to Unit 2 (Unit 2 calculates absolute)
          // FIX: Ensure Unit 2 exits Velocity Mode by appending Yaw arg if
          // missing
          if (cmd.indexOf(',', 4) == -1) {
            sendMotorCommand(cmd + ",0");
          } else {
            sendMotorCommand(cmd);
          }
        } else {
          Serial.println("RX_IGNORE: Out of Range (>60): " + String(pitch_val));
        }

        // 2. Stream IMU is handled in motionManager.update()
      }
    }
  } else if (cmd.startsWith("RGB,")) {
    // RGB Command Logic
    if (!g_system_config.heading_mode) {
      g_winch_color_override = true;
      int firstComma = cmd.indexOf(',');
      int secondComma = cmd.indexOf(',', firstComma + 1);
      int thirdComma = cmd.indexOf(',', secondComma + 1);

      if (firstComma > 0 && secondComma > 0 && thirdComma > 0) {
        g_winch_color_r = cmd.substring(firstComma + 1, secondComma).toInt();
        g_winch_color_g = cmd.substring(secondComma + 1, thirdComma).toInt();
        g_winch_color_b = cmd.substring(thirdComma + 1).toInt();

        // FIX: Immediately update global target for Fader
        g_target_rgb = visualTaskModule.strip.Color(
            g_winch_color_r, g_winch_color_g, g_winch_color_b);
      }
    }
  } else if (cmd.startsWith("Y,")) {
    // Direct Velocity Command (Debugging/Manual)
    // v2.3.8: Pass through to MotionManager if Tracking?
    // No, "Y" is Unit 1 Yaw.
    // But we are in "Winch Command" handler. Winch shouldn't send Y unless
    // debugging. We'll treat it as motor command if needed, but currently
    // MotionManager owns Yaw. Ignore to prevent conflict.
  } else {
    // Forward unknown commands to Unit 2 (e.g. "M2,..." or "CAL")
    sendMotorCommand(cmd);
  }
}

void sendMotorCommand(String cmd) { Serial1.println(cmd); }

void sendTelem() {
  if (g_debug_stream_enabled && g_bno_ok) {
    Serial.printf("{\"qw\":%.3f, \"qx\":%.3f, \"qy\":%.3f, \"qz\":%.3f, "
                  "\"cx\":%.2f, \"cy\":%.2f, \"spY\":%.2f, \"spP\":%.2f}\n",
                  g_sensor_data.quat_w, g_sensor_data.quat_x,
                  g_sensor_data.quat_y, g_sensor_data.quat_z,
                  g_sensor_data.centroid_x, g_sensor_data.centroid_y,
                  g_tracking_yaw_speed, g_tracking_pitch_speed);
  }
}
