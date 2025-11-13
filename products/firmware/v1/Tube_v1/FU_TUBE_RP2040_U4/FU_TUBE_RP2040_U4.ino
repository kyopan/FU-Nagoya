/***********************************************************************
 *   ________  ______  ______   __  ____ __
 *  /_  __/ / / / __ )/ ____/  / / / / // /
 *   / / / / / / __  / __/    / / / / // /_
 *  / / / /_/ / /_/ / /___   / /_/ /__  __/
 * /_/  \____/_____/_____/   \____/  /_/             
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

GY-85センサ座標系（基板印字に基づく）:
  X軸: Down方向（重力方向）→ YAW軸（X軸周りの回転）
  Y軸: Right方向 → PITCH軸（Y軸周りの回転）  
  Z軸: Forward方向 → ROLL軸（Z軸周りの回転）
*/

#include <Wire.h>
#include <SimpleFOC.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

#define PIN_ONBOARD_LED   16
#define PIN_NEO           29

#define PIN_U4_RX         9 // U3(Tube RP2040Zero)へUART2送受信
#define PIN_U4_TX         8 // U3(Tube RP2040Zero)へUART2送受信

#define PIN_PWM_U         11
#define PIN_PWM_V         12
#define PIN_PWM_W         13

#define PIN_FW_SWITCHER   28

#define NUM_LEDS          24

#define PIN_SDA0          4
#define PIN_SCL0          5

#define PIN_SDA1          26
#define PIN_SCL1          27

#define BAUD_USB          115200
#define BAUD_U1           38400
#define BAUD_U3           38400

#define DEBUG             false

//――― EEPROM設定 ―――
#define EEPROM_SIZE 512
#define EEPROM_MAGIC_NUMBER 0x12345678  // マジックナンバー（データ有効性確認用）
#define EEPROM_VERSION 1                // データ構造バージョン

// EEPROM アドレス定義
#define ADDR_MAGIC_NUMBER    0    // 4bytes: マジックナンバー
#define ADDR_VERSION         4    // 4bytes: データ構造バージョン
#define ADDR_CALIBRATION     8    // sizeof(MagCalibration): キャリブレーションデータ
#define ADDR_PITCH_OFFSET    40   // 4bytes: ピッチオフセット

// I2C addresses for GY-85 sensors
const byte QMC5883_ADDR = 0x0D;    // QMC5883L magnetometer I2C address
const byte ADXL345_ADDR = 0x53;    // ADXL345 accelerometer I2C address
const byte ITG_ADDR     = 0x68;    // https://dl.btc.pl/kamami_wa/itg3205.pdf

// ――― LED制御インスタンス ―――
Adafruit_NeoPixel pixel(1, PIN_ONBOARD_LED);
Adafruit_NeoPixel ring(NUM_LEDS, PIN_NEO);

//――― フライホイール制御定数 ―――
const float MAX_VELOCITY = 80.0f;    // rad/s, speed=100% 時の最大速度（4倍に向上）
const float MAX_RAMP     = 300.0f;   // V/s, accel=100% 時の最大ランプ（2倍に向上）

//――― センサー/ドライバ/モーター定義 ―――
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
BLDCDriver3PWM  driver = BLDCDriver3PWM(PIN_PWM_U, PIN_PWM_V, PIN_PWM_W);
BLDCMotor motor = BLDCMotor(7, 6.5, 330);

//――― 共有変数（core0⇔core1） ―――
volatile float g_target_vel   = 0.0f;      // [rad/s]
volatile float g_output_ramp  = MAX_RAMP * 0.3f; // 初期30%（スムーズな加速）
volatile float g_velocity_filter = 0.0f;   // ローパスフィルター値

//――― シリアル受信バッファ ―――
String serialBuffer;

// Communication buffer for U3 commands
const int CMD_BUF_SIZE = 128;
char cmdBuffer[CMD_BUF_SIZE];
int cmdIdx = 0;

// GY85センサーデータ用グローバル変数
volatile int16_t g_gy85_magX = 0, g_gy85_magY = 0, g_gy85_magZ = 0;
volatile int16_t g_gy85_accX = 0, g_gy85_accY = 0, g_gy85_accZ = 0;
volatile int16_t g_gy85_gyrX = 0, g_gy85_gyrY = 0, g_gy85_gyrZ = 0;

// 姿勢推定用変数
volatile float g_pitch = 0.0f;  // ピッチ角（度）
volatile float g_yaw = 0.0f;    // ヨー角（度）
volatile float g_roll = 0.0f;   // ロール角（度）

// 相補フィルタ用定数
const float COMPLEMENTARY_ALPHA = 0.98f;  // ジャイロの重み（高周波応答）
const float DT = 0.05f;  // サンプリング間隔（20Hz = 50ms）

// ジャイロ校正用変数
float gyro_offset_x = 0.0f;
float gyro_offset_y = 0.0f;
float gyro_offset_z = 0.0f;
bool gyro_calibrated = false;

// 地磁気キャリブレーション構造体
struct MagCalibration {
  float offset_x;    // X軸オフセット
  float offset_y;    // Y軸オフセット  
  float offset_z;    // Z軸オフセット
  float scale_x;     // X軸スケール係数
  float scale_y;     // Y軸スケール係数
  float scale_z;     // Z軸スケール係数
  bool valid;        // キャリブレーション有効フラグ
};

MagCalibration mag_cal = {0, 0, 0, 1.0f, 1.0f, 1.0f, false};

// YAWモーター制御関連
volatile float g_target_yaw = 0.0f;       // 目標ヨー角度（度）
volatile bool g_yaw_control_active = false; // YAW制御モードが有効かどうか

// YAW制御用PIDパラメータ（保守的な設定）
const float YAW_KP = 0.02f;   // 比例ゲイン（大幅に削減）
const float YAW_KI = 0.001f;  // 積分ゲイン（大幅に削減）  
const float YAW_KD = 0.005f;  // 微分ゲイン（削減）
const float YAW_DEADBAND = 5.0f; // デッドバンド（拡大）

