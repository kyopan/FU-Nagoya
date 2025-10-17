# Fragmentations of Unity (FU)

Common Nexus（ComoNe）群ロボットインスタレーション

## プロジェクト概要

**作品名**: Fragmentations of Unity（仮）
**会場**: Common Nexus（ComoNe）
**コンセプト**: 群と個 - 群行動の中に見える個性の探求

## 作品について

46個（または16個）の筒状ロボットが天井から吊り下げられ、ダクトレールシステムで制御されるインタラクティブ・インスタレーション。

各ロボットは来場者を感知し、群としての振る舞いと個としての個性を表現します。言語を持たないロボットたちが、動きや光によって来場者とコミュニケーションを図ります。

### 技術仕様

- **センシング**: Grid-EYE（8×8サーマルセンサー）による来場者検知
- **制御**: ダクトレール上の位置制御、個体の動作制御
- **挙動**: ポジティブ/ネガティブフィードバックループによる群行動
- **個性**: 各ロボットに異なるパラメータ設定

### 技術スタック

- Arduino / ESP32-S3
- Grid-EYE（サーマルセンサー）
- マイクアレイ（空間トラッキング）
- ダクトレール制御システム

### 研究テーマ

米澤拓郎教授（名古屋大学）の「共有リアリティ」研究と連携。異なる現実をつなぐインターリアリティの探求。

## チーム

- **米澤拓郎** - 名古屋大学（インターリアリティ研究）
- **菅野創** - 愛知県立芸術大学（ベルリン拠点、アーティスト）
- **興野悠太郎 (Kyopan)** - Kyopalab LLC（技術開発）

## 📊 プロジェクト進捗

**🔴 最新**: [PROGRESS.md](PROGRESS.md) - 詳細な進捗レポート（2025-10-17更新）

**現在のフェーズ**: 基板発注準備中（締切: 2025-10-24）

### 主要な完了事項
- ✅ ESP32-S3デュアルコアアーキテクチャ設計（V2）
- ✅ 電磁ブレーキ回路設計（フェイルセーフ対応）
- ✅ ステッパーモータートレース幅問題解決（10mil → 40mil）
- ✅ ESP32-S3ピンアサイン確定（13本GPIO最適配置）

## ドキュメント

### 📋 仕様書・設計
- [PROGRESS.md](PROGRESS.md) - **プロジェクト全体進捗レポート**
- [ESP32-S3ピンアサイン確定版](docs/specs/esp32s3-pinout-assignment.md)
- [PCBレイアウト解析](docs/specs/wpcbb-pinout-analysis.md)
- [評価基板設計タスク](docs/tasks/evaluation-board-design.md)

### 📝 開発ノート
- [Arduinoプルアップ抵抗講義](notes/arduino-pullup-resistor-lecture.md)
- [TX/RXピンアウト指示書](notes/tx-rx-connector-pinout-instructions-2025-10-17.md)
- [ステッパートレース幅解析](notes/winch-stepper-trace-width-analysis.md)
- [トレース幅修正完了報告](notes/winch-stepper-trace-width-fix-completed.md)

### 📄 企画資料
- [企画書PDF](docs/米澤拓郎%20+%20菅野創+Kyopalab%20チーム%20企画書.pdf)

## プロジェクト情報

**Status**: 🔄 開発中（基板発注準備フェーズ）
**Repository**: https://github.com/kyopan/FU-Nagoya
**基板発注締切**: 2025-10-24
