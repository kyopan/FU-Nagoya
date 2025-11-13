# セッション記録: PR基板設計キックオフ

**日付**: 2025-11-03
**参加者**: kyopan, CIC
**目的**: PR基板（Pitch Ring）V2の詳細設計開始

---

## 📊 セッション概要

FU@ComoNeプロジェクトの筒本体V2実装において、PR基板（Pitch Ring）の詳細設計とWBS作成を実施。

---

## 🎯 達成事項

### 1. 構造理解の明確化

**確定した構造:**
```
ウィンチケーブル
  ↓
HR (Horizontal Ring) = FR (Flat Ring)
  - 水平固定基板
  - ESP32-S3 #1, #2搭載
  - 24V→5V DCDC
  - YAW BLDC（真下）、Pitch BLDC
  ↓ Pitch軸±60°回転
PR (Pitch Ring)
  - リング状基板
  - 真鍮線接続点×12
  - GR/CR支持構造
  ↓ 真鍮線接続
GR (GridEye Ring) + CR (Compass Ring)
  - 評価ボード流用（到着待ち）
```

### 2. 配線仕様の確定

**真鍮線12点の信号定義:**
- **PRF（前面6点）**: W2_3, W4_5, SDA0, SCL0, 5V, GND
- **PRB（背面6点）**: W6, SDA1, SCL1, W4_5, 5V, GND

**ESP32-S3役割分担:**
- **S3 #1**: ウィンチ通信 + LED/Grid-EYE/GY-85制御
- **S3 #2**: Pitch/YAWモーター + エンコーダー制御

### 3. エンコーダー配置の明確化

- **Pitchエンコーダー**: AS5600（PB基板、HRに垂直取り付け）
- **YAWエンコーダー**: AS5600（CR基板）
- **DIRエンコーダー**: AS5600（ミニプラグメス、コンパス代替）

### 4. 設計方針の確定

| 項目 | 確定内容 |
|------|---------|
| **設計担当** | kyopan |
| **設計ツール** | EasyEDA |
| **PCB厚さ** | 1.6mm |
| **I2Cプルアップ** | ESP32-S3内部プルアップ使用 |
| **真鍮線接続** | 治具作成して半田付け |
| **評価PR到着** | 3日後（11/6） |

---

## 📄 作成ドキュメント

### 1. [tube-body-v2-implementation-plan.md](../../docs/tube-body-v2/tube-body-v2-implementation-plan.md)
- 筒本体V2全体の実装計画
- V1からV2への変更点整理
- ハードウェア要件、回路構成、テスト項目

### 2. [tube-body-v2-structure-understanding.md](../../docs/tube-body-v2/tube-body-v2-structure-understanding.md)
- kyopanの説明に基づく構造理解の確認ドキュメント
- 不明点の洗い出し
- Mermaid図による可視化

### 3. [PR-board-design-specification.md](../../docs/tube-body-v2/PR-board-design-specification.md)
- PR基板の詳細設計仕様書
- 配線図からの真鍮線12点の信号定義
- 機械的・電気的要件
- 評価PR vs 本番PRの差分

### 4. [PR-board-WBS.md](../../docs/tube-body-v2/PR-board-WBS.md)
- PR基板設計のWork Breakdown Structure
- 7フェーズ、46タスクの詳細計画
- ガントチャート、リスク管理、マイルストーン

### 5. [HR-mechanical-dimensions.md](../../docs/tube-body-v2/HR-mechanical-dimensions.md)
- HR基板の機械寸法設計ワークシート
- 寸法検討の手順
- 確認が必要な部品仕様リスト

### 6. [evaluation-PR-verification-plan.md](../../docs/tube-body-v2/evaluation-PR-verification-plan.md)
- 評価PR基板の検証計画（11/6-11/10）
- 5つの検証項目（LED、耐久性、通信、電源、真鍮線）
- 検証スケジュールと成果物

---

## 🔍 キーインサイト

### 1. HR = FRの用語統一
- V1企画書では「FR (Flat Ring)」
- V2配線図では「HR (Horizontal Ring)」
- 同一基板を指す → 以後HRで統一