// YAW制御用PID状態変数
float yaw_integral_error = 0.0f;
float yaw_previous_error = 0.0f;
unsigned long yaw_last_update = 0;

// YAW制御用滑らか加減速
float yaw_current_torque = 0.0f;        // 現在のトルク値
const float YAW_TORQUE_RAMP = 0.5f;      // トルクランプ（1/s）最大変化率（削減）
const float YAW_MAX_TORQUE = 0.3f;       // 最大トルク制限（削減）
const float YAW_DIRECTION = 1.0f;       // 制御方向（+1 or -1）

// Spark機能関連
bool g_spark_mode_active = false;        // Sparkモードが有効かどうか
unsigned long g_spark_start_time = 0;    // Sparkモード開始時刻
unsigned long g_spark_duration = 0;      // Sparkモード持続時間（ミリ秒）
bool g_spark_direction = true;           // true=正回転, false=逆回転
bool g_spark_long_duration = false;      // 4秒以上の長時間モードかどうか
const unsigned long SPARK_DIRECTION_CHANGE_INTERVAL = 5000;  // 5秒ごとに方向切り替え

// スムーズな色変化のための変数
float g_current_led_r = 0.0f, g_current_led_g = 0.0f, g_current_led_b = 0.0f; // 現在の色（float精度）
uint8_t g_target_led_r = 0, g_target_led_g = 0, g_target_led_b = 0;   // 目標の色
unsigned long g_last_color_update = 0;
const unsigned long COLOR_TRANSITION_INTERVAL = 20; // 20ms間隔で色を更新

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
  
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(current_r, current_g, current_b));
  }
  ring.show();
  
  // オンボードLEDも同期
  pixel.setPixelColor(0, pixel.Color(current_r, current_g, current_b));
  pixel.show();
}

void gy85_init(){
  Wire1.begin();                 // Wire1バスを開始
  Serial.println("Initializing GY-85 sensors...");
  
  // **QMC5883L磁気センサの初期化**
  // ソフトウェアリセット: レジスタ0x0Bに0x01を書き込み
  Wire1.beginTransmission(QMC5883_ADDR);
  Wire1.write(0x0B);             // Control register for reset (0x0B)
  Wire1.write(0x01);             // Write 0x01 to trigger reset
  Wire1.endTransmission();
  delay(50);
  // 連続測定モード設定: レジスタ0x09に制御設定を書き込む
  // （オーバーサンプリング512、レンジ2G、出力レート50Hz、連続測定モード）
  Wire1.beginTransmission(QMC5883_ADDR);
  Wire1.write(0x09);             // Control register 1 (0x09)
  Wire1.write(0xD1);             // 0xD1 = 11010001b: OSR=512 (00), RNG=2G (0), ODR=50Hz (10), Continuous(1)
  Wire1.endTransmission();
  Serial.println("QMC5883L magnetometer initialized");

  // **ADXL345加速度センサの初期化**
  // データフィルタ設定: レジスタ0x2Cにデータレート設定
  Wire1.beginTransmission(ADXL345_ADDR);
  Wire1.write(0x2C);             // Data rate control register (0x2C)
  Wire1.write(0x0A);             // 100Hz データレート
  Wire1.endTransmission();
  
  // データフォーマット設定: レジスタ0x31に±4gレンジ(0x01)を設定
  Wire1.beginTransmission(ADXL345_ADDR);
  Wire1.write(0x31);             // Data format register (0x31)
  Wire1.write(0x01);             // ±4gレンジ, フル解像度OFF（10-bitモード） 
  Wire1.endTransmission();
  
  // 測定有効化: レジスタ0x2Dの測定ビットをセットして計測開始
  Wire1.beginTransmission(ADXL345_ADDR);
  Wire1.write(0x2D);             // Power control register (0x2D)
  Wire1.write(0x08);             // Measure mode (D3=1)
  Wire1.endTransmission();
  Serial.println("ADXL345 accelerometer initialized (±4g range, 100Hz)");
  
  // **ITG3200ジャイロセンサの初期化**
  // サンプルレート設定: レジスタ0x15に分周比設定
  Wire1.beginTransmission(ITG_ADDR);
  Wire1.write(0x15);             // Sample rate divider register (0x15)
  Wire1.write(0x09);             // 100Hz サンプルレート (1kHz/(9+1))
  Wire1.endTransmission();
  
  // DLPF設定: レジスタ0x16にデジタルローパスフィルタ設定
  Wire1.beginTransmission(ITG_ADDR);
  Wire1.write(0x16);             // DLPF register (0x16)
  Wire1.write(0x1E);             // ±2000°/s range, 5Hz LPF
  Wire1.endTransmission();
  
  // パワーマネジメント: レジスタ0x3Eでスリープ解除
  Wire1.beginTransmission(ITG_ADDR);
  Wire1.write(0x3E);             // PWR_MGM Wakeup
  Wire1.write(0x00);             // Normal mode
  Wire1.endTransmission();
  Serial.println("ITG3200 gyroscope initialized (±2000°/s range, 100Hz)");
  
  delay(100);  // センサ安定化待機
  
  // ジャイロ校正実行
  calibrate_gyro();
}

void calibrate_gyro() {
  Serial.println("Gyro calibration starting... Keep device still for 3 seconds");
  
  float sum_x = 0, sum_y = 0, sum_z = 0;
  const int calibration_samples = 100;
  
  for (int i = 0; i < calibration_samples; i++) {
    // ジャイロデータ取得
    int16_t raw_x = readITGaxis(0x1D);
    int16_t raw_y = readITGaxis(0x1F);
    int16_t raw_z = readITGaxis(0x21);
    
    sum_x += raw_x;
    sum_y += raw_y;
    sum_z += raw_z;
    
    delay(30);  // 30ms間隔
  }
  
  // オフセット計算
  gyro_offset_x = sum_x / calibration_samples;
  gyro_offset_y = sum_y / calibration_samples;
  gyro_offset_z = sum_z / calibration_samples;
  
  gyro_calibrated = true;
  
  Serial.printf("Gyro calibration complete: X=%.1f, Y=%.1f, Z=%.1f\n", 
                gyro_offset_x, gyro_offset_y, gyro_offset_z);
}

