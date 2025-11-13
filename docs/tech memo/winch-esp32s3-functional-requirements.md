# Winch ESP32-S3 機能要件整理

**プロジェクト**: FU@ComoNe (Fragmentations of Unity @ Common Nexus)
**作成日**: 2025-10-17
**目的**: ESP32-S3ピンアサイン確定のための機能要件明確化

---

## 📋 目的

基板発注前に、ファームウェアの機能要件とハードウェア設計の整合性を確保し、ESP32-S3のピンアサインを確定する。

**現状**:
- メカ完成
- PCB回路図作成済み（WPCBB）
- ファームウェアv9実装済み

**必要作業**:
1. 機能要件の整理（このドキュメント）
2. PCB回路図との照合
3. ピンアサイン確定
4. ファームウェア修正

---

## 🎯 システム概要

### U1（ESP32-S3）の役割

```
┌─────────────────────────────────────────┐
│         インターネット/MQTTブローカー      │
└──────────────┬──────────────────────────┘
               │ WiFi
    ┌──────────▼──────────┐
    │   U1 (ESP32-S3)     │ ← メインコントローラー
    │  - WiFi/MQTT        │
    │  - I2C Master       │
    │  - UART Master      │
    └─┬─────────────────┬─┘
      │ I2C             │ UART (Serial1)
      │                 │
   ┌──▼───┐         ┌──▼───────┐
   │  U2  │         │    U3    │
   │ウィンチ│         │ LED制御  │
   │モーター│         │姿勢センサー│
   └──────┘         └──────────┘
```

### 主要機能
1. **WiFi/MQTT通信**: クラウドから制御コマンド受信
2. **I2C通信**: U2（ウィンチモーター）制御
3. **UART通信**: U3（LED制御）へデータ送信
4. **LED制御**: システムステータス表示（オンボードNeoPixel）

---

## 🔌 必須ピン機能要件

### 1. WiFi/MQTT通信

| 機能 | 要件 | GPIO制約 | 備考 |
|------|------|---------|------|
| **WiFi** | ESP32-S3内蔵 | なし | 追加ピン不要 |
| **MQTT** | ソフトウェア実装 | なし | WiFi経由 |

**実装箇所**: `mqttClient.cpp`

**MQTTトピック**:
- サブスクライブ: `<MAC>/idxy`, `cl/<ID>`, `ps/<ID>`, `dl/<ID>`, `at/<ID>`, `spark`, `ota`, `reboot`
- パブリッシュ: `status/<ID>`, `log/<ID>`

---

### 2. I2C通信（U1 → U2）

| 項目 | 仕様 | GPIO制約 |
|------|------|---------|
| **バスアドレス** | 0x08 (U2_SLAVE_ADDRESS) | - |
| **速度** | 100kHz | - |
| **SDA** | GPIO 8 | I2C対応GPIO必須 |
| **SCL** | GPIO 9 | I2C対応GPIO必須 |
| **プルアップ抵抗** | 4.7kΩ推奨 | 外部抵抗必要 |

**実装箇所**: `main.cpp:11-12`, `mqttClient.cpp`

**コマンド一覧**:
| コマンド | バイト構成 | 機能 |
|---------|-----------|------|
| `'Z'` | `'Z' + LSB + MSB` | 垂直位置(mm)、天井基準 |
| `'L'` | `'L' + level` | ダウンライト輝度(0-255) |
| `'I'` | `'I' + deviceId` | デバイスID通知 |
| `'S'` | `'S' + LSB + MSB` | Spark秒数 |
| `'H'` | 読み取り専用 | ホーミング状態(0=進行中, 1=完了) |

**PCB回路図確認**:
- ✅ TO WPCBMS コネクタ: TX=8, RX=9, SW=10
- ⚠️ TX/RXラベルがI2C SDA/SCLと一致するか確認必要

---

### 3. UART通信（U1 → U3）

