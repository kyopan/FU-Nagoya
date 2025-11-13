# ESP32-S3 Servo Test with Potentiometer + NeoPixel

RCサーボの駆動テストコード（可変抵抗による制御 + ビジュアルフィードバック）

## ハードウェア構成

### 必要な部品
- ESP32-S3 DevKit × 1
- RCサーボ (DS-M005 または互換品) × 1
- 10KΩ可変抵抗器 × 1
- ジャンパー線
- ※ NeoPixel LEDはESP32-S3オンボードRGB LED使用（追加部品不要）

### ピン接続

| コンポーネント | ピン | ESP32-S3 GPIO |
|---------------|-----|---------------|
| サーボ信号線 | Signal | GPIO 13 |
| サーボ電源 | VCC | 5V |
| サーボGND | GND | GND |
| 可変抵抗VCC | - | 3.3V |
| 可変抵抗GND | - | GND |
| 可変抵抗信号 | Wiper | GPIO 4 (ADC1_CH3) |
| NeoPixel LED | - | GPIO 48 (onboard) |

### 配線図

```
ESP32-S3 DevKit          RC Servo (DS-M005)
┌─────────────┐          ┌───────┐
│             │          │       │
│   GPIO 13  ─┼─────────▶│ Signal│
│             │          │       │
│     5V     ─┼─────────▶│  VCC  │
│             │          │       │
│    GND     ─┼─────────▶│  GND  │
│             │          └───────┘
│             │
│             │          10K Potentiometer
│             │          ┌───────┐
│    3.3V    ─┼─────────▶│  VCC  │
│             │          │       │
│   GPIO 4   ◀┼──────────│ Wiper │
│             │          │       │
│    GND     ─┼─────────▶│  GND  │
│             │          └───────┘
└─────────────┘
```

## 機能

### 動作概要
1. GPIO 4 (ADC1_CH3) で可変抵抗の電圧を12bit精度で読み取り (0-4095)
2. ADC値をサーボ角度 (0-180°) にマッピング
3. GPIO 13 からPWM信号でサーボを制御
4. オンボードNeoPixel LEDが角度に応じて色変化
   - 0° → 60°: Red → Yellow (赤→黄)
   - 60° → 120°: Yellow → Green (黄→緑)
   - 120° → 180°: Green → Blue (緑→青)
5. 50Hz (20ms間隔) で更新
6. シリアルモニタでADC値と角度をリアルタイム表示

### サーボ仕様
- パルス幅: 500-2500 μs（マイクロ秒精度制御）
- 角度範囲: 0-180°
- 分解能: 2000ステップ（0.5μs精度、従来の11倍）
- 更新レート: 50Hz (20ms間隔)

### ADC仕様
- 分解能: 12bit (0-4095)
- 減衰設定: 11dB (0-3.3V フルレンジ)
- サンプリング: 可変抵抗の位置に応じてリアルタイム
- ノイズフィルタ: 5サンプル移動平均
- デッドバンド: ±2° (ジッター防止)

## ビルド・書き込み

### PlatformIO CLI
```bash
cd /path/to/ServoTest

# ビルド
pio run

# 書き込み
pio run --target upload

# シリアルモニタ
pio device monitor -b 115200
```

### VS Code + PlatformIO拡張機能
1. プロジェクトフォルダを開く
2. PlatformIO: Build (Ctrl+Alt+B)
3. PlatformIO: Upload (Ctrl+Alt+U)
4. PlatformIO: Serial Monitor (Ctrl+Alt+S)

## シリアル出力例

```
=================================
ESP32-S3 Servo + NeoPixel Test
=================================
Pin Configuration:
  Servo Signal: GPIO 13
  Potentiometer: GPIO 4 (ADC1_CH3)
  NeoPixel LED: GPIO 48 (onboard)
=================================

[NeoPixel] Initialized onboard RGB LED
[NeoPixel] Brightness: 50/255
[NeoPixel] Color mapping: 0°=Red → 90°=Green → 180°=Blue
[Filter] Moving average: 5 samples, Deadband: ±2°

[ADC] Initialized 12-bit ADC (0-4095)
[Servo] Attached to GPIO 13
[Servo] Pulse width: 500-2500 us
[Servo] Angle range: 0-180 degrees
[Servo] Initial position: 90 degrees (center)

Setup complete. Starting servo + LED control...

[Update] Filtered Angle:  90° → 1500 μs (change: +90°)
[Update] Filtered Angle:  95° → 1555 μs (change: +5°)
[Update] Filtered Angle: 100° → 1611 μs (change: +5°)
[Update] Filtered Angle: 105° → 1666 μs (change: +5°)
...
```

## トラブルシューティング

### サーボが動かない
- 5V電源の供給を確認
- GPIO 13 の接続を確認
- サーボの動作電圧範囲を確認 (DS-M005: 4.8-6V)

### ADC値が変化しない
- 可変抵抗の配線確認 (VCC: 3.3V, GND: GND, Wiper: GPIO 4)
- 可変抵抗の動作確認 (テスターで抵抗値測定)

### NeoPixel LEDが光らない
- ESP32-S3 DevKitのオンボードLED搭載を確認
- GPIO 48がオンボードLEDに接続されているか確認
- 他のGPIOでNeoPixelを制御している場合、NEOPIXEL_PINを変更

### サーボがジッターする（ジリジリ音）
- **ソフトウェア対策** (実装済み):
  - デッドバンド: `DEADBAND_THRESHOLD = 2` (±2°の変化を無視)
  - 移動平均フィルタ: `FILTER_SAMPLES = 5` (5サンプル平均)
  - さらに改善する場合: `DEADBAND_THRESHOLD` を 3〜5 に増やす
  - 反応速度を犠牲にして滑らかさ優先: `FILTER_SAMPLES` を 8〜10 に増やす
- **ハードウェア対策**:
  - 5V電源に100μF〜470μF電解コンデンサを並列接続
  - 外部5V ACアダプタ使用（最低1A、推奨2A以上）
  - 10回転精密可変抵抗器への交換（より安定した入力）
- **更新レート調整**:
  - `UPDATE_INTERVAL_MS` を 50ms (20Hz) に変更

### シリアル出力が文字化け
- ボーレート 115200 を確認
- USB-CDC設定を確認 (`ARDUINO_USB_CDC_ON_BOOT=1`)

## V1との違い

| 項目 | V1 (Winch_v1/U1) | V2 (ServoTest) |
|------|------------------|----------------|
| 目的 | MQTT連携・複雑制御 | サーボ単体テスト |
| 通信 | WiFi + MQTT | なし（ローカル） |
| センサー | I2C複数系統 | ADC (可変抵抗のみ) |
| LED | NeoPixel多数制御 | オンボード1個 (角度視覚化) |
| OTA | 対応 | 非対応 |
| コード規模 | ~500行 | ~230行 (シンプル) |

## 今後の拡張

### Phase 2: WiFi連携
- MQTTでサーボ角度を遠隔制御
- WebSocketでリアルタイム角度送信

### Phase 3: マルチサーボ
- Pitch軸 + YAW軸の2軸制御
- SimpleFOC統合でBLDCモーター制御

### Phase 4: センサー統合
- AS5600エンコーダーでフィードバック制御
- PIDループで正確な位置制御

## ライセンス

MIT License

## 作成者

kyopan / CIC (2025-11-09)
