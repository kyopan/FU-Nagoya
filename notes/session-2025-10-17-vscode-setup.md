# セッション記録: VS Code開発環境セットアップ

**日付**: 2025-10-17
**プロジェクト**: FU@ComoNe Winch U1 Firmware
**目的**: VS Code + PlatformIOでの開発準備完了

---

## 📋 セッション概要

VS Codeで新しいセッションを開いてPlatformIO開発を行うための完全なセットアップを実施。

**開発ディレクトリ**: `projects/nagoya-tube-installation/firmware/Winch/U1/`

---

## ✅ 作成したファイル

### 1. 開発指示書

**ファイル**: `.claude/instructions.md`

**内容**:
- 開発目的と期限（2025-10-24）
- 5つのフェーズ別タスクリスト
- ピンアサイン確定手順
- ファームウェア修正チェックリスト
- ビルド・書き込み・テスト手順（コード例付き）
- トラブルシューティング（WiFi, MQTT, I2C, UART, ビルドエラー）
- 完了条件

**特徴**:
- 具体的なコード例を多数含む
- I2C/UART通信のテストコード完備
- シリアルモニター出力の期待値を明記
- 各フェーズの成功基準を明確化

---

### 2. Claude設定ファイル

**ファイル**: `.claude/claude_config.json`

**内容**:
- プロジェクト基本情報
- 参照ドキュメントリスト（優先度付き）
- 開発環境情報（Framework, Board, Version）
- 5フェーズのタスク構造
- I2C/UART/MQTTテスト仕様
- 重要な注意事項
- コーディングスタイル

**特徴**:
- JSON形式で機械可読
- テスト仕様を構造化
- I2Cコマンド一覧を完備
- UARTコマンドフォーマットを明記

---

### 3. VS Code設定

**ファイル**: `.vscode/settings.json`（既存ファイル更新）

**追加設定**:
- Arduino/PlatformIO用ファイル関連付け（.ino, .h → cpp）
- フォーマット設定（Google Style, 2スペース）
- PlatformIO自動ビルド無効化
- .pioディレクトリ除外（検索・監視）
- C/C++フォーマッター設定

**効果**:
- コード編集がスムーズに
- 不要なビルドを抑制
- 大量のビルド成果物をVS Codeが監視しない

---

### 4. README.md

**ファイル**: `README.md`

**内容**:
- クイックスタートガイド（3ステップ）
- 開発環境セットアップ
- 主要タスク一覧（Phase 1-5）
- テストコマンド例（I2C/UART）
- プロジェクト構造
- 関連ドキュメントリンク
- 完了基準チェックリスト

**特徴**:
- 初めて開く人でもすぐに開発開始できる
- コピペで使えるテストコード
- 視覚的に分かりやすいタスク進捗表示

---

## 🎯 開発フロー

### VS Codeセッション開始手順

1. **ディレクトリを開く**
   ```bash
   cd /Users/kyopan/Dropbox (個人)/Obsidian/kyopan/projects/nagoya-tube-installation/firmware/Winch/U1
   code .
   ```

2. **開発指示書を確認**
   - `.claude/instructions.md` を開く
   - 現在のフェーズとタスクを確認

3. **ファームウェア仕様を確認**（必要に応じて）
   - `U1_FIRMWARE_OVERVIEW.md` で機能詳細を確認
   - `../../docs/specs/winch-u1-esp32s3-pinout.md` でピン配置を確認

4. **開発開始**
   - Phase 1から順に作業
   - 各タスク完了後にチェックボックスをオン

---

## 📚 ドキュメント階層

```
開発指示書（.claude/instructions.md）
    ├─ クイックリファレンス（README.md）
    ├─ 完全仕様（U1_FIRMWARE_OVERVIEW.md）
    ├─ ピンアサイン（winch-u1-esp32s3-pinout.md）
    ├─ 回路設計（winch-circuit-design.md）
    └─ V1分析（v1-analysis.md）
```

**読む順序**:
1. `.claude/instructions.md` - 開発タスク把握
2. `README.md` - クイックスタート
3. `U1_FIRMWARE_OVERVIEW.md` - 機能詳細（必要に応じて）

---

## 🔧 開発タスク（Phase別）

### Phase 1: ピンアサイン確定
**所要時間**: 1-2時間

- [ ] 基板回路図確認（EasyEDA/KiCad）
- [ ] ESP32-S3周辺トレース追跡
- [ ] U2 I2C接続確認（GPIO 8/9）
- [ ] U3 UART接続確認（GPIO 17/18）
- [ ] `winch-u1-esp32s3-pinout.md` 更新

**成果物**: ピンアサイン最終決定表

---

### Phase 2: ファームウェア修正
**所要時間**: 1-2時間

- [ ] `main.cpp`: GPIO番号定義確認・修正
- [ ] `main.cpp`: `Wire.begin(SDA, SCL)` 引数確認
- [ ] `main.cpp`: `Serial1.begin(baud, config, RX, TX)` 引数確認
- [ ] `platformio.ini`: ビルド設定確認

**成果物**: 修正済みソースコード

---

### Phase 3: ビルド＆書き込み
**所要時間**: 30分-1時間

- [ ] `pio run` でビルド成功
- [ ] `pio run --target upload` で書き込み成功
- [ ] `pio device monitor` でシリアル出力確認

**成功基準**:
```
WiFi接続成功
MQTT接続成功
ID取得開始
```

---

### Phase 4: 通信テスト
**所要時間**: 2-3時間

#### I2C通信テスト
- [ ] U2デバイス検出
- [ ] ホーミング状態読み取り
- [ ] 位置制御（1000mm送信）
- [ ] ダウンライト（50%送信）

