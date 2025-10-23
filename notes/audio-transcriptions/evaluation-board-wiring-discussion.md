# 評価ボード配線ディスカッション

**日時**: 2025-10-23 10:52
**音声**: recording-2025-10-23-evaluation-board.m4a（約20分）
**参加者**: kyopan、鈴木さん（設計担当）
**関連資料**: [evaluation-board-sketch.jpg](../evaluation-board-sketch.jpg)

## 議論の目的

- 表裏基板の配線問題を解決
- 今日中に発注したい
- 組み付けコストを最小化

## 現状の問題点

### ミラーリングによる配線の複雑化

- 現状: ピッチリング（PR）とグリッドアイリング（GR）、コンパスリング（CR）が反転している
- メイン基板の上から見た構造:
  - ピッチリング: 5V + GND（グランド）が上部2箇所
  - この配置が反転すると、GRとGRが隣接し、配線がクロスして複雑化

### 配線の複雑性

- GRの配線がクロスする問題
- 基板の配置上の反転により、配線トポロジーが複雑になる

## 提案された解決策

### 1. 同じトポロジーで統一（ミラーリング不要）

**基本コンセプト**: ピッチリング、GR、CRを同じ向きで設計

- **PRF（Pitch Ring Front）** と **PRR（Pitch Ring Rear）**
- **GRF（Grideye Ring Front）** と **GRR（Grideye Ring Rear）**
- **CRF（Compass Ring Front）** と **CRR（Compass Ring Rear）**

従来は F/R が逆配線だったが、これを共通化する。

**重要**: GRとCRはそれぞれGrideyeとCompass（GY85）が乗るが、**共通基板を使用**する。

### 2. センサーの配置

- **GRF**: Grideyeを配置
- **CRR**: GY85（Compass）を配置

**注意**: GRとCRは共通基板のため、センサーは表裏で配置される。これにより配線がシンプルになる。

### 3. 真鍮線配線の詳細

#### 貫通配線（全リング共通）

- **真鍮線3 (Power)**: 5V - GR↔PR↔CR（LED/センサ電源供給用）
- **真鍮線4 (Power)**: GND - GR↔PR↔CR（LED/センサ電源供給用）
- **真鍮線1 (NeoPixel)**: PRR.NEOs2.DOUT → GRF.NEOs3.DIN
- **真鍮線2 (NeoPixel)**: GRR.NEOs4.DOUT → CRF.NEOs5.DIN

#### 部分配線（PR↔GR間のみ）

I2C通信用の個別配線:
- **真鍮線5 (I2C)**: PR.Grideye.SDA0 → GR.Grideye.SDA0
- **真鍮線6 (I2C)**: PR.Grideye.SCL0 → GR.Grideye.SCL0
- **真鍮線7 (I2C)**: PR.Compass.SDA1 → GR.Compass.SDA1
- **真鍮線8 (I2C)**: PR.Compass.SCL1 → GR.Compass.SCL1

#### 懸念点

- **真鍮線1**: CRにも接続されている可能性があり、データ衝突の懸念
- **真鍮線2**: PRを貫通しているが、電気的に接続していなければ問題ない

## ネオピクセル配線の詳細

### LED点灯パターン

1. **PRF** → 24個のNEOs1を点灯
2. **PRR** → 24個のNEOs2を点灯
3. PRR → **GRF** → 24個のNEOs3を点灯
4. GRF → **GRR** → 24個のNEOs4を点灯
5. GRR → **CRF** → 24個のNEOs5を点灯
6. CRF → **CRR** → 24個のNEOs6を点灯

### 配線の流れ

```
RP2040 GPIO29 → NEOs1 (PRF, 24LEDs)
             → NEOs2 (PRR, 24LEDs)
             → [真鍮線1] → NEOs3 (GRF, 24LEDs)
             → NEOs4 (GRR, 24LEDs)
             → [真鍮線2] → NEOs5 (CRF, 24LEDs)
             → NEOs6 (CRR, 24LEDs)
```

### 真鍮線の詳細仕様

#### NeoPixel用（信号線）

- **真鍮線1 (NeoPixel)**: PRR.NEOs2.DOUT → GRF.NEOs3.DIN
  - 懸念: CRにも接続されている可能性があり、データ衝突の懸念
