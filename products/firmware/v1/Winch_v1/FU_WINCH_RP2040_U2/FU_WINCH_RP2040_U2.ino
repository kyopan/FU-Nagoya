/***********************************************************************
 *  _       _______   __________  __   __  _____
 * | |     / /  _/ | / / ____/ / / /  / / / /__ \
 * | | /| / // //  |/ / /   / /_/ /  / / / /__/ /
 * | |/ |/ // // /|  / /___/ __  /  / /_/ // __/
 * |__/|__/___/_/ |_/\____/_/ /_/   \____//____/
 *
 * Author: Kyopalab. LLC
 * Email: kyono@kyopalab.com
 * Creation Date: 2025/06/10
 * Version: 0.4
 * License: Proprietary License
 * MCU: Waveshare RP2040-Zero
 ************************************************************************/

int SENSOR_THRESHOLD = 250;
bool DEBUG = 0;


#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

// For U1<->U2 I2C communication
#define U2_SLAVE_ADDRESS 0x08

#define PIN_ONBOARD_LED   16
#define PIN_U2_SDA        4        
#define PIN_U2_SCL        5
#define PIN_LED_PWM       14
#define PIN_SENSOR        27
#define PIN_SOLENOID      26
#define PIN_TMC2209_EN    2
#define PIN_TMC2209_TX    0
#define PIN_TMC2209_RX    1
#define PIN_TMC2209_STEP  7
#define PIN_TMC2209_DIR   8



// --- 制御周期設定 ---
long targetPosition = 0;
volatile bool newTargetArrived = false;

unsigned long lastUpperTime = 0;
unsigned long lastTime = 0;

volatile unsigned long MOVE_POS = 0;
unsigned long preMovePos = 0;
long HOMING_SPEED = 500;
long MAX_SPEED = 5000;
long MAX_ACCL = 3000;

volatile bool IS_HOMING = false; // 上昇動作中、ESP側でPollingして確認
volatile bool IS_HOMED = false;

const unsigned long HOMING_LIMIT_TIME = 35'000UL;

// -------------------------------------------------------------
// Waveshare RP2040 Zero Pin 26 PWM @ 50 kHz / 12-bit
// -------------------------------------------------------------

// デューティの最大値 (12ビットの場合 0〜4095)
static const uint16_t MAX_DUTY = 4095;
static const unsigned long PWM_FREQ = 20000UL;  // 20 kHz
// 2550 is 2.6V


// 
// ---------------------------
// TMC2209 UARTシリアルポートの定義
// ---------------------------
#define TMC_SERIAL Serial1  // RP2040 ZeroのUART0に対応 (GP0=TX, GP1=RX)

#define R_SENSE 0.11f        // センス抵抗(Ω)
#define DRIVER_ADDRESS 0b00  // TMC2209アドレス(必要に応じて変更)

TMC2209Stepper driver(&TMC_SERIAL, R_SENSE, DRIVER_ADDRESS);  // TMC2209

// ---------------------------
// AccelStepperの設定
// ---------------------------
AccelStepper stepper(AccelStepper::DRIVER, PIN_TMC2209_STEP, PIN_TMC2209_DIR);

// 200ステップ/回転 × 16分周 = 3200ステップ/回転 (例)
const long STEPS_PER_REV = 200L * 16;
const int WINCH_RADIUS = 25;  //[mm]

// speed(1～100) を [50～10000]steps/s にマッピング (コマンド操作用)
float mapSpeed(int val) {
  float minS = 50.0;
  float maxS = 7500.0;
  val = constrain(val, 1, 100);
  return minS + (maxS - minS) * (val - 1) / 99.0;
}

// acceleration(1～100) を [50～10000]steps/s^2 にマッピング (コマンド操作用)
float mapAcceleration(int val) {
  float minA = 50.0;
  float maxA = 10000.0;
  val = constrain(val, 1, 100);
  return minA + (maxA - minA) * (val - 1) / 99.0;
}

// ドライバ出力の有効/無効フラグ
volatile bool MOTOR_ENABLE = false;

Adafruit_NeoPixel pixel(1, PIN_ONBOARD_LED);

/* ---------------- グローバル変数 ---------------- */
volatile uint8_t deviceId = 0;   // 'I'
volatile uint16_t targetZ = 0;   // 'Z'
volatile uint8_t spotLight = 0;  // 'L'

