/***********************************************************************
 *   ________  ______  ______   __  _______
 *  /_  __/ / / / __ )/ ____/  / / / /__  /
 *   / / / / / / __  / __/    / / / / /_ <
 *  / / / /_/ / /_/ / /___   / /_/ /___/ /
 * /_/  \____/_____/_____/   \____//____/
 *
 *
 * Author: Kyopalab. LLC
 * Email: kyono@kyopalab.com
 * Creation Date: 2025/06/20
 * Version: 0.3
 * License: Proprietary License
 * MCU: Waveshare RP2040-Zero
 ************************************************************************/
// Waveshare RP2040 Zero
// https://www.waveshare.com/wiki/RP2040-Zero
// https://www.waveshare.com/w/upload/4/4c/RP2040_Zero.pdf
// https://www.waveshare.com/img/devkit/RP2040-Zero/RP2040-Zero-details-7.jpg
//
// https://www.mischianti.org/2022/09/19/waveshare-rp2040-zero-high-resolution-pinout-and-specs/
// https://www.mischianti.org/wp-content/uploads/2022/09/Waveshare-rp2040-zero-Raspberry-Pi-Pico-alternative-pinout.jpg

/*
                Pin#              Pin#
                    ___(_____)___
              5v 1 |   *USB C*   | 23 GPIO0
             GND 2 |             | 22 GPIO1
            3.3v 3 |             | 21 GPIO2
          GPIO29 4 |             | 20 GPIO3
          GPIO28 5 |             | 19 GPIO4
          GPIO27 6 |             | 18 GPIO5
          GPIO26 7 |             | 17 GPIO6
          GPIO15 8 |             | 16 GPIO7
          GPIO14 9 |__|_|_|_|_|__| 15 GPIO8
                      1 1 1 1 1
                      0 1 2 3 4

                    Pin10 = GPIO13
                    Pin11 = GPIO12
                    Pin12 = GPIO11
                    Pin13 = GPIO10
                    Pin14 = GPIO9
*/

#include <Wire.h>
#include <SimpleFOC.h>
#include <Adafruit_NeoPixel.h>

#define PIN_ONBOARD_LED   16
#define PIN_NEO           29

#define PIN_U1_RX         1 // U1(Tube RP2040Zero)へUART1送受信
#define PIN_U1_TX         0 // U1(Tube RP2040Zero)へUART1送受信
#define PIN_U4_RX         9 // U4(Tube RP2040Zero)へUART1送受信
#define PIN_U4_TX         8 // U4(Tube RP2040Zero)へUART1送受信

#define PIN_PWM_U         11
#define PIN_PWM_V         12
#define PIN_PWM_W         13

#define NUM_LEDS          24 // U3正面の24個のLEDリングのみ使用

// AS5600  Motor Encoder
#define PIN_SDA0          4
#define PIN_SCL0          5

// GRIDEYE
#define PIN_SDA1          26
#define PIN_SCL1          27

#define BAUD_USB          115200
#define BAUD_U1           38400
#define BAUD_U4           38400
#define DEBUG             false

//――― 通信最適化設定 ―――
#define SENSOR_UPDATE_HZ       50    // センサーデータ更新頻度
#define CONTROL_UPDATE_HZ      30    // 制御ロジック更新頻度
#define SMOOTHING_UPDATE_HZ    166   // スムーズング処理頻度
#define U4_COMMAND_HZ          20    // U4コマンド送信頻度

//――― 制御パラメータ ―――
#define MOTOR_OFFSET_DEGREES   60.0f  // モーターオフセット
#define PITCH_DEADBAND         0.3f   // ピッチ制御デッドバンド
#define MAX_CHANGE_PER_CYCLE   2.0f   // 制御周期あたり最大変化

// ---------- 受信用バッファ ----------
char rxBufU1[64]; uint8_t rxPosU1 = 0;

// AMG8833 I2C address : 0x69
const uint8_t AMG88XX_ADDR = 0x69;

Adafruit_NeoPixel pixel(1, PIN_ONBOARD_LED);
Adafruit_NeoPixel ring(NUM_LEDS, PIN_NEO);

// magnetic sensor instance - MagneticSensorI2C
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);

// BLDC motor & driver instance
BLDCMotor motor = BLDCMotor(7, 6.5, 330);
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_PWM_U, PIN_PWM_V, PIN_PWM_W);

const uint8_t RX_BUFFER_SIZE = 64;
char rxBuffer[RX_BUFFER_SIZE];   // buffer to accumulate incoming UART line
uint8_t rxIndex = 0;             // current buffer index