| 項目 | 仕様 | GPIO制約 |
|------|------|---------|
| **ボーレート** | 38400 baud | - |
| **データビット** | 8 | - |
| **パリティ** | なし | - |
| **ストップビット** | 1 | - |
| **TX** | GPIO 18 | UART対応GPIO必須 |
| **RX** | GPIO 17 | UART対応GPIO必須（未使用） |

**実装箇所**: `main.cpp:352`

**コマンドフォーマット**:
```
統合データパケット:
DATA,F,<f_r>,<f_g>,<f_b>,R,<r_r>,<r_g>,<r_b>,P,<pitch>,<yaw>,B,<brightness>,H,<enc_h>\n

Sparkコマンド:
SPARK,<秒数>\n

クリアコマンド:
C\n
```

**PCB回路図確認**:
- ✅ TO WPCBMS コネクタ: 24V, GND, NC:7, TX:8, RX:9, SW:10
- ⚠️ H2ヘッダー: GND, TX, RX の配線確認必要

---

### 4. オンボードLED（NeoPixel）

| 項目 | 仕様 | GPIO制約 |
|------|------|---------|
| **GPIO** | GPIO 48 | NeoPixel対応GPIO |
| **個数** | 1個 | - |
| **タイプ** | NEO_GRB + NEO_KHZ800 | - |

**実装箇所**: `main.cpp:10`

**用途**: システムステータス表示
- 起動時: 赤色点灯
- WiFi接続中: 青色点滅
- MQTT接続済み: 緑色
- エラー: 赤色点滅

**PCB回路図確認**:
- ✅ ESP32-S3-DevKitC-1ボードのオンボードLED（GPIO48内蔵）

---

### 5. LED Driver制御（外部LED用）

| 項目 | 仕様 | GPIO制約 |
|------|------|---------|
| **制御信号** | LED_C, LED_PWM | PWM対応GPIO推奨 |
| **ドライバIC** | TPS92200DDCR | - |
| **出力** | LED_A (24V駆動) | - |

**実装箇所**: ファームウェア内で未実装（U3経由でLED制御）

**PCB回路図確認**:
- ✅ LED_C, LED_PWMがESP32-S3に接続されている
- ⚠️ GPIO番号の確認必要

---

### 6. ステッパーモーター制御（TMC2209経由）

| 項目 | 仕様 | GPIO制約 |
|------|------|---------|
| **制御信号** | TMC2209_EN, MS1, MS2, STEP, DIR, RX | GPIO必須 |
| **ドライバIC** | TMC2209 | UART通信可能 |
| **接続** | STEPPER_H1, STEPPER_H2コネクタ | - |

**実装箇所**: U2基板経由（I2C制御）、U1からは直接制御なし

**PCB回路図確認**:
- ✅ TMC2209信号線はESP32-S3に接続されている
- ⚠️ GPIO番号の確認必要
- ⚠️ U2との役割分担確認必要（U1から直接制御するか、U2経由か）

---

### 7. 電磁ブレーキ制御

| 項目 | 仕様 | GPIO制約 |
|------|------|---------|
| **制御信号** | SOLENOID | GPIO必須 |
| **回路** | Q1 (20N06), Q2 (NPN), R6 (100kΩ) | - |
| **安全機能** | プルダウン抵抗でフェイルセーフ | - |

**実装箇所**: ファームウェア内で未実装（U2経由で制御可能性）

**PCB回路図確認**:
- ✅ SOLENOID信号線がESP32-S3に接続されている
- ⚠️ GPIO番号の確認必要
- ⚠️ ファームウェアでの実装必要性を確認

---

### 8. 電源系統

| 項目 | 仕様 | 備考 |
|------|------|------|
| **24V入力** | TO WPCBMS, Fuse経由 | 全体電源 |
| **5V DCDC** | TPS54202DDC (U1) | ESP32-S3用 |
| **5V BX-PM2.54-1-22PY** | H2ヘッダー | ESP32-S3ボード供給 |
| **電流容量** | TBD | 負荷計算必要 |

