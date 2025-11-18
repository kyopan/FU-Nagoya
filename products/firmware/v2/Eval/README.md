# FU v2 Eval Firmware

**作成日**: 2025-11-17
**ステータス**: Ready for testing
**目的**: v2基板完成までの評価テスト用にv1基板を流用

---

## 概要

v2ウインチ基板が正しく動作しない問題が発生したため、v1基板を流用して3台動作テストを実施するための評価ファームウェア。

**3つのファームウェア構成:**
1. `winch_esp32c6_eval` - ウインチ上部制御（ESP32-C6）
2. `winch_rp2040_eval` - ウインチ下部制御（Waveshare RP2040 Zero）
3. `tube_rp2040_eval` - 筒LED制御（Waveshare RP2040 Zero）

---

## ハードウェア構成

### 1. winch_esp32c6_eval
- **MCU**: ESP32-C6 DevKitM-1
- **役割**: Wi-Fi/MQTT通信、上下ユニット間通信制御
- **流用元**: v1 U1_mqtt

### 2. winch_rp2040_eval
- **MCU**: Waveshare RP2040-Zero
- **役割**: ステッパーモーター制御、ホーミング、2M上下動
- **流用元**: v1 FU_WINCH_RP2040_U2

### 3. tube_rp2040_eval
- **MCU**: Waveshare RP2040-Zero
- **役割**: NeoPixel LED制御（シリアル経由）
- **新規実装**: シリアルコマンドで色制御

---

## ビルド・書き込み手順

### 共通: PlatformIO CLI使用

```bash
# プロジェクトディレクトリに移動
cd /Users/kyopan/Dropbox\ \(個人\)/Obsidian/kyopan/projects/fu/products/firmware/v2/Eval

# ビルド
pio run -d [プロジェクト名]

# 書き込み
pio run -d [プロジェクト名] -t upload

# シリアルモニター起動
pio device monitor -d [プロジェクト名]
```

---

### 1. winch_esp32c6_eval

```bash
# ビルド
pio run -d winch_esp32c6_eval

# 書き込み
pio run -d winch_esp32c6_eval -t upload

# シリアルモニター（115200 baud）
pio device monitor -d winch_esp32c6_eval
```

**platformio.ini 設定:**
- Board: `esp32-c6-devkitm-1`
- Framework: Arduino
- Monitor speed: 115200

**主要機能:**
- Wi-Fi接続（SSID: FU）
- MQTT通信（Broker: 192.168.1.2）
- オンボードNeoPixel LED制御
- U2（RP2040）へのI2C通信

---

### 2. winch_rp2040_eval

```bash
# ビルド
pio run -d winch_rp2040_eval

# 書き込み
pio run -d winch_rp2040_eval -t upload

# シリアルモニター（115200 baud）
pio device monitor -d winch_rp2040_eval
```

**platformio.ini 設定:**
- Board: `waveshare_rp2040_zero`
- Framework: Arduino
- Monitor speed: 115200

**主要機能:**
- TMC2209ステッパー制御
- AccelStepperによる加減速制御
- ホーミングスイッチ検知
- 2M上下動（ランダムターン速度対応可能）
- ESP32C6からのI2C受信

**ピン配置:**
```cpp
PIN_TMC2209_EN    2
PIN_TMC2209_TX    0
PIN_TMC2209_RX    1
PIN_TMC2209_STEP  7
PIN_TMC2209_DIR   8
PIN_SENSOR        27  // ホーミングスイッチ
PIN_SOLENOID      26
PIN_U2_SDA        4
PIN_U2_SCL        5
```

---

### 3. tube_rp2040_eval

```bash
# ビルド
pio run -d tube_rp2040_eval

# 書き込み
pio run -d tube_rp2040_eval -t upload

# シリアルモニター（115200 baud）
pio device monitor -d tube_rp2040_eval
```

**platformio.ini 設定:**
- Board: `waveshare_rp2040_zero`
- Framework: Arduino
- Monitor speed: 115200

**ハードウェア設定（fu_v2_eval_board準拠）:**
- **NeoPixel LED**: GPIO 29
- **LED数**: 144個（72 LEDs × 2 strips）
  - 構成: 24×2 + 24×2 + 24×2 = 144 LEDs
- **Type**: NEO_GRB + NEO_KHZ800
- **Brightness**: 50/255 (デフォルト)

**主要機能:**
- NeoPixel LED制御（144個すべてに同一色設定）
- シリアルコマンドでRGB色制御

**シリアルコマンド:**
```
R<0-255>       - 赤色設定（例: R255）
G<0-255>       - 緑色設定（例: G128）
B<0-255>       - 青色設定（例: B0）
C<R>,<G>,<B>   - RGB一括設定（例: C255,100,0）
H              - ヘルプ表示
?              - 現在の色表示
```

**使用例:**
```
> R255
Red set to: 255
> G0
Green set to: 0
> B0
Blue set to: 0
Color set to RGB(255, 0, 0)  # 赤色

> C0,255,0
Color set to RGB(0, 255, 0)  # 緑色

> C100,150,200
Color set to RGB(100, 150, 200)  # 淡い青紫

> ?
Current color: RGB(100, 150, 200)
```

---

