# U1 Firmware Development Instructions

**プロジェクト**: FU@ComoNe Winch U1 Firmware
**ターゲット**: ESP32-S3-DevKitC-1
**フレームワーク**: Arduino + PlatformIO
**現在のバージョン**: v9

---

## 🎯 開発目的

Winch U1基板（ESP32-S3搭載）のファームウェアを、新しいピンアサインに対応させ、動作確認を完了させる。

**期限**: 2025-10-24（基板発注準備）

---

## 📋 作業概要

### 現在の状況
- ✅ メカニカル部分完成
- ✅ ファームウェアv9存在（既存コード）
- ⚠️ ピンアサイン未確定
- ⚠️ 通信テスト未実施

### 必要な作業
1. ピンアサイン確定（基板回路図から）
2. ファームウェア修正（GPIO番号更新）
3. ビルド＆書き込み
4. 通信テスト（I2C: U2, UART: U3）
5. システム統合テスト

---

## 🔍 参照ドキュメント

### 必読ドキュメント（優先度順）

1. **U1_FIRMWARE_OVERVIEW.md**（このディレクトリ）
   - ファームウェア完全仕様
   - システム構成、通信プロトコル、状態管理
   - 既存コードの動作詳細

2. **../../docs/specs/winch-u1-esp32s3-pinout.md**
   - ピンアサイン仕様（更新予定）
   - 現在確定済みピン: GPIO 8/9 (I2C), GPIO 17/18 (UART), GPIO 48 (NeoPixel)
   - テスト計画、ファームウェア修正チェックリスト

3. **../../docs/specs/winch-circuit-design.md**
   - Winch V2回路設計全体仕様
   - V1との比較、改良点

4. **../../notes/v1-analysis.md**
   - V1システム分析
   - 既存システムの課題

---

## 🔧 開発環境セットアップ

### 前提条件
- VS Code インストール済み
- PlatformIO Extension インストール済み
- ESP32-S3-DevKitC-1 ボード接続可能

### プロジェクトを開く

```bash
# このディレクトリで VS Code を開く
cd /path/to/projects/nagoya-tube-installation/firmware/Winch/U1
code .
```

### PlatformIO設定確認

**platformio.ini**:
```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps =
    knolleary/PubSubClient@^2.8
    adafruit/Adafruit NeoPixel@^1.12.3
```

---

## 📝 タスクリスト

### Phase 1: ピンアサイン確定

- [ ] **基板回路図を確認**
  - EasyEDA/KiCadファイルを開く
  - ESP32-S3周辺のトレースを追跡
  - U2 (I2C), U3 (UART), オンボードLEDの接続ピンを特定

- [ ] **実物基板で導通確認**（オプション）
  - テスターでSDA/SCL配線確認
  - TX/RX配線確認
  - 未使用ピンの配線状況確認

- [ ] **winch-u1-esp32s3-pinout.mdを更新**
  - 「ピンアサイン最終決定表」セクションを埋める
  - 確定済みGPIOを記録
  - 未使用ピンをリストアップ

**現在確定済みピン（ファームウェアベース）**:
```cpp
GPIO 48: NeoPixel RGB LED (オンボード)
GPIO 8:  I2C SDA (U2 ウィンチモーター)
GPIO 9:  I2C SCL (U2 ウィンチモーター)
GPIO 17: UART RX (U3 LED制御) - 未使用
GPIO 18: UART TX (U3 LED制御)
```

---

### Phase 2: ファームウェア修正

#### 2.1 GPIO定義の更新

**main.cpp** (行10-12):
```cpp
// 現在の定義
#define ONBOARD_LED_PIN 48
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// 確認必要: 基板回路図と一致しているか？
// 一致していない場合は修正
```

**main.cpp** (行352):
```cpp
// UART初期化
Serial1.begin(38400, SERIAL_8N1, 17, 18);  // RX=17, TX=18

// 確認必要: 基板回路図と一致しているか？
```

#### 2.2 修正チェックリスト

- [ ] `main.cpp`: GPIO番号定義確認・修正
- [ ] `main.cpp`: `Wire.begin(SDA, SCL)` 引数確認
- [ ] `main.cpp`: `Serial1.begin(baud, config, RX, TX)` 引数確認
- [ ] `main.cpp`: `Adafruit_NeoPixel` 初期化確認
- [ ] `platformio.ini`: ボード設定確認
- [ ] `platformio.ini`: ビルドフラグ確認

#### 2.3 変更が必要な可能性があるファイル

```
src/
├── main.cpp          # メインロジック、GPIO定義
├── mqttClient.cpp    # MQTT通信、I2C/UART送信
├── mqttClient.h
├── otaUpdater.cpp    # OTA更新
├── otaUpdater.h
└── globals.h         # グローバル変数
```