//――― 共有変数（core0⇔core1） ―――
volatile float g_target_rad     = 0.0f;
volatile float g_smoothed_target_rad = 0.0f;      // スムーズング済みターゲット
volatile float g_velocity_limit = 4.0f * 0.5f;    // 初期50%
volatile float g_output_ramp    = 1000.0f * 0.5f; // 初期50%
volatile float sharedPosition = 0.0;

//――― 速度制御+位置フィードバック用変数 ―――
volatile float g_target_velocity = 0.0f;          // 目標速度
volatile float g_position_error = 0.0f;           // 位置誤差
volatile float g_filtered_velocity = 0.0f;        // フィルタ済み速度
//――― シリアル受信バッファ ―――
String serialBuffer;

// 座標系・姿勢制御用変数
float g_pitch_angle = 0.0f;        // ピッチ角度（前後傾き）[度]
float g_roll_angle = 0.0f;         // ロール角度（左右傾き）[度] 

// 制御目標値
float g_target_pitch = 0.0f;       // 目標ピッチ角 [度] ±60°

// U1からの姿勢指令データ
struct PYData {
  int16_t pitchC;   // ピッチ×100 (centi-degrees)
  uint16_t yawC;    // ヨー×100 (centi-degrees)
};
PYData g_u1_posture_cmd = {0, 0};
volatile bool g_u1_posture_received = false;
unsigned long g_last_u1_posture_time = 0;  // 最後にU1データを受信した時刻

volatile bool is_calibration_active = true; 
volatile bool is_calibration_finished = false; 

// 制御モード
enum ControlMode {
  MODE_MANUAL_CONTROL,    // 手動制御モード（U1からの指令）
  MODE_STABILIZATION,     // 安定化モード
  MODE_SPARK              // スパークモード
};
ControlMode g_control_mode = MODE_STABILIZATION;  // 起動時は安定化モード（水平維持）

// SPARK mode variables
bool g_spark_mode_active = false;          // SPARKモード状態
unsigned long g_spark_start_time = 0;      // SPARKモード開始時刻
unsigned long g_spark_duration = 0;        // SPARK持続時間（秒）
float g_spark_target = 0.0f;               // SPARK動作の現在ターゲット
bool g_spark_direction = true;             // SPARK動作方向（true=正方向, false=負方向）
unsigned long g_spark_last_change = 0;     // 最後の方向転換時刻
unsigned long g_led_spark_last_update = 0; // LED SPARK演出の最終更新時刻
ControlMode g_previous_control_mode = MODE_STABILIZATION; // SPARK前の制御モード保存

// 補正済み姿勢データ
int16_t g_corrected_accX = 0, g_corrected_accY = 0, g_corrected_accZ = 0;
bool g_attitude_data_valid = false;
float motor_calibration_active_offset = 0;

// マッピング情報を受信するためのフラグ
bool g_suppress_debug = true;

// 目の動きのようなLED演出用変数
float g_eye_position = 0.0f;           // 現在の目の位置（0-23）
float g_eye_target_position = 0.0f;    // 目標の目の位置
unsigned long g_last_eye_update = 0;   // 最後の目の更新時刻
const unsigned long EYE_UPDATE_INTERVAL = 50; // 目の更新間隔（ms）

// U1からのLEDデータ保持用変数
uint8_t g_u1_led_r = 0, g_u1_led_g = 0, g_u1_led_b = 0; // 初期値：黒

// スムーズな色変化のための変数
float g_current_led_r = 0.0f, g_current_led_g = 0.0f, g_current_led_b = 0.0f; // 現在の色（float精度）
uint8_t g_target_led_r = 0, g_target_led_g = 0, g_target_led_b = 0;   // 目標の色
unsigned long g_last_color_update = 0;
const unsigned long COLOR_TRANSITION_INTERVAL = 20; // 20ms間隔で色を更新

// Timing
unsigned long lastGy85Time = 0;
unsigned long lastThermTime = 0;

const float ROLL_THRESHOLD = 5.0;
int isOffsetSet = 0;


