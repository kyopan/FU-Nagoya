# U1ファームウェア 完全概要

## システム全体像

U1（ESP32-S3）は、天井吊り下げ式インタラクティブ照明システムのメインコントローラーです。

```
┌─────────────────────────────────────────┐
│         インターネット/MQTTブローカー      │
└──────────────┬──────────────────────────┘
               │ WiFi
    ┌──────────▼──────────┐
    │   U1 (ESP32-S3)     │ ← このファームウェア
    │  メインコントローラー  │
    └─┬─────────────────┬─┘
      │ I2C             │ UART (Serial1)
      │                 │
   ┌──▼───┐         ┌──▼───────┐
   │  U2  │         │    U3    │
   │ウィンチ│         │ LED制御  │
   │モーター│         │姿勢センサー│
   └──────┘         └─────┬────┘
                          │
                      ┌───▼────┐
                      │   U4   │
                      │LEDリング│
                      └────────┘
```

---

## ファームウェアバージョン

- **現在のバージョン**: `fwVersion = 9` (main.cpp:10)
- **ターゲットボード**: ESP32-S3-DevKitC-1
- **フラッシュサイズ**: 8MB
- **RAM使用量**: 11.0% (35,920 / 327,680 bytes)
- **Flash使用量**: 18.3% (611,017 / 3,342,336 bytes)

---

## 主要機能

### 1. デバイス初期化シーケンス

#### setup() (main.cpp:336-367)

起動時の処理フロー:

1. **NeoPixel初期化** - オンボードLED：赤色点灯
2. **シリアル通信開始**
   - デバッグ用: 115200 baud
   - U3通信用: 38400 baud (GPIO17:RX, GPIO18:TX)
3. **WiFi省電力モードOFF** - `WiFi.setSleep(false)`
4. **I2Cマスター初期化** - SDA=GPIO8, SCL=GPIO9
5. **ランダム遅延** - 0-10秒（バースト接続回避）
6. **WiFi接続** - マルチAP対応
7. **MAC取得** - デバイスID要求用
8. **MQTTサーバー接続** - SSID別ブローカー自動選択
9. **初期LED演出** - フェードイン（2秒）

---

### 2. ネットワーク管理

#### WiFi接続 (main.cpp:308-331)

**マルチAP対応:**
- `FU` (パスワード: `spark!!!!!`)
- `PRACHTSAALstudio` (パスワード: `04692645027246925387`)

**自動ブローカー選択** (main.cpp:24-31):
| SSID | MQTTブローカーIP |
|------|-----------------|
| `FU` | 192.168.1.2 |
| `PRACHTSAALstudio` | 192.168.178.178 |
| デフォルト | 192.168.0.10 |

**エラーハンドリング:**
- 接続失敗10回で自動再起動

---

#### MQTT通信 (mqttClient.cpp)

**サブスクライブトピック:**

| トピック | データ形式 | サイズ | 機能 |
|---------|-----------|-------|------|
| `<MAC>/idxy` | `ID,x,y,enc` | ASCII | デバイスID・位置・エンコーダー値取得 |
| `cl/<ID>` | RGB | 3 bytes | LED色制御 |
| `ps/<ID>` | mm+pitch+yaw | 6 bytes | 位置(mm) + 姿勢(pitch/yaw) |
| `dl/<ID>` | level | 1 byte | ダウンライト輝度(0-255) |
| `at/<ID>` | mode | 1 byte | 自律モードON/OFF |
| `spark` | seconds | ASCII | Sparkアニメーション秒数(1-300) |
| `ota` | - | - | OTA更新トリガー |
| `reboot` | - | - | 再起動コマンド |

**パブリッシュトピック:**

| トピック | データ | 頻度 | 用途 |
|---------|--------|------|------|
| `ini/<MAC>` | 空メッセージ | 5秒毎 | ID要求（未取得時） |
| `version` | [deviceID, fwVersion] | 1回 | バージョン報告 |

---

### 3. デバイスID取得プロセス

#### mqttClient.cpp:261-352

