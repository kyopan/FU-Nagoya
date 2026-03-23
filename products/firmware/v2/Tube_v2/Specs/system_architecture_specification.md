# Robotic Light Tube System: 仕様書 (System Architecture Specification)

**Version**: 2.2 (Unit1 v2.2.37 / Unit2 v2.2.33)
**Last Updated**: 2026-02-06
**Focus**: Dual-Core Stability, Precise Status Feedback, Robust Calibration

---

## 1. 全体構造 (Suspended Reaction Wheel System)

本システムは、天井のWinchから給電/通信ケーブルにより吊り下げられた、2軸（Yaw/Pitch）の可動式ライトチューブである。

| 部位 | 役割・構造 |
|---|---|
| **固定点 (Slip Ring Rotor)** | Winchケーブルと接続される最上部。**回転しない（Winch/天井に対し固定）**。 |
| **メインボディ (Stater群)** | `Slip Ring Stater`, `Pitch BLDC Stater`, `Yaw BLDC Stater` が互いに固定され、システムの核となるブロック。 |
| **Yaw軸 (Reaction Wheel)** | 最下部にある赤色の `Yaw BLDC Rotor`。メインボディに対し回転することで、**反作用 (Reaction Torque)** によりメインボディ自体の向き（方位）を変える。 |
| **Pitch軸 (Direct Drive)** | メインボディから左右に伸びる軸。`Pitch BLDC Rotor` (青) が `Unit 1` (筒) を保持し、上下にチルトさせる。 |

## 2. ユニット構成と配線

(Skipped for Brevity...)

#### 制御・通信ロジック (Unit 1 Core Logic)
*   **起動シーケンス (Silence & Stabilize)**:
    *   **0-7秒**: 全機能待機。LED消灯。
    *   **7-27秒**: **自動キャリブレーション (Auto-Cal)**。`CAL,YAW` コマンドでYawモータを全力回転させ、ケーブル癖取りと磁気センサ学習を行う。
    *   **30秒**: **Auto-Tare (基準点リセット)**。
        *   **安定性チェック**: ジャイロ値が `0.2 rad/s` 未満で静止している場合のみ実行。
        *   完了時、リングLEDが **桜色 (Sakura Pink)** にフェードインし、操作可能となる。
#### 自動キャリブレーション動作
`CAL,YAW` コマンド受信時に実行。
*   **パターン**: 右旋回(4s) -> 左旋回(4s) -> 右旋回(4s) -> 左旋回(4s)
*   **速度**: 14.0 rad/s (約800 deg/s)
*   **目的**: 最大速度での長時間加速により、筒を実際に回転させ、BNO055の地磁気センサを学習させる。
*   **目的**: ケーブルのねじれ解消と、全体動作確認。
*   **安全機構**: Tare完了まで、Winchからのモーター操作コマンドは全てブロックされる。
*   **コマンドバリデーション**:
    *   **Yaw (`Y<val>`)**:
        *   `0.0` ~ `360.0`: Unit 2へ転送 (通常制御)。
        *   `400.0`: **緊急停止**。Unit 2へ `Y0.0` を送信し、回転を止める。
        *   上記以外: 無視。
    *   **Pitch (`POS,<val>`)**:
        *   `-60.0` ~ `60.0`: Unit 2へ転送 (通常制御)。追従モードOFF、LEDは桜色に戻る。
        *   `90.0`: **Tracking Mode ON**。GridEyeによる熱源追従を開始。LEDは **肌色 (Skin Color) + ブリージング** に変化。
        *   上記以外: 無視。
    *   **LED Color (`RGB,<r>,<g>,<b>`)**:
        *   指定されたRGB値にフェードする。Unit 1のVisionタスクが処理。
    *   **注記**: Winchからの直接的な `Y` コマンドは、Unit 1の安全フィルタにより現在ブロックされている（内部のTracking ModeまたはAuto-Calからの発行のみ有効）。
*   **リカバリ**:
    *   `TARE` コマンド: Winchから送信することで、いつでも強制的にTareを実行可能（ズレ修正用）。

### Unit 1 & 2 (Electronics)
*   **配置**: どちらもメインボディ（Pitch Stater基板など）に固定されている。
*   **役割分担 (v2.2 Updated)**:
    *   **Unit 1**: LEDマスター, センサー統合 (GridEye/BNO), 通信ブリッジ (Winch <-> Unit 2)。
    *   **Unit 2**: モーターマスター (Pitch/Yaw 制御), キャリブレーション実行。

