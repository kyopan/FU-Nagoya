#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <Update.h>
#include <WiFiMulti.h>
#include <Wire.h>
// #include "defines.h" 

// ---------------------------------------------------------
// グローバル変数の宣言 (実体は main.cpp で定義)
// ---------------------------------------------------------
extern int fwVersion;

// MACやID関連
extern char macWithoutColons[13];
extern char deviceId[4];
extern uint8_t deviceIdNumber;

// Wi-Fi/MQTT関連
extern const char* ssid;
extern const char* password;
extern const char* mqtt_server;
extern WiFiClient espClient;
extern PubSubClient mqttClient;
extern WiFiMulti wifiMulti;


// NeoPixel関連
extern Adafruit_NeoPixel pixels;

// フラグや各種変数
extern bool idAcquired;
extern bool colorTopicSubscribed;
extern bool psTopicSubscribed;
extern bool dlTopicSubscribed;
extern bool atTopicSubscribed;
extern bool rbTopicSubscribed; // 追加: "rb/" トピックのサブスクライブ状態
extern float xValue, yValue;
extern bool imuInitialized;
extern bool homed;
extern uint8_t autonomousMode;

// OTA関連
extern bool OTAprogress;
extern long contentLength;
extern bool isValidContentType;
extern int retryCount;
extern const int maxRetry;
extern String host;
extern int port;
extern String bin;

// タイマー管理
extern unsigned long lastMacPublish;
extern unsigned long lastDataPublish;
extern const unsigned long macPublishInterval;
extern const unsigned long dataPublishInterval;

// ---------------------------------------------------------
// コンパイル時定数（旧 #define）
// ---------------------------------------------------------
inline constexpr uint8_t NEOPIXEL_PIN = 8;
inline constexpr uint8_t U2_SLAVE_ADDRESS = 0x08;
inline constexpr uint8_t PIN_ONBOARD_LED = 8;
inline constexpr uint8_t PIN_U1_SDA = 6; // I2C to U2
inline constexpr uint8_t PIN_U1_SCL = 7;
inline constexpr uint8_t PIN_U1_RX = 10; // UART1 to U3
inline constexpr uint8_t PIN_U1_TX = 11;

extern float enc_horizontal; // MQTT から取得する水平エンコーダー値

// 筒リングLED輝度 (U3へ送信後U4にも反映)
inline constexpr uint8_t BRIGHTNESS = 255; // 0〜255
// ---------------------------------------------------------
// 共通ヘルパー関数 (main.cpp で定義)
// ---------------------------------------------------------
void setLEDColor(uint8_t r, uint8_t g, uint8_t b);
void sendDataPacketToU3(uint8_t f_r, uint8_t f_g, uint8_t f_b, uint8_t r_r, uint8_t r_g, uint8_t r_b, float pitch_deg, float yaw_deg, uint8_t brightness);
void setOnboardLEDColor(uint8_t r, uint8_t g, uint8_t b);
// ホーミング関数 (mqttClient.cpp で定義)
bool pollHoming(uint16_t every_ms);

// 降下制御関数
extern bool descent_completed;
extern bool descent_initiated;
void initiate_descent();
void check_descent_completion();

extern uint8_t current_f_r, current_f_g, current_f_b;
extern uint8_t current_r_r, current_r_g, current_r_b;
extern float current_pitch, current_yaw;

#endif