// EEPROM操作関数
void save_mag_calibration() {
  uint32_t magic = EEPROM_MAGIC_NUMBER;
  uint32_t version = EEPROM_VERSION;
  
  EEPROM.put(ADDR_MAGIC_NUMBER, magic);
  EEPROM.put(ADDR_VERSION, version);
  EEPROM.put(ADDR_CALIBRATION, mag_cal);
  EEPROM.commit();
  
  Serial.println("Magnetometer calibration saved to EEPROM");
}

bool load_mag_calibration() {
  uint32_t magic, version;
  
  EEPROM.get(ADDR_MAGIC_NUMBER, magic);
  EEPROM.get(ADDR_VERSION, version);
  
  if (magic != EEPROM_MAGIC_NUMBER || version != EEPROM_VERSION) {
    Serial.println("No valid magnetometer calibration found in EEPROM");
    return false;
  }
  
  EEPROM.get(ADDR_CALIBRATION, mag_cal);
  
  if (mag_cal.valid) {
    Serial.printf("Magnetometer calibration loaded: Offset(%.2f,%.2f,%.2f) Scale(%.3f,%.3f,%.3f)\n",
                  mag_cal.offset_x, mag_cal.offset_y, mag_cal.offset_z,
                  mag_cal.scale_x, mag_cal.scale_y, mag_cal.scale_z);
    return true;
  }
  
  Serial.println("Invalid magnetometer calibration in EEPROM");
  return false;
}

// Enterキー待機関数
void waitForEnter(const char* message) {
  Serial.printf("%s (Press Enter to continue...)\n", message);
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        // バッファクリア
        while (Serial.available()) Serial.read();
        break;
      }
    }
    delay(10);
  }
}

// 段階的データ収集関数
void collectMagData(int16_t& max_x, int16_t& min_x, int16_t& max_y, int16_t& min_y, 
                   int16_t& max_z, int16_t& min_z, unsigned long duration) {
  unsigned long start_time = millis();
  int sample_count = 0;
  
  while (millis() - start_time < duration) {
    // 地磁気データ取得
    Wire1.beginTransmission(QMC5883_ADDR);
    Wire1.write(0x00);
    Wire1.endTransmission(false);
    Wire1.requestFrom(QMC5883_ADDR, (uint8_t)6);
    
    if (Wire1.available() >= 6) {
      uint16_t lsb = Wire1.read();
      uint16_t msb = Wire1.read();
      int16_t mag_x = (int16_t)((msb << 8) | lsb);
      
      lsb = Wire1.read();
      msb = Wire1.read();
      int16_t mag_y = (int16_t)((msb << 8) | lsb);
      
      lsb = Wire1.read();
      msb = Wire1.read();
      int16_t mag_z = (int16_t)((msb << 8) | lsb);
      
      // 最大・最小値更新
      if (mag_x > max_x) max_x = mag_x;
      if (mag_x < min_x) min_x = mag_x;
      if (mag_y > max_y) max_y = mag_y;
      if (mag_y < min_y) min_y = mag_y;
      if (mag_z > max_z) max_z = mag_z;
      if (mag_z < min_z) min_z = mag_z;
      
      sample_count++;
      
      // リアルタイムデータ表示（500msおき）
      if ((millis() - start_time) % 500 < 50) {
        Serial.printf("MAG X:%6d Y:%6d Z:%6d | Range X:%4d Y:%4d Z:%4d\n", 
                     mag_x, mag_y, mag_z, max_x-min_x, max_y-min_y, max_z-min_z);
      }
    }
    
    delay(50); // 20Hz サンプリング
  }
  
  Serial.printf("Step complete! Samples: %d\n", sample_count);
}