#### UART通信テスト
- [ ] 統合データパケット送信
- [ ] LED色変化確認（赤→緑→青）
- [ ] Sparkコマンド送信
- [ ] Sparkアニメーション確認

#### システム統合テスト
- [ ] MQTT経由LED制御
- [ ] MQTT経由位置制御
- [ ] MQTT経由Sparkモード

**成果物**: テスト結果ログ

---

### Phase 5: ドキュメント更新
**所要時間**: 30分

- [ ] `winch-u1-esp32s3-pinout.md` にテスト結果追記
- [ ] `../../TODO.md` の各サブタスクをチェック
- [ ] TODO集約スクリプト実行

**コマンド**:
```bash
cd /Users/kyopan/Dropbox (個人)/Obsidian/kyopan
python3 .todos/sync-todos.py
```

---

## 🧪 テストコード例

### I2C通信テスト関数

```cpp
void testI2CConnection() {
  Serial.println("=== I2C Communication Test ===");

  // ホーミング状態読み取り
  Wire.requestFrom(U2_SLAVE_ADDRESS, 2);
  if (Wire.available() == 2) {
    char tag = Wire.read();
    int status = Wire.read();
    Serial.printf("[TEST] Homing: tag='%c', status=%d\n", tag, status);
  }

  // 位置制御（1000mm）
  Wire.beginTransmission(U2_SLAVE_ADDRESS);
  Wire.write('Z');
  Wire.write(0xE8);  // LSB
  Wire.write(0x03);  // MSB
  byte err = Wire.endTransmission();
  Serial.printf("[TEST] Position (1000mm): err=%d\n", err);

  Serial.println("=== I2C Test Complete ===");
}
```

### UART通信テスト関数

```cpp
void testUARTConnection() {
  Serial.println("=== UART Communication Test ===");

  // 赤色LED
  Serial1.printf("DATA,F,255,0,0,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");
  delay(1000);

  // 緑色LED
  Serial1.printf("DATA,F,0,255,0,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");
  delay(1000);

  // 青色LED
  Serial1.printf("DATA,F,0,0,255,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");
  delay(1000);

  // Sparkモード（10秒）
  Serial1.printf("SPARK,10\n");

  Serial.println("=== UART Test Complete ===");
}
```

### setup()への追加

```cpp
void setup() {
  // ... 既存のセットアップコード ...

  // テスト実行（開発中のみ）
  testI2CConnection();
  testUARTConnection();
}
```

---

## 🐛 トラブルシューティング

### よくある問題と解決策

#### 問題1: I2C error=2 (NACK on address)

**原因**: U2デバイスが応答しない

**解決策**:
1. U2の電源確認
2. I2Cプルアップ抵抗確認（4.7kΩ）
3. SDA/SCL配線確認（逆接続していないか）
4. スレーブアドレス確認（0x08）

---

#### 問題2: UARTでLEDが反応しない

**原因**: TX/RX配線または設定ミス

**解決策**:
1. TX/RX配線確認（クロス接続が必要な場合あり）
2. ボーレート確認（38400 baud）
3. U3の電源確認
4. シリアルモニターでパケット送信を確認

---

#### 問題3: WiFi接続失敗

**原因**: SSID/パスワード不一致

**解決策**:
1. `main.cpp` のWiFi設定確認
2. WiFiルーター電波強度確認
3. ESP32-S3アンテナ接続確認

---

## 📊 期待される開発時間

| Phase | タスク | 所要時間 |
|-------|--------|---------|
| 1 | ピンアサイン確定 | 1-2時間 |
| 2 | ファームウェア修正 | 1-2時間 |
| 3 | ビルド＆書き込み | 0.5-1時間 |
| 4 | 通信テスト | 2-3時間 |
| 5 | ドキュメント更新 | 0.5時間 |
| **合計** | | **5-9時間** |

**推奨スケジュール**: 2日間（各4-5時間）

---

## 🔗 関連ファイル

### 作成・更新されたファイル

```
firmware/Winch/U1/
├── .claude/
│   ├── instructions.md       # 新規作成
│   └── claude_config.json    # 新規作成
├── .vscode/
│   └── settings.json         # 更新
├── README.md                 # 新規作成
├── U1_FIRMWARE_OVERVIEW.md   # 既存（参照用）
└── src/
    └── main.cpp              # 修正予定
```

### 参照ドキュメント

```
docs/specs/
├── winch-u1-esp32s3-pinout.md    # ピンアサイン仕様
└── winch-circuit-design.md        # 回路設計仕様

notes/
├── v1-analysis.md                 # V1システム分析
└── session-2025-10-17-firmware-integration.md  # 前セッション記録

TODO.md                            # プロジェクト全体TODO
```

---

## ✅ 完了条件

- [x] `.claude/instructions.md` 作成
- [x] `.claude/claude_config.json` 作成
- [x] `.vscode/settings.json` 更新
- [x] `README.md` 作成
- [ ] VS Codeで開発セッション開始
- [ ] Phase 1-5のタスク完了
- [ ] TODO.md更新・集約

---

## 🎯 次のアクション

1. **VS Codeを開く**
   ```bash
   cd /Users/kyopan/Dropbox (個人)/Obsidian/kyopan/projects/nagoya-tube-installation/firmware/Winch/U1
   code .
   ```

2. **開発指示書を確認**
   - `.claude/instructions.md` を開く

3. **Phase 1から開始**
   - 基板回路図の確認
   - ピンアサイン確定

4. **Claude/Copilotと協働**
   - テストコードの実装
   - デバッグ支援
   - ドキュメント更新

---

**VS Code開発環境の準備が完了しました。新しいセッションを開いて開発を開始してください！**

**記録者**: Claude (CIC - Context Integration Coordinator)