```
フロー:
1. 起動後、ini/<MAC> に空メッセージ送信（5秒毎）
   └─ トピック例: "ini/80B54EC31150"

2. <MAC>/idxy トピックから "ID,x,y,radian" 受信
   └─ 例: "123,10.0,15.0,3.14"

3. パースして保存:
   - deviceId: "123"
   - deviceIdNumber: 123 (uint8_t)
   - xValue: 10.0 (float)
   - yValue: 15.0 (float)
   - enc_horizontal: 3.14 (float)

4. バージョン情報をMQTTで送信
   └─ Payload: [deviceIdNumber, fwVersion]

5. I2C経由でU2にdeviceIdNumberを送信
   └─ コマンド: 'I' + deviceIdNumber

6. 制御トピック一括サブスクライブ:
   - cl/<deviceId>
   - ps/<deviceId>
   - dl/<deviceId>
   - at/<deviceId>
   - rb/<deviceId>

7. idAcquired = true → ホーミングシーケンス開始
```

---

### 4. ホーミングシーケンス

#### main.cpp:404-428 & mqttClient.cpp:35-67

**目的**: ウィンチを天井基準位置（ホームポジション）に移動

**処理フロー:**

```
1. 条件: idAcquired && !homed の間実行

2. pollHoming()を20ms毎に実行

3. I2C通信:
   Wire.requestFrom(U2_SLAVE_ADDRESS, 2)
   ├─ Byte 0: タグ ('H')
   └─ Byte 1: ステータス (0=進行中, 1=完了)

4. ステータス=1なら:
   - homed = true
   - オンボードLED消灯
   - U3に "C\n" コマンドを10回送信

5. ホーミング中の制限:
   - MQTT再接続スキップ（ID未取得時を除く）
   - LED/位置制御コマンド無視
```

**重要**: ホーミング完了まで、すべての制御コマンドは無視されます。

---

### 5. LED制御システム

#### 3段階のLED構成

1. **オンボードLED** (ESP32-S3 GPIO48)
   - 用途: システムステータス表示
   - タイプ: NeoPixel RGB LED

2. **U3 LEDコントローラー**
   - フロント/リアLED制御
   - UART経由で統合パケット受信

3. **U4 LEDリング** (U3経由)
   - メイン照明
   - U3から制御

---

#### 統合データパケット (main.cpp:180-214)

**フォーマット:**
```c
Serial1.printf(
  "DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%.2f,%.2f,B,%hhu,H,%.2f\n",
  f_r, f_g, f_b,       // フロントRGB (0-255)
  r_r, r_g, r_b,       // リアRGB (0-255)
  pitch_deg,           // Pitch角度 (float)
  yaw_deg,             // Yaw角度 (float)
  brightness,          // 自律モード (0 or 1)
  enc_horizontal       // 水平エンコーダー値 (float)
);
```

**例:**
```
DATA,F,255,100,50,R,200,150,100,P,45.50,180.25,B,1,H,3.14
```

**重複送信防止:**
- LED値: 完全一致で送信スキップ
- Float値: 0.01以上の差がない場合スキップ

---

#### LED演出機能

**1. フェードイン演出** (main.cpp:115-139)

```
目的: 起動時の滑らかな点灯

パラメータ:
- fadeSteps: 10ステップ
- fadeDelay: 40ms/ステップ
- 総時間: 400ms

処理:
for (step = 0 to 10):
  progress = step / 10
  fade_r = target_r * progress
  fade_g = target_g * progress
  fade_b = target_b * progress

  sendDataPacketToU3(...)
  setOnboardLEDColor(...)
  delay(40ms)
```

**2. Sparkモード** (main.cpp:217-297)

```
トリガー: MQTTで秒数指定（1-300秒）

エフェクト:
- 虹色レインボー（1秒で1周期）
- きらめき効果（0.7-1.0倍の輝度変調）
- 更新頻度: 20Hz (50ms間隔)

送信先:
- U3: UART経由 "SPARK,<秒数>\n"
- U2: I2C経由 'S' + LSB + MSB

終了処理:
- タイムアウトで自動終了
- 通常色に復帰
- U2/U3に終了コマンド送信
```