void calibrate_magnetometer() {
  Serial.println("=== 3-Axis Magnetometer Calibration ===");
  Serial.println("This will guide you through 3 rotation axes.");
  Serial.println("Rotate clockwise 360° in 8 seconds for each axis.");
  
  waitForEnter("Ready to start calibration?");
  
  // 最大・最小値の初期化
  int16_t mag_max_x = -32768, mag_max_y = -32768, mag_max_z = -32768;
  int16_t mag_min_x = 32767, mag_min_y = 32767, mag_min_z = 32767;
  
  // ステップ1: X軸周り回転（YAW軸）
  Serial.println("\n--- Step 1/3: YAW rotation (around X-axis) ---");
  Serial.println("Hold device horizontal, rotate around X-axis (Down)");
  Serial.println("Keep X-axis vertical, rotate in horizontal plane");
  waitForEnter("Position ready?");
  Serial.println("Rotate clockwise 360° NOW! (8 seconds)");
  collectMagData(mag_max_x, mag_min_x, mag_max_y, mag_min_y, mag_max_z, mag_min_z, 8000);
  
  // ステップ2: Y軸周り回転（PITCH軸）
  Serial.println("\n--- Step 2/3: PITCH rotation (around Y-axis) ---");
  Serial.println("Hold device vertically, rotate around Y-axis (Right)");
  Serial.println("Keep Y-axis horizontal, rotate like a wheel");
  waitForEnter("Position ready?");
  Serial.println("Rotate clockwise 360° NOW! (8 seconds)");
  collectMagData(mag_max_x, mag_min_x, mag_max_y, mag_min_y, mag_max_z, mag_min_z, 8000);
  
  // ステップ3: Z軸周り回転（ROLL軸）
  Serial.println("\n--- Step 3/3: ROLL rotation (around Z-axis) ---");
  Serial.println("Hold device horizontally, rotate around Z-axis (Forward)");
  Serial.println("Keep Z-axis horizontal (forward direction), rotate like a steering wheel");
  waitForEnter("Position ready?");
  Serial.println("Rotate clockwise 360° NOW! (8 seconds)");
  collectMagData(mag_max_x, mag_min_x, mag_max_y, mag_min_y, mag_max_z, mag_min_z, 8000);
  
  // キャリブレーション結果計算
  Serial.println("\n=== Calculating calibration parameters ===");
  
  mag_cal.offset_x = (mag_max_x + mag_min_x) / 2.0f;
  mag_cal.offset_y = (mag_max_y + mag_min_y) / 2.0f;
  mag_cal.offset_z = (mag_max_z + mag_min_z) / 2.0f;
  
  // スケール係数計算（球体化）
  float range_x = mag_max_x - mag_min_x;
  float range_y = mag_max_y - mag_min_y;
  float range_z = mag_max_z - mag_min_z;
  float avg_range = (range_x + range_y + range_z) / 3.0f;
  
  mag_cal.scale_x = avg_range / range_x;
  mag_cal.scale_y = avg_range / range_y;
  mag_cal.scale_z = avg_range / range_z;
  mag_cal.valid = true;
  
  Serial.println("=== 3-Axis Calibration Complete! ===");
  Serial.printf("Raw ranges - X:[%d,%d] Y:[%d,%d] Z:[%d,%d]\n", 
                mag_min_x, mag_max_x, mag_min_y, mag_max_y, mag_min_z, mag_max_z);
  Serial.printf("Offsets - X:%.2f Y:%.2f Z:%.2f\n", 
                mag_cal.offset_x, mag_cal.offset_y, mag_cal.offset_z);
  Serial.printf("Scales - X:%.3f Y:%.3f Z:%.3f\n", 
                mag_cal.scale_x, mag_cal.scale_y, mag_cal.scale_z);
  
  // 品質チェック
  float range_ratio_xy = range_x / range_y;
  float range_ratio_xz = range_x / range_z;
  float range_ratio_yz = range_y / range_z;
  
  Serial.printf("Quality check - Range ratios: XY=%.2f XZ=%.2f YZ=%.2f\n", 
                range_ratio_xy, range_ratio_xz, range_ratio_yz);
  
  if (range_ratio_xy > 0.7f && range_ratio_xy < 1.3f && 
      range_ratio_xz > 0.7f && range_ratio_xz < 1.3f && 
      range_ratio_yz > 0.7f && range_ratio_yz < 1.3f) {
    Serial.println("✓ Calibration quality: GOOD");
  } else {
    Serial.println("⚠ Calibration quality: POOR - consider recalibrating");
  }
  
  // EEPROM保存
  save_mag_calibration();
}

void setup() {
  Serial.begin(BAUD_USB);
  Serial2.begin(BAUD_U3); // RP2040_U3(This)<->RP2040_U4
  pinMode(PIN_FW_SWITCHER, INPUT_PULLUP);
  pixel.begin();
  pixel.setPixelColor(0, ring.Color(0, 0, 0));
  pixel.show();
  ring.begin();
  ring.setBrightness(255);
  ring.clear();
  // EEPROM初期化
  EEPROM.begin(EEPROM_SIZE);
  
  // 地磁気キャリブレーション読み込み
  if (!load_mag_calibration()) {
    Serial.println("No magnetometer calibration found.");
    Serial.println("Send 'c' command to start calibration.");
  }
  startupAnimation();
  
  gy85_init();

  
  
}

// ----- Birth of Life Animation -----
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

void setup1() {
  // I2C & エンコーダ初期化
  Wire.begin();
  sensor.init();

  // ドライバ初期化
  driver.voltage_power_supply = 5;
  driver.init();
  // モーターにドライバ・センサーをリンク
  motor.linkDriver(&driver);
  motor.linkSensor(&sensor);

  // トルク制御モード（唸り音軽減）
  motor.controller = MotionControlType::torque;

  // トルク制御用パラメータ（静音性重視）
  motor.voltage_limit = 3.0;          // 電圧制限を下げて静音化
  motor.current_limit = 0.5;          // 電流制限（0.5A）で静音化
  motor.velocity_limit = MAX_VELOCITY; // 速度制限は維持

  // 初期 FOC
  motor.init();
  motor.initFOC();
}

void loop() {
  // シリアルコマンド処理
  handleSerialCommands();
  
  // GY85を定期的に読み取り
  static unsigned long lastGy85Read = 0;
  if (millis() - lastGy85Read >= 50) {  // 20Hz更新
    lastGy85Read = millis();
    read_gy85(); 
    update_attitude();  // 姿勢推定実行
    output_9_axis();
  }
  
  // U3からのコマンド処理
  handleU3Commands();
  
  // Sparkモード更新
  if (g_spark_mode_active) {
    update_spark_mode();
  } else {
    // 通常時はスムーズな色変化を実行
    subtleChromaticShift();
  }
}