// スムーズな色変化関数
void subtleChromaticShift() {
  unsigned long current_time = millis();
  
  if (current_time - g_last_color_update < COLOR_TRANSITION_INTERVAL) return;
  g_last_color_update = current_time;
  
  // 補間係数（0.0-1.0）
  const float INTERPOLATION_SPEED = 0.15f; // 遅めの変化でスムーズに
  
  // 各色成分を線形補間（float精度で計算）
  float r_diff = (float)g_target_led_r - g_current_led_r;
  float g_diff = (float)g_target_led_g - g_current_led_g;
  float b_diff = (float)g_target_led_b - g_current_led_b;
  
  g_current_led_r += r_diff * INTERPOLATION_SPEED;
  g_current_led_g += g_diff * INTERPOLATION_SPEED;
  g_current_led_b += b_diff * INTERPOLATION_SPEED;
  
  // SPARKモード中は色変化をスキップ
  if (g_spark_mode_active) return;
  
  // すべてのLEDに現在の色を適用（整数値にキャスト）
  uint8_t current_r = (uint8_t)(g_current_led_r + 0.5f); // 四捨五入
  uint8_t current_g = (uint8_t)(g_current_led_g + 0.5f);
  uint8_t current_b = (uint8_t)(g_current_led_b + 0.5f);
  
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(current_r, current_g, current_b));
  }
  ring.show();
  
  // オンボードLEDも同期
  pixel.setPixelColor(0, pixel.Color(current_r, current_g, current_b));
  pixel.show();
}

// 位置フィードバック制御関数（速度制御用）
void calculate_position_feedback_velocity() {
  // 現在位置取得
  float current_position = motor.shaftAngle();
  
  // 位置誤差計算
  g_position_error = g_smoothed_target_rad - current_position;
  
  // P制御で速度指令を計算（応答性向上）
  const float VELOCITY_P_GAIN = 2.5f; // 位置誤差→速度変換ゲイン（応答性向上）
  g_target_velocity = g_position_error * VELOCITY_P_GAIN;
  
  // 速度制限適用
  const float MAX_VELOCITY = 1.2f; // 最大速度を向上（応答性向上）
  g_target_velocity = constrain(g_target_velocity, -MAX_VELOCITY, MAX_VELOCITY);
  
  // デッドバンド適用（小さな誤差では制御しない）
  const float POSITION_DEADBAND = 0.015f; // 約0.86度のデッドバンド
  if (abs(g_position_error) < POSITION_DEADBAND) {
    g_target_velocity = 0.0f;
  }
}

// 速度指令のフィルタリング関数
void filter_velocity_command() {
  // ローパスフィルターで速度指令を滑らかに（応答性向上）
  const float VELOCITY_FILTER_ALPHA = 0.4f; // フィルタ係数向上：応答性重視
  g_filtered_velocity = g_filtered_velocity * (1.0f - VELOCITY_FILTER_ALPHA) + g_target_velocity * VELOCITY_FILTER_ALPHA;
  
  // レート制限（急激な変化を防ぐ）
  const float MAX_VELOCITY_CHANGE = 0.2f; // 1制御周期での最大変化（応答性向上）
  static float prev_filtered_velocity = 0.0f;
  
  float velocity_diff = g_filtered_velocity - prev_filtered_velocity;
  if (abs(velocity_diff) > MAX_VELOCITY_CHANGE) {
    if (velocity_diff > 0) {
      g_filtered_velocity = prev_filtered_velocity + MAX_VELOCITY_CHANGE;
    } else {
      g_filtered_velocity = prev_filtered_velocity - MAX_VELOCITY_CHANGE;
    }
  }
  
  prev_filtered_velocity = g_filtered_velocity;
}

// ターゲット位置のスムーズング関数（速度制御用に調整）
void smooth_target_position() {
  // ローパスフィルターによるスムーズング（応答性向上）
  const float SMOOTHING_FACTOR = 0.3f; // スムーズング係数向上：応答性重視
  
  // 指数移動平均によるスムーズング
  g_smoothed_target_rad = g_smoothed_target_rad * (1.0f - SMOOTHING_FACTOR) + g_target_rad * SMOOTHING_FACTOR;
  
  // 追加のレート制限（応答性向上）
  const float MAX_VELOCITY_RAD_PER_SEC = 1.0f; // 最大角速度向上
  const float MAX_CHANGE_PER_10MS = MAX_VELOCITY_RAD_PER_SEC * 0.01f; // 10ms間隔での最大変化
  
  static float prev_smoothed_target = 0.0f;
  float velocity_limited_target = prev_smoothed_target;
  
  float target_diff = g_smoothed_target_rad - prev_smoothed_target;
  if (abs(target_diff) > MAX_CHANGE_PER_10MS) {
    if (target_diff > 0) {
      velocity_limited_target = prev_smoothed_target + MAX_CHANGE_PER_10MS;
    } else {
      velocity_limited_target = prev_smoothed_target - MAX_CHANGE_PER_10MS;
    }
  } else {
    velocity_limited_target = g_smoothed_target_rad;
  }
  
  g_smoothed_target_rad = velocity_limited_target;
  prev_smoothed_target = g_smoothed_target_rad;
}

