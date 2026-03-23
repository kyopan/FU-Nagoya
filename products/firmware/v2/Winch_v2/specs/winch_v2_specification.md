# Winch v2 仕様書

## 1. システム概要
**Winch v2** は、「Fragmentations of Unity」プロジェクトにおける昇降ユニット（Winch）の制御ファームウェアです。
本システムはデバイス全体の**マスターコントローラー**として機能し、自身のステッピングモーター制御による昇降機能に加え、スリップリング経由で接続された下部ユニット（Tube Unit）への通信ブリッジおよび電源供給管理を行います。

### 主な役割
1.  **昇降制御 (Lift)**: ステッピングモーターと電磁ブレーキを用いた高精度な昇降（キャリブレーション機能付き）。
2.  **通信ハブ (Bridge)**: WiFi/MQTT経由でクラウド（サーバー）からの指令を受信し、Winchの動作とTubeへの指令（UART転送）を振り分けます。
3.  **状態表示 (UI)**: 基板上のステータスLEDおよび投光用LED（Spotlight）による動作状態の可視化。

---

## 2. ハードウェア仕様
### MCU
*   **デバイス**: ESP32-S3
*   **フラッシュ**: 16MB (想定) / PlatformIO環境依存

### ピン配置 (Pinout)
| 機能 | GPIO | 備考 |
| :--- | :--- | :--- |
| **Tube UART TX** | 9 | 下部ユニットへの送信 |
| **Tube UART RX** | 8 | 下部ユニットからの受信 |
| **TMC2209 EN** | 10 | モーターEnable (Active LOW) |
| **TMC2209 STEP** | 13 | |
| **TMC2209 DIR** | 14 | |
| **TMC2209 UART TX** | 17 | ドライバ設定用 |
| **TMC2209 UART RX** | 18 | ドライバ設定用 |
| **Solenoid Brake** | 3 | 電磁ブレーキ (Active LOW: LOW=Lock, HIGH=Unlock) |
| **End Sensor** | 46 | 原点検知スイッチ (INPUT_PULLUP: LOW=Triggered) |
| **Sensor LED** | 7 | センサー検知確認用LED |
| **Status LED** | 16 | 4x WS2812B (Power, WiFi, Motor, Tube) |
| **Spotlight PWM** | 15 | 3W LED調光用 (Active HIGH) |
| **Onboard LED** | 48 | ESP32-S3内蔵 WS2812 (デバッグ用) |

---

## 3. 機構・制御パラメータ
### モーター設定
*   **ドライバ**: TMC2209 (UART制御)
*   **マイクロステップ**: 32 microsteps
*   **RMS電流**: 1000mA
*   **StealthChop**: 有効 (静音駆動)

### 機構パラメータ
*   **ドラム有効半径**: 50 mm (キャリブレーション補正値)
*   **最大ストローク高さ**: 2800 mm (論理上の MAX_HEIGHT)
*   **最大速度**: 5000 steps/sec
*   **最大加速度**: 5000 steps/sec²
*   **ステップ換算**: 6400 steps/rev (200 * 32)
    *   計算式: `height_mm` ↔ `steps` の相互変換ロジックを実装

### キャリブレーション (Homing)
*   **方式**: EndSwitch (上限リミットスイッチ) 基準
*   **シーケンス**:
    1.  低速 (1000 steps/sec) で巻き上げ方向へ移動
    2.  センサー検知 (GPIO 46 = LOW) で即時停止
    3.  指定オフセット (`-400 steps`) 分だけ下降方向にバックオフ
    4.  現在位置を `0` (Home / 2800mm地点) としてリセット
*   **タイムアウト**: 120秒 (これを超えると強制停止)

---

## 4. ソフトウェアロジック
### 起動シーケンス
1.  **ハードウェア初期化**: ブレーキは**LOCKED** (安全確保)、センサー・LED初期化。
2.  **通信接続**: WiFi接続確立 (切断時はスタンドアロンモードまたは停止)。
3.  **キャリブレーション**: 自動的にホーミング動作を実行。
4.  **MQTT接続**: サーバーへ接続し、Device IDを取得して運用開始。
5.  **Spotlight演出**: 起動完了通知として投光LEDがフェードイン・アウト。

### 通信プロトコル
#### MQTT (Upstream)
*   **Broker**: `192.168.1.2` (デフォルト)
*   **Topics**:
    *   `ini/[MAC]`: 初期化要求 (Pub)
    *   `[MAC]/idxy`: Device ID (`1`, `2`...) 割り当て (Sub)
    *   `ps/[ID]`: 位置・姿勢指令 (Sub) -> Binary Payload `[Z_mm(2)][Pitch(2)][Yaw(2)]`
    *   `cl/[ID]`: 色指令 (Sub) -> Binary Payload `[R][G][B]`
    *   `dl/[ID]`: 投光LED輝度 (Sub) -> Binary Payload `[PWM]`
    *   `ota`: OTAアップデートトリガー (Sub)
    *   `rb/[ID]`: リブート/再キャリブレーション要求 (Sub)
    *   `fu/device/[MAC]/heartbeat`: 状態報告 (Pub / 30秒毎)

#### UART (Downstream to Tube)
*   **Baudrate**: 38400 bps
*   **Format**: ASCII Text, Newline terminated
*   **Commands**:
    *   `POS,Pitch,Yaw`: 姿勢制御 (Pitch, Yawはfloat)
    *   `RGB,R,G,B`: カラー制御 (0-255)
*   **受信**: Tubeからの生存信号 (Keep-Alive) を常時監視し、5秒途絶で切断と判定。

---

## 5. ステータスLED仕様
基板上の4連LED (GPIO 16) により、各サブシステムの健全性を表示します。
**定義済みカラー:**
*   🔴 **Red**: No Connection / Error
*   🟠 **Orange (Yellow)**: Connecting / Calibrating
*   🔵 **Blue**: Connected / Calibrated
*   ⚫ **Black (OFF)**: Ready (全てのLEDがBlueになった瞬間に全消灯)

| LED No. | 対象 | Red (Error) | Orange (Connecting) | Blue (Connected) |
| :--- | :--- | :--- | :--- | :--- |
| **0** | **WiFi** | No connection | Connecting | Connected |
| **1** | **MQTT** | No connection | Connecting | Connected |
| **2** | **Motor** | TMC init fail | Calibrating | Calibrated |
| **3** | **Tube** | No connection | Connecting | Connected |

**特記事項:**
*   4つ全てのLEDが **Blue** (Connected/Calibrated) になると、**全てのLEDを消灯 (Black)** して「System Ready」状態を示します。
*   いずれか1つでも異常が発生または再接続中になった場合、該当するLEDが点灯します。

---

## 6. 安全装置・フェールセーフ
*   **ブレーキ制御**:
    *   モーター非励磁時は常に **LOCKED** (落下防止)。
    *   モーター動作時のみ **UNLOCK**。
    *   順序: Enable Motor → Delay → Unlock Brake / Lock Brake → Delay → Disable Motor。
*   **センサー保護**:
    *   通常動作中もEnd Sensorがトリガーされた場合は即時停止 (Emergency Stop)。
*   **Watchdog**:
    *   Core 1で動作するモータースレッドは高頻度で動作するが、定期的にWDTへYieldを行う。
