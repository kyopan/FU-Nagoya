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
#include <WiFi.h>
#include <Wire.h>

#define FIRMWARE_VERSION "2.3.9"

void performCalibrationSequence(); // Forward Declaration

/*
 * v2.3.9 Changelog:
 * - Tuning (Tracking): Reduced Yaw Velocity Gain to P=5.0 (was 15.0).
 *   - Rationale: High Gain caused "D5m Y_FAULT" (Overcurrent/Acceleration
 * Trip). v2.3.8 Changelog:
 * - Improvement (Tracking): Switch Yaw to VELOCITY CONTROL (`Y,val`).
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
      // Stop velocity commands? No, just send a safe POS?
      // "POS" without args? Or just ensure next Normal loop handles it.
      // Unit 2 will receive Position commands in Normal Mode loop.
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

      // 1. Alignment Phase
      if (is_aligning) {
        unsigned long elapsed = now - align_start_time;
        if (elapsed < ALIGN_DURATION) {
          float progress = (float)elapsed / ALIGN_DURATION;
          // Interpolate current start pitch to Horizon (0.0)
          float target = align_start_pitch * (1.0f - progress);
          trk_pitch_cmd = target;
          // trk_yaw_position held at init value (Current Heading)
        } else {
          is_aligning = false;
          trk_pitch_cmd = 0.0f; // Firm Horizon
          Serial.println("TRK: ALIGN_DONE -> SEARCH/TRACK (Horizon)");
        }
      }
      // 2. Active Tracking (Only if Sensor OK AND Heat Valid)
      else if (g_grideye_ok && valid_thermal) {
        // --- YAW (Velocity Control v2.3.8) ---
        float err_x = cx - 3.5f;
        if (fabs(err_x) > YAW_DEADBAND) {
          // Proportional Velocity Control
          // Target Right (cx > 3.5) -> Err > 0.
          // To Turn Right, we need NEGATIVE Velocity (Inverse Logic confirmed).
          // Gain 5.0 (Reduced from 15.0 in v2.3.9 to prevent Fault).
          float vel_cmd = -err_x * 5.0f;
          trk_yaw_velocity = vel_cmd;
        } else {
          trk_yaw_velocity = 0.0f;
        }

        // --- PITCH (Position Control) ---
        float err_y = cy - 3.5f;
        float pitch_step = 0.0f;
        if (fabs(err_y) > PITCH_DEADBAND) {
          float base_speed = (err_y > 0) ? PITCH_P1 : -PITCH_P1;
          pitch_step = base_speed + (err_y * PITCH_P2);
        }
        // Physics Logic: Hand Up -> Err Negative -> Look Up.
        // POS += Step.
        trk_pitch_cmd += pitch_step;

        // CLAMP
        if (trk_pitch_cmd > PITCH_MAX)
          trk_pitch_cmd = PITCH_MAX;
        if (trk_pitch_cmd < PITCH_MIN)
          trk_pitch_cmd = PITCH_MIN;
      } else {
        // No Heat: Stop Yaw
        trk_yaw_velocity = 0.0f;
      }

      // Debug
      if (now - last_debug_print > 500) {
        last_debug_print = now;
        Serial.printf(
            "TRK_LOOP: cx=%.1f cy=%.1f P=%.1f Y_VEL=%.1f OK=%d Valid=%d\n", cx,
            cy, trk_pitch_cmd, trk_yaw_velocity, g_grideye_ok, valid_thermal);
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
        // Yaw: Velocity (Y,val)
        Serial1.printf("Y,%.2f\n", trk_yaw_velocity);
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
    if (new_color != current_color || breathing_enabled) {
      current_color = new_color;
      for (int i = 0; i < count; i++)
        strip.setPixelColor(start_index + i, current_color);
    }
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
          if (pixels[i] > 26.0f) {
            float val = pixels[i] - 26.0f;
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
      for (int i = 0; i < 24; i++)
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
          sendMotorCommand(cmd);
        } else {
          Serial.println("RX_IGNORE: Out of Range (>60): " + String(pitch_val));
        }

        // 2. Stream IMU is handled in motionManager.update()
      }
    }
  } else if (cmd.startsWith("RGB,")) {
    // Parse using sscanf for robustness
    int r, g, b;
    if (sscanf(cmd.c_str(), "RGB,%d,%d,%d", &r, &g, &b) == 3) {
      g_target_rgb =
          visualTaskModule.strip.Color(r, g, b); // Use Public Strip Instance
      Serial.printf("RX_WINCH_RGB: Parsed R=%d G=%d B=%d -> Color=0x%06X\n", r,
                    g, b, g_target_rgb);

      // Standard Behavior: Set Target color for VisualTask Fader
      // visualTaskModule.strip.fill(g_target_rgb); // Removed Direct Write
      // visualTaskModule.strip.show();
      Serial.println("RX_WINCH_RGB: Target Set for Fader");
    } else {
      Serial.println("RX_WINCH_RGB: Parse Failed (" + cmd + ")");
    }
  } else {
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