**HSVからRGBへの変換:**
```c
phase = (elapsed % 1000) / 1000.0 * 2.0 * PI
r = 128 + 127 * sin(phase)
g = 128 + 127 * sin(phase + 2.0943)  // 120度位相差
b = 128 + 127 * sin(phase + 4.1888)  // 240度位相差
```

---

### 6. 位置・姿勢制御

#### 位置制御（Z軸） (mqttClient.cpp:72-99)

**座標系変換:**
```
MQTT受信: 床基準
  ├─ 0mm = 床
  └─ 2800mm = 天井
      ↓
  変換: mm_ceiling = 2800 - mm_floor
      ↓
U2送信: 天井基準
  ├─ 0mm = 天井
  └─ 2800mm = 床
```

**I2C送信フォーマット:**
```c
Wire.beginTransmission(U2_SLAVE_ADDRESS);
Wire.write('Z');                // タグ
Wire.write(mm_ceiling & 0xFF);  // LSB
Wire.write(mm_ceiling >> 8);    // MSB
Wire.endTransmission();
```

---

#### 姿勢制御 (mqttClient.cpp:367-397)

**MQTT受信フォーマット（6バイト）:**
```c
struct PoseData {
  uint16_t mm;       // 垂直位置 (little-endian)
  int16_t pitchC;    // Pitch × 100 (little-endian)
  uint16_t yawC;     // Yaw × 100 (little-endian)
};
```

**変換:**
```
pitchC: -6000 〜 +6000 → pitch: -60.00° 〜 +60.00°
yawC:   0 〜 36000     → yaw:   0.00° 〜 360.00°
```

**処理フロー:**
```c
memcpy(&mm, payload + 0, 2);
memcpy(&pitchC, payload + 2, 2);
memcpy(&yawC, payload + 4, 2);

float z = mm / 1000.0f;        // メートル変換
float pitch = pitchC / 100.0f; // 度変換
float yaw = yawC / 100.0f;

if (homed) {
  sendZtoU2_mm(mm);            // I2C → U2
  sendDataPacketToU3(...);     // UART → U3
}
```

---

#### ダウンライト制御 (mqttClient.cpp:101-118)

**MQTT受信:**
- トピック: `dl/<deviceId>`
- データ: 1 byte (0-255)

**I2C送信:**
```c
Wire.beginTransmission(U2_SLAVE_ADDRESS);
Wire.write('L');    // タグ
Wire.write(level);  // 輝度 (0-255)
Wire.endTransmission();
```

---

### 7. 自律モード

#### mqttClient.cpp:400-407

**制御:**
- トピック: `at/<deviceId>`
- データ: 1 byte
  - `1` (0x01): 自律モードON
  - `0` (0x00): 手動モード

**動作:**
```c
autonomousMode = (uint8_t)(payload[0] == 1);

// U3へ統合パケットで送信
sendDataPacketToU3(
  current_f_r, current_f_g, current_f_b,
  current_r_r, current_r_g, current_r_b,
  current_pitch, current_yaw,
  autonomousMode  // ← ここで送信
);
```

**用途**: GridEye（赤外線アレイセンサー）連動想定

---

### 8. OTA（無線ファームウェア更新）

#### otaUpdater.cpp:7-107

**トリガー:**
- MQTTトピック: `ota`

**処理フロー:**
```
1. OTAprogress = true
   └─ loop()の通常処理停止

2. 遅延挿入: deviceIdNumber × 2秒
   └─ 目的: 一斉接続回避

3. HTTP接続:
   ├─ Host: comone-fragmented-unity-fw.s3.ap-northeast-1.amazonaws.com
   ├─ Port: 80
   └─ Path: /fu.bin

4. ヘッダー検証:
   ├─ HTTP/1.1 200 OK
   ├─ Content-Length: 取得
   └─ Content-Type: application/octet-stream

5. ファームウェア書き込み:
   Update.begin(contentLength)
   Update.writeStream(http)
   Update.end()

6. 検証:
   if (Update.isFinished())
     ESP.restart()
   else
     エラー処理

エラー時:
- 最大20回リトライ
- 失敗後、ESP.restart()
```