// Spark機能関連
volatile bool sparkMode = false;
volatile unsigned long sparkDuration = 0;  // spark持続時間（ミリ秒）
volatile unsigned long sparkStartTime = 0;
volatile unsigned long sparkLastMove = 0;  // 最後の移動時刻
volatile unsigned long sparkMoveInterval = 1000;  // 移動間隔（可変）
const uint16_t SPARK_MIN_HEIGHT = 100;   // 10cm
const uint16_t SPARK_MAX_HEIGHT = 1000;  // 1m
// 生命らしい動作パラメータ
volatile float sparkBreathPhase = 0.0f;   // 呼吸のような動作の位相
volatile float sparkRandomSeed = 0.0f;    // ランダムシード
// Spark実行前の状態保存
volatile uint16_t preSparkHeight = 0;     // Spark前の高さ
volatile uint8_t preSparkLED = 0;         // Spark前のLED輝度

// プーリーアンロック用タイマー変数
volatile bool pully_unlock_timer_active = false;
volatile unsigned long pully_unlock_start_time = 0;
const unsigned long PULLY_UNLOCK_DURATION = 1000;  // 10000ms

/*  I²C でデータを受信したときに呼ばれるコールバック
    howMany : 今回届いたバイト数
*/
void receiveEvent(int howMany) {
  if (howMany < 2) {  // タグ＋最低 1 byte がないと無効
    while (Wire.available()) Wire.read();
    return;
  }

  char tag = Wire.read();  // 1 バイト目 = タグ
  --howMany;               // 残りバイト数

  switch (tag) {

    /* ---- 'I' : 1 byte (ID) ---- */
    case 'I':
      // Iを受信したらホーミング開始
      if (howMany >= 1) {
        deviceId = Wire.read();
        Serial.print("My id:");
        Serial.println(deviceId);

        // TMC初期化
        if(stepperInit()){
          // TMC初期化成功
          // ホーミング中は黄色に点灯
          showLED(100, 100, 0);
        }else{
          showLED(100, 0, 100);
        }

        

        // デバイスID応じた起動遅延（デバイス間の初期化競合回避）
        delay(deviceId * 100);
        // モーター有効化
        digitalWrite(PIN_TMC2209_EN, LOW);
        
        MOTOR_ENABLE = true;

        // 実際のホーミング開始
        Serial.println("Starting actual homing process...");

        // フラグ管理
        IS_HOMING = true;
        IS_HOMED = false;
      }
      break;

    /* ---- 'Z' : 2 byte (高さ・Little Endian) ---- */
    case 'Z':
      if (howMany >= 2) {
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        targetZ = (hi << 8) | lo;
        /* ★ デバッグ出力 */
        Serial.print(F("[I2C] Z = "));  // F() はフラッシュ格納でRAM節約
        Serial.println(targetZ);        // 10 進
        MOVE_POS = targetZ;
      }
      break;

    /* ---- 'L' : 1 byte (ライト) ---- */
    case 'L':
      if (howMany >= 1) {
        spotLight = Wire.read();
        /* ★ デバッグ出力 */
        Serial.print(F("[I2C] L = "));
        Serial.println(spotLight);
        /* ★ 実際のLED制御に反映（Sparkモード中は無効） */
        if (!sparkMode) {
          analogWrite(PIN_LED_PWM, spotLight);
          Serial.print(F("LED PWM set to: "));
          Serial.println(spotLight);
        }
      }
      break;

    /* ---- 'S' : 2 byte (Spark) ---- */
    case 'S':
      if (howMany >= 2) {
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        uint16_t duration_seconds = (hi << 8) | lo;
        
        if (duration_seconds > 0) {
          // Spark実行前の状態を保存
          preSparkHeight = MOVE_POS;
          preSparkLED = spotLight;
          
          // Sparkモード開始
          sparkMode = true;
          sparkDuration = duration_seconds * 1000UL;  // 秒をミリ秒に変換
          sparkStartTime = millis();
          sparkLastMove = millis();
          sparkMoveInterval = 800 + (uint16_t)(getHardwareRandom() * 400);  // 初期間隔：0.8-1.2秒
          sparkBreathPhase = 0.0f;
          sparkRandomSeed = getHardwareRandom();  // 0.0-1.0のランダムシード
          
          Serial.print(F("[I2C] SPARK START = "));
          Serial.print(duration_seconds);
          Serial.println(F(" seconds"));
          
          // SparkモードのためにLED OFF（他のLEDを阻害しないため）
          analogWrite(PIN_LED_PWM, 0);
          
          // 適度な速度・加速度設定（生命らしい動作のため）
          stepper.setMaxSpeed(MAX_SPEED * 0.7f);  // 少し抑えた速度
          stepper.setAcceleration(MAX_ACCL * 0.8f);  // なめらかな加速
          
          // 初期位置を中間高度に設定
          MOVE_POS = (SPARK_MIN_HEIGHT + SPARK_MAX_HEIGHT) / 2;
          
        } else {
          // Sparkモード終了
          sparkMode = false;
          Serial.println(F("[I2C] SPARK END"));
          
          // 通常のLED制御に復帰
          analogWrite(PIN_LED_PWM, preSparkLED);
        }
      }
      break;

    /* ---- 不明タグ ---- */
    default:
      // 受信バッファを空にして無視
      while (Wire.available()) Wire.read();
      break;
  }

  /* 残っていたら読み捨てる（長さミスマッチ対策） */
  while (Wire.available()) Wire.read();
}