void handleSerialCommands() {
  static String serialBuffer = "";
  
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        serialBuffer.trim();
        
        if (serialBuffer == "c") {
          calibrate_magnetometer();
        } else if (serialBuffer == "r") {
          mag_cal = {0, 0, 0, 1.0f, 1.0f, 1.0f, false};
          save_mag_calibration();
          Serial.println("Magnetometer calibration reset");
        } else if (serialBuffer == "i") {
          if (mag_cal.valid) {
            Serial.printf("Mag Cal: Offset(%.2f,%.2f,%.2f) Scale(%.3f,%.3f,%.3f)\n",
                          mag_cal.offset_x, mag_cal.offset_y, mag_cal.offset_z,
                          mag_cal.scale_x, mag_cal.scale_y, mag_cal.scale_z);
          } else {
            Serial.println("No valid magnetometer calibration");
          }
        }
        
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

void loop1() {
  // FOC アルゴリズム
  motor.loopFOC();
  
  // 動作モード判定と制御実行
  if (g_spark_mode_active) {
    // Sparkモード：最高速度回転
    apply_spark_flywheel_control();
  } else if (g_yaw_control_active) {
    // YAW制御モード：角度制御
    float yaw_torque = calculate_yaw_control();
    motor.move(yaw_torque);
  } else {
    // 停止モード：トルク0
    motor.move(0.0f);
  }
}

void read_gy85(){
  // **QMC5883Lから磁気データ取得** (X, Y, Z各16ビット)
  Wire1.beginTransmission(QMC5883_ADDR);
  Wire1.write(0x00);                      // データ出力レジスタ先頭 (X軸LSBが0x00)
  Wire1.endTransmission(false);           // STOPせずにリピートスタート
  Wire1.requestFrom(QMC5883_ADDR, (uint8_t)6);
  if (Wire1.available() >= 6) {
    uint16_t lsb = Wire1.read();
    uint16_t msb = Wire1.read();
    g_gy85_magX = (int16_t)((msb << 8) | lsb);   // X軸 16ビット値
    lsb = Wire1.read();
    msb = Wire1.read();
    g_gy85_magY = (int16_t)((msb << 8) | lsb);   // Y軸 16ビット値
    lsb = Wire1.read();
    msb = Wire1.read();
    g_gy85_magZ = (int16_t)((msb << 8) | lsb);   // Z軸 16ビット値
  }

  // **ADXL345から加速度データ取得** (X, Y, Z各16ビット)
  Wire1.beginTransmission(ADXL345_ADDR);
  Wire1.write(0x32);                      // データレジスタ先頭 (X軸LSBが0x32)
  Wire1.endTransmission(false);
  Wire1.requestFrom(ADXL345_ADDR, (uint8_t)6);
  if (Wire1.available() >= 6) {
    uint16_t lsb = Wire1.read();
    uint16_t msb = Wire1.read();
    g_gy85_accX = (int16_t)((msb << 8) | lsb);   // X軸加速度 16ビット値（実質10ビット有効）
    lsb = Wire1.read();
    msb = Wire1.read();
    g_gy85_accY = (int16_t)((msb << 8) | lsb);   // Y軸加速度
    lsb = Wire1.read();
    msb = Wire1.read();
    g_gy85_accZ = (int16_t)((msb << 8) | lsb);   // Z軸加速度
  }
  // ITG3505からジャイロデータを取得
  g_gy85_gyrX = readITGaxis(0x1D);   // XOUT_H …0x1D–0x1E
  g_gy85_gyrY = readITGaxis(0x1F);   // YOUT_H …0x1F–0x20
  g_gy85_gyrZ = readITGaxis(0x21);   // ZOUT_H …0x21–0x22
}

int16_t readITGaxis(uint8_t highReg) {
  Wire1.beginTransmission(ITG_ADDR);
  Wire1.write(highReg);
  Wire1.endTransmission(false);
  Wire1.requestFrom(ITG_ADDR, (uint8_t)2);
  return (Wire1.read() << 8) | Wire1.read();
}

// 角度差の正規化（-180〜180度）
float normalize_angle_diff(float angle_diff) {
  while (angle_diff > 180.0f) angle_diff -= 360.0f;
  while (angle_diff < -180.0f) angle_diff += 360.0f;
  return angle_diff;
}

// YAW制御関数（滑らか加減速付き）
float calculate_yaw_control() {
  if (!g_yaw_control_active || !gyro_calibrated) {
    // 制御無効時は滑らかに停止
    if (yaw_current_torque != 0.0f) {
      unsigned long current_time = millis();
      float dt = (current_time - yaw_last_update) / 1000.0f;
      yaw_last_update = current_time;
      
      if (dt > 0.0001f) {
        float max_change = YAW_TORQUE_RAMP * dt;
        if (yaw_current_torque > 0.0f) {
          yaw_current_torque = max(0.0f, yaw_current_torque - max_change);
        } else {
          yaw_current_torque = min(0.0f, yaw_current_torque + max_change);
        }
      }
    }
    return yaw_current_torque;
  }
  
  unsigned long current_time = millis();
  float dt = (current_time - yaw_last_update) / 1000.0f; // 秒に変換
  
  if (dt <= 0.0001f) {
    return yaw_current_torque;  // 時間差が小さすぎる場合は前回値を返す
  }
  
  yaw_last_update = current_time;
  
  // 現在のYAW角度取得
  float current_yaw = g_yaw;  // ジャイロ積分値を使用
  
  // 目標角度との誤差計算
  float yaw_error = normalize_angle_diff(g_target_yaw - current_yaw);
  
  // 目標トルク計算
  float target_torque = 0.0f;
  
  // デッドバンド処理
  if (abs(yaw_error) >= YAW_DEADBAND) {
    // PID計算
    // 比例項
    float proportional = YAW_KP * yaw_error;
    
    // 積分項
    yaw_integral_error += yaw_error * dt;
    yaw_integral_error = constrain(yaw_integral_error, -100.0f, 100.0f); // 積分飽和防止
    float integral = YAW_KI * yaw_integral_error;
    
    // 微分項
    float derivative = YAW_KD * (yaw_error - yaw_previous_error) / dt;
    yaw_previous_error = yaw_error;
    
    // PID出力計算
    float pid_output = proportional + integral + derivative;
    
    // 目標トルク設定（制御方向適用）
    target_torque = constrain(pid_output * YAW_DIRECTION, -YAW_MAX_TORQUE, YAW_MAX_TORQUE);
  } else {
    // デッドバンド内では積分項リセット
    yaw_integral_error = 0.0f;
    target_torque = 0.0f;
  }
  
  // 滑らか加減速（トルクランプ適用）
  float torque_diff = target_torque - yaw_current_torque;
  float max_change = YAW_TORQUE_RAMP * dt;
  
  if (abs(torque_diff) <= max_change) {
    // 目標に到達
    yaw_current_torque = target_torque;
  } else {
    // ランプ制限適用
    if (torque_diff > 0.0f) {
      yaw_current_torque += max_change;
    } else {
      yaw_current_torque -= max_change;
    }
  }
  
  return yaw_current_torque;
}

void update_attitude() {
  if (!gyro_calibrated) return;
  
  // 加速度からピッチ・ロール計算（静的な姿勢）
  // 実測値に基づくスケール係数修正
  float acc_x_g = g_gy85_accX / 128.0f;  // 実測値から調整
  float acc_y_g = g_gy85_accY / 128.0f;
  float acc_z_g = g_gy85_accZ / 128.0f;
  
  // 座標系変換（正しい座標系：X=Down, Y=Right, Z=Forward）
  // 主重力軸は-X（Down方向）
  // ピッチ：Y軸周りの回転（前後傾斜、Z軸とX軸から計算）
  float pitch_acc = atan2(-acc_z_g, -acc_x_g) * 180.0f / PI;
  // ロール：Z軸周りの回転（左右傾斜、Y軸とX軸から計算）
  float roll_acc = atan2(-acc_y_g, -acc_x_g) * 180.0f / PI;
  
  // ジャイロデータ補正（校正値差し引き）
  // ITG3200: ±2000°/s range → 16.4 LSB/°/s (データシート値)
  float gyro_x_dps = (g_gy85_gyrX - gyro_offset_x) / 16.4f;  
  float gyro_y_dps = (g_gy85_gyrY - gyro_offset_y) / 16.4f;
  float gyro_z_dps = (g_gy85_gyrZ - gyro_offset_z) / 16.4f;
  
  // ジャイロによる角度積分（正しい座標系）
  g_yaw += gyro_x_dps * DT;    // X軸ジャイロ → ヨー（X軸周りの回転）
  g_pitch += gyro_y_dps * DT;  // Y軸ジャイロ → ピッチ（Y軸周りの回転）
  g_roll += gyro_z_dps * DT;   // Z軸ジャイロ → ロール（Z軸周りの回転）
  
  // 加速度の信頼性判定（重力加速度に近いかチェック）
  float acc_magnitude = sqrt(acc_x_g * acc_x_g + acc_y_g * acc_y_g + acc_z_g * acc_z_g);
  float acc_trust_factor = 1.0f;
  
  // 加速度が重力（1g）から大きく外れている場合は信頼度を下げる
  if (acc_magnitude < 0.8f || acc_magnitude > 1.2f) {
    acc_trust_factor = 0.1f;  // 信頼度を大幅に下げる
  } else if (acc_magnitude < 0.9f || acc_magnitude > 1.1f) {
    acc_trust_factor = 0.5f;  // 信頼度を下げる
  }
  
  // 相補フィルタ適用（加速度でジャイロドリフト補正）
  float effective_alpha = COMPLEMENTARY_ALPHA + (1.0f - COMPLEMENTARY_ALPHA) * (1.0f - acc_trust_factor);
  g_pitch = effective_alpha * g_pitch + (1.0f - effective_alpha) * pitch_acc;
  g_roll = effective_alpha * g_roll + (1.0f - effective_alpha) * roll_acc;
  
  // 地磁気センサでヨー補正（キャリブレーション済みかつ比較的水平時）
  if (mag_cal.valid && abs(g_pitch) < 45.0f && abs(g_roll) < 45.0f) {
    // 地磁気データにキャリブレーション適用
    float mag_x_cal = (g_gy85_magX - mag_cal.offset_x) * mag_cal.scale_x;
    float mag_y_cal = (g_gy85_magY - mag_cal.offset_y) * mag_cal.scale_y;
    float mag_z_cal = (g_gy85_magZ - mag_cal.offset_z) * mag_cal.scale_z;
    
    // ティルト補正後の地磁気ベクトル計算
    float mag_x_corrected = mag_x_cal * cos(g_pitch * PI / 180.0f) + 
                           mag_z_cal * sin(g_pitch * PI / 180.0f);
    float mag_y_corrected = mag_x_cal * sin(g_roll * PI / 180.0f) * sin(g_pitch * PI / 180.0f) +
                           mag_y_cal * cos(g_roll * PI / 180.0f) -
                           mag_z_cal * sin(g_roll * PI / 180.0f) * cos(g_pitch * PI / 180.0f);
    
    // ヨー角計算（地磁気北を基準）
    float yaw_mag = atan2(-mag_y_corrected, mag_x_corrected) * 180.0f / PI;
    
    // ヨー角の相補フィルタ（弱めの補正）
    float yaw_diff = yaw_mag - g_yaw;
    // 角度差の正規化（-180〜180度）
    while (yaw_diff > 180.0f) yaw_diff -= 360.0f;
    while (yaw_diff < -180.0f) yaw_diff += 360.0f;
    
    g_yaw += 0.02f * yaw_diff;  // 2%の重みで地磁気補正
  }
  
  // 角度の正規化（-180〜180度）
  while (g_yaw > 180.0f) g_yaw -= 360.0f;
  while (g_yaw < -180.0f) g_yaw += 360.0f;
}

void output_9_axis() {
  // RAW data and attitude output with debug info
  static unsigned long lastCoordDebug = 0;
  if (millis() - lastCoordDebug >= 500) {  // 2Hz出力（詳細確認のため）
    lastCoordDebug = millis();
    
    // RAWデータ出力
    Serial.printf("RAW - ACC X:%6d Y:%6d Z:%6d", g_gy85_accX, g_gy85_accY, g_gy85_accZ);
    Serial.printf(", GYRO X:%6d Y:%6d Z:%6d", g_gy85_gyrX, g_gy85_gyrY, g_gy85_gyrZ);
    Serial.printf(", MAG X:%6d Y:%6d Z:%6d\n", g_gy85_magX, g_gy85_magY, g_gy85_magZ);
    
    // 物理単位変換後のデータ
    float acc_x_g = g_gy85_accX / 128.0f;
    float acc_y_g = g_gy85_accY / 128.0f;
    float acc_z_g = g_gy85_accZ / 128.0f;
    Serial.printf("ACC(g) - X:%6.3f Y:%6.3f Z:%6.3f", acc_x_g, acc_y_g, acc_z_g);
    
    if (gyro_calibrated) {
      float gyro_x_dps = (g_gy85_gyrX - gyro_offset_x) / 16.4f;
      float gyro_y_dps = (g_gy85_gyrY - gyro_offset_y) / 16.4f;
      float gyro_z_dps = (g_gy85_gyrZ - gyro_offset_z) / 16.4f;
      Serial.printf(", GYRO(°/s) - X:%6.2f Y:%6.2f Z:%6.2f\n", gyro_x_dps, gyro_y_dps, gyro_z_dps);
      
      // 加速度から計算した静的角度
      float pitch_acc = atan2(-acc_z_g, -acc_x_g) * 180.0f / PI;
      float roll_acc = atan2(-acc_y_g, -acc_x_g) * 180.0f / PI;
      Serial.printf("ACC_ANGLE - Pitch:%7.2f° Roll:%7.2f°\n", pitch_acc, roll_acc);
      
      // 最終姿勢データ出力
      Serial.printf("FINAL_ATTITUDE - Pitch:%7.2f° Yaw:%7.2f° Roll:%7.2f°\n", 
                    g_pitch, g_yaw, g_roll);
                    
      // YAW制御情報出力
      if (g_yaw_control_active) {
        float yaw_error = normalize_angle_diff(g_target_yaw - g_yaw);
        float motor_velocity = motor.shaftVelocity();
        Serial.printf("YAW_CTRL - Tgt:%6.1f° Cur:%6.1f° Err:%6.1f° Trq:%5.3f Vel:%5.1f\n", 
                      g_target_yaw, g_yaw, yaw_error, yaw_current_torque, motor_velocity);
      } else {
        Serial.printf("YAW_CTRL - Torque:%5.3f Active:NO\n", yaw_current_torque);
      }
                    
      // 地磁気による磁北角度（参考）
      if (mag_cal.valid) {
        float mag_x_cal = (g_gy85_magX - mag_cal.offset_x) * mag_cal.scale_x;
        float mag_y_cal = (g_gy85_magY - mag_cal.offset_y) * mag_cal.scale_y;
        float mag_z_cal = (g_gy85_magZ - mag_cal.offset_z) * mag_cal.scale_z;
        Serial.printf("MAG_CAL - X:%7.1f Y:%7.1f Z:%7.1f", mag_x_cal, mag_y_cal, mag_z_cal);
        
        if (abs(g_pitch) < 45.0f && abs(g_roll) < 45.0f) {
          float mag_x_corrected = mag_x_cal * cos(g_pitch * PI / 180.0f) + 
                                 mag_z_cal * sin(g_pitch * PI / 180.0f);
          float mag_y_corrected = mag_x_cal * sin(g_roll * PI / 180.0f) * sin(g_pitch * PI / 180.0f) +
                                 mag_y_cal * cos(g_roll * PI / 180.0f) -
                                 mag_z_cal * sin(g_roll * PI / 180.0f) * cos(g_pitch * PI / 180.0f);
          float yaw_mag = atan2(-mag_y_corrected, mag_x_corrected) * 180.0f / PI;
          Serial.printf(", MAG_YAW:%7.2f°\n", yaw_mag);
        } else {
          Serial.println(", MAG_YAW: N/A (tilted)");
        }
      } else {
        Serial.println("MAG_CAL - Not calibrated");
      }
    } else {
      Serial.println(", GYRO - Not calibrated");
      Serial.println("ATTITUDE - Gyro not calibrated");
    }
    
    Serial.println("=====================================");
  }
}

// Sparkモード開始
void start_spark_mode(unsigned long duration_ms) {
  g_spark_mode_active = true;
  g_spark_start_time = millis();
  g_spark_duration = duration_ms;
  g_spark_direction = true;  // 最初は正回転
  
  // 4秒以上かどうかを判定
  g_spark_long_duration = (duration_ms >= 4000);
  
  Serial.printf("U4 SPARK mode started for %lu ms\n", duration_ms);
  Serial.printf("Flywheel max velocity: %.1f rad/s\n", MAX_VELOCITY);
  
  if (g_spark_long_duration) {
    Serial.println("Long duration mode: 5sec intervals alternating CW/CCW");
  } else {
    Serial.println("Short duration mode: CW rotation only");
  }
  
  // フライホイールを最高速度（80 rad/s）で正回転開始（キビキビした動きのため最大加速度）
  g_target_vel = MAX_VELOCITY;
  g_output_ramp = MAX_RAMP;
  
  // Sparkアニメーション開始
  start_spark_led_animation();
}

// Sparkモード停止
void stop_spark_mode() {
  g_spark_mode_active = false;
  g_target_vel = 0.0f;  // フライホイール停止
  
  Serial.println("U4 SPARK mode stopped");
  
  // LEDを通常状態に戻す
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(100, 50, 50));  // ピンク色に戻す
  }
  ring.show();
  pixel.setPixelColor(0, pixel.Color(100, 50, 50));
  pixel.show();
}

