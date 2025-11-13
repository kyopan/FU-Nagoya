# Nagoya Tube Installation TODO

## In Progress

- [~] 評価ボード設計・発注（JLCPCB）
  - Started: 2025-10-16
  - Deadline: 2025-10-24（JLCPCB発注締切）
  - Assigned: 鈴木さん
  - Priority: 最優先
  - Details: ウィンチ検証用評価ボード（RP2040 + NeoPixel 48個 + 24V→5V 2A電源回路）
  - Document: [evaluation-board-design.md](docs/tasks/evaluation-board-design.md)

## Pending

- [ ] Winch回路設計V2
  - Priority: High
  - Assigned: kyopan
  - Estimated: 2-3週間
  - Details: V1をベースに改良、モーター制御最適化、通信安定性向上
  - Document: [winch-circuit-design.md](docs/specs/winch-circuit-design.md)
  - Tasks:
    - [ ] V1回路の分析・課題抽出
    - [ ] モーター種別決定（ステッピング vs DC）
    - [ ] 回路図作成（EasyEDA/KiCad）
    - [ ] PCBレイアウト設計
    - [ ] プロトタイプ製作・テスト

- [ ] 筒の重量見積もり
  - Priority: High
  - Estimated: 2h
  - Details: 搭載モジュールをリストアップし、合計重量×1.2倍を算出
  - Blocking: 評価ボードの重量調整機構設計

- [ ] V1 EGDAファイルの回路抽出
  - Priority: High
  - Estimated: 3h
  - Details: 既存V1（7枚構成）から必要な回路（電源、RP2040、NeoPixel）を抽出
  - Blocking: 評価ボード回路設計

- [ ] ケーブル断線テストプログラム作成
  - Priority: High
  - Estimated: 4h
  - Assigned: kyopan
  - Deadline: 2025-10-25
  - Details: 双方向通信を定期的に実行し、1ヶ月間ログ記録するテストプログラム作成

- [ ] 名古屋現地テスト準備（10/31）
  - Priority: Medium
  - Assigned: kyopan, 鈴木さん
  - Details: ウィンチ5台 + 評価ボード5台を持ち込み、動作検証

- [ ] 上條さん研修準備（2025-11-13）
  - Priority: Low
  - Details: 名古屋大学院進学予定・上條さんのKyopalab研修サポート準備（2026年4月から筒サポート本格化）

## Completed

- [x] プロジェクト初期化
  - Completed: 2025-10-13
  - Details: README作成、ディレクトリ構造整備

- [x] 音声タスク指示の文字起こし
  - Completed: 2025-10-16
  - Details: 新規録音32.m4a、33.m4aをWhisperで文字起こし→タスクドキュメント化
