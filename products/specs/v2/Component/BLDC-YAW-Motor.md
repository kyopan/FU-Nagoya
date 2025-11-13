# BLDC YAW Motor 仕様書

**作成日**: 2025-11-09
**ステータス**: ✅ V1と同等

---

## 基本仕様

| 項目 | 仕様 |
|------|------|
| **型番** | V1と同じ（型番未記載） |
| **タイプ** | ブラシレスDCモーター (BLDC) |
| **KV値** | 330 RPM/V |
| **定格電圧** | 5V |
| **定格電流** | 1A |
| **抵抗** | 6.5Ω |
| **極数** | 7極 (ポールペア数) |

---

## 配置

| 項目 | 仕様 |
|------|------|
| **実装位置** | HD (Horozon Disc) 下部 |
| **回転軸** | YAW軸（水平回転） |
| **用途** | HD全体の水平回転 / ケーブルねじれ補正 |
| **エンコーダー** | AS5600 (AS-Y) |

---

## 制御方式

### SimpleFOC制御

| 項目 | 仕様 |
|------|------|
| **制御IC** | DRV8311 |
| **制御方式** | Field Oriented Control (FOC) |
| **制御Core** | ESP32-S3 #2 Core 0 |
| **フィードバック** | AS5600 エンコーダー (I2C) |
| **ポールペア数** | 7 |

### DRV8311 仕様

| 項目 | 仕様 |
|------|------|
| **型番** | DRV8311 |
| **メーカー** | Texas Instruments |
| **最大電流** | 2.5A (連続) |
| **動作電圧** | 4.5-40V |
| **制御方式** | 3相ブリッジドライバ |
| **インターフェース** | SPI (設定用) |

---

## 電気特性

### 電圧-回転数特性

| 電圧 | 回転数 (理論値) |
|------|----------------|
| 3.7V | 1,221 RPM |
| 5V   | 1,650 RPM |
| 7.4V | 2,442 RPM |

**計算式:**
```
RPM = KV値 × 電圧
    = 330 RPM/V × 5V
    = 1,650 RPM
```

### 電流-トルク特性

| 項目 | 値 |
|------|-----|
| **定格電流** | 1A |
| **最大電流** | 2.5A (DRV8311制限) |
| **抵抗** | 6.5Ω |
| **電圧降下** | 1A × 6.5Ω = 6.5V |

---

## SimpleFOC制御パラメータ

### 基本設定

```cpp
// モーター設定
BLDCMotor motor = BLDCMotor(7); // 7極

// ドライバ設定
BLDCDriver3PWM driver = BLDCDriver3PWM(PWM_A, PWM_B, PWM_C, EN);

// エンコーダー設定
AS5600Encoder encoder = AS5600Encoder(0x36); // AS-Y

// FOC設定
motor.linkSensor(&encoder);
motor.linkDriver(&driver);

// 制御パラメータ
motor.voltage_power_supply = 5.0;
motor.voltage_limit = 5.0;
motor.velocity_limit = 100; // rad/s
motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
motor.torque_controller = TorqueControlType::voltage;
motor.controller = MotionControlType::velocity;

// PIDパラメータ（要調整）
motor.PID_velocity.P = 0.2;
motor.PID_velocity.I = 0.0;
motor.PID_velocity.D = 0.0;
```

### 制御ループ

```cpp
void loop() {
  // エンコーダー読取
  encoder.update();

  // FOC制御
  motor.loopFOC();
  motor.move(target_velocity);

  // ケーブルねじれ補正
  if (cable_twist_detected()) {
    motor.move(-target_velocity); // 逆回転
  }
}
```

---

## ケーブルねじれ補正ロジック

```cpp
// AS-S (スリップリング角度) 読取
uint16_t cable_angle = AS5600_Read(AS_S_ADDRESS);

// ねじれ判定（±90°以上）
if (abs(cable_angle - 180) > 90) {
  // YAW逆回転でケーブル巻き戻し
  float correction_speed = calculate_correction(cable_angle);
  motor.move(-correction_speed);
} else {
  // 通常YAW制御
  motor.move(target_velocity);
}
```

---

## 電源仕様

| 項目 | 仕様 |
|------|------|
| **供給電圧** | 5V (DCDC出力) |
| **動作電流** | 1A (定格) |
| **最大電流** | 2.5A (DRV8311制限) |
| **電源系統** | DCDC 5V出力 |

---

## 機構設計

| 項目 | 仕様 |
|------|------|
| **取付位置** | HD下部中央 |
| **取付方法** | ネジ固定 or クランプ固定 |
| **回転軸方向** | 垂直（YAW軸） |

**決定事項:**
- [ ] モーター取付構造設計
- [ ] エンコーダー磁石取付位置
- [ ] 配線経路設計

---

## 参考資料

- [DRV8311 Datasheet](https://www.ti.com/lit/ds/symlink/drv8311.pdf)
- [SimpleFOC Library](https://simplefoc.com/)
- [V1 ファームウェア参照](../../firmware/v1/)

---

## 決定事項

- [x] モーター型番: V1と同じ
- [x] KV値: 330 RPM/V
- [x] 定格電圧・電流: 5V / 1A
- [x] ドライバIC: DRV8311
- [x] ポールペア数: 7極
- [ ] PIDパラメータ調整
- [ ] 機構設計確定

---

## 変更履歴

| 日付 | 変更内容 | 担当 |
|------|---------|------|
| 2025-11-09 | 初版作成（V1仕様継承） | CIC |
