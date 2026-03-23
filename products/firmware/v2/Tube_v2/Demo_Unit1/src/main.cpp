/* ============================================================================
 *  ______     __         __  __     _ __    ___
 * /_  __/_ __/ /  ___   / / / /__  (_) /_  <  /
 *  / / / // / _ \/ -_) / /_/ / _ \/ / __/  / /
 * /_/  \_,_/_.__/\__/  \____/_//_/_/\__/  /_/
 *
 *  FU Tube v2 - Offline Demo (Unit 1 Standalone)
 * ============================================================================
 * 生き物のような振る舞いのデモ
 * - GridEye: 人の体温を感知して反応
 * - BNO055: 自分の姿勢を把握
 * - NeoPixel: 生物発光 (96 body LEDs)
 * - Unit2 (Serial1): チルト/回転モーター制御
 *
 * WiFi / OTA / Winch通信 なし
 * ============================================================================
 * States:
 *   DORMANT    -> ほぼ静止、深い藍色の微光
 *   BREATHING  -> 静かな呼吸、青みがかったシアン
 *   SEARCHING  -> ゆっくりと旋回しながら探索
 *   NOTICING   -> 熱源を感知、アンバー色に変化
 *   CURIOUS    -> 熱源に向けてチルト・追跡
 *   EXCITED    -> 至近距離で興奮、金色に輝きスピン
 *   RETREATING -> 落ち着きを取り戻す
 * ============================================================================
 */

#include <Adafruit_AMG88xx.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#define FIRMWARE_VERSION "demo-1.0"

// --- Pin Definitions ---
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

// LED Groups (each 24 LEDs)
#define GROUP_GR_FRONT_START 0
#define GROUP_GR_BACK_START 24
#define GROUP_CR_FRONT_START 48
#define GROUP_CR_BACK_START 72
#define GROUP_SIZE 24