### センサー/LED接続 (The Blue Line)
メインボディ（固定側）から回転する筒（先端）への接続は、**長さ115mmの「Blue Line」**（6本線）によって直結されている。スリップリングは通らない。

*   **配線内容**: 5V, GND, Din, Dout, **SDA**, **SCL**
*   **接続先**:
    *   **Left Side Tip**: GridEye (I2C), LED Tape
    *   **Right Side Tip**: BNO055 (I2C), LED Tape
*   **重要**: I2CバスはメインボディからBlue Line経由で先端まで伸びている。

## 3. Pitch関節の詳細仕様

### 機械構造
*   **駆動**: Pitch BLDC Motorによるダイレクトドライブ (ギア比 1:1)。
*   **回転方向 (Right Side View - 図3)**:
    *   **CW (時計回り)**: BNO055が**下**を向く (Tilt Down)。
    *   **CCW (反時計回り)**: BNO055が**上**を向く (Tilt Up)。
*   **座標系**:
    *   **+Z**: 上方向 (吊り下げ方向)。
    *   **+Y**: 水平方向。

### VRセンサー (RK09Y11L0001) 特性 (Legacy/Reference)
※現在はBNO055とAS5600による制御が主流だが、物理リミットの理解として参照。
*   **有効範囲**: 120° ~ 180° (ADC: 0~4096)。水平は150°付近。
*   **物理稼働範囲**: 水平±60°。

## 4. Yaw関節の詳細仕様 (Reaction Wheel Control)

*   **駆動**: Yaw BLDC Motorによるリアクションホイール制御。
*   **原理**: 赤いローター（フライホイール）を加速/減速させることでボディにトルクを与える。
*   **課題と対策**:
    *   **最短経路計算**: 0°/360°境界での暴走を防ぐ正規化処理。
    *   **定常外乱 (ねじれ)**: 積分項 (I) によるトルク維持。
    *   **飽和管理**: ホイール回転数が限界に達した場合のUnwind（減速）シーケンス。

## 5. ファームウェア仕様詳細 (v2.2.x - Dual Core Implementation)

v2.2系では、ESP32-S3のデュアルコア性能を活用し、「高負荷な視覚/運動制御」と「通信/ロジック」を分離することで、システムの安定性と応答性を飛躍的に向上させた。

### 5.1 Unit 1 (Sensor Hub & LED Master)
**Version**: `v2.2.37`

#### デュアルコア構成
| Core | タスク名 | 優先度 | 役割 |
| :--- | :--- | :--- | :--- |
| **Core 0** | `visualTask` | 1 | **視覚効果 & センサー & Auto-Tare**<br>NeoPixel と I2Cセンサー (BNO055, GridEye) のポーリングを担当。<br>起動後3秒でAuto-Tareを実行し、基準姿勢を確立する。 |
| **Core 1** | `loop()` | 1 | **通信ブリッジ**<br>UART2 (Winch) と UART1 (Unit 2) のパケット中継を最優先処理。 |

#### ステータスLED (4-Pixel Strip @ Top)
| LED番号 | 名前 | 役割 | 色 / 動作 |
| :--- | :--- | :--- | :--- |
| **99** | **PWR** | 電源 | **青点灯** (常時ON) |
| **98** | **CONN** | 通信 | **青点灯**: 接続確立 (Winchパケット受信ラッチ)<br>**赤点灯**: 未接続 |
| **97** | **SENS** | GridEye | **青点灯**: 初期化成功<br>**赤点灯**: 初期化失敗 |
| **96** | **MOT** | BNO Calib | **青点灯**: Mag Calib完了 (3)<br>**青点滅**: キャリブレーション中 (<3)<br>**赤点灯**: 初期化失敗 |

#### 制御ロジック
*   **Auto-Tare (Heading Reset)**: 起動後 **30秒** 時点で実行。重力ベクトル(Pitch/Roll)は維持しつつ、**Yaw(Heading)のみを現在の方位に合わせてリセット(0度)** する。これにより、起動時の傾き影響を排除しつつ北基準を確保する。
*   **自動キャリブレーショントリガー**: 起動後 **7秒** 経過で `CAL,YAW` コマンドを Unit 2 へ送信。
*   **データストリーミング**: Unit 1 が補正済み(Tare済み)のBNOセンサ値を集約し、WinchおよびUnit 2へ配信。

