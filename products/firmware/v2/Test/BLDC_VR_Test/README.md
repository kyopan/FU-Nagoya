# FU Tube v2 - Pitch Motor Test Firmware

**作成日**: 2025-11-13
**ボード**: ESP32-S3-MINI-1-N8
**開発環境**: PlatformIO + Arduino Framework

---

## 概要

ESP32-S3とBLDCモーター + 可変抵抗器（50kΩ）を用いたPitchモーターの動作テストファームウェア。

**テスト項目:**
1. 可変抵抗器による角度検出（ADC読取）
2. SimpleFOCによるBLDCモーター制御
3. 速度制御 + 位置フィードバック制御
4. キャリブレーション機能

---

## ハードウェア構成

### ESP32-S3 GPIO割当（#2）

| 機能 | GPIO | 備考 |
|------|------|------|
| **ADC (Pitch角度)** | **GPIO10** | 可変抵抗器角度読取（12bit ADC） |
| **FOC PWM_U (Pitch)** | **GPIO14** | PitchモーターU相 |
| **FOC PWM_V (Pitch)** | **GPIO15** | PitchモーターV相 |
| **FOC PWM_W (Pitch)** | **GPIO16** | PitchモーターW相 |

### 可変抵抗器仕様

| 項目 | 仕様 |
|------|------|
| **型番** | RK0971210-F15-C0-B503 |
| **抵抗値** | 50kΩ ±20% |
| **回転角度** | 300° ±5° |
| **出力電圧** | 0-3.3V（ESP32-S3 ADC入力） |
| **ADC分解能** | 12bit (4096段階) → 約0.073°/LSB |

### BLDCモーター仕様

| 項目 | 仕様 |
|------|------|
| **ポールペア数** | 7 |
| **相抵抗** | 6.5Ω |
| **KV値** | 330 |
| **制御方式** | SimpleFOC（速度制御 + 位置フィードバック） |

---

## セットアップ

### 1. 開発環境

**必須:**
- [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)（VSCode拡張機能）
- または PlatformIO Core（CLI）

### 2. プロジェクトのビルド

```bash
cd /path/to/Test
pio run
```

### 3. ファームウェア書き込み

```bash
# 自動書き込み（USB接続時）
pio run --target upload

# シリアルポート指定
pio run --target upload --upload-port /dev/ttyACM0
```

**書き込み方法:**
1. ESP32-S3をUSB Type-Cケーブルで接続
2. 自動的にダウンロードモードに入る（自動リセット対応）
3. 書き込み完了後、自動的にプログラムが起動

### 4. シリアルモニタ起動

```bash
pio device monitor
```

---

## 使い方

### 起動時の表示

```
========================================
FU Tube v2 - Pitch Motor Test
ESP32-S3 + BLDC + Variable Resistor
========================================

Variable Resistor Sensor initialized
BLDC Driver initialized
Motor initialized with SimpleFOC

========================================
Commands:
  C        - Calibrate variable resistor
  E        - Enable motor
  D        - Disable motor
  T<angle> - Set target angle (e.g., T45)
  S        - Show current status
========================================

ADC:2048 | Volt:1.65V | Angle:150.00° | Target:  0.00° | Motor:OFF
```

### コマンド一覧

| コマンド | 説明 | 例 |
|---------|------|-----|
| **C** | キャリブレーション実行 | `C` |
| **E** | モーター有効化 | `E` |
| **D** | モーター無効化 | `D` |
| **T<角度>** | 目標角度設定（0-300°） | `T45`, `T150` |
| **S** | 詳細ステータス表示 | `S` |

### キャリブレーション手順

1. シリアルモニタで`C`コマンド入力
2. Pitch軸を物理的に**0°位置**に移動
3. Enterキーを押す → ADC最小値（`adc_min`）取得
4. Pitch軸を物理的に**300°位置**に移動
5. Enterキーを押す → ADC最大値（`adc_max`）取得
6. キャリブレーション完了

**キャリブレーション例:**
```
========================================
Variable Resistor Calibration
========================================
Rotate the Pitch axis to 0° position
Press ENTER to continue...
<Enterキー入力>
0° position ADC value: 120

Rotate the Pitch axis to 300° position
Press ENTER to continue...
<Enterキー入力>
300° position ADC value: 3950

✓ Calibration completed successfully!
ADC range: 120 - 3950
Resolution: 0.078°/LSB
========================================
```

