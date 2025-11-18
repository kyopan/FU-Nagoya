// ========================================
// Wi-Fi/MQTT DISABLE for UART-only evaluation
// ========================================
#define DISABLE_WIFI

#include "globals.h"
#ifndef DISABLE_WIFI
#include "mqttClient.h"
#include "otaUpdater.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiMulti.h>
#endif
#include <Adafruit_NeoPixel.h>
// #include "defines.h"

int fwVersion = 9;

char macWithoutColons[13];
char deviceId[4] = "";  // ※ deviceId のサイズも globals.h と合わせる必要があります
uint8_t deviceIdNumber;

#ifndef DISABLE_WIFI
// Wi-Fi/MQTT 関連
const char* ssid        = "FU";
const char* password    = "spark!!!!!";
const char* mqtt_server = "192.168.1.2";
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WiFiMulti wifiMulti;

IPAddress pickBroker(const String &ssid)
{
  if (ssid == "FU")
    return IPAddress(192, 168, 1, 2);
  if (ssid == "PRACHTSAALstudio")
    return IPAddress(192, 168, 178, 178);
  return IPAddress(192, 168, 0, 10); // デフォルト
}
#endif

// ESP32C6 Onboard LED関連
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

#ifndef DISABLE_WIFI
// 各種フラグや変数
bool idAcquired           = false;
bool colorTopicSubscribed = false;
bool psTopicSubscribed = false;
bool dlTopicSubscribed = false;
bool atTopicSubscribed = false;
bool rbTopicSubscribed = false; // 追加: "rb/" トピックのサブスクライブ状態
float xValue = 0.0, yValue = 0.0;
bool imuInitialized       = false;
uint8_t autonomousMode = 0; // 自律モードの状態 (0=OFF, 1=ON)
// OTA 関連
bool OTAprogress          = false;

// タイマー管理など（例）
unsigned long lastMacPublish   = 0;
unsigned long lastDataPublish  = 0;
const unsigned long macPublishInterval  = 5000;
const unsigned long dataPublishInterval = 1000;
#else
// UART-only mode variables
uint8_t autonomousMode = 0; // 自律モードの状態 (0=OFF, 1=ON)
#endif

// LED制御関連
bool mqtt_led_override = false;        // MQTTからのLED制御が有効かどうか
unsigned long last_mqtt_led_time = 0;  // 最後にMQTTからLED制御された時刻

// 現在の状態を保持
uint8_t current_f_r = 0, current_f_g = 0, current_f_b = 0;
uint8_t current_r_r = 0, current_r_g = 0, current_r_b = 0;
float current_pitch = 0.0f, current_yaw = 0.0f;

#ifndef DISABLE_WIFI
// ----- 追加：globals.h で extern 宣言している OTA 関連変数の定義 -----
// ※ これらの変数が otaUpdater.cpp で使われるので、必ず定義してください。
String host = "comone-fragmented-unity-fw.s3.ap-northeast-1.amazonaws.com";
int port = 80;
String bin = "/fu.bin";

long contentLength = 0;
bool isValidContentType = false;
int retryCount = 0;
const int maxRetry = 20;

// ----- 送信テスト用設定（例） -----
unsigned long lastSendTestPublish = 0;
int sendFlag = 1;
int sendFramerate = 1;
int sendPayloadSize = 10;
#endif

// ---------------------------------------------------------
// extern uint8_t PIN_U1_SDA; // I2C to U2
// extern uint8_t PIN_U1_SCL;

bool homed = false; // ホームポジションにいるかどうか

float enc_horizontal = 0.0; // MQTT から取得する水平エンコーダー値

// ----- LED Gradation variables -----
unsigned long lastHueSendTime = 0;
const unsigned long HUE_SEND_INTERVAL = 100; // 100ms interval
float currentHue = 220.0f; // Start with elegant blue-purple
float hueSpeed = 0.5f;     // Smooth speed for gradation

// Elegant HUE ranges (avoid vulgar colors)
const float HUE_BLUE_PURPLE_MIN = 220.0f;
const float HUE_BLUE_PURPLE_MAX = 280.0f;
const float HUE_CYAN_GREEN_MIN = 160.0f;
const float HUE_CYAN_GREEN_MAX = 200.0f;
const float HUE_GOLD_ORANGE_MIN = 20.0f;
const float HUE_GOLD_ORANGE_MAX = 50.0f;

