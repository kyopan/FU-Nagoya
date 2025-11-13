# FU (Fragmentations of Unity) プロジェクト固有指示

**プロジェクトID**: FU
**旧名称**: nagoya-tube-installation (NTI)
**最終更新**: 2025-11-09

---

## 🎯 プロジェクト概要

名古屋展示空間における吊り下げ筒型デバイスのインスタレーション作品。
天井Winchから吊り下げられた筒が上下動・回転しながら光と動きで空間演出。

---

## 📁 ディレクトリ構造ルール

### ⚠️ 重要: 作業ディレクトリ固定

**全ての作業は下記ディレクトリで実施:**
```
/Users/kyopan/Dropbox (個人)/Obsidian/kyopan/projects/fu/products
```

### 標準ディレクトリ構成

```
fu/
├── products/                    # 実装・製品ファイル（作業ディレクトリ）
│   ├── firmware/
│   │   ├── v1/                 # V1ファームウェア（参照用）
│   │   │   ├── Tube_v1/
│   │   │   │   ├── FU_TUBE_RP2040_U3/
│   │   │   │   └── FU_TUBE_RP2040_U4/
│   │   │   └── Winch_v1/
│   │   └── v2/                 # V2ファームウェア（開発中）
│   │       ├── Test/
│   │       ├── Tube_v2/
│   │       └── Winch_v2/
│   ├── modeling/
│   │   ├── Assets/
│   │   ├── v1/                 # V1モデリングデータ（参照用）
│   │   └── v2/                 # V2モデリングデータ（開発中）
│   ├── pcb/
│   │   ├── v1/                 # V1基板設計（参照用）
│   │   └── v2/                 # V2基板設計（開発中）
│   └── specs/
│       ├── test/               # テスト仕様
│       ├── v1/                 # V1仕様書（参照用）
│       └── v2/                 # V2仕様書（開発中）
│           ├── architect_v2/   # アーキテクチャ・設計仕様
│           ├── tube_v2/        # Tube側仕様
│           └── winch_v2/       # Winch側仕様
│
├── docs/                        # プロジェクト管理ドキュメント
│   ├── tasks/                  # タスク管理
│   └── meetings/               # 議事録
│
├── notes/                       # 開発ノート・セッション記録
└── README.md                    # プロジェクト概要
```

---

## 🔧 作業ディレクトリ詳細

### Firmware: `/products/firmware/v2/`

**目的:** V2ファームウェア開発

**サブディレクトリ:**
```
v2/
├── Test/                       # テスト用コード
├── Tube_v2/                    # Tube側ファームウェア
└── Winch_v2/                   # Winch側ファームウェア
```

**ルール:**
- V1ファームウェアは参照のみ、編集禁止
- V2開発は必ず `/products/firmware/v2/` 配下

---

### PCB: `/products/pcb/v2/`

**目的:** V2基板設計（KiCad/EasyEDA）

**ルール:**
- V1基板は参照のみ、編集禁止
- V2基板設計は `/products/pcb/v2/` 配下で実施

---

### Specs: `/products/specs/v2/`

**目的:** V2仕様書（設計・実装すべて）

**配置ファイル例:**
```
v2/
├── architect_v2/               # アーキテクチャ・設計仕様
│   ├── tube-v2-circuit-design.md
│   ├── system-architecture.md
│   └── winch-control-protocol.md
├── tube_v2/                    # Tube側仕様
│   └── (Tube関連仕様書)
└── winch_v2/                   # Winch側仕様
    └── (Winch関連仕様書)
```

**ルール:**
- **全ての仕様書を `/products/specs/v2/` 配下に配置**
- 設計レベル仕様: `architect_v2/` サブディレクトリ
- Tube側仕様: `tube_v2/` サブディレクトリ
- Winch側仕様: `winch_v2/` サブディレクトリ

---

## ⚙️ 技術スタック

### V2構成

| カテゴリ | 採用技術 |
|---------|---------|
| **HD マイコン** | ESP32-S3 × 2 (4コア) |
| **GR/CR マイコン** | 未定 (RP2040 or ESP32-C3) |
| **LED** | NeoPixel WS2812B × 96 |
| **センサー** | GridEye + GY-85 + AS5600×2 |
| **モーター** | BLDC (YAW) + RCサーボ DS-M005 (Pitch) |
| **通信** | UART 38400 baud |
| **電源** | 24V → 5V 2A DCDC |
| **開発環境** | PlatformIO + Arduino Framework |

---

## 🚨 重要ルール

### 1. ディレクトリ移動禁止

**原則:** 作業は必ず `/products/` 配下で実施

```bash
# ✅ 正しい
cd /Users/kyopan/Dropbox\ \(個人\)/Obsidian/kyopan/projects/fu/products

# ❌ 間違い
cd /Users/kyopan/Dropbox\ \(個人\)/Obsidian/project-repository/FU-Nagoya
```

### 2. V1ファームウェア保護

**ルール:** V1は参照のみ、編集・削除禁止

- `/products/firmware/v1/` - 読み取り専用
- V2開発は `/products/firmware/v2/` で実施

### 3. 仕様書の一元管理

**ルール:**
- **全ての仕様書は `/products/specs/v2/` 配下に配置**
- `/docs/` ディレクトリはプロジェクト管理用（タスク・議事録のみ）
- 設計仕様と実装仕様を分離せず、一箇所で管理

---

## 📋 V2開発状況

### 完了
- [x] GR/CR基板製作完了（V1ベース）
- [x] V1ファームウェア動作確認

### 進行中
- [ ] HD基板設計
- [ ] ESP32-S3台数決定（1基 or 2基）
- [ ] GR/CR通信方式決定（UART or I2C or SPI）
- [ ] V2ファームウェア設計

### 未着手
- [ ] スリップリング選定
- [ ] Pitchサーボ統合
- [ ] YAWモーター取付設計

---

## 🔗 関連ドキュメント

- [README.md](README.md) - プロジェクト概要
- [PROGRESS.md](PROGRESS.md) - 進捗管理
- [tube-v2-circuit-design.md](products/specs/v2/architect_v2/tube-v2-circuit-design.md) - V2回路設計仕様

---

## 変更履歴

| 日付 | 変更内容 | 担当 |
|------|---------|------|
| 2025-11-09 | プロジェクト固有CLAUDE.md初版作成 | kyopan |
| 2025-11-09 | ディレクトリ構造を実際の `/products/` 構成に合わせて修正 | CIC |
