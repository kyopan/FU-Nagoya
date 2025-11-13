#include "mqttClient.h"
#include "otaUpdater.h"  // callback内で execOTA() を呼ぶため
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// ※ 以下、globals.h 等で extern 宣言されている変数を利用する前提です
extern char macWithoutColons[];
extern char deviceId[];
extern uint8_t deviceIdNumber;
extern bool idAcquired;
extern bool colorTopicSubscribed;
extern bool psTopicSubscribed;
extern bool dlTopicSubscribed;
extern bool rbTopicSubscribed;
extern float xValue;
extern float yValue;
extern PubSubClient mqttClient;
extern int fwVersion;
extern int sendFlag;
extern int sendFramerate;
extern int sendPayloadSize;
extern bool homed;
extern float enc_horizontal; // MQTT から取得する水平エンコーダー値
extern uint8_t autonomousMode; // 自律モードの状態 (0=OFF, 1=ON)

// 外部関数
extern void setLEDColor(uint8_t r, uint8_t g, uint8_t b);

extern void startSparkMode(int duration_seconds);

extern void setOnboardLEDColor(uint8_t r, uint8_t g, uint8_t b);

// i2C polling関数
bool pollHoming(uint16_t every_ms = 20)
{
  static uint32_t t0 = 0;
  if (millis() - t0 < every_ms)
    return false;
  t0 = millis();

  int bytesReceived = Wire.requestFrom(U2_SLAVE_ADDRESS, 2);
  if (bytesReceived == 2) {
    char tag = Wire.read();
    int status = Wire.read();
    
    Serial.print(" [I2C: bytes=");
    Serial.print(bytesReceived);
    Serial.print(", tag='");
    Serial.print(tag);
    Serial.print("', status=");
    Serial.print(status);
    Serial.print("] ");
    
    if (tag == 'H') {
      setOnboardLEDColor(0,0,0); // ホーミング完了時はオンボードLEDを消灯
      return status == 1; // 1=完了
    }
  } else {
    // Serial.print(" [I2C ERROR: bytes=");
    // Serial.print(bytesReceived);
    // Serial.print("] ");
    Serial.print(".");
  }
    
  return false;
}


// 送信側の mm は「床=0 mm, 天井=2800 mm」
// → U2 へは「天井=0 mm, 床=2800 mm」で送りたい
void sendZtoU2_mm(uint16_t mm_floor)
{
  const uint16_t MAX_MM = 2800; // 2.8 m = 2800 mm

  // 1) 範囲外ならクランプ
  if (mm_floor > MAX_MM)
    mm_floor = MAX_MM;

  // 2) 逆変換: 天井基準へ
  uint16_t mm_ceiling = MAX_MM - mm_floor; // 0 … 2800

  // 3) I²C 送信 (little-endian)
  Wire.beginTransmission(U2_SLAVE_ADDRESS);
  Wire.write('Z');               // タグ
  Wire.write(mm_ceiling & 0xFF); // LSB
  Wire.write(mm_ceiling >> 8);   // MSB
  byte err = Wire.endTransmission();

  // 4) デバッグ表示
  if (err == 0)
  {
    Serial.printf("I2C sent Z = %u mm (ceiling-ref)\n", mm_ceiling);
  }
  else
  {
    Serial.printf("I2C error: %u\n", err);
  }
}

void sendDLtoU2(uint8_t level) // ---- I2C 送信用 (dl) ----
{
  Wire.beginTransmission(U2_SLAVE_ADDRESS);
  Wire.write('L');   // 1byte コマンドタグ
  Wire.write(level); // 本体データ 1byte
  byte err = Wire.endTransmission();

  if (err == 0)
  {
    Serial.print("I2C sent L = ");
    Serial.println(level); // 0–255
  }
  else
  {
    Serial.print("I2C error: ");
    Serial.println(err);
  }
}
/*  ---- I2C 送信用 : device-ID (“I” タグ) ----
    deviceIdNumber を 1 byte で送りたい場合は uint8_t に、
    2 byte 以上が必要なら uint16_t などに合わせてください。
*/
void sendIDtoU2(uint8_t deviceIdNumber) // ← サイズを変えたい場合は型を変更
{
  Wire.beginTransmission(U2_SLAVE_ADDRESS);

  Wire.write('I');            // 1 byte コマンドタグ（識別子 “I”）
  Wire.write(deviceIdNumber); // 本体データ 1 byte

  byte err = Wire.endTransmission();

  if (err == 0)
  {
    Serial.print("I2C sent I = ");
    Serial.println(deviceIdNumber); // 0–255
  }
  else
  {
    Serial.print("I2C error: ");
    Serial.println(err);
  }
}