uint32_t bluePurple = ring.Color(48, 23, 155);
uint32_t white = ring.Color(118, 118, 118);
uint32_t blue = ring.Color(0, 0, 255);
uint32_t red = ring.Color(255, 0, 0);
uint32_t green = ring.Color(0, 255, 0);
uint32_t pink = ring.Color(100, 50, 50);
uint32_t black = ring.Color(0, 0, 0);

void initialize(){
  Serial.begin(BAUD_USB);
  Serial1.begin(BAUD_U1); // RP2040_U3(This)<->ESP32C6_U1
  Serial2.begin(BAUD_U4); // RP2040_U3(This)<->RP2040_U4
  amg8833_init();

  pixel.begin();
  pixel.setPixelColor(0, pink);
  pixel.show();

  ring.begin();
  ring.clear();

}

// ---------------------------
// CORE 0
// ---------------------------
void setup() {
  initialize();
  ring.setBrightness(255);
  
  startupAnimation();
  gridEyeTest();
}

unsigned long calibration_start_time = 0;
void loop() {
  /*
   * CORE 0 Function
   *   1. Serial send/recieve
   *     1.1 UART1: U3(This) <-> U1(ESP32)
   *     1.2 UART2: U3(This) <-> U4(RP2040)
   *
   *   2. AMG8833 GridEye
   */

  // read_data_from_u4(); // 削除：U4からのGY85データ受信を停止

  // 姿勢角・方位角計算（GY85データから）- 削除（簡素化）
  // static unsigned long lastAttitudeCalc = 0;
  // if (millis() - lastAttitudeCalc >= (1000 / SENSOR_UPDATE_HZ)) {
  //   lastAttitudeCalc = millis();
  //   calculate_attitude_and_heading();
  // }


  // 制御ロジック実行
  static unsigned long lastControlUpdate = 0;
  if (millis() - lastControlUpdate >= (1000 / CONTROL_UPDATE_HZ)) {
    lastControlUpdate = millis();
    execute_control_logic();
  }
  
  // ターゲット値のスムーズング（応答性向上）
  static unsigned long lastSmoothingUpdate = 0;
  if (millis() - lastSmoothingUpdate >= (1000 / SMOOTHING_UPDATE_HZ)) {
    lastSmoothingUpdate = millis();
    smooth_target_position();
  }
  
  // LED演出の優先度制御
  if (g_spark_mode_active) {
    // SPARKモード中はスパーク演出を最優先
    update_spark_animation();
  } else {
    // 通常時はスムーズな色変化を実行
    subtleChromaticShift();
  }

  handleU1Receive();

  // if (is_calibration_finished) {
  //   Serial.print("Calibrated angle: ");
  //   Serial.print(motor_calibration_active_offset * RAD_TO_DEG);
  //   Serial.print(" ");
  //   Serial.print("Current Speed: ");
  //   Serial.println(g_target_velocity);
  // }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer == "C") {
        if (!is_calibration_finished) {
          is_calibration_active = true;
          calibration_start_time = millis();
          for (uint16_t i = 0; i < NUM_LEDS; i++) {
            ring.setPixelColor(i, ring.Color(200, 200, 0));
          }
          ring.show();
        }
      }
      serialBuffer = "";
    } else {
      serialBuffer += c;
    }
  }

  if (is_calibration_active) {
    if (millis() - calibration_start_time > 1000) {
      Serial.print("Hello!!!");
      is_calibration_active = false;
    }
  }
}

// ---------------------------
// CORE 1
// ---------------------------
void setup1() {
  Wire.begin(); // For AS5600
  sensor.init();

  // ドライバ初期化
  driver.voltage_power_supply = 5;
  driver.init();

  // モーターとリンク
  motor.linkDriver(&driver);
  motor.linkSensor(&sensor);

  // 制御モード設定：速度制御+位置フィードバック（クリック音削減）
  motor.controller = MotionControlType::velocity;

  // 速度制御用PIDパラメータ（応答性向上・クリック音削減）
  motor.voltage_limit           = 1.5;   // 電圧制限
  motor.PID_velocity.P          = 0.1;   // 速度P向上：応答性重視
  motor.PID_velocity.I          = 0.3;   // 速度I向上：定常偏差減少
  motor.PID_velocity.D          = 0.01;  // 速度D適度に向上：振動抑制
  motor.LPF_velocity.Tf         = 0.15;  // LPF軽減：応答性向上
  motor.velocity_limit          = 1.3;   // 速度制限向上：応答性重視
  motor.PID_velocity.output_ramp = 150.0; // ランプ適度に向上：応答性とスムーズさのバランス

  // FOC 初期化
  motor.init();
  motor.initFOC();
}