// ---------------------------------------------------------
// ヘルパー関数
// ---------------------------------------------------------
void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  // pixels.setPixelColor(0, pixels.Color(r, g, b)); // strip.gamma8(value)
  uint8_t r_corr = pixel.gamma8(r);
  uint8_t g_corr = pixel.gamma8(g);
  uint8_t b_corr = pixel.gamma8(b);
  setOnboardLEDColor(r, g, b);

  // MQTTからのLED制御をマーク
  mqtt_led_override = true;
  last_mqtt_led_time = millis();

  // 現在のLED状態を更新
  current_f_r = r_corr; current_f_g = g_corr; current_f_b = b_corr;
  current_r_r = r_corr; current_r_g = g_corr; current_r_b = b_corr;
  // 現在の姿勢データと合わせて統合パケット送信
  sendDataPacketToU3(current_f_r, current_f_g, current_f_b, current_r_r, current_r_g, current_r_b, current_pitch, current_yaw, autonomousMode);
}

void setTubeInitialColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  // フェードイン演出: 黒(0,0,0)から目標色(r,g,b)まで2秒かけて徐々に明るく
  const int fadeSteps = 10;        // フェードステップ数
  const int fadeDelay = 40;        // 各ステップ間の遅延(ms) → 2秒総時間
  
  for (int step = 0; step <= fadeSteps; step++) {
    float progress = (float)step / fadeSteps;
    
    // 目標色への線形補間
    uint8_t fade_r = (uint8_t)(r * progress);
    uint8_t fade_g = (uint8_t)(g * progress);
    uint8_t fade_b = (uint8_t)(b * progress);
    // uint8_t fade_brightness = (uint8_t)(brightness * progress);
    
    // U3とオンボードLEDを同期してフェードイン
    sendDataPacketToU3(fade_r, fade_g, fade_b, fade_r, fade_g, fade_b, current_pitch, current_yaw, autonomousMode);
    setOnboardLEDColor(fade_r, fade_g, fade_b);
    
    delay(fadeDelay);
  }
  
  // 最終的な目標色を確実に設定
  sendDataPacketToU3(r, g, b, r, g, b, 0, 0, autonomousMode); // brightness を1 or 0に変更　isAutonomous
  setOnboardLEDColor(r, g, b);
}