// Sparkモード更新
void update_spark_mode() {
  unsigned long elapsed_time = millis() - g_spark_start_time;
  
  // 時間終了チェック
  if (elapsed_time >= g_spark_duration) {
    stop_spark_mode();
    return;
  }
  
  // 4秒以上の長時間モードの場合：5秒ごとに方向切り替え
  if (g_spark_long_duration) {
    // 5秒間隔で方向を決定
    unsigned long direction_phase = elapsed_time / SPARK_DIRECTION_CHANGE_INTERVAL;
    bool should_be_clockwise = (direction_phase % 2 == 0);  // 偶数フェーズ=正回転、奇数フェーズ=逆回転
    
    // 方向変更が必要な場合
    if (should_be_clockwise != g_spark_direction) {
      g_spark_direction = should_be_clockwise;
      
      // キビキビした動きのため最大加速度に設定
      g_output_ramp = MAX_RAMP;
      
      if (g_spark_direction) {
        g_target_vel = MAX_VELOCITY;  // 正回転
        Serial.printf("SPARK: Switching to CW rotation at %.1f sec (Phase %lu)\n", 
                     elapsed_time / 1000.0f, direction_phase);
      } else {
        g_target_vel = -MAX_VELOCITY; // 逆回転
        Serial.printf("SPARK: Switching to CCW rotation at %.1f sec (Phase %lu)\n", 
                     elapsed_time / 1000.0f, direction_phase);
      }
    }
  } else {
    // 4秒未満の短時間モード：正回転のみ
    g_target_vel = MAX_VELOCITY;
    g_output_ramp = MAX_RAMP;  // キビキビした動きのため最大加速度
  }
  
  // SparkLEDアニメーション更新
  update_spark_led_animation();
}