void loop1() {
  /*
   * CORE 1 Function (速度制御+位置フィードバック版)
   *   1. BLDC Control with velocity mode
   *     1.1 DRV8311 Control with FOC
   *     1.2 Read AS5600 magnet encoder
   *     1.3 Position feedback to velocity control
   */
  motor.loopFOC();
  
  if (is_calibration_active && !is_calibration_finished) {
    motor_calibration_active_offset = motor.shaftAngle();
    is_calibration_finished = true;
  }
  // 位置フィードバック制御ロジック実行
  calculate_position_feedback_velocity();
  filter_velocity_command();
  
  if (is_calibration_finished)
  // 速度制御でモーター駆動（クリック音削減）
  motor.move(g_filtered_velocity);
}

void amg8833_init(){
  // For AMG8833
  Wire1.begin();
  // **AMG8833センサの初期化**
  // ソフトリセット: レジスタ0x01に0x3Fを書き込み、初期状態にリセット [oai_citation:16‡cdn-learn.adafruit.com](https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf?1498680225#:~:text=command%20Operating%20mode%200x30%20Flag,Mode%201%3A%201FPS%200%3A%2010FPS)
  Wire1.beginTransmission(AMG88XX_ADDR);
  Wire1.write(0x01);           // Reset register (0x01)
  Wire1.write(0x3F);           // Initial reset command (0x3F) [oai_citation:17‡cdn-learn.adafruit.com](https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf?1498680225#:~:text=command%20Operating%20mode%200x30%20Flag,Setting%20Frame%20Mode%201%3A%201FPS)
  Wire1.endTransmission();
  delay(2);  // リセット後はわずかな待ち時間

  // 動作モード設定: レジスタ0x00(PCTL)に0x00を書き込みノーマルモードへ [oai_citation:18‡cdn-learn.adafruit.com](https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf?1498680225#:~:text=0x00%20Normal%20mode%200x10%20Sleep,mode)
  Wire1.beginTransmission(AMG88XX_ADDR);
  Wire1.write(0x00);           // Power Control register (PCTL, 0x00)
  Wire1.write(0x00);           // 0x00 = Normal mode
  Wire1.endTransmission();

  // フレームレート設定: レジスタ0x02(FPSC)に0x00を書き込み10FPSに設定（初期値0x00） [oai_citation:19‡cdn-learn.adafruit.com](https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf?1498680225#:~:text=,Mode%201%3A%201FPS%200%3A%2010FPS)
  Wire1.beginTransmission(AMG88XX_ADDR);
  Wire1.write(0x02);           // Frame Rate register (0x02)
  Wire1.write(0x00);           // 0x00 = 10 FPS (0x01 would set 1 FPS)
  Wire1.endTransmission();
}

// ----- Simplified Startup Animation -----
void startupAnimation() {
  const unsigned long totalDuration = 5000; // 5秒間の演出
  const unsigned long fadeInDuration = 1000;  // 1秒フェードイン
  const unsigned long fadeOutDuration = 1000; // 1秒フェードアウト
  const unsigned long breathingDuration = totalDuration - fadeInDuration - fadeOutDuration; // 3秒間の呼吸
  
  unsigned long startTime = millis();
  
  for (unsigned long elapsed = 0; elapsed < totalDuration; elapsed += 50) {
    ring.clear();
    float globalIntensity = 1.0f;
    
    // フェードイン (0-1秒)
    if (elapsed < fadeInDuration) {
      globalIntensity = (float)elapsed / fadeInDuration;
    }
    // フェードアウト (4-5秒)
    else if (elapsed >= (totalDuration - fadeOutDuration)) {
      float fadeProgress = (elapsed - (totalDuration - fadeOutDuration)) / (float)fadeOutDuration;
      globalIntensity = 1.0f - fadeProgress;
    }
    
    // 呼吸のような脈動パターン（フェードイン・アウト中も継続）
    float breathPhase = (elapsed / 1000.0f) * PI; // 1秒で半周期（ゆっくりとした呼吸）
    float breathCycle = sin(breathPhase) * 0.3f + 0.7f; // 0.4-1.0の範囲で脈動
    
    for (int i = 0; i < NUM_LEDS; i++) {
      // 各LEDの位置に基づく波紋効果
      float ledPhase = (float)i / NUM_LEDS * 2 * PI;
      float waveIntensity = sin(ledPhase + (elapsed / 200.0f)) * 0.2f + 0.8f; // 0.6-1.0の範囲
      
      // 最終的な明度計算
      float finalIntensity = globalIntensity * breathCycle * waveIntensity;
      
      // 生命の色：深いピンク→紫→青のグラデーション
      float colorPhase = ((elapsed / 1000.0f) + (float)i / NUM_LEDS) * 2 * PI;
      uint8_t r = (uint8_t)(120 * finalIntensity * (0.8f + 0.2f * sin(colorPhase)));
      uint8_t g = (uint8_t)(60 * finalIntensity * (0.6f + 0.4f * sin(colorPhase + PI/3)));
      uint8_t b = (uint8_t)(200 * finalIntensity * (0.7f + 0.3f * sin(colorPhase + PI/2)));
      
      ring.setPixelColor(i, ring.Color(r, g, b));
    }
    
    ring.show();
    delay(50);
  }
  
  // 完全消灯
  ring.clear();
  ring.show();
  
  // オンボードLEDも同期して消灯
  pixel.setPixelColor(0, pixel.Color(0, 0, 0));
  pixel.show();
}