---

### Phase 3: ビルド＆書き込み

#### 3.1 ビルド

PlatformIOターミナルで実行:
```bash
pio run
```

**期待される出力**:
```
RAM:   [=         ]  11.0% (used 35920 bytes from 327680 bytes)
Flash: [===       ]  18.3% (used 611017 bytes from 3342336 bytes)
========================= [SUCCESS] Took X.XX seconds =========================
```

**エラーが出た場合**:
- ライブラリ依存関係を確認: `pio lib install`
- コンパイルエラーを修正
- GPIO番号の型確認（uint8_t推奨）

#### 3.2 書き込み

```bash
pio run --target upload
```

**書き込み前の確認**:
- ESP32-S3がUSB接続されているか
- COMポート/デバイスパスが正しいか（platformio.iniで指定可能）
- BOOTボタンを押しながらリセット（書き込みモード）

#### 3.3 シリアルモニター起動

```bash
pio device monitor
```

**期待される起動ログ**:
```
Connecting to WiFi...
Connected to WiFi: FU
MACADDR: 80B54EC31150
Connecting to MQTT broker...
MQTT connected
Subscribing to: 80B54EC31150/idxy
```

---

### Phase 4: 通信テスト

#### 4.1 I2C通信テスト（U1 ↔ U2）

**テストコード追加先**: `main.cpp` の `setup()` または `loop()`

```cpp
// I2C通信テスト関数
void testI2CConnection() {
  Serial.println("=== I2C Communication Test ===");

  // 1. ホーミング状態読み取り
  Wire.requestFrom(U2_SLAVE_ADDRESS, 2);
  if (Wire.available() == 2) {
    char tag = Wire.read();
    int status = Wire.read();
    Serial.printf("[TEST] Homing status: tag='%c', status=%d\n", tag, status);
  } else {
    Serial.println("[ERROR] Failed to read from U2");
  }

  // 2. 位置制御テスト（1000mm = 0x03E8）
  Wire.beginTransmission(U2_SLAVE_ADDRESS);
  Wire.write('Z');
  Wire.write(0xE8);  // LSB
  Wire.write(0x03);  // MSB
  byte err = Wire.endTransmission();
  Serial.printf("[TEST] Position command (1000mm): I2C error=%d\n", err);

  // 3. ダウンライト輝度テスト（50% = 128）
  Wire.beginTransmission(U2_SLAVE_ADDRESS);
  Wire.write('L');
  Wire.write(128);
  err = Wire.endTransmission();
  Serial.printf("[TEST] Downlight (50%%): I2C error=%d\n", err);

  Serial.println("=== I2C Test Complete ===");
}
```

**テスト実行**:
- `setup()` の最後に `testI2CConnection();` を追加
- シリアルモニターで結果確認
- I2C error=0 なら成功

**期待される出力**:
```
=== I2C Communication Test ===
[TEST] Homing status: tag='H', status=1
[TEST] Position command (1000mm): I2C error=0
[TEST] Downlight (50%): I2C error=0
=== I2C Test Complete ===
```

---

#### 4.2 UART通信テスト（U1 → U3）

**テストコード追加先**: `main.cpp`

```cpp
// UART通信テスト関数
void testUARTConnection() {
  Serial.println("=== UART Communication Test ===");

  // 1. 統合データパケット送信（赤色LED）
  Serial.println("[TEST] Sending DATA packet (Red LED)");
  Serial1.printf("DATA,F,255,0,0,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");
  delay(1000);

  // 2. 緑色LED
  Serial.println("[TEST] Sending DATA packet (Green LED)");
  Serial1.printf("DATA,F,0,255,0,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");
  delay(1000);

  // 3. 青色LED
  Serial.println("[TEST] Sending DATA packet (Blue LED)");
  Serial1.printf("DATA,F,0,0,255,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");
  delay(1000);

  // 4. Sparkコマンド
  Serial.println("[TEST] Sending SPARK command (10 sec)");
  Serial1.printf("SPARK,10\n");
  delay(2000);

  // 5. クリアコマンド
  Serial.println("[TEST] Sending CLEAR command");
  Serial1.printf("C\n");

  Serial.println("=== UART Test Complete ===");
}
```

**テスト実行**:
- `setup()` の最後に `testUARTConnection();` を追加
- U3のLED動作を目視確認
- シリアルモニターでコマンド送信を確認

**期待される動作**:
- 赤→緑→青とLED色が変化
- Sparkモードで虹色アニメーション（10秒間）
- クリアでLED消灯

---

#### 4.3 システム統合テスト

