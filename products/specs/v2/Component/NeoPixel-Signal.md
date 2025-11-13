# NeoPixel Signal 仕様書

**作成日**: 2025-11-09
**ステータス**: ✅ 確定

---

## 基本仕様

| 項目 | 仕様 |
|------|------|
| **信号タイプ** | シリアル (1-wire) |
| **信号電圧** | 3.3V (ESP32出力) |
| **信号線最大長** | 30cm (HD → GR → CR) |
| **リフレッシュレート** | 400kHz |
| **データ線バッファIC** | 不使用 |
| **LED総数** | 96個 (GR 48個 + CR 48個) |

---

## 信号経路

```
HD (ESP32-S3 #1)
  ↓ (FPC)
PR取付機構
  ↓ (FPC/ハーネス)
GR Board → NeoPixel × 48 (前24 + 後24)
  ↓ (基板間配線)
CR Board → NeoPixel × 48 (前24 + 後24)
```

**総距離:**
- HD → PR: ~5cm (FPC)
- PR → GR: ~10cm (FPC/ハーネス)
- GR → CR: ~15cm (基板間配線)
- **合計: ~30cm**

---

## WS2812B タイミング仕様

### ビットタイミング

| 項目 | T0 (0-bit) | T1 (1-bit) |
|------|-----------|-----------|
| **HIGH時間** | 0.4μs ±0.15μs | 0.8μs ±0.15μs |
| **LOW時間** | 0.85μs ±0.15μs | 0.45μs ±0.15μs |
| **合計** | 1.25μs | 1.25μs |

### リセット信号

| 項目 | 仕様 |
|------|------|
| **リセット時間** | >50μs (LOW) |
| **用途** | 全LED更新完了後にラッチ |

---

## リフレッシュレート

**クロック周波数:** 400kHz

| 項目 | 計算 |
|------|------|
| **1ビット時間** | 1 / 400kHz = 2.5μs |
| **1ピクセル** | 24 bits × 2.5μs = 60μs |
| **96ピクセル** | 96 × 60μs = 5.76ms |
| **リセット時間** | 50μs |
| **1フレーム時間** | 5.76ms + 50μs = 5.81ms |
| **最大fps** | 1 / 5.81ms = **172 fps** |

**実用fps:**
- 60fps: 余裕あり ✅
- 100fps: 余裕あり ✅
- 172fps: 理論上限

---

## ESP32-S3 RMT制御

### RMT (Remote Control) ペリフェラル

| 項目 | 仕様 |
|------|------|
| **タイプ** | ハードウェア制御 |
| **精度** | 高精度タイミング (80MHz APBクロック) |
| **DMA対応** | あり（CPU負荷低減） |
| **チャンネル数** | 8チャンネル |

### 設定例

```cpp
#include <driver/rmt.h>

#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define RMT_TX_GPIO GPIO_NUM_XX // 未割当
#define LED_COUNT 96

// RMT設定
rmt_config_t config = {
  .rmt_mode = RMT_MODE_TX,
  .channel = RMT_TX_CHANNEL,
  .gpio_num = RMT_TX_GPIO,
  .clk_div = 2, // 80MHz / 2 = 40MHz (0.025μs精度)
  .mem_block_num = 1,
  .tx_config = {
    .loop_en = false,
    .carrier_en = false,
    .idle_level = RMT_IDLE_LEVEL_LOW,
    .idle_output_en = true,
  }
};

rmt_config(&config);
rmt_driver_install(config.channel, 0, 0);
```

### LED更新

```cpp
// WS2812B用のRMTアイテム生成
rmt_item32_t led_data[LED_COUNT * 24]; // 96個 × 24bits

// RGB → RMTアイテム変換
for (int i = 0; i < LED_COUNT; i++) {
  uint8_t g = led_colors[i][0];
  uint8_t r = led_colors[i][1];
  uint8_t b = led_colors[i][2];

  encode_grb(g, r, b, &led_data[i * 24]);
}

// 送信
rmt_write_items(RMT_TX_CHANNEL, led_data, LED_COUNT * 24, true);
```

---

## シグナルインテグリティ

### 信号線長の影響

| 距離 | 影響 | 対策 |
|------|------|------|
| **~30cm** | 軽微 | 不要 |
| **30-50cm** | 波形なまり | バッファIC検討 |
| **>50cm** | エラー発生 | バッファIC必須 |

**V2での信号線長:** 30cm → 対策不要 ✅

### データ線バッファIC（不使用）

**候補:**
- 74HCT245: 8bit双方向バッファ
- 74HCT125: 4bit片方向バッファ

**不要な理由:**
- 信号線長30cm以内
- 3.3V信号でWS2812B動作確認済み
- ノイズ環境良好（I2C/UARTと分離）

**決定事項:**
- [ ] 実機テストで信号品質確認
- [ ] 必要に応じてバッファIC追加

---

## 配線設計

### 信号線とGND

| 項目 | 推奨 |
|------|------|
| **データ線** | AWG 26-30 |
| **GND並走** | データ線の隣に配置 |
| **ツイストペア** | 推奨（ノイズ対策） |

### FPC配線

| 項目 | 仕様 |
|------|------|
| **Data信号** | FPC 1層目 |
| **GND** | FPC 1層目（Data隣接） |
| **パターン幅** | 0.2mm以上 |

---

## ノイズ対策

### 既存対策

| 項目 | 状態 |
|------|------|
| **I2C/UART分離** | ✅ 別系統 |
| **電源分離** | ✅ DCDC出力 |
| **GND強化** | ✅ 4層基板 |

### 追加対策（必要に応じて）

| 項目 | 対策 |
|------|------|
| **フェライトビーズ** | データ線に挿入 |
| **コンデンサ** | 各LED電源ピンに0.1μF |
| **シールド** | FPCシールド追加 |

---

## 決定事項

- [x] 信号線最大長: 30cm
- [x] リフレッシュレート: 400kHz
- [x] データ線バッファIC: 不使用
- [ ] ESP32-S3 RMT設定確定
- [ ] 実機での信号品質測定
- [ ] 必要に応じてノイズ対策追加

---

## 参考資料

- [WS2812B Datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf)
- [ESP32-S3 RMT Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/rmt.html)
- [FastLED Library](https://github.com/FastLED/FastLED)

---

## 変更履歴

| 日付 | 変更内容 | 担当 |
|------|---------|------|
| 2025-11-09 | 初版作成 | CIC |