// Spark用フライホイール制御
void apply_spark_flywheel_control() {
  // 最高速度・最高加速度でダイレクト制御
  g_velocity_filter = g_target_vel;  // フィルタリング無し
  
  // 高速PID制御でトルク計算
  float current_velocity = motor.shaftVelocity();
  float velocity_error = g_velocity_filter - current_velocity;
  
  // Spark用高応答PIDパラメータ
  const float KP_SPARK = 0.08f;   // 高応答
  const float KI_SPARK = 0.02f;   // 積分強化
  const float KD_SPARK = 0.005f;  // 微分強化
  
  static float spark_integral_error = 0.0f;
  static float spark_previous_error = 0.0f;
  
  // 積分項計算
  spark_integral_error += velocity_error * 0.01f;
  spark_integral_error = constrain(spark_integral_error, -2.0f, 2.0f);
  
  // 微分項計算
  float derivative_error = (velocity_error - spark_previous_error) / 0.01f;
  spark_previous_error = velocity_error;
  
  // 高応答PID出力計算
  float torque_target = KP_SPARK * velocity_error + 
                       KI_SPARK * spark_integral_error + 
                       KD_SPARK * derivative_error;
  
  // Spark用高トルク制限
  const float MAX_SPARK_TORQUE = 0.7f; // 通常の2.6倍
  torque_target = constrain(torque_target, -MAX_SPARK_TORQUE, MAX_SPARK_TORQUE);
  
  motor.move(torque_target);
}

