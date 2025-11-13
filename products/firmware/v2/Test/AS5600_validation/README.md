# AS5600 Side-Mount Validation Test

## 目的

スリップリング角度検出用AS5600（AS-S）の側面センシング構成を検証

## 検証内容

### 標準構成 vs 側面構成

**標準構成（軸方向磁化）:**
```
    AS5600センサー
         ↓
    [N | S]  ← ディスク磁石（上から見てNS分割）
    ───┴───
     回転軸
```

**側面構成（今回の検証）:**
```
AS5600センサー →  [磁石]  ← 90°回転配置
                   │
              スリップリング
```

## ハードウェア準備

### 必要な機材
- [x] AS5600評価ボード（手元にあり）
- [x] ESP32-S3開発ボード
- [x] 標準ディスク磁石（6mm径 × 2.5mm厚）
- [ ] ジャンパーワイヤー（4本: VCC, GND, SDA, SCL）
- [ ] ブレッドボード（任意）
- [ ] 定規・ノギス（距離測定用）

### 配線

| AS5600 | ESP32-S3 |
|--------|----------|
| VCC    | 3.3V     |
| GND    | GND      |
| SDA    | GPIO8    |
| SCL    | GPIO9    |

## PlatformIO セットアップ

### 1. プロジェクトを開く

```bash
cd /Users/kyopan/Dropbox\ \(個人\)/Obsidian/kyopan/projects/fu/products/firmware/v2/Test/AS5600_validation
code .  # VS Code起動
```

### 2. ビルド & アップロード

```bash
# ビルド
pio run

# アップロード
pio run --target upload

# シリアルモニター起動
pio device monitor
```

### 3. VS Code操作

- **ビルド**: `Ctrl+Alt+B` (macOS: `Cmd+Option+B`)
- **アップロード**: `Ctrl+Alt+U` (macOS: `Cmd+Option+U`)
- **シリアルモニター**: `Ctrl+Alt+S` (macOS: `Cmd+Option+S`)

## 実験手順

### 1. 標準構成での動作確認（ベースライン）

1. 磁石をAS5600センサーの真上に配置（標準構成）
2. プログラムを実行
3. 磁石を手動で360°回転
4. 出力結果を記録

**期待される結果:**
```
✓ DETECTED  AGC: 120-140 ✓  Magnitude: 1500-3000
Raw: 0-4095 (線形に変化)
```

### 2. 側面配置での検証（メイン実験）

#### 実験2-1: 距離 0.5mm
1. 磁石を90°回転（側面がセンサーに向く）
2. センサー-磁石間距離: 0.5mm
3. 360°回転テスト
4. 結果記録

#### 実験2-2: 距離 1mm
（同上）

#### 実験2-3: 距離 2mm
（同上）

#### 実験2-4: 距離 3mm
（同上）

### 3. 評価基準

| 項目 | 合格条件 |
|------|---------|
| **磁石検出** | `✓ DETECTED` が常に表示 |
| **角度範囲** | Raw値が0-4095を線形にカバー |
| **AGC値** | 96-160の範囲内（最適: 128±32） |
| **Magnitude** | 1000以上（磁場強度十分） |
| **安定性** | 同一角度で値のブレが±10以内 |

## シリアルモニター出力例

```
========================================
AS5600 Validation Test
Side-Mounted Magnet Configuration
========================================

✓ AS5600 detected

Test started - Rotate magnet slowly 360°
----------------------------------------

Raw: 0      Angle: 0.00°    Status: ✓ DETECTED   AGC: 128 ✓  Magnitude: 2048
Raw: 100    Angle: 8.79°    Status: ✓ DETECTED   AGC: 130 ✓  Magnitude: 2055
Raw: 200    Angle: 17.58°   Status: ✓ DETECTED   AGC: 127 ✓  Magnitude: 2040
...
```

## 結果記録テンプレート

### 標準構成（ベースライン）

| 項目 | 結果 |
|------|------|
| 磁石検出 | ✓ / ✗ |
| AGC範囲 | min-max |
| Magnitude | min-max |
| 角度線形性 | 良好 / 不良 |
| 備考 | - |

### 側面配置 - 距離 0.5mm

| 項目 | 結果 |
|------|------|
| 磁石検出 | ✓ / ✗ |
| AGC範囲 | min-max |
| Magnitude | min-max |
| 角度線形性 | 良好 / 不良 |
| 備考 | - |

### 側面配置 - 距離 1mm

| 項目 | 結果 |
|------|------|
| 磁石検出 | ✓ / ✗ |
| AGC範囲 | min-max |
| Magnitude | min-max |
| 角度線形性 | 良好 / 不良 |
| 備考 | - |

### 側面配置 - 距離 2mm

| 項目 | 結果 |
|------|------|
| 磁石検出 | ✓ / ✗ |
| AGC範囲 | min-max |
| Magnitude | min-max |
| 角度線形性 | 良好 / 不良 |
| 備考 | - |

### 側面配置 - 距離 3mm

| 項目 | 結果 |
|------|------|
| 磁石検出 | ✓ / ✗ |
| AGC範囲 | min-max |
| Magnitude | min-max |
| 角度線形性 | 良好 / 不良 |
| 備考 | - |

## トラブルシューティング

### "✗ AS5600 NOT detected"
- I2C配線を確認（SDA/SCL逆接続？）
- AS5600のVCC電圧確認（3.3V供給？）
- I2Cプルアップ抵抗確認（通常2.2kΩ-4.7kΩ）
- `pio device list` でデバイス認識確認

### "✗ NO MAGNET"
- 磁石の向きを確認（NS極の方向）
- センサー-磁石間距離を調整（0.5-3mm）
- 磁石の磁力確認（ネオジム推奨）

### "[TOO WEAK]"
- 磁石をセンサーに近づける
- より強力な磁石に交換（N35 → N52）

### "[TOO STRONG]"
- 磁石をセンサーから離す
- より弱い磁石に交換

### "AGC値が範囲外"
- センサー-磁石間距離を調整
- 磁石の向きを微調整
- AGC < 96: 磁石を近づける
- AGC > 160: 磁石を離す

### シリアルモニターが開かない
```bash
# デバイス確認
pio device list

# 手動でシリアルモニター起動
pio device monitor --baud 115200
```

## 次のステップ

### 検証成功の場合
1. 最適距離を決定（AGC値が最も安定）
2. 径方向磁化リング磁石を発注
3. リング磁石での本格検証
4. tube-v2-circuit-design.mdに結果反映

### 検証失敗の場合
1. 代替案A: 小型ディスク磁石を複数配置
2. 代替案B: より強力な磁石（N52グレード）
3. 代替案C: AS5600の代わりに別のエンコーダー検討

## 関連ドキュメント

- [tube-v2-circuit-design.md](../../specs/v2/architect_v2/tube-v2-circuit-design.md) - V2回路設計仕様
- [AS5600データシート](https://ams.com/documents/20143/36005/AS5600_DS000365_5-00.pdf)
- [AS5600コンポーネント仕様](../../specs/v2/Component/AS5600.md)

## プロジェクト構造

```
AS5600_validation/
├── platformio.ini       # PlatformIO設定
├── src/
│   └── main.cpp        # メインプログラム
└── README.md           # このファイル
```