void update_spark_animation() {
  if (!g_spark_mode_active) return;
  
  unsigned long current_time = millis();
  const unsigned long SPARK_LED_UPDATE_INTERVAL = 30; // 30ms更新で高速演出
  
  if (current_time - g_led_spark_last_update < SPARK_LED_UPDATE_INTERVAL) return;
  g_led_spark_last_update = current_time;
  
  // LEDをクリア
  ring.clear();
  
  // スパーク演出：高速でランダムに光る稲妻エフェクト
  static uint8_t spark_phase = 0;
  spark_phase = (spark_phase + 1) % 255;
  
  // 複数のスパーク点を同時に表示
  for (int spark_num = 0; spark_num < 5; spark_num++) {
    // 各スパークの位置を疑似ランダムで決定
    uint8_t spark_seed = (current_time / 50 + spark_num * 73) % 255;
    int led_pos = (spark_seed * spark_num + spark_phase) % NUM_LEDS;
    
    // スパークの明度を計算（高速点滅）
    float spark_intensity = sin((current_time + spark_num * 200) * 0.02f) * 0.5f + 0.5f;
    spark_intensity = pow(spark_intensity, 0.5f); // ガンマ補正で明度を強調
    
    if (spark_intensity > 0.3f) { // 閾値以上の時のみ点灯
      // 白～青白い稲妻色
      uint8_t white_component = 255 * spark_intensity;
      uint8_t blue_component = 200 * spark_intensity;
      uint8_t red_component = 150 * spark_intensity;
      
      ring.setPixelColor(led_pos, ring.Color(red_component, white_component, blue_component));
      
      // 隣接LEDにも微弱な光を拡散
      int prev_led = (led_pos - 1 + NUM_LEDS) % NUM_LEDS;
      int next_led = (led_pos + 1) % NUM_LEDS;
      
      uint8_t diffuse_intensity = spark_intensity * 0.3f;
      ring.setPixelColor(prev_led, ring.Color(red_component * diffuse_intensity, 
                                               white_component * diffuse_intensity, 
                                               blue_component * diffuse_intensity));
      ring.setPixelColor(next_led, ring.Color(red_component * diffuse_intensity, 
                                               white_component * diffuse_intensity, 
                                               blue_component * diffuse_intensity));
    }
  }
  
  // 全体的なフラッシュ効果（低頻度で全LEDが一瞬光る）
  if ((current_time % 800) < 50) { // 800msに一度、50ms間全体フラッシュ
    uint8_t flash_intensity = 150;
    for (int i = 0; i < NUM_LEDS; i++) {
      ring.setPixelColor(i, ring.Color(flash_intensity, flash_intensity * 1.2, flash_intensity * 0.8));
    }
  }
  
  ring.show();
  
  // オンボードLEDも同期してスパーク
  uint8_t onboard_intensity = (sin(current_time * 0.03f) * 0.5f + 0.5f) * 255;
  pixel.setPixelColor(0, pixel.Color(onboard_intensity * 0.6, onboard_intensity, onboard_intensity * 0.8));
  pixel.show();
}