**セキュリティ:**
- HTTP接続（非暗号化）
- S3バケットから直接取得

---

### 9. メインループ処理

#### main.cpp:374-472

```
┌─────────────────────────────────────┐
│          loop() 実行フロー            │
└─────────────────────────────────────┘

10秒毎:
  └─ U3にテストコマンド送信 (デバッグ用)
     Serial1.printf("SPARK,%d\n", 10);

OTA進行中でない場合:

  1. MQTT接続チェック:
     if (!mqttClient.connected())
       if (homed || !idAcquired)
         reconnect()
       else
         スキップ（ホーミング中）

  2. mqttClient.loop()
     └─ MQTTメッセージ受信処理

  3. updateSparkLEDs()
     └─ Sparkモード実行中なら更新

  4. ホーミング監視:
     if (!homed && idAcquired)
       pollHoming(20ms)
       if (完了)
         homed = true
         U3に "C\n" × 10回送信

  5. ID未取得時:
     if (!idAcquired)
       5秒毎に ini/<MAC> 送信
```

---

## 通信プロトコル詳細

### I2C通信（U1 → U2）

**スレーブアドレス**: `0x08` (U2_SLAVE_ADDRESS)

| コマンド | バイト構成 | データ型 | 機能 | 実装 |
|---------|-----------|---------|------|------|
| `'Z'` | `'Z' + LSB + MSB` | uint16_t | 垂直位置(mm) | mqttClient.cpp:72 |
| `'L'` | `'L' + level` | uint8_t | ダウンライト輝度 | mqttClient.cpp:101 |
| `'I'` | `'I' + deviceId` | uint8_t | デバイスID通知 | mqttClient.cpp:123 |
| `'S'` | `'S' + LSB + MSB` | uint16_t | Spark秒数 | main.cpp:229 |
| `'H'` | 読み取り専用 | 'H' + status | ホーミング状態 | mqttClient.cpp:35 |

**例:**
```c
// 位置制御: 1400mm（天井基準）
Wire.write('Z');
Wire.write(0x78);  // 1400 & 0xFF
Wire.write(0x05);  // 1400 >> 8

// ダウンライト: 50%輝度
Wire.write('L');
Wire.write(128);

// ホーミング状態読み取り
Wire.requestFrom(0x08, 2);
char tag = Wire.read();      // 'H'
int status = Wire.read();    // 0 or 1
```

---

### UART通信（U1 → U3）

**ポート**: Serial1 (38400 baud)

#### 統合データパケット
```
フォーマット:
DATA,F,<f_r>,<f_g>,<f_b>,R,<r_r>,<r_g>,<r_b>,P,<pitch>,<yaw>,B,<brightness>,H,<enc_h>\n

例:
DATA,F,255,128,64,R,200,100,50,P,45.50,180.25,B,1,H,3.14\n
```

#### Sparkコマンド
```
フォーマット:
SPARK,<秒数>\n

例:
SPARK,10\n     (10秒間Sparkモード)
SPARK,0\n      (Sparkモード終了)
```

#### クリアコマンド
```
フォーマット:
C\n

用途: ホーミング完了時に10回送信
```

---

## ハードウェア仕様

### ピン配置（ESP32-S3）

| GPIO | 機能 | 接続先 | 備考 |
|------|------|--------|------|
| 48 | NeoPixel RGB LED | オンボード | システムステータス表示 |
| 8 | I2C SDA | U2 (ウィンチ) | 位置・照度制御 |
| 9 | I2C SCL | U2 (ウィンチ) | 位置・照度制御 |
| 17 | UART RX | U3 (LED制御) | データ受信用（未使用） |
| 18 | UART TX | U3 (LED制御) | 統合パケット送信 |

### ボーレート設定

| インターフェース | ボーレート | 用途 |
|----------------|-----------|------|
| Serial (USB) | 115200 | デバッグ出力 |
| Serial1 (UART) | 38400 | U3通信 |
| I2C | 100kHz | U2通信（デフォルト） |