// --- Hardware Objects ---
Adafruit_NeoPixel strip(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Adafruit_AMG88xx amg;
Adafruit_BNO055 bno(55, 0x28, &Wire1);

bool g_grideye_ok = false;
bool g_bno_ok = false;

// --- Sensor Data ---
float g_euler_x = 0, g_euler_y = 0;
float g_centroid_x = 3.5f, g_centroid_y = 3.5f;
bool g_heat_detected = false;
float g_heat_intensity = 0.0f;
float g_pixels[64];

// ============================================================================
// Color Helpers
// ============================================================================
struct RGB {
  uint8_t r, g, b;
};

// HSV to RGB (h=0..360, s=0..1, v=0..1)
RGB hsv2rgb(float h, float s, float v) {
  h = fmod(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s;
  float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r1, g1, b1;
  if      (h < 60)  { r1=c; g1=x; b1=0; }
  else if (h < 120) { r1=x; g1=c; b1=0; }
  else if (h < 180) { r1=0; g1=c; b1=x; }
  else if (h < 240) { r1=0; g1=x; b1=c; }
  else if (h < 300) { r1=x; g1=0; b1=c; }
  else              { r1=c; g1=0; b1=x; }
  return {
    (uint8_t)min(255, (int)((r1 + m) * 255)),
    (uint8_t)min(255, (int)((g1 + m) * 255)),
    (uint8_t)min(255, (int)((b1 + m) * 255))
  };
}

// Smooth lerp utility
float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

// ============================================================================
// Smooth Float (exponential smoothing)
// ============================================================================
struct SmoothFloat {
  float current = 0.0f;
  float target = 0.0f;
  float alpha = 0.05f; // smoothing factor (0=frozen, 1=instant)

  void set(float v) { current = v; target = v; }
  void moveTo(float t, float a = -1.0f) {
    target = t;
    if (a >= 0) alpha = a;
  }
  void update() {
    float diff = target - current;
    if (fabsf(diff) < 0.001f) current = target;
    else current += diff * alpha;
  }
};

// ============================================================================
// Biological State Machine
// ============================================================================
enum BioState {
  STATE_DORMANT,
  STATE_BREATHING,
  STATE_SEARCHING,
  STATE_NOTICING,
  STATE_CURIOUS,
  STATE_EXCITED,
  STATE_RETREATING
};

BioState g_state = STATE_DORMANT;
unsigned long g_state_enter_ms = 0;
unsigned long g_state_duration_ms = 0;

// Per-state animated parameters
SmoothFloat g_hue;           // Base LED hue (0..360)
SmoothFloat g_sat;           // LED saturation
SmoothFloat g_val;           // LED value (brightness)
SmoothFloat g_breath_rate;   // Breathing phase increment per frame
SmoothFloat g_yaw_vel;       // Yaw velocity sent to Unit2
SmoothFloat g_pitch;         // Pitch target sent to Unit2

float g_breath_phase = 0.0f;

// Timing
unsigned long g_last_sensor_ms = 0;
unsigned long g_last_led_ms = 0;
unsigned long g_last_motor_ms = 0;
unsigned long g_last_eval_ms = 0;

// ============================================================================
// State Transitions
// ============================================================================
void enterState(BioState next, unsigned long duration_ms = 0) {
  if (g_state == next) return;
  g_state = next;
  g_state_enter_ms = millis();
  g_state_duration_ms = duration_ms;

  switch (next) {
    case STATE_DORMANT:    Serial.println("BIO: -> DORMANT");    break;
    case STATE_BREATHING:  Serial.println("BIO: -> BREATHING");  break;
    case STATE_SEARCHING:  Serial.println("BIO: -> SEARCHING");  break;
    case STATE_NOTICING:   Serial.println("BIO: -> NOTICING");   break;
    case STATE_CURIOUS:    Serial.println("BIO: -> CURIOUS");    break;
    case STATE_EXCITED:    Serial.println("BIO: -> EXCITED");    break;
    case STATE_RETREATING: Serial.println("BIO: -> RETREATING"); break;
  }
}

// ============================================================================
// Sensor Update
// ============================================================================
void updateSensors() {
  unsigned long now = millis();
  if (now - g_last_sensor_ms < 50) return;
  g_last_sensor_ms = now;

  // BNO055 orientation
  if (g_bno_ok) {
    imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    g_euler_x = euler.x();
    g_euler_y = euler.y();
  }

  // GridEye thermal centroid
  if (g_grideye_ok) {
    amg.readPixels(g_pixels);
    float sum_val = 0, sum_x = 0, sum_y = 0;
    for (int i = 0; i < 64; i++) {
      float temp = g_pixels[i];
      if (temp > 23.0f) {
        float w = temp - 23.0f;
        sum_val += w;
        sum_x += (i % 8) * w;
        sum_y += (i / 8) * w;
      }
    }
    if (sum_val > 1.5f) {
      g_centroid_x = sum_x / sum_val;
      g_centroid_y = sum_y / sum_val;
      g_heat_intensity = constrain(sum_val / 40.0f, 0.0f, 1.0f);
      g_heat_detected = true;
    } else {
      g_heat_detected = false;
      g_heat_intensity = 0.0f;
    }
  }
}

// ============================================================================
// State Evaluation (decide transitions)
// ============================================================================
void evaluateState() {
  unsigned long now = millis();
  if (now - g_last_eval_ms < 300) return;
  g_last_eval_ms = now;

  unsigned long in_state = now - g_state_enter_ms;

  switch (g_state) {

    case STATE_DORMANT:
      // Wake up after 6-10 seconds
      if (in_state > 6000 + (unsigned long)random(4000)) {
        enterState(STATE_BREATHING, 18000 + (unsigned long)random(12000));
      }
      break;

    case STATE_BREATHING:
      if (g_heat_detected && g_heat_intensity > 0.25f) {
        enterState(STATE_NOTICING, 2500);
      } else if (in_state > g_state_duration_ms) {
        // Occasionally go searching
        if (random(3) == 0) {
          enterState(STATE_SEARCHING, 7000 + (unsigned long)random(5000));
        } else {
          enterState(STATE_BREATHING, 15000 + (unsigned long)random(10000));
        }
      }
      break;

    case STATE_SEARCHING:
      if (g_heat_detected && g_heat_intensity > 0.2f) {
        enterState(STATE_NOTICING, 2000);
      } else if (in_state > g_state_duration_ms) {
        enterState(STATE_BREATHING, 15000 + (unsigned long)random(8000));
      }
      break;

    case STATE_NOTICING:
      if (in_state > g_state_duration_ms) {
        if (g_heat_detected && g_heat_intensity > 0.2f) {
          enterState(STATE_CURIOUS, 10000 + (unsigned long)random(6000));
        } else {
          enterState(STATE_BREATHING, 12000 + (unsigned long)random(8000));
        }
      }
      break;

    case STATE_CURIOUS:
      if (g_heat_detected && g_heat_intensity > 0.7f) {
        enterState(STATE_EXCITED, 5000 + (unsigned long)random(4000));
      } else if (!g_heat_detected && in_state > 4000) {
        enterState(STATE_RETREATING, 5000);
      } else if (in_state > g_state_duration_ms) {
        enterState(STATE_RETREATING, 4000);
      }
      break;

    case STATE_EXCITED:
      if (in_state > g_state_duration_ms) {
        enterState(STATE_RETREATING, 6000);
      }
      break;

    case STATE_RETREATING:
      if (in_state > g_state_duration_ms) {
        enterState(STATE_DORMANT);
      }
      break;
  }
}

// ============================================================================
// Behavior Update (smooth parameter targeting per state)
// ============================================================================
void updateBehavior() {
  float t = millis() / 1000.0f;
  float in_state_t = (millis() - g_state_enter_ms) / 1000.0f;

  switch (g_state) {

    case STATE_DORMANT:
      // Deep indigo, barely alive
      g_hue.moveTo(235.0f, 0.015f);
      g_sat.moveTo(0.75f,  0.015f);
      g_val.moveTo(0.06f,  0.008f);
      g_breath_rate.moveTo(0.007f, 0.008f); // Very slow ~0.07Hz
      g_yaw_vel.moveTo(0.0f,  0.02f);
      g_pitch.moveTo(0.0f,    0.01f);
      break;

    case STATE_BREATHING:
      // Soft bioluminescent cyan-blue
      g_hue.moveTo(190.0f, 0.025f);
      g_sat.moveTo(0.85f,  0.025f);
      g_val.moveTo(0.28f,  0.018f);
      g_breath_rate.moveTo(0.013f, 0.018f); // ~0.13Hz = calm breathing
      // Gentle pendulum tilt
      g_pitch.moveTo(9.0f * sinf(t * 0.18f), 0.025f);
      g_yaw_vel.moveTo(0.0f, 0.025f);
      break;

    case STATE_SEARCHING:
      // Teal, slow scanning rotation
      g_hue.moveTo(165.0f, 0.03f);
      g_sat.moveTo(0.9f,   0.03f);
      g_val.moveTo(0.32f,  0.025f);
      g_breath_rate.moveTo(0.017f, 0.025f);
      // Sweep rotation
      g_yaw_vel.moveTo(10.0f, 0.04f);
      // Slow pitch scan
      g_pitch.moveTo(18.0f * sinf(t * 0.25f), 0.025f);
      break;

    case STATE_NOTICING:
      // Warm amber flash - something found!
      g_hue.moveTo(45.0f, 0.06f);
      g_sat.moveTo(1.0f,  0.06f);
      g_val.moveTo(0.40f, 0.05f);
      g_breath_rate.moveTo(0.022f, 0.04f);
      // Stop rotation, orient toward heat
      g_yaw_vel.moveTo(0.0f, 0.06f);
      if (g_heat_detected) {
        float err_x = g_centroid_x - 3.5f;
        float err_y = g_centroid_y - 3.5f;
        float yaw_cmd = constrain(-err_x * 18.0f, -30.0f, 30.0f); // production: -= err_x
        g_yaw_vel.moveTo(yaw_cmd, 0.05f);
        g_pitch.moveTo(constrain(err_y * -7.0f, -30.0f, 30.0f), 0.04f);
      }
      break;

    case STATE_CURIOUS:
      // Warm orange-gold, actively tracking
      g_hue.moveTo(30.0f + g_heat_intensity * 10.0f, 0.04f);
      g_sat.moveTo(1.0f,  0.04f);
      g_val.moveTo(0.45f + g_heat_intensity * 0.12f, 0.04f);
      g_breath_rate.moveTo(0.025f + g_heat_intensity * 0.015f, 0.03f);
      if (g_heat_detected) {
        float err_x = g_centroid_x - 3.5f;
        float err_y = g_centroid_y - 3.5f;
        float yaw_cmd = constrain(err_x * 22.0f, -40.0f, 40.0f);
        g_yaw_vel.moveTo(yaw_cmd, 0.05f);
        g_pitch.moveTo(constrain(err_y * -12.0f, -45.0f, 45.0f), 0.04f);
      } else {
        // Drift, searching
        g_yaw_vel.moveTo(5.0f * sinf(t * 0.8f), 0.03f);
        g_pitch.moveTo(g_pitch.current * 0.92f, 0.03f);
      }
      break;

    case STATE_EXCITED:
      // Brilliant gold-white, spinning with excitement!
      g_hue.moveTo(42.0f, 0.07f);
      g_sat.moveTo(0.9f + 0.1f * sinf(t * 5.0f), 0.07f);
      g_val.moveTo(0.65f, 0.07f);
      g_breath_rate.moveTo(0.055f, 0.06f); // Rapid pulsing
      // Active spin + pitch oscillation
      g_yaw_vel.moveTo(32.0f + 14.0f * sinf(t * 2.3f), 0.06f);
      g_pitch.moveTo(22.0f * sinf(t * 1.8f), 0.05f);
      break;

    case STATE_RETREATING:
      // Fade back toward blue, decelerate
      g_hue.moveTo(205.0f, 0.02f);
      g_sat.moveTo(0.65f,  0.02f);
      g_val.moveTo(0.14f,  0.015f);
      g_breath_rate.moveTo(0.010f, 0.015f);
      g_yaw_vel.moveTo(0.0f,  0.025f);
      g_pitch.moveTo(0.0f,    0.018f);
      break;
  }

  // Apply smooth updates
  g_hue.update();
  g_sat.update();
  g_val.update();
  g_breath_rate.update();
  g_yaw_vel.update();
  g_pitch.update();
}

// ============================================================================
// Organic shimmer (multi-frequency sin sum, no random calls = smooth)
// ============================================================================
float organicShimmer(int idx, float t, float phase_offset) {
  float f1 = sinf(t * 2.1f + idx * 0.27f + phase_offset);
  float f2 = sinf(t * 1.3f + idx * 0.44f + phase_offset * 1.7f);
  float f3 = sinf(t * 0.8f + idx * 0.13f + phase_offset * 0.5f);
  return 0.82f + 0.18f * ((f1 + f2 + f3) / 3.0f);
}

// Traveling-wave along a group (creates flowing ring effect)
float travelWave(int idx, float t, float speed, float direction = 1.0f) {
  float phase = t * speed - direction * (idx / (float)GROUP_SIZE) * TWO_PI;
  return 0.75f + 0.25f * sinf(phase);
}

// ============================================================================
// LED Rendering
// ============================================================================
void renderLeds() {
  unsigned long now = millis();
  if (now - g_last_led_ms < 16) return; // ~60 fps
  g_last_led_ms = now;

  float t = now / 1000.0f;

  // Advance breathing phase
  g_breath_phase += g_breath_rate.current;
  if (g_breath_phase > TWO_PI) g_breath_phase -= TWO_PI;
  float breath = 0.4f + 0.6f * (0.5f + 0.5f * sinf(g_breath_phase));

  // Base brightness from breath
  float base_val = g_val.current * breath;

  // Ring phase offsets for 4 groups (creates rotational sweep effect)
  // GR_FRONT=0, GR_BACK=PI, CR_FRONT=PI/2, CR_BACK=3*PI/2
  float group_phase[4] = { 0.0f, (float)M_PI, (float)(M_PI / 2.0f), (float)(3.0f * M_PI / 2.0f) };
  int group_starts[4] = {
    GROUP_GR_FRONT_START,
    GROUP_GR_BACK_START,
    GROUP_CR_FRONT_START,
    GROUP_CR_BACK_START
  };

  // Hue variation: slightly different hue per group for depth
  float group_hue_offset[4] = { 0.0f, 8.0f, -5.0f, 12.0f };

  for (int g = 0; g < 4; g++) {
    int start = group_starts[g];
    float gp = group_phase[g];
    float gh = g_hue.current + group_hue_offset[g];

    for (int i = 0; i < GROUP_SIZE; i++) {
      // Organic shimmer
      float shimmer = organicShimmer(i, t, gp);
      // Traveling wave (rotational sweep) - direction alternates per group pair
      float direction = (g < 2) ? 1.0f : -1.0f;
      float wave = travelWave(i, t, 1.2f, direction);
      float mult = shimmer * wave;

      // Saturation modulation: inner shimmer slightly desaturates
      float sat_mod = g_sat.current * (0.9f + 0.1f * sinf(t * 0.7f + i * 0.2f + gp));

      // Final brightness
      float v_final = base_val * mult;
      v_final = constrain(v_final, 0.0f, 1.0f);

      RGB c = hsv2rgb(gh, sat_mod, v_final);
      strip.setPixelColor(start + i, c.r, c.g, c.b);
    }
  }

  // --- State-specific overlays ---

  // NOTICING: amber ripple from center outward
  if (g_state == STATE_NOTICING) {
    float ripple_speed = 3.0f;
    float ripple_t = (millis() - g_state_enter_ms) / 1000.0f;
    for (int g = 0; g < 4; g++) {
      for (int i = 0; i < GROUP_SIZE; i++) {
        float phase = ripple_t * ripple_speed - (i / (float)GROUP_SIZE) * TWO_PI;
        float ripple = 0.5f + 0.5f * sinf(phase);
        int idx = group_starts[g] + i;
        uint32_t current = strip.getPixelColor(idx);
        uint8_t r = min(255, (int)((current >> 16 & 0xFF) + (int)(80 * ripple)));
        uint8_t gv = min(255, (int)((current >> 8 & 0xFF) + (int)(40 * ripple)));
        uint8_t b = max(0, (int)((current & 0xFF) - (int)(40 * ripple)));
        strip.setPixelColor(idx, r, gv, b);
      }
    }
  }

  // CURIOUS/EXCITED: hot-spot highlight on front LEDs facing heat
  if (g_state == STATE_CURIOUS || g_state == STATE_EXCITED) {
    float dist = sqrtf(powf(g_centroid_x - 3.5f, 2) + powf(g_centroid_y - 3.5f, 2));
    float proximity = constrain(1.0f - dist / 5.5f, 0.0f, 1.0f);
    float pulse = 0.5f + 0.5f * sinf(t * (g_state == STATE_EXCITED ? 8.0f : 4.0f));
    float intensity = proximity * pulse;

    // Front groups get warm white-gold highlight
    for (int i = 0; i < GROUP_SIZE; i++) {
      auto addBoost = [&](int idx) {
        uint32_t c = strip.getPixelColor(idx);
        int r = min(255, (int)((c >> 16 & 0xFF) + (int)(100 * intensity)));
        int gv = min(255, (int)((c >> 8 & 0xFF) + (int)(70 * intensity)));
        int b = max(0, (int)((c & 0xFF) - (int)(50 * intensity)));
        strip.setPixelColor(idx, r, gv, b);
      };
      addBoost(GROUP_GR_FRONT_START + i);
      addBoost(GROUP_CR_FRONT_START + i);
    }
  }

  // EXCITED: additional corona shimmer on all groups
  if (g_state == STATE_EXCITED) {
    float flash = 0.4f + 0.6f * fabsf(sinf(t * 7.0f));
    for (int i = 0; i < 96; i++) {
      uint32_t c = strip.getPixelColor(i);
      int r = min(255, (int)((c >> 16 & 0xFF) + (int)(30 * flash)));
      int gv = min(255, (int)((c >> 8 & 0xFF) + (int)(20 * flash)));
      strip.setPixelColor(i, r, gv, (c & 0xFF));
    }
  }

  // SEARCHING: rotating spotlight effect (one bright LED sweeping around)
  if (g_state == STATE_SEARCHING) {
    float sweep = fmodf(t * 0.8f, 1.0f); // 0..1 over time
    int sweep_led = (int)(sweep * 96.0f) % 96;
    for (int d = -2; d <= 2; d++) {
      int idx = (sweep_led + d + 96) % 96;
      uint32_t c = strip.getPixelColor(idx);
      float boost = 1.0f - fabsf((float)d) * 0.4f;
      int r = min(255, (int)((c >> 16 & 0xFF) + (int)(60 * boost)));
      int gv = min(255, (int)((c >> 8 & 0xFF) + (int)(80 * boost)));
      int bv = min(255, (int)((c & 0xFF) + (int)(40 * boost)));
      strip.setPixelColor(idx, r, gv, bv);
    }
  }

  // --- Status LEDs (pulse subtly with breath) ---
  float status_bright = (uint8_t)(4 + 4 * breath);
  strip.setPixelColor(LED_STAT_PWR,  0, 0, status_bright);  // Dim blue: alive
  strip.setPixelColor(LED_STAT_SENS, g_grideye_ok ? strip.Color(0, 0, status_bright)
                                                   : strip.Color(status_bright, 0, 0));
  strip.setPixelColor(LED_STAT_CONN, 0, 0, status_bright);
  strip.setPixelColor(LED_STAT_MOT,  g_bno_ok ? strip.Color(0, 0, status_bright)
                                              : strip.Color(status_bright, 0, 0));
  strip.show();
}

// ============================================================================
// Motor Commands to Unit2 via Serial1
// ============================================================================
void updateMotor() {
  unsigned long now = millis();
  if (now - g_last_motor_ms < 20) return; // 50 Hz (Unit2の制御周期に合わせる)
  g_last_motor_ms = now;

  // --- IMU ストリーム (CRITICAL: Unit2 のyaw reaction wheel制御に必須) ---
  // production code と同様に常時50Hzで送信する
  // Unit2 は I コマンドが来ないとyaw制御を更新しない
  if (g_bno_ok) {
    // Inverted heading (Unit2側の座標系に合わせる)
    float inv_heading = 360.0f - g_euler_x;
    if (inv_heading >= 360.0f) inv_heading -= 360.0f;
    Serial1.printf("I,%.2f,%.2f,%.2f\n", inv_heading, g_euler_y, 0.0f);
  }

  // --- モーター制御コマンド (トラッキング/動作状態のみ) ---
  bool should_command = (g_state == STATE_SEARCHING  ||
                         g_state == STATE_NOTICING   ||
                         g_state == STATE_CURIOUS    ||
                         g_state == STATE_EXCITED    ||
                         g_state == STATE_BREATHING);

  if (should_command) {
    float safe_pitch = constrain(g_pitch.current, -55.0f, 55.0f);
    float safe_yaw   = constrain(g_yaw_vel.current, -45.0f, 45.0f);

    // Pitch position command
    Serial1.printf("POS,%.1f,0\n", safe_pitch);

    // Yaw velocity command
    if (fabsf(safe_yaw) > 0.5f) {
      Serial1.printf("Y%.2f\n", safe_yaw);
    } else {
      Serial1.println("Y0.00");
    }
  } else {
    // DORMANT / RETREATING: 中立位置に戻す
    Serial1.println("POS,0,0");
    Serial1.println("Y0.00");
  }
}

// ============================================================================
// Startup animation: awakening from the deep
// ============================================================================
void startupAnimation() {
  Serial.println("STARTUP: Awakening sequence...");

  // Phase 1: Emerge from darkness (deep violet → blue)
  for (int step = 0; step <= 120; step++) {
    float t = step / 120.0f;
    // Hue: violet (270°) → deep blue (220°)
    float hue = 270.0f - t * 50.0f;
    float val = t * 0.18f;
    RGB c = hsv2rgb(hue, 0.9f, val);
    strip.fill(strip.Color(c.r, c.g, c.b));
    strip.show();
    delay(16);
  }

  // Phase 2: Brief cyan pulse (life signal)
  for (int step = 0; step <= 40; step++) {
    float t = sinf(step / 40.0f * M_PI);
    RGB c = hsv2rgb(185.0f, 0.8f, 0.18f + t * 0.25f);
    strip.fill(strip.Color(c.r, c.g, c.b));
    strip.show();
    delay(16);
  }

  // Phase 3: Fade to dormant glow
  for (int step = 0; step <= 60; step++) {
    float t = step / 60.0f;
    float val = lerpf(0.43f, 0.05f, t);
    RGB c = hsv2rgb(220.0f, 0.8f, val);
    strip.fill(strip.Color(c.r, c.g, c.b));
    strip.show();
    delay(20);
  }

  Serial.println("STARTUP: Animation complete");
}

// ============================================================================
// Calibration sequence (drives Unit2 reaction wheel to establish zero)
// ============================================================================
void calibrationSequence() {
  Serial.println("CAL: Waiting 5s for Unit2 InitFOC...");

  // Show pulsing white during wait
  unsigned long start = millis();
  while (millis() - start < 5000) {
    float t = (millis() - start) / 1000.0f;
    float v = 0.15f + 0.1f * sinf(t * 3.0f);
    RGB c = hsv2rgb(190.0f, 0.5f, v);
    strip.fill(strip.Color(c.r, c.g, c.b));
    strip.show();
    delay(16);
  }

  Serial.println("CAL: Spin Y+20 (4s)...");
  Serial1.println("Y20.00");
  start = millis();
  while (millis() - start < 4000) {
    float t = (millis() - start) / 1000.0f;
    float hue = 170.0f + t * 15.0f;
    RGB c = hsv2rgb(hue, 0.8f, 0.2f + 0.05f * sinf(t * 8.0f));
    strip.fill(strip.Color(c.r, c.g, c.b));
    strip.show();
    delay(16);
  }

  Serial.println("CAL: Spin Y-20 (4s)...");
  Serial1.println("Y-20.00");
  start = millis();
  while (millis() - start < 4000) {
    float t = (millis() - start) / 1000.0f;
    float hue = 185.0f + t * 15.0f;
    RGB c = hsv2rgb(hue, 0.8f, 0.2f + 0.05f * sinf(t * 8.0f));
    strip.fill(strip.Color(c.r, c.g, c.b));
    strip.show();
    delay(16);
  }

  Serial.println("CAL: Stop");
  Serial1.println("Y0.00");
  Serial1.println("POS,0,0");

  Serial.println("CAL: Complete");
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\n\n=== FU TUBE DEMO v%s ===\n", FIRMWARE_VERSION);

  // Unit2 UART
  Serial1.begin(115200, SERIAL_8N1, -1, D8);
  Serial.println("Serial1 (Unit2) OK [D8]");

  // NeoPixel boot glow
  strip.begin();
  strip.setBrightness(128);
  strip.fill(strip.Color(3, 1, 8)); // Deep indigo
  strip.show();

  // GridEye
  Wire.begin(PIN_GRIDEYE_SDA, PIN_GRIDEYE_SCL);
  if (amg.begin()) {
    g_grideye_ok = true;
    Serial.println("GridEye OK");
  } else {
    Serial.println("GridEye FAIL (continuing)");
  }

  // BNO055
  Wire1.begin(PIN_BNO_SDA, PIN_BNO_SCL);
  if (bno.begin()) {
    g_bno_ok = true;
    bno.setExtCrystalUse(true);
    Serial.println("BNO055 OK");
  } else {
    Serial.println("BNO055 FAIL (continuing)");
  }

  // Initial smooth values
  g_hue.set(230.0f);
  g_sat.set(0.75f);
  g_val.set(0.05f);
  g_breath_rate.set(0.007f);
  g_yaw_vel.set(0.0f);
  g_pitch.set(0.0f);

  // Startup animation
  startupAnimation();

  // Calibration (Unit2 motor initialization)
  calibrationSequence();

  // Begin biological behavior
  enterState(STATE_DORMANT);

  Serial.println("=== DEMO RUNNING ===");
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  updateSensors();
  evaluateState();
  updateBehavior();
  renderLeds();
  updateMotor();
}