void update_spark_mode() {
  if (!g_spark_mode_active) return;
  
  unsigned long current_time = millis();
  
  // SPARK期間終了チェック
  if (current_time - g_spark_start_time >= g_spark_duration) {
    // SPARKモード終了
    g_spark_mode_active = false;
    g_control_mode = g_previous_control_mode; // 元のモードに復帰
    g_target_pitch = 0.0f; // 水平に戻す
    Serial.println("SPARK mode ended - returning to normal operation");
    return;
  }
  
  // 激しい前後動作（50ms毎に方向転換）
  const unsigned long SPARK_CHANGE_INTERVAL = 50; // ms
  if (current_time - g_spark_last_change >= SPARK_CHANGE_INTERVAL) {
    g_spark_direction = !g_spark_direction;
    g_spark_last_change = current_time;
    
    // 最大速度で±45度の範囲で激しく動く
    const float MAX_SPARK_ANGLE = 45.0f; // 度
    g_spark_target = g_spark_direction ? MAX_SPARK_ANGLE : -MAX_SPARK_ANGLE;
    g_target_pitch = g_spark_target;
  }
}

void execute_control_logic() {
  // SPARKモードチェック（最優先）
  update_spark_mode();
  if (g_spark_mode_active) {
    // SPARKモード中は他の制御をスキップ
    return;
  }
  
  // U1データを使用
  if (g_u1_posture_received) {
    float pitch_deg = g_u1_posture_cmd.pitchC / 100.0f;  // centi-degree → 度単位

    // U1 Pitch指令を目標値に設定
    g_target_pitch = constrain(pitch_deg, -60.0f, 60.0f);
    // フラグをリセット（次の指令まで待機）
    g_u1_posture_received = false;
  }
  
  // ピッチモーター制御（絶対位置制御）
  float pitch_error = g_target_pitch - g_pitch_angle;

  // 絶対位置制御：目標ピッチ角を直接モーター角度に変換
  // モーター制御方向の調整
  float control_gain = -1.0f;  // 制御方向（球体のピッチと逆方向にモーターが動く）
  
  // デッドバンド追加：小さな誤差では制御しない
  if (abs(pitch_error) < PITCH_DEADBAND) {
    // デッドバンド内では目標値を変更しない
    // g_target_rad は前回値を維持
  } else {
    // 絶対位置制御：目標ピッチ角を直接モーター角度として設定（オフセット補正付き）
    float motor_angle_deg = (g_target_pitch * control_gain) + MOTOR_OFFSET_DEGREES;
    float new_target = constrain(motor_angle_deg, 0.0f, 120.0f) * DEG_TO_RAD;
    
    // 急激な変化を制限（設定値/制御周期で応答性向上）
    const float MAX_CHANGE_PER_CYCLE_RAD = MAX_CHANGE_PER_CYCLE * DEG_TO_RAD;
    float target_diff = new_target - g_target_rad;
    if (abs(target_diff) > MAX_CHANGE_PER_CYCLE_RAD) {
      if (target_diff > 0) {
        g_target_rad += MAX_CHANGE_PER_CYCLE_RAD;
      } else {
        g_target_rad -= MAX_CHANGE_PER_CYCLE_RAD;
      }
    } else {
      g_target_rad = new_target;
    }
  }
}

/* -------------------- U1 受信 -------------------- */
void handleU1Receive() {
  // テキストデータ受信（統一形式）
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {      // 行が完成
          rxBufU1[rxPosU1] = '\0';
          // U1からの統合パケット "DATA,F,r,g,b,R,r,g,b,P,pitchF,yawF,B,brightness,H,enc_horizontal" 形式の解析
      if (strncmp(rxBufU1, "DATA,", 5) == 0) {
        uint8_t f_r, f_g, f_b, r_r, r_g, r_b, brightness;
        float pitchF, yawF, pitch_ref;
        if (sscanf(rxBufU1, "DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%f,%f,B,%hhu,H,%f", 
                       &f_r, &f_g, &f_b, &r_r, &r_g, &r_b, &pitchF, &yawF, &brightness, &pitch_ref) == 10) {

          Serial.printf(
            "U1→U3: F=%u,%u,%u, R=%u,%u,%u, P=%u, Y=%u, B=%u, R=%u\n",
            f_r, f_g, f_b,
            r_r, r_g, r_b,
            pitchF, yawF,
            brightness,
            pitch_ref
          );
              
              // U3のLED輝度設定
              ring.setBrightness(brightness);
              pixel.setBrightness(brightness);
              
              // 目標色を設定（実際の色変化はsubtleChromaticShift()で処理）
              g_target_led_r = f_r;
              g_target_led_g = f_g;
              g_target_led_b = f_b;
              
              // U4に統合パケット形式で送信
              Serial2.printf("DATA,R,%u,%u,%u,Y,%u,B,%u\n", r_r, r_g, r_b, yawF, brightness);
              Serial.printf("U1→U3→U4: R=%u,%u,%u, Y=%u, B=%u\n", r_r, r_g, r_b, yawF, brightness);
            }
      } else if (strncmp(rxBufU1, "C", 1) == 0) {
        if (!is_calibration_finished) {
          is_calibration_active = true;
          calibration_start_time = millis();
          for (uint16_t i = 0; i < NUM_LEDS; i++) {
            ring.setPixelColor(i, ring.Color(200, 200, 0));
          }
          ring.show();
        }
          }
          // SPARK command parsing "SPARK,duration_seconds"
          else if (strncmp(rxBufU1, "SPARK,", 6) == 0) {
            float duration_seconds;
            if (sscanf(rxBufU1, "SPARK,%f", &duration_seconds) == 1) {
              // SPARKモード開始
              g_previous_control_mode = g_control_mode;  // 現在のモードを保存
              g_control_mode = MODE_SPARK;
              g_spark_mode_active = true;
              g_spark_start_time = millis();
              g_spark_duration = (unsigned long)(duration_seconds * 1000.0f); // 秒→ミリ秒変換
              g_spark_last_change = millis();
              g_spark_direction = true;
              g_spark_target = 0.0f;
              
              // SPARKコマンドをU4に転送
              Serial2.printf("SPARK,%.1f\n", duration_seconds);
              
              Serial.printf("U1→U3: SPARK mode activated for %.1f seconds (forwarded to U4)\n", duration_seconds);
            }
          }
          
          rxPosU1 = 0;        // バッファをリセット
        } else {
          if (rxPosU1 < sizeof(rxBufU1) - 1) rxBufU1[rxPosU1++] = c;
        }
  }
}