### 5.2 Unit 2 (Motor Master)
**Version**: `v2.2.33`

#### デュアルコア構成
| Core | タスク名 | 優先度 | 役割 |
| :--- | :--- | :--- | :--- |
| **Core 0** | `yawMotorTask` | 1 | **Yaw制御 (高速FOC)**<br>リアクションホイールのFOCループ専用。`setup()` 完了後に起動される。 |
| **Core 1** | `loop()` | 1 | **Pitch制御 & メインロジック**<br>Pitch/Yawのハードウェア初期化、PitchのFOCループ、シリアルコマンド解析。 |

#### 初期化シーケンス (Sequential Init)
安定性を重視し、セットアップはメインコアで順次実行される。
1.  **Hardware Init (Core 1)**: Pitchモーター初期化 -> Yawモーター初期化。
2.  **Offset Load**: EEPROMからPitchオフセット(`p_off`)を読み込み。
3.  **Task Launch**: ハードウェア準備完了後、`yawMotorTask` (Core 0) を起動。
4.  **Ready**: 
    *   **Pitch**: キャリブレーション済みなら `Angle Mode` (水平維持)、未完了なら `Velocity Mode` (0固定)。
    *   **Yaw**: キャリブレーション完了フラグ (`g_is_calibrated`) が立つまで制御待機 (Target 0)。

#### 制御パラメータ
*   **Pitch (位置制御)**: `P=0.2`, `I=2.0` (高Iゲインで重力保持), `Angle Limit=±60°`
*   **Yaw (速度制御 - Reaction Wheel)**:
    *   **PID**: `P=1.0`, `I=0.02`, `D=0.05`。出力極性は **正 (Positive)**。
    *   **Anti-Stiction Dither (微振動)**: **[DISABLED in v2.2.36]**<br>BNO055への振動ノイズ干渉（Rollドリフト等の原因）を防ぐため、ディザ機能はコード上で無効化された。
    *   **飽和対策 (Anti-Runaway)**: 
        *   積分項リミット: `±1000`
        *   飽和検知閾値: `200 rad/s` (約1900rpm)
        *   Unwind減速レート: `250 rad/s^2` (強力なブレーキで暴走停止)

#### LED Indicator & Visualization
*   **Heading Indicator (Bar Graph)**: BNO055の方位(0-360°)を0-24pxのバーグラフとして表示。
    *   **GR_FRONT (0-23)**: 青色 (Blue)
    *   **CR_BACK (72-95)**: 赤色 (Red)
*   **Winch Color Indicator**: Winchから指定された色(通常はサクラ色)を表示。
    *   **GR_BACK (24-47)**
    *   **CR_FRONT (48-71)**
*   **Status Indicators**: 4つの独立したPixelがシステム状態を表示。

#### サポートコマンド (Unit 2)
以下のシリアルコマンドを受け付ける。
*   `POS,<angle>`: Pitch角度制御 (-60 ~ +60度, 90=Tracking)。
*   `Y<angle>`: Yaw絶対角制御 (0.0 ~ 360.0)。
*   `CAL,YAW`: Yaw自動キャリブレーション開始。
*   `SET_HORIZON`: 現在のPitch角度を水平(0度)としてオフセット記録。
*   `CLEAR_CAL`: Pitchキャリブレーション情報を消去。
*   `PV,<velocity>`: Pitch速度直接制御 (Debug用)。
*   `I,<h>,<p>,<r>`: Unit 1からのIMU補正戻り値 (Heading/Pitch/Roll)。

## 6. デプロイメント & OTA

*   **S3 Bucket**: `comone-fragmented-unity-fw` (Region: `ap-northeast-1`)
*   **ファームウェアパス**:
    *   Unit 1: `/tube_v2_unit1/tube_unit1_v2.xx.xx.bin`
    *   Unit 2: `/tube_v2_unit2/tube_unit2_v2.xx.xx.bin`
*   **更新トリガー**:
    *   MQTT/Serial経由の `OTA,START` コマンドで実行。
    *   Unit 1 がトリガーを受け取ると、Unit 2 へも `OTA,START` を転送し、ペアで更新を行う。