### モーター制御例

```
# モーター有効化
E
Motor ENABLED

# 目標角度45°に設定
T45
Target angle set to: 45.00 degrees

# 目標角度150°に設定
T150
Target angle set to: 150.00 degrees

# モーター無効化
D
Motor DISABLED
```

### ステータス表示例

```
# Sコマンドで詳細ステータス表示
S

========================================
Current Status
========================================
Variable Resistor:
  ADC Value: 2048 / 4095
  Voltage:   1.650 V
  Angle:     150.00 degrees

Calibration:
  Status:    DONE
  ADC Min:   120 (0°)
  ADC Max:   3950 (300°)
  Range:     3830
  Resolution: 0.078°/LSB

Motor:
  State:     ENABLED
  Target:    150.00 degrees
  Current:   150.12 degrees
  Error:     -0.12 degrees

Motor Parameters:
  Voltage Limit:   1.50 V
  Velocity Limit:  1.30 rad/s
  P Gain:          0.100
  I Gain:          0.300
  D Gain:          0.010
========================================
```

---

## SimpleFOC制御パラメータ

### 速度制御 + 位置フィードバック方式

**制御フロー:**
```
[目標角度] → [位置誤差計算] → [P制御で速度指令] → [速度制限] → [SimpleFOC速度制御] → [モーター駆動]
     ↑                                                                          ↓
     └────────────────────── [可変抵抗器ADC読取] ←─────────────────────────┘
```

### PIDパラメータ

| パラメータ | 値 | 説明 |
|-----------|-----|------|
| **motor.voltage_limit** | 1.5V | 電圧制限 |
| **motor.PID_velocity.P** | 0.1 | 速度比例ゲイン |
| **motor.PID_velocity.I** | 0.3 | 速度積分ゲイン |
| **motor.PID_velocity.D** | 0.01 | 速度微分ゲイン |
| **motor.LPF_velocity.Tf** | 0.15 | ローパスフィルタ時定数 |
| **motor.velocity_limit** | 1.3 rad/s | 最大速度制限 |
| **motor.PID_velocity.output_ramp** | 150.0 | 出力ランプレート |

### 位置フィードバック制御パラメータ

| パラメータ | 値 | 説明 |
|-----------|-----|------|
| **VELOCITY_P_GAIN** | 2.5 | 位置誤差→速度変換ゲイン |
| **MAX_VELOCITY** | 1.2 rad/s | 最大速度 |
| **POSITION_DEADBAND** | 0.015 rad | 位置制御デッドバンド（約0.86°） |

---

## トラブルシューティング

### 書き込みエラー

**症状:** `Failed to connect to ESP32`

**対策:**
1. BOOTボタンを押しながらRESETボタンを押す
2. RESETボタンを離す
3. BOOTボタンを離す
4. 再度書き込み実行

### シリアル接続エラー

**症状:** シリアルポートが見つからない

**対策（Linux/macOS）:**
```bash
# デバイス確認
ls /dev/tty*

# ESP32-S3は通常 /dev/ttyACM0 として認識される
```

**対策（macOS）:**
```bash
# ポート権限付与
sudo chmod 666 /dev/ttyACM0
```

### モーターが動かない

**症状:** モーター有効化しても動かない

**チェックリスト:**
1. 電源供給確認（5V）
2. PWMピン配線確認（GPIO14, 15, 16）
3. モータードライバ動作確認
4. シリアルモニタのエラーメッセージ確認

### 角度が安定しない

**症状:** ADC値が不安定に変動

**対策:**
1. 可変抵抗器の配線確認（VCC, GND, OUT）
2. シールド線使用（ノイズ対策）
3. GNDプレーン近接配線
4. デカップリングコンデンサ追加（0.1µF）

---

## 参考資料

- [Variable-Resistor-RK0971210.md](../../specs/v2/Component/Variable-Resistor-RK0971210.md) - 可変抵抗器仕様書
- [ESP32-S3.md](../../specs/v2/Component/ESP32-S3.md) - ESP32-S3仕様書
- [SimpleFOC Documentation](https://docs.simplefoc.com/) - SimpleFOC公式ドキュメント
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)

---

## 変更履歴

| 日付 | 変更内容 | 担当 |
|------|---------|------|
| 2025-11-13 | 初版作成（ESP32-S3 + BLDC + 可変抵抗器テスト） | CIC |