// 旧sendPitchYawToU3関数は統合パケット方式により削除済み
// 現在はsendDataPacketToU3()を使用

/*
// ---------- U3 受信側 (Serial1) ---------- /

struct PYData
{
  int16_t pitchC;
  uint16_t yawC;
};

bool recvPitchYaw(PYData &out) // 1パケット取れたら true
{
  static uint8_t buf[5];
  static uint8_t idx = 0;

  while (Serial1.available())
  {
    uint8_t b = Serial1.read();

    if (idx == 0) // タグ待ち
    {
      if (b == 'P')
        buf[idx++] = b; // タグを保存し次へ
      // それ以外は無視（同期取り）
    }
    else
    {
      buf[idx++] = b;

      if (idx == 5) // 必要数そろった
      {
        // --- ペイロード展開 ---
        memcpy(&out.pitchC, buf + 1, 2); // little-endian
        memcpy(&out.yawC, buf + 3, 2);

        idx = 0; // 次パケットに備えリセット
        return true;
      }
    }
  }
  return false; // まだパケット未完成
}

/// ---------- Arduino スケッチ例 ----------  /

void setup()
{
  Serial.begin(115200);  // PC へログ
  Serial1.begin(115200); // U2→U3 と同じボーレート
}

void loop()
{
  PYData data;
  if (recvPitchYaw(data))
  {
    // 必要に応じて degree へ戻す
    float pitchDeg = data.pitchC / 100.0f;
    float yawDeg = data.yawC / 100.0f;

    Serial.print(F("Pitch = "));
    Serial.print(pitchDeg, 2);
    Serial.print(F(" ° ,  Yaw = "));
    Serial.print(yawDeg, 2);
    Serial.println(F(" °"));

    // --- ここでモーター制御などへ渡す ---
  }
}

*/