- **真鍮線2 (NeoPixel)**: GRR.NEOs4.DOUT → CRF.NEOs5.DIN
  - 注意: PRを貫通しているが、電気的に接続していなければ問題ない

#### 電源用（貫通配線）

- **真鍮線3 (Power)**: 5V - GR↔PR↔CR（LED/センサ電源供給用）
- **真鍮線4 (Power)**: GND - GR↔PR↔CR（LED/センサ電源供給用）

#### I2C用（部分配線: PR↔GR間のみ）

- **真鍮線5 (I2C)**: PR.Grideye.SDA0 → GR.Grideye.SDA0
- **真鍮線6 (I2C)**: PR.Grideye.SCL0 → GR.Grideye.SCL0
- **真鍮線7 (I2C)**: PR.Compass.SDA1 → GR.Compass.SDA1
- **真鍮線8 (I2C)**: PR.Compass.SCL1 → GR.Compass.SCL1

### 共通基板の設計

**基板構成:**
- GRとCRは**同じ基板を使用**（表裏でハンダ付け）
- **GRF**: Grideyeを配置（表面）
- **CRR**: GY85（Compass）を配置（裏面）

**懸念点:**
- NEOs6.DOUT が真鍮線1または真鍮線2にぶつかると、データが衝突する可能性
- 対処: 真鍮線2を分断する必要があるかもしれない

**確認事項:**
- 同じ基板をGRとCRで使用した場合、貫通配線が通信上問題ないか
- センサー配置（GRF: Grideye / CRR: GY85）が機械的・電気的に干渉しないか

## ハンダ付けと組み付けの順序

### 提案された手順

1. **PRを先に取り付け**
2. **真鍮線1〜8を通す**
   - 貫通配線: 真鍮線1, 2, 3, 4（全リング貫通）
   - 部分配線: 真鍮線5, 6, 7, 8（PR↔GR間のみ）
3. **GRとCRを軸化中に固定**
4. **ハンダ付け**
5. **真鍮線がカーブしてもOK**（無理に直線にしない）

### 懸念点: 応力による接続の信頼性

- 常に曲げた状態で力がかかると、ハンダ接合部に負荷
- 2年間の使用を考慮すると、何の力もかからない状態が理想
- 必要に応じて「折る」処理が必要かもしれない

## 決定事項

### 確定した配線

#### 貫通配線（全リング共通）

1. **真鍮線3 (Power)**: 5V - GR↔PR↔CR
2. **真鍮線4 (Power)**: GND - GR↔PR↔CR
3. **真鍮線1 (NeoPixel)**: PRR.NEOs2.DOUT → GRF.NEOs3.DIN（※CRへの接続懸念あり）
4. **真鍮線2 (NeoPixel)**: GRR.NEOs4.DOUT → CRF.NEOs5.DIN（※PRを貫通）

#### 部分配線（PR↔GR間のみ）

5. **真鍮線5 (I2C)**: PR.Grideye.SDA0 → GR.Grideye.SDA0
6. **真鍮線6 (I2C)**: PR.Grideye.SCL0 → GR.Grideye.SCL0
7. **真鍮線7 (I2C)**: PR.Compass.SDA1 → GR.Compass.SDA1
8. **真鍮線8 (I2C)**: PR.Compass.SCL1 → GR.Compass.SCL1

### 次のステップ

1. **鈴木さんに依頼**:
   - Pitch Ringのミラー設計を廃止
   - Grideye RingとCompass Ringで同じ基板を使用する設計
   - 貫通配線による組み付けコスト削減

2. **kyopanが実施**:
   - CICによる解析（同じ基板で通信上問題ないか検証）
   - ネオピクセル配線の詳細検証

3. **シンプル化の方向性**:
   - 以前のフィージビリティ検証でシンプルな設計が確認済み
   - この方向で進める

## 添付資料

- [evaluation-board-sketch.jpg](../evaluation-board-sketch.jpg) - 手書きスケッチ
- Document EVAL: 上部に配線構成図、下部に複数のSVG/CAD検討案

## メモ

- SOPA-5.0（5mm厚）とSOPA-3.0（3mm厚）の比較検討
- M5ネジ、M2コネクタの配置
- Grideye RingとCompass Ringのボード固定方法（R、Lの配置）