### 2. PRリング幅の課題
- HR外径70mm、PRリング外径74mmと仮定すると、リング幅1mm
- 真鍮線×12の配置が困難な可能性
- HR外径を小さくしてPRリング幅を確保する検討必要

### 3. 評価PRの役割明確化
- スリップリング・モーター・HRが無い簡易版
- 通信・LED・耐久性テスト専用
- 本番PR設計の検証データ収集が目的

### 4. 設計の依存関係
```
HR機械寸法確定
  ↓
PR外径・内径決定
  ↓
PRリング幅確定
  ↓
真鍮線配置設計
  ↓
回路図作成
  ↓
PCBレイアウト
```

---

## ⚠️ 未確認事項・リスク

### 機械設計（作成中）
- [ ] PR基板外径・内径寸法
- [ ] PRリング幅（真鍮線×12配置可能性）
- [ ] Pitch軸機構の詳細
- [ ] 取り付け穴位置・径

### 部品仕様（確認必要）
- [ ] スリップリング直径・高さ
- [ ] YAW BLDC直径・高さ
- [ ] Pitch BLDC直径・高さ
- [ ] ESP32-S3モジュール型番・サイズ
- [ ] DCDCモジュール型番・サイズ
- [ ] 真鍮線の線径・材質

### 設計判断（保留中）
- [ ] HR-PR間コネクタ選定
- [ ] 半田付け治具の詳細設計
- [ ] リング幅不足時の対応策

---

## 📅 次のアクションステップ

### 即座に実施（11/3-11/5）

#### kyopan
1. **HR機械寸法確定**
   - スリップリング、モーター、ESP32-S3、DCDCの仕様確認
   - HR外径・内径決定
   - PRリング幅の確保可能性検討

2. **Pitch軸機構設計**
   - 機械設計図面作成
   - ±60°回転機構の詳細設計

3. **部品選定**
   - HR-PR間コネクタ選定
   - 真鍮線仕様決定

#### CIC
- 評価PR到着待ち（11/6）
- 検証計画の準備

### 評価PR到着後（11/6-11/10）

- LED光り方確認（11/6）
- ウィンチ耐久性テスト（11/7-11/8）
- 通信チェック（11/9-11/10）
- 検証結果を本番PR設計にフィードバック

### 本番PR設計開始（11/11〜）

- 機械寸法確定後、回路図作成開始
- EasyEDAで設計
- PCBレイアウト → 発注 → 組み立て → 検証

---

## 💬 kyopanからのフィードバック

### 確定情報
- HR基板設計: kyopan担当
- 機械寸法: 作成中
- 評価PR: 3日後到着
- 検証内容: LED光り方、ウィンチ耐久性、通信チェック

### 設計方針
- I2C: 内部プルアップ使用
- 真鍮線: 治具で半田付け
- PCB: 1.6mm厚
- ツール: EasyEDA

---

## 📝 学び・改善点

### CIC自身の理解深化
- 配線図から信号経路を正確に読み取れた
- HR = FRの用語統一に気づけた
- 機械寸法とPCB設計の依存関係を整理できた

### 次回セッションへの引き継ぎ
- HR機械寸法の確定が最優先
- 評価PR検証結果を本番設計に反映する流れを確立
- 真鍮線半田付け治具の詳細設計が必要

---

## 🔗 関連ドキュメント

- [tube-body-v2-implementation-plan.md](../../docs/tube-body-v2/tube-body-v2-implementation-plan.md)
- [tube-body-v2-structure-understanding.md](../../docs/tube-body-v2/tube-body-v2-structure-understanding.md)
- [PR-board-design-specification.md](../../docs/tube-body-v2/PR-board-design-specification.md)
- [PR-board-WBS.md](../../docs/tube-body-v2/PR-board-WBS.md)
- [HR-mechanical-dimensions.md](../../docs/tube-body-v2/HR-mechanical-dimensions.md)
- [evaluation-PR-verification-plan.md](../../docs/tube-body-v2/evaluation-PR-verification-plan.md)
- [actuar-wiring-2025-10-23.png](../actuar-wiring-2025-10-23.png)

---

**記録者**: CIC
**承認**: kyopan