void calcTargetPosition(long pt_new) {
  float rev = (float)pt_new / (2 * PI * WINCH_RADIUS);
  targetPosition = (long)(rev * (float)STEPS_PER_REV);
}


/* ───────── 要求: ESP が 2byte 読みに来る ───────── */
void onRequest() {
  Wire.write('H');            // タグ
  Wire.write(IS_HOMED ? 1 : 0);  // 0=未完,1=完了
  Serial.print("onRequest called, IS_HOMED=");
  Serial.print(IS_HOMED ? "true" : "false");
  Serial.print(", IS_HOMING=");
  Serial.println(IS_HOMING ? "true" : "false");
}

bool stepperInit() {
  pinMode(PIN_TMC2209_EN, OUTPUT);
  pinMode(PIN_TMC2209_STEP, OUTPUT);
  pinMode(PIN_TMC2209_DIR, OUTPUT);

  digitalWrite(PIN_TMC2209_EN, LOW);  // 初期状態はOFF
  // UART初期化
  TMC_SERIAL.begin(115200);

  // TMC2209 初期設定
  driver.begin();
  driver.pdn_disable(true);         // PDN/UARTピンをUART制御用に使用 [oai_citation:14‡github.com](https://github.com/teemuatlut/TMC2208Stepper#:~:text=driver,toff%280x2%29%3B%20%2F%2F%20Enable%20driver)
  driver.I_scale_analog(false);     // 電流制御をVREFピンではなくUART設定に基づくモードに [oai_citation:15‡github.com](https://github.com/teemuatlut/TMC2208Stepper#:~:text=driver,toff%280x2%29%3B%20%2F%2F%20Enable%20driver)
  driver.rms_current(800, 1.0f);    // モーター電流設定: RMS 800mAに設定 (静止トルクも同等)
  driver.microsteps(16);            // マイクロステップ設定: 1/16ステップに設定
  driver.pwm_autoscale(true);       // ステルスチョップモード用: 電流制御を自動調整 [oai_citation:16‡forum.arduino.cc](https://forum.arduino.cc/t/operating-nema17-stepper-motor-at-higher-velocity-with-tmc2209-based-on-rp2040-pi-pico/1273070#:~:text=driver)
  readDriverInfo();

  // driver.toff(5);
  // driver.blank_time(24);

  // driver.rms_current(800);  // 800mA
  // driver.microsteps(16);    // 16分周

  // // ステルスチョップをデフォルト有効
  driver.en_spreadCycle(false);
  driver.pdn_disable(true);
  driver.I_scale_analog(false);

  // // CoolStep有効化 (SpreadCycle移行時のみ動作)
  driver.TPWMTHRS(200);  // ステルスチョップが有効な速度上限
  driver.semin(5);
  driver.semax(2);
  driver.seup(2);
  driver.sedn(1);
  // TMC2209用には sgt() は存在しないのでコメントアウト/削除
  // driver.sgt(4);  // ← TMC2209では使用不可 (エラーになる)

  // AccelStepper 最大速度・加速度設定
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(MAX_ACCL);

  // ドライバON
  MOTOR_ENABLE = true;
  digitalWrite(PIN_TMC2209_EN, !MOTOR_ENABLE);  // Enable=LOWアクティブ
  IS_HOMING = true;

  return (driver.microsteps()==16)?true:false;
}