// SparkLEDアニメーション開始
void start_spark_led_animation() {
  // 全LEDを初期化
  ring.clear();
  ring.show();
}

// SparkLEDアニメーション更新
void update_spark_led_animation() {
  static unsigned long lastUpdate = 0;
  
  if (millis() - lastUpdate >= 30) {  // 33Hz更新
    lastUpdate = millis();
    
    // 時間ベースのアニメーション
    unsigned long elapsed = millis() - g_spark_start_time;
    
    // 高速虹色効果
    float hue_phase = (elapsed % 2000) / 2000.0f * 2.0f * PI;  // 2秒で1周期
    
    for (int i = 0; i < NUM_LEDS; i++) {
      // 各LEDに位相差を付けて虹色効果
      float led_phase = hue_phase + (i * 2.0f * PI / NUM_LEDS);
      
      // HSVからRGBへの変換
      uint8_t r = (uint8_t)(128 + 127 * sin(led_phase));
      uint8_t g = (uint8_t)(128 + 127 * sin(led_phase + 2.0943f));  // 120°位相差
      uint8_t b = (uint8_t)(128 + 127 * sin(led_phase + 4.1888f));  // 240°位相差
      
      // きらめき効果（高周波変調）
      float sparkle = 0.6f + 0.4f * sin((elapsed + i * 50) % 300 / 300.0f * 2.0f * PI);
      r = (uint8_t)(r * sparkle);
      g = (uint8_t)(g * sparkle);
      b = (uint8_t)(b * sparkle);
      
      ring.setPixelColor(i, ring.Color(r, g, b));
    }
    
    // 定期的なフラッシュ効果（1秒毎）
    if ((elapsed % 1000) < 100) {  // 100ms間白色フラッシュ
      for (int i = 0; i < NUM_LEDS; i++) {
        ring.setPixelColor(i, ring.Color(255, 255, 255));
      }
      pixel.setPixelColor(0, pixel.Color(255, 255, 255));
    } else {
      // 通常の虹色
      uint8_t onboard_r = (uint8_t)(128 + 127 * sin(hue_phase));
      uint8_t onboard_g = (uint8_t)(128 + 127 * sin(hue_phase + 2.0943f));
      uint8_t onboard_b = (uint8_t)(128 + 127 * sin(hue_phase + 4.1888f));
      pixel.setPixelColor(0, pixel.Color(onboard_r, onboard_g, onboard_b));
    }
    
    ring.show();
    pixel.show();
  }
}

void handleU3Commands() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      cmdBuffer[cmdIdx] = '\0';
      cmdIdx = 0;
      
      // Sparkコマンド処理
      if (strncmp(cmdBuffer, "SPARK,", 6) == 0) {
        float duration_seconds = atof(cmdBuffer + 6);
        if (duration_seconds > 0) {
          start_spark_mode((unsigned long)(duration_seconds * 1000));
        } else {
          stop_spark_mode();
        }
        return;
      }
      
      // U3からの統合パケット "DATA,R,r,g,b,Y,yaw8,B,brightness" 形式の解析
      if (strncmp(cmdBuffer, "DATA,", 5) == 0) {
        uint8_t r, g, b, yaw8, brightness;
        if (sscanf(cmdBuffer, "DATA,R,%hhu,%hhu,%hhu,Y,%hhu,B,%hhu", 
                   &r, &g, &b, &yaw8, &brightness) == 5) {
          
          // 目標色を設定（実際の色変化はsubtleChromaticShift()で処理）
          g_target_led_r = r;
          g_target_led_g = g;
          g_target_led_b = b;
          
          // LED輝度設定
          ring.setBrightness(brightness);
          pixel.setBrightness(brightness);
          
          // ヨー角をモーター制御に変換
          float yawDeg = yaw8 * 360.0f / 255.0f;
          
          // YAW制御を有効化
          g_target_yaw = yawDeg;
          g_yaw_control_active = true;

        }
      }
    }
    else if (c != '\r' && cmdIdx < CMD_BUF_SIZE - 1) {
      cmdBuffer[cmdIdx++] = c;
    } else if (cmdIdx >= CMD_BUF_SIZE - 1) {
      // バッファオーバーフロー防止：リセット
      cmdIdx = 0;
      Serial.println("CMD buffer overflow, reset");
    }
  }
}