## 動作テスト手順

### Phase 1: 個別動作確認

#### ESP32-C6
```bash
1. winch_esp32c6_eval 書き込み
2. シリアルモニター起動
3. Wi-Fi接続確認（SSID: FU）
4. MQTT接続確認（Broker: 192.168.1.2）
5. オンボードLED点灯確認
```

#### RP2040 Winch
```bash
1. winch_rp2040_eval 書き込み
2. シリアルモニター起動
3. ホーミング動作確認（センサー検知）
4. 上昇動作確認（手動で距離測定）
5. 下降動作確認
6. ターン動作確認（速度変更）
```

#### RP2040 Tube
```bash
1. tube_rp2040_eval 書き込み
2. シリアルモニター起動
3. 初期化確認: "NeoPixel strip initialized on GPIO 29"
4. LED数確認: "LED count: 144"
5. コマンド "R255" → 全144個のLEDが赤色点灯確認
6. コマンド "G255" → 全144個のLEDが緑色点灯確認
7. コマンド "B255" → 全144個のLEDが青色点灯確認
8. コマンド "C255,100,50" → オレンジ色確認（全LED同一色）
9. グラデーションテスト（手動で複数色設定）
```

---

### Phase 2: 3台同時動作テスト

```bash
# 準備
1. 各ユニットを設置・配線
2. 電源投入順序: RP2040 Tube → RP2040 Winch → ESP32-C6
3. 各ユニットのシリアルモニター確認

# テスト実施
1. ホーミング動作（3台同時）
2. 2M上下動（ランダムターン速度）
3. LED発光パターン確認（tube_rp2040経由）
4. 風環境テスト（エアコン）
5. 30分連続動作テスト
```

**評価項目:**
- ベアリング+ケーブル耐環境性
- ホーミングスイッチ動作
- ウインチ静音性
- LED発光パターン
- 上下ユニット間通信

詳細: [v2-winch-eval-3units-test-spec.md](../../../docs/specs/v2-winch-eval-3units-test-spec.md)

---

## トラブルシューティング

### ビルドエラー

#### ESP32-C6: platform-espressif32エラー
```bash
# platform URLを最新に更新
[env:esp32-c6-devkitm-1]
platform = https://github.com/tasmota/platform-espressif32/releases/download/2024.09.10/platform-espressif32.zip
```

#### RP2040: platform-raspberrypiエラー
```bash
# platformを正しく指定
[env:waveshare_rp2040_zero]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
```

---

### 書き込みエラー

#### デバイスが認識されない
```bash
# macOS: デバイス確認
ls /dev/cu.*

# 権限確認
sudo chmod 666 /dev/cu.usbserial-*

# PlatformIO再起動
pio device list
```

#### RP2040: BOOTSELモード
```bash
1. BOOTSELボタンを押しながらUSB接続
2. RP2040がストレージデバイスとして認識
3. PlatformIOから書き込み実行
```

---

### 動作エラー

#### ESP32-C6: Wi-Fi接続失敗
```cpp
// SSID/パスワード確認
const char* ssid     = "FU";
const char* password = "spark!!!!!";
```

#### RP2040 Winch: ホーミング失敗
```cpp
// センサー閾値確認（globals変数）
int SENSOR_THRESHOLD = 250;

// ホーミング速度調整
long HOMING_SPEED = 500;
```

#### RP2040 Tube: LED点灯しない
```bash
# NeoPixel電源確認（RP2040 Zeroは自動）
# ピン番号確認
#define PIN_NEOPIXEL    16
```

---

## ファイル構造

```
Eval/
├── README.md                          # このファイル
├── winch_esp32c6_eval/
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   ├── globals.h
│   │   ├── mqttClient.cpp
│   │   ├── mqttClient.h
│   │   ├── otaUpdater.cpp
│   │   └── otaUpdater.h
│   ├── include/
│   ├── lib/
│   └── test/
├── winch_rp2040_eval/
│   ├── platformio.ini
│   ├── src/
│   │   └── main.cpp                   # v1 FU_WINCH_RP2040_U2.ino移植
│   ├── include/
│   ├── lib/
│   └── test/
└── tube_rp2040_eval/
    ├── platformio.ini
    ├── src/
    │   └── main.cpp                   # シリアル色制御実装
    ├── include/
    ├── lib/
    └── test/
```

---

## 次のステップ

```markdown
[ ] 各ファームウェアのビルド確認
[ ] 各ユニットへの書き込み
[ ] 個別動作確認
[ ] 3台セットアップ
[ ] 動作テスト実施
[ ] 評価結果まとめ
[ ] v2基板改善へのフィードバック
```

---

## 参考資料

- [v2 動作テスト仕様書](../../../docs/specs/v2-winch-eval-3units-test-spec.md)
- [v1 Winch U1_mqtt](../../v1/Winch_v1/U1_mqtt/)
- [v1 Winch RP2040](../../v1/Winch_v1/FU_WINCH_RP2040_U2/)
- [PlatformIO Documentation](https://docs.platformio.org/)

---

## 変更履歴

| 日付 | 変更内容 | 担当 |
|------|---------|------|
| 2025-11-17 | 初版作成（3つのEvalファームウェア） | CIC |