---

## 状態管理

### グローバルフラグ (globals.h)

```c
// デバイス状態
bool idAcquired = false;           // デバイスID取得済み
bool homed = false;                // ホーミング完了
bool OTAprogress = false;          // OTA進行中

// LED制御
bool mqtt_led_override = false;    // MQTT LED制御有効
unsigned long last_mqtt_led_time;  // 最終LED制御時刻

// Spark機能
bool spark_mode_active = false;    // Sparkモード実行中
unsigned long spark_start_time;    // 開始時刻
unsigned long spark_duration;      // 持続時間(ms)

// 動作モード
uint8_t autonomousMode = 0;        // 自律モード (0=OFF, 1=ON)
```

### トピックサブスクライブ状態

```c
bool colorTopicSubscribed = false;  // cl/<ID>
bool psTopicSubscribed = false;     // ps/<ID>
bool dlTopicSubscribed = false;     // dl/<ID>
bool atTopicSubscribed = false;     // at/<ID>
bool rbTopicSubscribed = false;     // rb/<ID>
```

### 現在の状態保持

```c
// LED状態
uint8_t current_f_r, current_f_g, current_f_b;  // フロントRGB
uint8_t current_r_r, current_r_g, current_r_b;  // リアRGB

// 姿勢状態
float current_pitch = 0.0f;  // Pitch角度
float current_yaw = 0.0f;    // Yaw角度

// エンコーダー
float enc_horizontal = 0.0;  // 水平エンコーダー値
```

---

## エラーハンドリング

### WiFi接続エラー

```c
// main.cpp:319-325
int connectAttempts = 0;
while (wifiMulti.run() != WL_CONNECTED) {
  connectAttempts++;
  if (connectAttempts > 10) {
    ESP.restart();  // 10回失敗で再起動
  }
  delay(500);
}
```

### MQTT接続エラー

```c
// mqttClient.cpp:468-543
void reconnect() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect(clientId.c_str())) {
      // サブスクライブ処理
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      delay(5000);  // 5秒待機して再試行
    }

    // タイムアウト処理（コメントアウト中）
    // if (millis() - startAttemptTime > 30000) {
    //   ESP.restart();
    // }
  }
}
```

### OTAエラー

```c
// otaUpdater.cpp:62-69
if (!http.connect(host.c_str(), port)) {
  if (retryCount < maxRetry) {
    retryCount++;
    execOTA();  // 再試行（最大20回）
  } else {
    Serial.println("Max retries reached. OTA failed.");
    ESP.restart();  // 再起動
  }
}
```

### I2Cエラー

```c
// mqttClient.cpp:88
byte err = Wire.endTransmission();
if (err != 0) {
  Serial.printf("I2C error: %u\n", err);
  // 処理継続（致命的エラーとして扱わない）
}
```

---

## デバッグ機能

### シリアル出力

**フォーマット:**
```c
// MAC取得時
Serial.println("MACADDR: " + mac);

// ID取得時
Serial.printf("Parsed ID: %s, x: %.2f, y: %.2f, radian: %.2f\n",
              deviceId, xValue, yValue, enc_horizontal);

// ホーミング状態
Serial.print("[I2C: bytes=2, tag='H', status=1]");

// 位置・姿勢受信
Serial.printf("z=%.3f m  pitch=%.2f°  yaw=%.2f°\n", z, pitch, yaw);

// 統合パケット送信
Serial.printf("DATA,F,%hhu,%hhu,%hhu,R,%hhu,%hhu,%hhu,P,%.2f,%.2f,B,%hhu,H,%.2f\n", ...);
```

### テストコマンド（送信側）

```c
// main.cpp:235-248
// 送信フラグ・フレームレート・ペイロードサイズ制御
MQTTトピック:
- sendFlag (0 or 1)
- sendFramerate (fps)
- sendPayloadSize (bytes)
```

---

## パフォーマンス最適化

### 重複送信防止