**PCB回路図確認**:
- ✅ 5V電源レギュレーター配置確認
- ⚠️ 電流容量がESP32-S3（最大500mA）+ 周辺回路に十分か確認

---

## 📊 GPIO使用状況サマリー

### 現在確定済みピン（ファームウェアv9ベース）

| GPIO | 機能 | 接続先 | 実装箇所 | Status |
|------|------|--------|---------|--------|
| **48** | NeoPixel RGB LED | オンボード | main.cpp:10 | ✅ 確定 |
| **8** | I2C SDA | U2 (ウィンチ) | main.cpp:11 | ✅ 確定 |
| **9** | I2C SCL | U2 (ウィンチ) | main.cpp:12 | ✅ 確定 |
| **17** | UART RX (Serial1) | U3 (LED) | main.cpp:352 | ✅ 確定（未使用） |
| **18** | UART TX (Serial1) | U3 (LED) | main.cpp:352 | ✅ 確定 |

### PCB回路図から判明した追加接続

| 信号名 | 推定機能 | PCB接続先 | GPIO（未確定） | 優先度 |
|--------|---------|-----------|--------------|--------|
| **LED_C** | LED制御 | TPS92200DDCR | TBD | Medium |
| **LED_PWM** | LED PWM | TPS92200DDCR | TBD | Medium |
| **TMC2209_EN** | ステッパー有効 | TMC2209 | TBD | High（U2経由なら不要） |
| **TMC2209_MS1** | マイクロステップ | TMC2209 | TBD | Low（U2経由なら不要） |
| **TMC2209_MS2** | マイクロステップ | TMC2209 | TBD | Low（U2経由なら不要） |
| **TMC2209_STEP** | ステップパルス | TMC2209 | TBD | High（U2経由なら不要） |
| **TMC2209_DIR** | 回転方向 | TMC2209 | TBD | High（U2経由なら不要） |
| **TMC2209_RX** | UART RX | TMC2209 | TBD | Medium（診断機能） |
| **SOLENOID** | 電磁ブレーキ | Q2 Base | TBD | High |

### 未使用GPIO（拡張用予約）

ESP32-S3で使用可能なGPIO:
- GPIO 0-7, 10-16, 19-21, 35-42, 45-47
- 内部使用（使用禁止）: GPIO 26-37（SPI Flash/PSRAM）

---

## 🔍 PCB回路図の詳細確認項目

### 1. TO WPCBMS コネクタ（左上）

**ピン配置（上から）**:
1. 24V
2. 24V
3. 24V
4. GND
5. GND
6. GND
7. NC (No Connect)?
8. TX（I2C SDA?）
9. RX（I2C SCL?）
10. SW

**確認事項**:
- [ ] TX/RXがI2C SDA/SCLと一致するか
- [ ] SWの用途（プルアップスイッチ?）
- [ ] NCピンの実際の接続

### 2. H2ヘッダー（5V DCDC出力）

**ピン配置**:
- 22 (GND)
- 21 (TX)
- 20 (RX)
- ... (以下続く)

**確認事項**:
- [ ] GND, TX, RXの接続先確認
- [ ] 5V供給ピンの位置確認

### 3. TMC2209周辺

**接続信号**:
- TMC2209_EN, MS1, MS2, STEP, DIR, RX
- STEPPER_H1 (M_BLK, M_GRN, M_RED, M_BLU)
- STEPPER_H2 (配線確認必要)

**確認事項**:
- [ ] TMC2209制御がU1から直接か、U2経由か
- [ ] ステッパー電源ラインのトレース幅（40mil確認済み）

### 4. LED Driver周辺

**接続信号**:
- LED_C, LED_PWM → TPS92200DDCR → LED_A

**確認事項**:
- [ ] LED_C, LED_PWMのGPIO番号
- [ ] LED制御の実装必要性（U3経由なら不要）