// -------------------------------------------------
// mqttCallback(): MQTTメッセージ受信時のコールバック
// -------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[64];
  if (length >= sizeof(message)) {
    length = sizeof(message) - 1;
  }
  strncpy(message, (char*)payload, length);
  message[length] = '\0'; // null終端

  // Serial.print("Message arrived [");
  // Serial.print(topic);
  // Serial.print("] ");
  // Serial.println(message);

  // --- 送信テスト用の制御トピック ---
  if (strcmp(topic, "sendFlag") == 0) {
    sendFlag = atoi(message);
    Serial.print("Updated sendFlag: ");
    Serial.println(sendFlag);
    return;
  }
  if (strcmp(topic, "sendFramerate") == 0) {
    sendFramerate = atoi(message);
    if (sendFramerate <= 0) sendFramerate = 1;
    Serial.print("Updated sendFramerate: ");
    Serial.println(sendFramerate);
    return;
  }
  if (strcmp(topic, "sendPayloadSize") == 0) {
    sendPayloadSize = atoi(message);
    if (sendPayloadSize < 0) sendPayloadSize = 0;
    Serial.print("Updated sendPayloadSize: ");
    Serial.println(sendPayloadSize);
    return;
  }

  // --- 以下、既存のトピック処理 ---

  // 例: "xxxxxxxxxxxx/idxy" トピックを受信してデバイスID、x,y、ラジアン値を取得
  char idxyTopic[32];
  snprintf(idxyTopic, sizeof(idxyTopic), "%s/idxy", macWithoutColons);
  if (strcmp(topic, idxyTopic) == 0)
  {
    // 例: "123,10.0,15.0,3.14" というカンマ区切りの文字列
    char *token = strtok(message, ",");
    // float radianValue = 0.0; // ラジアン値格納用変数

    Serial.println("Parsing idxy message...");
    if (token != NULL)
    {
      strncpy(deviceId, token, sizeof(deviceId));
      deviceIdNumber = (uint8_t)atoi(token);
      token = strtok(NULL, ",");
      Serial.println("deviceID is acquired");
    }
    if (token != NULL)
    {
      xValue = atof(token);
      token = strtok(NULL, ",");
      Serial.println("xVal is acquired");
    }
    if (token != NULL)
    {
      yValue = atof(token);
      token = strtok(NULL, ",");
      Serial.println("yVal is acquired");
    }
    if (token != NULL)
    {
      enc_horizontal = atof(token);
      Serial.println("radianVal is acquired");
    }

    Serial.printf("Parsed ID: %s, x: %.2f, y: %.2f", deviceId, xValue, yValue);
    if (token != NULL)
    {
      Serial.printf(", radian: %.2f", enc_horizontal);
    }
    Serial.println();

    idAcquired = true;

    // バージョン情報送信
    byte versionPayload[2];
    versionPayload[0] = byte(deviceIdNumber);
    versionPayload[1] = fwVersion;
    mqttClient.publish("version", versionPayload, 2, true);
    Serial.println("Sent version info via MQTT.");

    // I2C へ deviceIdNumber を送信
    sendIDtoU2(deviceIdNumber);


    // cl/<deviceId> トピックへのサブスクライブ（まだなら）
    if (!colorTopicSubscribed) {
      char colorTopic[64];
      snprintf(colorTopic, sizeof(colorTopic), "cl/%s", deviceId);
      mqttClient.subscribe(colorTopic, 0);
      colorTopicSubscribed = true;
      Serial.printf("Subscribed to %s\n", colorTopic);
    }
    if (!psTopicSubscribed)    {
      char psTopic[64];
      snprintf(psTopic, sizeof(psTopic), "ps/%s", deviceId);
      mqttClient.subscribe(psTopic, 0);
      psTopicSubscribed = true;
      Serial.printf("Subscribed to %s\n", psTopic);
    }

    if (!dlTopicSubscribed)    {
      char dlTopic[64];
      snprintf(dlTopic, sizeof(dlTopic), "dl/%s", deviceId);
      mqttClient.subscribe(dlTopic, 0);
      dlTopicSubscribed = true;
      Serial.printf("Subscribed to %s\n", dlTopic);
    }
    if (!atTopicSubscribed){
      char atTopic[64];
      snprintf(atTopic, sizeof(atTopic), "at/%s", deviceId);
      mqttClient.subscribe(atTopic, 0);
      atTopicSubscribed = true;
      Serial.printf("Subscribed to %s\n", atTopic);
    }
    if (!rbTopicSubscribed)
    {
      char rbTopic[64];
      snprintf(rbTopic, sizeof(rbTopic), "rb/%s", deviceId);
      mqttClient.subscribe(rbTopic, 0);
      rbTopicSubscribed = true;
      Serial.printf("Subscribed to %s\n", rbTopic);
    }

    return;
  }

  // ホーミング状態のポーリングはmain.cppのloop()で実行

  // 受信トピックが "ps/..." で、かつペイロード長が3バイトならRGB値としてNeoPixel更新
  if (strncmp(topic, "cl/", 3) == 0 && length == 3) {
    byte r = payload[0];
    byte g = payload[1];
    byte b = payload[2];
    if (homed) setLEDColor(r, g, b);
    return;
  }
  /* ---------- 位置・向き (ps/…) ---------- */
  // "<HhH" = uint16(L) + int16(L) + uint16(L)  = 6 byte
  if (strncmp(topic, "ps/", 3) == 0 && length == 6)
  {
    uint16_t mm;
    int16_t pitchC;
    uint16_t yawC;

    memcpy(&mm, payload + 0, 2); // little-endian
    memcpy(&pitchC, payload + 2, 2);
    memcpy(&yawC, payload + 4, 2);

    float z = mm / 1000.0f;        // back to metres
    float pitch = pitchC / 100.0f; // back to degrees
    float yaw = yawC / 100.0f;

    if (homed) {
      sendZtoU2_mm(mm); // ← mm のまま送るなら (例)
      
      // 姿勢データを更新して統合パケット送信
      extern uint8_t current_f_r, current_f_g, current_f_b;
      extern uint8_t current_r_r, current_r_g, current_r_b;
      extern float current_pitch, current_yaw;
      
      current_pitch = pitch;
      current_yaw = yaw;
      
      sendDataPacketToU3(current_f_r, current_f_g, current_f_b, current_r_r, current_r_g, current_r_b, current_pitch, current_yaw, autonomousMode);
    }
  
    Serial.printf("z=%.3f m  pitch=%.2f°  yaw=%.2f°\n", z, pitch, yaw);
    return;
  }


  if (strncmp(topic, "at/", 3) == 0 && length == 1) {
    // 自律モード制御
    autonomousMode = (uint8_t)(payload[0] == 1); // 1ならtrue、0ならfalse
    // 自律モードON時の初期化処理
    Serial.println("Autonomous mode ON");
    sendDataPacketToU3(current_f_r, current_f_g, current_f_b, current_r_r, current_r_g, current_r_b, current_pitch, current_yaw, autonomousMode);
    return;
  }

  // if (strncmp(topic, "at/", 3) == 0){
    
  //   uint8_t status = payload[0];
  //   Serial.printf("status: %d\n", status);
  //   if (status == 49){ // 1
  //     // GridEye Pitch==90
  //     current_pitch = 90.0;
  //     if (homed) {
  //       Serial.println("P90!!");
  //       sendDataPacketToU3(current_f_r, current_f_g, current_f_b, current_r_r, current_r_g, current_r_b, 90.0, current_yaw, 200);
  //     }
  //   }else{
  //     // Yaw disable
  //     current_yaw = 400.0;
  //     if (homed){
  //       Serial.println("Y400!!");
  //       sendDataPacketToU3(current_f_r, current_f_g, current_f_b, current_r_r, current_r_g, current_r_b, current_pitch, 400, 200);
  //     }
  //   }
  //   return;
  // }

  if (strncmp(topic, "dl/", 3) == 0 && length == 1){
    // ダウンライト制御
    uint8_t level = payload[0];
    if (homed) sendDLtoU2(level); // 追加
    return;
  }

  // ota トピック受信時：OTA処理開始
  if (strcmp(topic, "ota") == 0) {
    Serial.println("Received OTA message, calling execOTA()");
    execOTA();
    return;
  }

  // reboot トピック受信時：ESP32再起動
  if (strcmp(topic, "reboot") == 0) {
    Serial.println("Received reboot call, rebooting...");
    ESP.restart();
    return;
  }

  // spark トピック受信時：spark機能開始
  if (strcmp(topic, "spark") == 0) {
    int sparkDuration = atoi(message);
    if (sparkDuration > 0 && sparkDuration <= 300) {  // 最大5分制限
      Serial.printf("Received spark command: %d seconds\n", sparkDuration);
      startSparkMode(sparkDuration);
    } else {
      Serial.println("Invalid spark duration (1-300 seconds allowed)");
    }
    return;
  }
}