```c
// main.cpp:195-198
bool led_same = (f_r == last_f_r && ...);
bool pitch_same = (abs(pitch_deg - last_pitch) < 0.01f);
bool yaw_same = (abs(yaw_deg - last_yaw) < 0.01f);
bool enc_h_same = (abs(enc_horizontal - last_enc_h) < 0.01f);

if (led_same && pitch_same && yaw_same && enc_h_same) {
  return;  // 送信スキップ
}
```

### タイマー管理

```c
// main.cpp:50-53
unsigned long lastMacPublish = 0;
unsigned long lastDataPublish = 0;
const unsigned long macPublishInterval = 5000;   // 5秒
const unsigned long dataPublishInterval = 1000;  // 1秒
```

### バースト接続回避

```c
// main.cpp:354
delay(random(0, 10000));  // 起動時0-10秒ランダム遅延

// otaUpdater.cpp:13
unsigned long delayMs = deviceIdNumber * 2000;  // OTA時2秒×ID
```

---

## セキュリティ考慮事項

### 脆弱性

1. **平文パスワード**: WiFi認証情報がハードコード
   ```c
   const char* password = "spark!!!!!";
   ```

2. **非暗号化通信**:
   - MQTT: 平文通信（ポート1883）
   - OTA: HTTP接続（非HTTPS）

3. **認証なし**:
   - MQTTクライアント認証なし
   - OTA更新時の署名検証なし

### 推奨対策

1. 認証情報を環境変数化
2. MQTT over TLS (ポート8883)
3. OTA署名検証の実装
4. デバイス証明書の使用

---

## 将来の拡張性

### 予約機能

```c
// main.cpp:87-92
// 降下制御関数（未実装）
extern bool descent_completed;
extern bool descent_initiated;
void initiate_descent();
void check_descent_completion();
```

### コメントアウト機能

```c
// GridEye Pitch制御（コメントアウト済）
// mqttClient.cpp:409-429
// if (status == 49) {  // '1'
//   current_pitch = 90.0;
//   sendDataPacketToU3(...);
// }
```

---

## トラブルシューティング

### よくある問題

#### 1. WiFi接続失敗
**症状**: 起動後、連続的に "." が表示
**原因**: SSID/パスワード不一致、電波弱い
**解決**: WiFi設定確認、アンテナ確認

#### 2. MQTT接続失敗
**症状**: "Attempting MQTT connection... failed, rc=-2"
**原因**: ブローカー到達不可
**解決**: ブローカーIP確認、ネットワーク疎通確認

#### 3. ホーミング完了しない
**症状**: "pollHoming=false" が継続
**原因**: U2応答なし、I2C通信エラー
**解決**: U2接続確認、I2Cプルアップ抵抗確認

#### 4. LED点灯しない
**症状**: コマンド送信してもLED変化なし
**原因**: homed=falseの状態
**解決**: ホーミング完了待ち

#### 5. OTA更新失敗
**症状**: "Failed to connect to ESP32-S3"
**原因**: S3バケット到達不可、コンテンツ不正
**解決**: ネットワーク確認、バイナリファイル確認

---

## 技術スタック

### ライブラリ依存関係

```ini
lib_deps =
  knolleary/PubSubClient@^2.8        # MQTT通信
  adafruit/Adafruit NeoPixel@^1.12.3 # LED制御
```

### フレームワーク

- **Arduino Core for ESP32**: 3.0.5
- **IDF Version**: 5.1系

### ビルドツール

- **PlatformIO**: 最新版
- **Compiler**: xtensa-esp32s3-elf-gcc 12.2.0

---

## まとめ

U1ファームウェアは、以下の役割を担う：

1. **ネットワークハブ**: WiFi/MQTT経由でクラウド接続
2. **デバイスマネージャー**: ID管理、状態同期
3. **コマンドルーター**: MQTTコマンドをI2C/UART変換
4. **LED制御オーケストレーター**: 複数LED系統の統合制御
5. **OTA対応**: リモートファームウェア更新

システム全体の「脳」として、各サブシステム（U2/U3/U4）を統合制御しています。