void readDriverInfo(){
  uint32_t drvVersion = driver.version();
  if (drvVersion == 0xFFFFFFFF) {
    Serial.println("TMC2209: 通信エラー. 配線/アドレスを確認してください。");
  } else {
    Serial.print("TMC2209 version: 0x");
    Serial.println(drvVersion, HEX);
  }
  // 設定値のUART読出しテスト
  Serial.println("TMC2209設定の読出し結果:");
  Serial.print("  Microsteps = ");
  Serial.println(driver.microsteps());   // 設定されたマイクロステップを読出し
  Serial.print("  RMS Current = ");
  Serial.print(driver.rms_current());
  Serial.println(" mA");                // 設定されたラン電流値を読出し
  Serial.println();
}

void motorOperation(){
  static unsigned long homingStartTime = 0;
  
  if(IS_HOMING){
    // ホーミングモード
    // I2C のIコマンドで実行される
    if(homingStartTime == 0){
      homingStartTime = millis();
      pully_unlock(true);  // プーリーアンロック
      Serial.println("Homing process initiated");
    }
    
    // ホーミング中の処理
    stepper.setMaxSpeed(HOMING_SPEED);
    stepper.setSpeed(-HOMING_SPEED);  // 上方向（センサー方向）に移動
    stepper.runSpeed();
    
    // センサー検知でホーミング完了
    if(analogRead(PIN_SENSOR) < SENSOR_THRESHOLD){
      Serial.println("Homing sensor detected!");

      // フラグ更新
      IS_HOMING = false;
      IS_HOMED = true;

      // LED消灯
      showLED(0, 0, 0);
      stepper.setCurrentPosition(0);  // 現在位置を原点として設定
      
      homingStartTime = 0;  // タイマーリセット
      analogWrite(PIN_LED_PWM, 0);  // LED OFF
      Serial.println("Homing completed successfully");
      Serial.print("IS_HOMED flag set to: ");
      Serial.println(IS_HOMED ? "true" : "false");
      stepper.setMaxSpeed(MAX_SPEED);
      stepper.setAcceleration(MAX_ACCL);
      
      // // ホーミング完了後のI2C状態確認・再初期化
      delay(100);
      Serial.println("Verifying I2C communication...");
      Wire.end();
      Wire.begin(U2_SLAVE_ADDRESS);
      Wire.onReceive(receiveEvent);
      Wire.onRequest(onRequest);
      Serial.println("I2C communication verified");
    } 
  }else{
    // 通常モード
    
    // Sparkモード中の処理
    if (sparkMode) {
      updateSparkMovement();
    } else {
      // 通常の動作
      if (preMovePos != MOVE_POS) {
        // 移動に変化があったら計算実行
        calcTargetPosition(MOVE_POS);
        preMovePos = MOVE_POS;
      }
      stepper.moveTo(targetPosition);
    }
    stepper.run();
  }
}

// Core 0 setup
void setup() {
  Serial.begin(115200);
  pinInit();
  onboardLedInit();
  Wire.begin(U2_SLAVE_ADDRESS);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(onRequest);
}

// Core 1 setup
void setup1() {
  stepperInit();
  pully_unlock(true);
  checkSensor(DEBUG);
  // checkTMC();
  stepper.moveTo(5000);
}

void checkTMC(){
  while(1){
    // Microsteps = 16
    // RMS Current = 489 mA
    readDriverInfo();
  }
}
void checkSensor(bool debug){
  while(debug){
    analogWrite(PIN_LED_PWM, 200);
    Serial.println(analogRead(PIN_SENSOR));
  }
}

// static unsigned long lastSwitchTime = 0;
// const unsigned long switchInterval = 4000;

// Core 0 loop
void loop() {
  // シリアル入力を常時チェック
  serialCommandCheck();
}

// Core 1 loop

void loop1(){
  motorOperation();
  
  // プーリーアンロックタイマー処理
  pully_unlock_timer_handler();

  // Motor working test
  // motor_test();
}
void motor_test(){
  
  if (stepper.distanceToGo() == 0){
      readDriverInfo();
      stepper.moveTo(-stepper.currentPosition());
  }

    stepper.run();
}