void gridEyeTest(){
  // **サーミスタ温度の読み取り** (0x0E,0x0Fから12ビット温度)
  Wire1.beginTransmission(AMG88XX_ADDR);
  Wire1.write(0x0E);                      // Thermistor LSB register (0x0E)
  Wire1.endTransmission(false);
  Wire1.requestFrom(AMG88XX_ADDR, (uint8_t)2);
  int16_t thermRaw = 0;
  if (Wire1.available() >= 2) {
    uint8_t thermLSB = Wire1.read();
    uint8_t thermMSB = Wire1.read();
    thermRaw = (thermMSB << 8) | thermLSB;           // 12ビット生データを16ビット変数へ
    if (thermRaw & 0x0800) thermRaw |= 0xF000;       // 符号拡張 (ビット11が1の場合はビット12-15も1に)
  }
  float thermTempC = thermRaw * 0.0625;  // 分解能0.0625℃/LSBで換算 [oai_citation:20‡cdn-learn.adafruit.com](https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf?1498680225#:~:text=Temperature%20Output%20Resolution%200,0625%E2%84%83)

  // **8x8画素温度の読み取り** (レジスタ0x80から0xFFまで128バイト連続データ)
  Wire1.beginTransmission(AMG88XX_ADDR);
  Wire1.write(0x80);                      // Pixel0 温度データ先頭アドレス (0x80)
  Wire1.endTransmission(false);
  Wire1.requestFrom(AMG88XX_ADDR, (uint8_t)128);
  int16_t pixels[64];
  for (int i = 0; i < 64; i++) {
    if (Wire1.available() >= 2) {
      uint8_t pixelLSB = Wire1.read();
      uint8_t pixelMSB = Wire1.read();
      int16_t raw12 = (pixelMSB << 8) | pixelLSB;
      if (raw12 & 0x0800) raw12 |= 0xF000;           // 12ビット符号付 -> 16ビット符号付へ拡張
      pixels[i] = raw12;
    } else {
      pixels[i] = 0;
    }
  }

  // **結果の表示**: サーミスタ温度と8x8画素温度マトリクスをシリアルに出力
  Serial.print("Thermistor Temp: ");
  Serial.print(thermTempC, 2);           // 内蔵サーミスタ温度 [℃] (小数点以下2桁表示)
  Serial.println(" C");

  Serial.println("Pixel temperatures [8x8] (degC):");
  // 8x8マトリクスで表示
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int16_t rawVal = pixels[row * 8 + col];
      float tempC = rawVal * 0.25;                    // 分解能0.25℃/LSBで℃に変換 [oai_citation:21‡cdn-learn.adafruit.com](https://cdn-learn.adafruit.com/assets/assets/000/043/261/original/Grid-EYE_SPECIFICATIONS%28Reference%29.pdf?1498680225#:~:text=Temperature%20Output%20Resolution%200,0625%E2%84%83)
      Serial.print(tempC, 2);
      Serial.print("  ");
    }
    Serial.println();
  }
  Serial.println();  // 区切りの空行
}