### 5. 電磁ブレーキ回路

**回路構成**:
- SOLENOID → R8 (10kΩ) → Q2 Base
- R6 (100kΩ) プルダウン → Q2 Base → GND
- Q2 Collector → Q1 Gate
- Q1 (20N06) → S_DRN

**確認事項**:
- [ ] SOLENOID信号のGPIO番号
- [ ] ファームウェアでの制御実装必要性

---

## ✅ 次のアクション

### Phase 1: PCB回路図の精密確認

1. **EasyEDA/KiCadファイルを開く**
   - 各信号線のGPIO番号を追跡
   - ネットリストを確認

2. **接続先の特定**
   - TO WPCBMS コネクタの各ピン → ESP32-S3 GPIO
   - H2ヘッダーの各ピン → ESP32-S3 GPIO
   - TMC2209信号 → ESP32-S3 GPIO または U2経由
   - SOLENOID → ESP32-S3 GPIO

3. **ドキュメント更新**
   - [winch-u1-esp32s3-pinout.md](winch-u1-esp32s3-pinout.md) に確定ピンを記載

### Phase 2: ファームウェア機能実装確認

1. **TMC2209制御**
   - U1から直接制御 → GPIO割り当て必要
   - U2経由で制御 → I2C経由で制御、追加GPIO不要

2. **電磁ブレーキ制御**
   - フェイルセーフ確認（プルダウン抵抗により、起動時ブレーキON）
   - ファームウェアでの制御実装

3. **LED Driver制御**
   - 外部LED必要性の確認
   - U3経由で十分ならGPIO不要

### Phase 3: ピンアサイン確定

1. **winch-u1-esp32s3-pinout.md 更新**
   - 全GPIO機能の最終決定
   - 未使用GPIOの予約用途決定

2. **ファームウェア修正**
   - main.cpp, mqttClient.cpp のGPIO定義更新
   - 新機能の実装（必要に応じて）

3. **ビルド&テスト**
   - PlatformIOでビルド
   - I2C/UART通信テスト

### Phase 4: 基板発注

- **締切**: 2025-10-24
- **発注先**: JLCPCB
- **仕様確認**: トレース幅、銅箔厚、部品実装

---

## 🔗 関連ドキュメント

| ドキュメント | 用途 |
|------------|------|
| [winch-u1-esp32s3-pinout.md](winch-u1-esp32s3-pinout.md) | ピンアサイン仕様（更新対象） |
| [U1_FIRMWARE_OVERVIEW.md](../../firmware/Winch/U1/U1_FIRMWARE_OVERVIEW.md) | ファームウェア完全概要 |
| [winch-circuit-design-v2.md](winch-circuit-design-v2.md) | Winch V2回路設計仕様 |
| [winch-electromagnetic-brake-circuit.md](winch-electromagnetic-brake-circuit.md) | 電磁ブレーキ回路仕様 |
| [winch-stepper-trace-width-analysis.md](../../notes/winch-stepper-trace-width-analysis.md) | ステッパートレース幅解析 |

---

## 📝 メモ

### PCB回路図ラベルの疑問点

1. **TX/RX vs SDA/SCL**:
   - TO WPCBMSコネクタのTX/RXラベル
   - ファームウェアではI2C SDA/SCL（GPIO8/9）
   - ラベルミス or UART/I2C兼用設計?

2. **TMC2209制御主体**:
   - U1から直接制御（GPIO必要）
   - U2が独立制御（I2C経由でコマンド送信）
   - 現状ファームウェアはU2経由前提

3. **LED Driver必要性**:
   - 外部LEDを直接制御する設計?
   - U3経由で十分なら未使用でOK

これらの疑問点を解消するため、PCB回路図の精密確認が必要です。

---

**優先度**: 🔴 最優先（2025-10-24発注締切）
**次のステップ**: PCB回路図をEasyEDAで開いて各信号線を追跡