void pully_unlock(bool unlock) {
  // 瞬間的に12V定格のソレノイドに24Vを印加することで、プーリーに負荷がかかっている状況でも強力な力でロックを解除する
  // しかし、12V定格のいソレノイドに24Vを印加するため、そのままでは燃える!!
  // このため100msだけ24Vを印加し、その後PWMで3V程度で駆動させ、アンロック状態を維持する
  if (unlock) {
    // SOL ON
    Serial.println("Pully Unlocked");
    analogWrite(PIN_SOLENOID, 0);
    // タイマー割り込みを開始
    pully_unlock_timer_active = true;
    pully_unlock_start_time = millis();
  } else {
    // SOL OFF
    Serial.println("Pully Locked");
    analogWrite(PIN_SOLENOID, 255);
    pully_unlock_timer_active = false;
  }
}

// プーリーアンロックタイマー処理関数
void pully_unlock_timer_handler() {
  if (pully_unlock_timer_active) {
    unsigned long current_time = millis();
    if (current_time - pully_unlock_start_time >= PULLY_UNLOCK_DURATION) {
      // 100ms経過後にPWMで3V程度に設定
      analogWrite(PIN_SOLENOID, 205);
      pully_unlock_timer_active = false;
    }
  }
}

void controlMotor(int h, int s, int a) {
  float maxS = mapSpeed(s);
  stepper.setMaxSpeed(maxS);
  Serial.print("[Max Speed] = ");
  Serial.print(maxS);
  Serial.println(" steps/s");

  float acc = mapAcceleration(a);
  stepper.setAcceleration(acc);
  Serial.print("[Acceleration] = ");
  Serial.print(acc);
  Serial.println(" steps/s^2");

  if (h < 0)
    h = 0;
  if (h > 3000)
    h = 3000;
  // height -> 回転数 -> ステップ数
  float rev = (float)h / (2 * PI * WINCH_RADIUS);  // height=300 => 10回転
  long targetSteps = (long)(rev * (float)STEPS_PER_REV);
  stepper.moveTo(targetSteps);
  // moving = true;
  Serial.print("[Height command] -> moveTo steps=");
  Serial.println(targetSteps);
}

// ---------------------------
// シリアル入力の処理
// ---------------------------
void serialCommandCheck() {
  static String inputString = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputString.length() > 0) {
        parseCommand(inputString);
        inputString = "";
      }
    } else {
      inputString += c;
    }
  }
}

// ---------------------------
// コマンド解析
// ---------------------------
void parseCommand(const String &cmd) {
  // 空白で区切る
  String tokens[4];
  int index = 0;
  int start = 0;
  for (int i = 0; i < (int)cmd.length(); i++) {
    if (cmd[i] == ' ') {
      tokens[index++] = cmd.substring(start, i);
      start = i + 1;
      if (index >= 3)
        break;
    }
  }
  tokens[index++] = cmd.substring(start);

  // 小文字化してコマンド抽出
  String command = tokens[0];
  command.toLowerCase();

  if (command == "z") {
    if (index > 1) {
      int h = tokens[1].toInt();
      if (h < 0)
        h = 0;
      if (h > 3000)
        h = 3000;
      MOVE_POS = h;
    } else {
      Serial.println("Usage: height <0-3000>");
    }
  }
  // --- Homing ---
  else if (command == "h") {
    Serial.println("Manual homing initiated");
    digitalWrite(PIN_TMC2209_EN, LOW);  // モーター有効化
    MOTOR_ENABLE = true;
    IS_HOMING = true;
    IS_HOMED = false;
  }

  // --- info ---
  else if (command == "i") {
   readDriverInfo();
  }

  else if(command == "t"){
    // Stepper target position
    MOVE_POS =  tokens[1].toInt();
    Serial.print("[Target pos] = ");
    Serial.print(MOVE_POS);
    Serial.println(" mm");
  }

  // --- LED ---
  else if (command == "l") {
    if (index > 1) {
      int a = tokens[1].toInt();
      analogWrite(PIN_LED_PWM, a);
      Serial.print("[LED] = ");
      Serial.println(a);
    } else {
      Serial.println("Usage: led <0-255>");
    }
  }

  // --- Proximity sensor ---
  else if (command =="p") {
    Serial.print("[SENSOR] = ");
    Serial.println(analogRead(PIN_SENSOR));
  }

  // --- Solenoid ---
  else if (command == "s") {
    if (index > 1) {
      int a = tokens[1].toInt();
      if (a == 1) {
        // SOL ON
        pully_unlock(true);
      } else if (a == 0) {
        // SOL OFF
        pully_unlock(false);
      }
      // analogWrite(PIN_SOLENOID, a);
      Serial.print("[SOL] = ");
      Serial.println(a);
    } else {
      Serial.println("Usage: solenoid <1-100>");
    }
  }
  
  // --- I2C Reset ---
  else if (command == "r") {
    Serial.println("Reinitializing I2C...");
    Wire.end();
    delay(100);
    Wire.begin(U2_SLAVE_ADDRESS);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(onRequest);
    Serial.println("I2C reinitialized");
  } else {
    Serial.println("Unknown command. Type 'help' for list.");
  }
}