**テストシナリオ**:

1. **起動シーケンス**
   ```
   ✓ WiFi接続
   ✓ MQTT接続
   ✓ デバイスID取得（<MAC>/idxy トピック）
   ✓ ホーミング完了
   ```

2. **MQTT経由のLED制御**
   ```bash
   # MQTTブローカーから送信（別ターミナル）
   mosquitto_pub -h 192.168.1.2 -t "cl/123" -m $'\xFF\x00\x00'  # 赤色
   mosquitto_pub -h 192.168.1.2 -t "cl/123" -m $'\x00\xFF\x00'  # 緑色
   ```

3. **MQTT経由の位置制御**
   ```bash
   # 位置指定（1500mm = 0x05DC）
   mosquitto_pub -h 192.168.1.2 -t "ps/123" -m $'\xDC\x05\x00\x00\x00\x00'
   ```

4. **Sparkモード**
   ```bash
   # 10秒間Spark
   mosquitto_pub -h 192.168.1.2 -t "spark" -m "10"
   ```

**成功基準**:
- シリアルモニターでMQTTメッセージ受信確認
- U3のLED色が変化
- U2のモーター動作（位置制御）
- Sparkモードでレインボーアニメーション

---

### Phase 5: ドキュメント更新

#### 5.1 ピンアサイン確定後

- [ ] `../../docs/specs/winch-u1-esp32s3-pinout.md` を更新
  - 「ピンアサイン最終決定表」を完成
  - テスト結果を記録

#### 5.2 テスト完了後

- [ ] `../../TODO.md` を更新
  - 各サブタスクのチェックボックスをオン
  - テスト結果を記録

#### 5.3 TODO集約

```bash
cd /Users/kyopan/Dropbox (個人)/Obsidian/kyopan
python3 .todos/sync-todos.py
```

---

## 🐛 トラブルシューティング

### 問題: WiFi接続失敗

**症状**:
```
Connecting to WiFi...
...................
```

**解決策**:
1. SSID/パスワード確認（main.cpp:24-31）
2. WiFiルーター電波強度確認
3. ESP32-S3アンテナ接続確認

---

### 問題: MQTT接続失敗

**症状**:
```
Attempting MQTT connection... failed, rc=-2
```

**解決策**:
1. ブローカーIP確認（SSIDに応じた自動選択）
2. ネットワーク疎通確認: `ping 192.168.1.2`
3. MQTTブローカー起動確認

---

### 問題: I2C通信エラー

**症状**:
```
[ERROR] Failed to read from U2
I2C error: 2
```

**エラーコード**:
- `0`: 成功
- `1`: データ長超過
- `2`: NACK on address（デバイス応答なし）
- `3`: NACK on data
- `4`: その他エラー

**解決策**:
1. U2接続確認（物理的に接続されているか）
2. I2Cプルアップ抵抗確認（4.7kΩ推奨）
3. SDA/SCL配線確認（逆接続していないか）
4. U2のスレーブアドレス確認（0x08）

---

### 問題: UART通信エラー

**症状**: U3のLEDが反応しない

**解決策**:
1. TX/RX配線確認（クロス接続が必要）
2. ボーレート確認（38400 baud）
3. U3の電源確認
4. シリアルモニターでパケット送信を確認

---

### 問題: ビルドエラー

**症状**:
```
error: 'ONBOARD_LED_PIN' was not declared in this scope
```

**解決策**:
1. GPIO番号定義を確認（main.cpp冒頭）
2. ヘッダーファイルのインクルード確認
3. platformio.ini の lib_deps 確認

---

## 📞 サポート

### ドキュメント参照優先順位

1. **U1_FIRMWARE_OVERVIEW.md** - ファームウェア仕様の完全版
2. **winch-u1-esp32s3-pinout.md** - ピンアサイン・テスト計画
3. **ESP32-S3 Datasheet** - 公式ハードウェア仕様

### 質問時に提供する情報

- シリアルモニター出力（全文）
- 変更したコード（差分）
- テストした手順
- 期待される動作 vs 実際の動作

---

## ✅ 完了条件

- [ ] ピンアサイン確定・ドキュメント更新
- [ ] ファームウェア修正完了
- [ ] ビルド成功
- [ ] ESP32-S3への書き込み成功
- [ ] I2C通信テスト成功（U2との通信確認）
- [ ] UART通信テスト成功（U3への送信確認）
- [ ] システム統合テスト成功（MQTT経由の制御確認）
- [ ] TODO.md更新・集約

**すべて完了したら基板発注準備完了です！**

---

**開発を開始する準備が整いました。VS Codeでこのディレクトリを開いてPlatformIO開発を始めてください。**
