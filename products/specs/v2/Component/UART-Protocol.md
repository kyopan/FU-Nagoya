# UART Protocol 仕様書

**作成日**: 2025-11-09
**ステータス**: ✅ V1プロトコル継承

---

## 基本仕様

| 項目 | 仕様 |
|------|------|
| **ボーレート** | 38400 baud |
| **データビット** | 8bit |
| **パリティ** | あり (Odd/Even 未指定) |
| **ストップビット** | 1bit |
| **フロー制御** | なし |
| **エラー検出** | CRC/パリティ |
| **バッファサイズ** | 未定 |

---

## 通信経路

```
Winch (ESP32-S3 U1)
  ↓
[4極ステレオミニプラグケーブル 3m]
  ↓
Slip Ring (連続回転機構)
  ↓
HD (ESP32-S3 #1)
```

**物理接続:**
- TX (Winch → HD): Ring2
- RX (HD → Winch): Ring3 (Sleeve)

---

## プロトコル仕様

### Winch → HD (ダウンリンク)

**フォーマット (V1準拠):**
```
"DATA,F,r,g,b,R,r,g,b,P,pitch,yaw,B,brightness,H,enc_horizontal\n"
```

**パラメータ:**

| フィールド | 説明 | 値範囲 | 例 |
|-----------|------|-------|-----|
| `DATA` | ヘッダー | 固定 | `DATA` |
| `F` | 前面色指定 | 固定 | `F` |
| `r,g,b` | 前面RGB | 0-255 | `255,0,0` (赤) |
| `R` | 背面色指定 | 固定 | `R` |
| `r,g,b` | 背面RGB | 0-255 | `0,255,0` (緑) |
| `P` | Pitch/YAW指定 | 固定 | `P` |
| `pitch` | Pitch角度 | 0-180° | `90` |
| `yaw` | YAW角度 | 0-360° | `180` |
| `B` | 輝度指定 | 固定 | `B` |
| `brightness` | 輝度 | 0-255 | `128` |
| `H` | 水平エンコーダー | 固定 | `H` |
| `enc_horizontal` | 水平角度 | 0-360° | `45` |

**例:**
```
DATA,F,255,0,0,R,0,255,0,P,90,180,B,128,H,45\n
```
→ 前面赤、背面緑、Pitch 90°、YAW 180°、輝度50%、水平角45°

### HD → Winch (アップリンク)

**ステータス:** 形式未定

**想定フォーマット (検討中):**
```
"STATUS,G,temp1,...,temp64,C,ax,ay,az,gx,gy,gz,mx,my,mz,S,cable_angle\n"
```

**パラメータ候補:**

| フィールド | 説明 | 値 |
|-----------|------|-----|
| `STATUS` | ヘッダー | 固定 |
| `G` | GridEye | 固定 |
| `temp1,...,temp64` | 温度データ | 64個の温度値 |
| `C` | Compass (GY-85) | 固定 |
| `ax,ay,az` | 加速度 | 3軸加速度 |
| `gx,gy,gz` | ジャイロ | 3軸角速度 |
| `mx,my,mz` | 磁気 | 3軸磁気 |
| `S` | Slip Ring | 固定 |
| `cable_angle` | ケーブル角度 | 0-360° |

**決定事項:**
- [ ] アップリンクフォーマット確定
- [ ] 送信周期決定（1Hz? 10Hz?）
- [ ] データ圧縮の必要性検討

---

## エラー検出

### CRC/パリティ

**仕様:**
- CRC: 方式未定（CRC-8? CRC-16?）
- パリティ: Odd or Even 未指定

**検討事項:**
- [ ] CRC方式選定（CRC-8 推奨）
- [ ] パリティビット設定（Odd推奨）
- [ ] エラー時の再送ロジック

### エラー処理例

```cpp
// パリティチェック
if (check_parity(data) == false) {
  Serial.println("Parity error");
  return;
}

// CRC チェック
uint8_t crc_calc = calculate_crc8(data);
if (crc_calc != received_crc) {
  Serial.println("CRC error");
  return;
}
```

---

## バッファサイズ

**ダウンリンク (Winch → HD):**
- 最大メッセージ長: 約 80 bytes
- 推奨バッファ: 128 bytes

**アップリンク (HD → Winch):**
- GridEye 64ピクセル: 64 × 4 bytes (float) = 256 bytes
- GY-85 9軸: 9 × 4 bytes = 36 bytes
- 合計: ~300 bytes
- 推奨バッファ: 512 bytes

**決定事項:**
- [ ] ESP32-S3 UARTバッファサイズ設定
- [ ] データ圧縮検討（JSON? バイナリ?）

---

## ESP32-S3 UART設定

### 初期化コード

```cpp
// UART設定
#define UART_NUM UART_NUM_1
#define TX_PIN GPIO_NUM_XX // 未割当
#define RX_PIN GPIO_NUM_XX // 未割当
#define BUF_SIZE 512

void uart_init() {
  uart_config_t uart_config = {
    .baud_rate = 38400,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_ODD, // 仮
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };

  uart_param_config(UART_NUM, &uart_config);
  uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
}
```

### 受信処理

```cpp
void uart_receive() {
  uint8_t data[128];
  int len = uart_read_bytes(UART_NUM, data, sizeof(data), 100 / portTICK_PERIOD_MS);

  if (len > 0) {
    data[len] = '\0';
    parse_command((char*)data);
  }
}
```

---

## タイミング仕様

### ダウンリンク (Winch → HD)

| 項目 | 仕様 |
|------|------|
| **送信周期** | 10Hz (100ms間隔) 推定 |
| **メッセージ長** | 約 80 bytes |
| **転送時間** | 80 bytes × 10 bits / 38400 baud = 20.8ms |

### アップリンク (HD → Winch)

| 項目 | 仕様 |
|------|------|
| **送信周期** | 未定（1Hz? 10Hz?） |
| **メッセージ長** | ~300 bytes |
| **転送時間** | 300 bytes × 10 bits / 38400 baud = 78ms |

**決定事項:**
- [ ] 送信周期の最適化
- [ ] データ圧縮によるメッセージ長削減検討

---

## 参考資料

- [V1 UART Protocol参照](../../firmware/v1/)
- [ESP32-S3 UART API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/uart.html)

---

## 決定事項

- [x] ボーレート: 38400 baud
- [x] フロー制御: なし
- [x] エラー検出: CRC/パリティ
- [x] ダウンリンク形式: V1準拠
- [ ] パリティ設定: Odd or Even
- [ ] CRC方式選定
- [ ] アップリンク形式確定
- [ ] バッファサイズ確定

---

## 変更履歴

| 日付 | 変更内容 | 担当 |
|------|---------|------|
| 2025-11-09 | 初版作成（V1プロトコル継承） | CIC |