void setOnboardLEDColor(uint8_t r, uint8_t g, uint8_t b){
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

// // 統合データパケット送信関数
// void sendDataPacketToU3(uint8_t f_r, uint8_t f_g, uint8_t f_b, uint8_t r_r, uint8_t r_g, uint8_t r_b, float pitch_deg, float yaw_deg, uint8_t brightness) {
//   // pitch: -60°〜+60° → 0〜255
//   uint8_t pitch8 = constrain((pitch_deg + 60.0f) * 255.0f / 120.0f, 0, 255);
  
//   // yaw: 0°〜360° → 0〜255
//   uint8_t yaw8 = constrain(yaw_deg * 255.0f / 360.0f, 0, 255);
  
//   // 重複送信防止：前回と同じデータなら送信しない
//   static uint8_t last_f_r = 255, last_f_g = 255, last_f_b = 255;
//   static uint8_t last_r_r = 255, last_r_g = 255, last_r_b = 255;
//   static uint8_t last_pitch8 = 255, last_yaw8 = 255;
  
//   // LED値は完全一致、pitch/yawは1以上の差があれば送信
//   bool led_same = (f_r == last_f_r && f_g == last_f_g && f_b == last_f_b &&
//                    r_r == last_r_r && r_g == last_r_g && r_b == last_r_b);
//   bool pitch_same = (abs((int)pitch8 - (int)last_pitch8) < 1);
//   bool yaw_same = (abs((int)yaw8 - (int)last_yaw8) < 1);
  
//   if (led_same && pitch_same && yaw_same) {
//     return; // 同じデータなので送信しない
//   }
  
//   // 前回データを更新
//   last_f_r = f_r; last_f_g = f_g; last_f_b = f_b;
//   last_r_r = r_r; last_r_g = r_g; last_r_b = r_b;
//   last_pitch8 = pitch8; last_yaw8 = yaw8;
  
//   // 統合パケット送信
//   Serial1.printf("DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%hhu,%hhu,B,%hhu\n", f_r, f_g, f_b, r_r, r_g, r_b, pitch8, yaw8, brightness);
//   Serial.printf("DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%hhu,%hhu,B,%hhu\n", f_r, f_g, f_b, r_r, r_g, r_b, pitch8, yaw8, brightness);
// }

// 統合データパケット送信関数
void sendDataPacketToU3(uint8_t f_r, uint8_t f_g, uint8_t f_b, uint8_t r_r, uint8_t r_g, uint8_t r_b, float pitch_deg, float yaw_deg, uint8_t brightness)
{
  // 重複送信防止：前回と同じデータなら送信しない
  static uint8_t last_f_r = 255, last_f_g = 255, last_f_b = 255;
  static uint8_t last_r_r = 255, last_r_g = 255, last_r_b = 255;
  static float last_pitch = 999.0f, last_yaw = 999.0f;
  static float last_enc_h = 999.0f;

  // LED値は完全一致、float値は0.01以上の差があれば送信
  bool led_same = (f_r == last_f_r && f_g == last_f_g && f_b == last_f_b &&
                   r_r == last_r_r && r_g == last_r_g && r_b == last_r_b);
  bool pitch_same = (abs(pitch_deg - last_pitch) < 0.01f);
  bool yaw_same = (abs(yaw_deg - last_yaw) < 0.01f);
  bool enc_h_same = (abs(enc_horizontal - last_enc_h) < 0.01f);

  if (led_same && pitch_same && yaw_same && enc_h_same)
  {
    return; // 同じデータなので送信しない
  }

  // 前回データを更新
  last_f_r = f_r;
  last_f_g = f_g;
  last_f_b = f_b;
  last_r_r = r_r;
  last_r_g = r_g;
  last_r_b = r_b;
  last_pitch = pitch_deg;
  last_yaw = yaw_deg;
  last_enc_h = enc_horizontal;

  // 統合パケット送信（pitch, yaw, enc_horizontalをfloatで送信）
  Serial1.printf("DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%.2f,%.2f,B,%hhu,H,%.2f\n", f_r, f_g, f_b, r_r, r_g, r_b, pitch_deg, yaw_deg, brightness, enc_horizontal);
  Serial.printf("DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%.2f,%.2f,B,%hhu,H,%.2f\n", f_r, f_g, f_b, r_r, r_g, r_b, pitch_deg, yaw_deg, brightness, enc_horizontal);
}


// Send elegant HUE gradation to U3 (tube_rp2040_eval)
void sendElegantHue() {
  unsigned long now = millis();

  // Check send interval
  if (now - lastHueSendTime < HUE_SEND_INTERVAL) {
    return;
  }
  lastHueSendTime = now;

  // Update HUE value (smooth gradation)
  currentHue += hueSpeed;

  // Cycle through elegant HUE ranges
  // Blue-purple (220-280°) → Cyan-green (160-200°) → Gold-orange (20-50°)
  if (currentHue >= HUE_BLUE_PURPLE_MIN && currentHue < HUE_BLUE_PURPLE_MAX) {
    // In blue-purple range
    if (currentHue >= HUE_BLUE_PURPLE_MAX - 1.0f) {
      currentHue = HUE_CYAN_GREEN_MIN; // Jump to cyan-green
    }
  }
  else if (currentHue >= HUE_CYAN_GREEN_MIN && currentHue < HUE_CYAN_GREEN_MAX) {
    // In cyan-green range
    if (currentHue >= HUE_CYAN_GREEN_MAX - 1.0f) {
      currentHue = HUE_GOLD_ORANGE_MIN; // Jump to gold-orange
    }
  }
  else if (currentHue >= HUE_GOLD_ORANGE_MIN && currentHue < HUE_GOLD_ORANGE_MAX) {
    // In gold-orange range
    if (currentHue >= HUE_GOLD_ORANGE_MAX - 1.0f) {
      currentHue = HUE_BLUE_PURPLE_MIN; // Jump back to blue-purple
    }
  }
  else {
    // Out of range, reset to start
    currentHue = HUE_BLUE_PURPLE_MIN;
  }

  // Send HUE value to U3 via UART
  Serial1.printf("HUE,%.1f\n", currentHue);

  // Debug output
  Serial.printf("Sent HUE: %.1f\n", currentHue);
}

#ifndef DISABLE_WIFI
void getMacAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  sprintf(macWithoutColons, "%02X%02X%02X%02X%02X%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  // Serial.print("MAC address: ");
  // Serial.println(macWithoutColons);
}

void setup_wifi() {
  wifiMulti.addAP("FU", "spark!!!!!");
  // wifiMulti.addAP("CIC_Local", "qwertyuiop");
  wifiMulti.addAP("PRACHTSAALstudio", "04692645027246925387");
  delay(10);
  Serial.println("\nConnecting to WiFi...");
  // WiFi.begin(ssid, password);
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   Serial.print(".");
  // }
  int connectAtempts = 0;
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");  // Keep the serial monitor lit!
    connectAtempts++;
    if (connectAtempts > 10) ESP.restart();
    delay(500);
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  String mac = WiFi.macAddress();
  Serial.println("MACADDR: " + mac);
}
#endif

// ---------------------------------------------------------
// setup()
// ---------------------------------------------------------
void setup() {
  // NeoPixel初期化
  pixel.begin();
  pixel.setBrightness(50);
  setOnboardLEDColor(100, 0, 0);
  Serial.begin(115200);

  // UART1 to U3 (tube_rp2040_eval)
  // ESP32-C6: TX=GPIO11, RX=GPIO10
  // RP2040: RX=GPIO1, TX=GPIO0
  Serial1.begin(9600, SERIAL_8N1, PIN_U1_RX, PIN_U1_TX);
  Serial.println("UART1 initialized: TX=GPIO11, RX=GPIO10, 9600 baud");

  // while (1)
  // {
  //   setTubeInitialColor(40, 10, 150, 255);
  // }
  
  // RS485 Test (removed blocking loop)
  Wire.begin(PIN_U1_SDA, PIN_U1_SCL); // I2C マスターとして初期化

#ifndef DISABLE_WIFI
  WiFi.setSleep(false);  // 省電力モードOFF

  // バースト回避用にランダムな遅延
  delay(random(0, 10000));

  // Wi-Fi接続＆MAC取得
  setup_wifi();
  getMacAddress();


  // MQTT設定
  // mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setServer(pickBroker(WiFi.SSID()), 1883); // ←ここが肝
  mqttClient.setCallback(mqttCallback);
#endif

  Serial.println("Setup done.");
  setTubeInitialColor(100, 50, 50, 255);
}

// ---------------------------------------------------------
// loop()
// ---------------------------------------------------------
void loop() {

#ifndef DISABLE_WIFI
  if (!OTAprogress) {
    // MQTT再接続処理（ホーミング中はスキップ、ただしID未取得時は許可）
    if (!mqttClient.connected()) {
      if (homed || !idAcquired) {
        Serial.println("MQTT client not connected, attempting to reconnect...");
        reconnect();
      } else {
        // ホーミング中かつID取得済みの場合のみスキップ
        static unsigned long lastHomingMqttLog = 0;
        if (millis() - lastHomingMqttLog > 5000) {
          lastHomingMqttLog = millis();
          Serial.println("MQTT reconnection skipped during homing process");
        }
      }
    }
    mqttClient.loop();
#else
  {
#endif

    // Send elegant HUE gradation to tube (U3)
    sendElegantHue();

#ifndef DISABLE_WIFI

    // ホーミング状態の継続的なポーリング
    if (!homed && idAcquired) {
      static unsigned long lastDebugPrint = 0;
      bool homingResult = pollHoming(20); // 20msごとにホーミング状態をポーリング
      if (homingResult) {
        homed = true;
        Serial.println("HOMING COMPLETED!");
        for (int i = 0; i < 10; i++) {
          Serial1.printf("C\n");
          delay(10);
        }
        delay(100);
      } else {
        // 1秒毎にデバッグ情報を出力
        if (millis() - lastDebugPrint >= 1000) {
          lastDebugPrint = millis();
          Serial.print(" [pollHoming=false, homed=");
          Serial.print(homed ? "true" : "false");
          Serial.print(", idAcquired=");
          Serial.print(idAcquired ? "true" : "false");
          Serial.println("]");
        } else {
          Serial.print(".");  // 簡潔な進行表示
        }
      }
    }

    unsigned long now = millis();

    // 未ID取得の場合は、5秒毎に ini/<MAC> トピックへ空メッセージ送信（初期化用）
    if (!idAcquired) {
      if (now - lastMacPublish > macPublishInterval) {
        lastMacPublish = now;
        char iniTopic[32];
        snprintf(iniTopic, sizeof(iniTopic), "ini/%s", macWithoutColons);
        mqttClient.publish(iniTopic, "", true);
        Serial.print("Published initialization message to ");
        Serial.println(iniTopic);
      }
    }
    // ID取得済みの場合、かつ送信フラグがONならテストデータを送信
    // こちらテストなんで不要です。
    // else {
    //   if (sendFlag == 1) {
    //     // 送信間隔は sendFramerate（fps）により決定（例：1fpsなら1000ms間隔）
    //     int interval = 1000 / sendFramerate;
    //     if (now - lastSendTestPublish >= (unsigned long)interval) {
    //       lastSendTestPublish = now;

    //       // 指定バイト数分のランダムな数字文字列を生成
    //       // String randomData = "";
    //       // for (int i = 0; i < sendPayloadSize; i++) {
    //       //   randomData += char('0' + random(0, 10));
    //       // }

    //       // // トピックを "sendTest/<deviceId>" とする
    //       // String topic = String("sendTest/") + deviceId;

    //       // // 生成したランダムな数字文字列をペイロードとして送信
    //       // mqttClient.publish(topic.c_str(), randomData.c_str());
    //       // Serial.print("Published to ");
    //       // Serial.print(topic);
    //       // Serial.print(": ");
    //       // Serial.println(randomData);
    //     }
    //   }
    // }
#endif
  }
  // OTA進行中はOTAUpdater側で処理
}