void pinInit() {
  pinMode(PIN_LED_PWM, OUTPUT);
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_SOLENOID, OUTPUT);

  // PWMの基本周波数を指定
  analogWriteFreq(PWM_FREQ);
  // PWM分解能を8ビットに設定 (0〜255)
  analogWriteResolution(8);

  // LED OFF
  analogWrite(PIN_LED_PWM, 0);

  // Pully lock
  analogWrite(PIN_SOLENOID, 255);
}

void onboardLedInit(){
  pixel.begin();
  showLED(100, 0, 0);
}

void showLED(uint8_t r, uint8_t g, uint8_t b){
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

// ハードウェア乱数生成関数
float getHardwareRandom() {
  return (analogRead(29) & 0x3FF) / 1023.0f;  // 0.0-1.0の範囲
}

// Sparkモードでの生命らしいランダム上下運動
void updateSparkMovement() {
  // Spark時間終了チェック
  if (millis() - sparkStartTime >= sparkDuration) {
    sparkMode = false;
    Serial.println("SPARK mode ended");
    
    // Spark実行前の状態に復帰
    MOVE_POS = preSparkHeight;
    calcTargetPosition(MOVE_POS);
    analogWrite(PIN_LED_PWM, preSparkLED);
    
    Serial.print("Restored height: ");
    Serial.print(preSparkHeight);
    Serial.print("mm, LED: ");
    Serial.println(preSparkLED);
    
    return;
  }
  
  // Spark LED演出の更新
  updateSparkLEDs();
  
  // 生命らしい動作の更新
  unsigned long currentTime = millis();
  
  // 新しい目標位置を決定する時間か確認
  if (currentTime - sparkLastMove >= sparkMoveInterval) {
    sparkLastMove = currentTime;
    
    // 呼吸のような動作の位相を更新
    sparkBreathPhase += 0.3f + sparkRandomSeed * 0.4f;  // 0.3-0.7の増分
    if (sparkBreathPhase > 2.0f * PI) sparkBreathPhase -= 2.0f * PI;
    
    // 生命らしい高度計算
    // ベース：呼吸のような緩やかな変化
    float breathEffect = sin(sparkBreathPhase) * 0.3f + 0.5f;  // 0.2-0.8
    
    // ランダム要素：予測不可能な動き（ハードウェア乱数使用）
    float randomEffect = getHardwareRandom();  // 0.0-1.0
    
    // ノイズ：小さな揺らぎ
    float noiseEffect = sin(currentTime * 0.01f + sparkRandomSeed * 10.0f) * 0.1f;
    
    // 高度範囲内での目標位置計算
    float heightRange = SPARK_MAX_HEIGHT - SPARK_MIN_HEIGHT;
    float normalizedHeight = breathEffect * 0.5f + randomEffect * 0.4f + noiseEffect * 0.1f;
    normalizedHeight = constrain(normalizedHeight, 0.0f, 1.0f);
    
    uint16_t newTarget = SPARK_MIN_HEIGHT + (uint16_t)(heightRange * normalizedHeight);
    
    // 極端な変化を避ける（生命らしい制限）
    uint16_t currentTarget = MOVE_POS;
    int16_t heightDiff = abs((int16_t)newTarget - (int16_t)currentTarget);
    
    if (heightDiff > heightRange * 0.6f) {
      // 変化が大きすぎる場合は中間点に制限
      if (newTarget > currentTarget) {
        newTarget = currentTarget + heightRange * 0.4f;
      } else {
        newTarget = currentTarget - heightRange * 0.4f;
      }
    }
    
    MOVE_POS = constrain(newTarget, SPARK_MIN_HEIGHT, SPARK_MAX_HEIGHT);
    calcTargetPosition(MOVE_POS);
    
    // 次の移動間隔をランダムに設定（0.6-2.0秒）（ハードウェア乱数使用）
    sparkMoveInterval = 600 + (uint16_t)(getHardwareRandom() * 1400);
    
    // 速度もランダムに変化（生命らしい変化）（ハードウェア乱数使用）
    float speedMultiplier = 0.4f + getHardwareRandom() * 0.5f;  // 0.4-0.9
    stepper.setMaxSpeed(MAX_SPEED * speedMultiplier);
    
    // 加速度も変化（急がしい動きと緩やかな動きを混在）（ハードウェア乱数使用）
    float accelMultiplier = 0.5f + getHardwareRandom() * 0.6f;  // 0.5-1.1
    stepper.setAcceleration(MAX_ACCL * accelMultiplier);
    
    Serial.print("SPARK: New target=");
    Serial.print(MOVE_POS);
    Serial.print("mm, interval=");
    Serial.print(sparkMoveInterval);
    Serial.print("ms, speed=");
    Serial.print(speedMultiplier, 2);
    Serial.print(", accel=");
    Serial.println(accelMultiplier, 2);
  }
  
  // ベジェ曲線またはダイレクト移動
  stepper.moveTo(targetPosition);
}

// Spark LED演出関数
void updateSparkLEDs() {
  static unsigned long lastSparkLEDUpdate = 0;
  unsigned long currentTime = millis();
  
  // 30ms間隔で高速更新
  if (currentTime - lastSparkLEDUpdate < 30) return;
  lastSparkLEDUpdate = currentTime;
  
  // 稲妻のようなランダムフラッシュ効果
  float sparkElapsed = (currentTime - sparkStartTime) / 1000.0f;
  
  // メインの稲妻効果（高速点滅）
  float lightningPhase = sin(currentTime * 0.05f + getHardwareRandom() * 10.0f);
  float lightningIntensity = 0.0f;
  
  // 閾値以上で突発的に光る
  if (lightningPhase > 0.6f) {
    lightningIntensity = (lightningPhase - 0.6f) / 0.4f; // 0.6-1.0を0.0-1.0にマップ
    lightningIntensity = pow(lightningIntensity, 0.3f); // より鋭い光に
  }
  
  // ベースの脈動効果
  float basePhase = sin(sparkElapsed * 2.0f * PI) * 0.3f + 0.7f; // ゆっくりとした脈動
  
  // ランダムなちらつき効果
  float flickerEffect = 0.8f + 0.2f * sin(currentTime * 0.02f + getHardwareRandom() * 20.0f);
  
  // 最終的な明度計算
  float finalIntensity = (basePhase * flickerEffect + lightningIntensity * 2.0f);
  finalIntensity = constrain(finalIntensity, 0.0f, 1.0f);
  
  // 稲妻色：青白い光
  uint8_t sparkR = (uint8_t)(200 * finalIntensity);
  uint8_t sparkG = (uint8_t)(230 * finalIntensity);
  uint8_t sparkB = (uint8_t)(255 * finalIntensity);
  
  // ハードウェア乱数でときどき色を変化
  if (getHardwareRandom() > 0.9f) {
    // 稀に紫や赤を混ぜる
    sparkR = (uint8_t)(255 * finalIntensity);
    sparkG = (uint8_t)(100 * finalIntensity);
    sparkB = (uint8_t)(200 * finalIntensity);
  }
  
  // LED出力
  uint8_t ledOutput = (uint8_t)(255 * finalIntensity);
  analogWrite(PIN_LED_PWM, ledOutput);
  
  // オンボードLEDも同期
  if (finalIntensity > 0.7f) {
    showLED(sparkR / 4, sparkG / 4, sparkB / 4); // オンボードLEDは少し暗めに
  } else {
    showLED(0, 0, 0);
  }
}