// -------------------------------------------------
// reconnect(): MQTTサーバ再接続処理
// -------------------------------------------------
void reconnect() {
  unsigned long startAttemptTime = millis(); // 再接続試行開始時刻
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // 一意なクライアントID生成
    String clientId = "ESP32C6-";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      setOnboardLEDColor(0, 100, 0);
      // "xxxxxxxxxxxx/idxy" トピックにサブスクライブ
      char idxyTopic[32];
      snprintf(idxyTopic, sizeof(idxyTopic), "%s/idxy", macWithoutColons);
      mqttClient.subscribe(idxyTopic, 1);
      Serial.printf("Subscribed to %s\n", idxyTopic);

      // 既にID取得済みなら、全ての制御トピックへ再サブスクライブ
      if (idAcquired && (deviceId[0] != '\0')) {
        char colorTopic[64];
        snprintf(colorTopic, sizeof(colorTopic), "cl/%s", deviceId);
        mqttClient.subscribe(colorTopic, 1);
        colorTopicSubscribed = true;
        Serial.printf("Re-subscribed to %s\n", colorTopic);
        
        char psTopic[64];
        snprintf(psTopic, sizeof(psTopic), "ps/%s", deviceId);
        mqttClient.subscribe(psTopic, 1);
        psTopicSubscribed = true;
        Serial.printf("Re-subscribed to %s\n", psTopic);
        
        char dlTopic[64];
        snprintf(dlTopic, sizeof(dlTopic), "dl/%s", deviceId);
        mqttClient.subscribe(dlTopic, 1);
        dlTopicSubscribed = true;
        Serial.printf("Re-subscribed to %s\n", dlTopic);

        // GridEye有効
        char atTopic[64];
        snprintf(atTopic, sizeof(atTopic), "at/%s", deviceId);
        mqttClient.subscribe(atTopic, 1);
        atTopicSubscribed = true;
        Serial.printf("Re-subscribed to %s\n", atTopic);
        
        char rbTopic[64];
        snprintf(rbTopic, sizeof(rbTopic), "rb/%s", deviceId);
        mqttClient.subscribe(rbTopic, 1);
        rbTopicSubscribed = true;
        Serial.printf("Re-subscribed to %s\n", rbTopic);
      }
      
      // ota トピックにサブスクライブ
      mqttClient.subscribe("ota", 1);
      Serial.println("Subscribed to ota topic.");

      // spark トピックにサブスクライブ
      mqttClient.subscribe("spark", 1);
      Serial.println("Subscribed to spark topic.");

      // 送信テスト用制御トピックにサブスクライブ
      mqttClient.subscribe("sendFlag", 1);
      mqttClient.subscribe("sendFramerate", 1);
      mqttClient.subscribe("sendPayloadSize", 1);
      Serial.println("Subscribed to send control topics: sendFlag, sendFramerate, sendPayloadSize.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" -> try again in 5 seconds");
      delay(5000);
    }
    // 30秒以上接続できなかった場合は再起動
    // if (millis() - startAttemptTime > 30000) {
    //   Serial.println("MQTT connection timeout, restarting device...");
    //   ESP.restart();
    // }
  }
}