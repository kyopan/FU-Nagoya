# U1 Firmware - ESP32-S3 Winch Controller

**プロジェクト**: FU@ComoNe (Fragmentations of Unity @ Common Nexus)
**バージョン**: v9
**ターゲット**: ESP32-S3-DevKitC-1
**フレームワーク**: Arduino + PlatformIO

---

## 🚀 クイックスタート

### 1. VS Codeでプロジェクトを開く

```bash
cd /path/to/projects/nagoya-tube-installation/firmware/Winch/U1
code .
```

### 2. 開発指示書を確認

**📘 必読**: [.claude/instructions.md](.claude/instructions.md)

このドキュメントに以下が含まれています：
- 開発目的と期限（2025-10-24）
- タスクリスト（Phase 1-5）
- ピンアサイン確認方法
- ファームウェア修正手順
- ビルド・書き込み・テスト手順
- トラブルシューティング

### 3. ファームウェア仕様を確認

**📖 完全仕様**: [U1_FIRMWARE_OVERVIEW.md](U1_FIRMWARE_OVERVIEW.md)

このドキュメントに以下が含まれています：
- システム全体像
- 主要機能（WiFi/MQTT/I2C/UART）
- 通信プロトコル詳細
- LED制御システム
- 状態管理

### 4. ピンアサイン仕様を確認

**🔌 ピンアサイン**: [../../docs/specs/winch-u1-esp32s3-pinout.md](../../docs/specs/winch-u1-esp32s3-pinout.md)

現在確定済みピン：
- GPIO 48: NeoPixel RGB LED (オンボード)
- GPIO 8/9: I2C SDA/SCL (U2 ウィンチモーター)
- GPIO 17/18: UART RX/TX (U3 LED制御)

---

## 🔧 開発環境

### 必要なツール

- **VS Code**: https://code.visualstudio.com/
- **PlatformIO Extension**: VS Code拡張機能
- **ESP32-S3ドライバ**: USB接続用（自動インストール）

### ライブラリ依存

以下は`platformio.ini`で自動インストールされます：

```ini
lib_deps =
    knolleary/PubSubClient@^2.8        # MQTT通信
    adafruit/Adafruit NeoPixel@^1.12.3 # LED制御
```

---

## 📝 主要タスク

### Phase 1: ピンアサイン確定 ✅

- [ ] 基板回路図確認（EasyEDA/KiCad）
- [ ] 実物基板で導通確認
- [ ] `winch-u1-esp32s3-pinout.md` 更新

### Phase 2: ファームウェア修正 ⏳

- [ ] `main.cpp`: GPIO番号定義更新
- [ ] `main.cpp`: I2C/UART初期化確認
- [ ] `platformio.ini`: ビルド設定確認

### Phase 3: ビルド＆書き込み ⏳

```bash
# ビルド
pio run

# 書き込み
pio run --target upload

# シリアルモニター
pio device monitor
```

### Phase 4: 通信テスト ⏳

- [ ] I2C通信テスト (U1 ↔ U2)
- [ ] UART通信テスト (U1 → U3)
- [ ] システム統合テスト

### Phase 5: ドキュメント更新 ⏳

- [ ] `winch-u1-esp32s3-pinout.md` 更新
- [ ] `../../TODO.md` 更新
- [ ] TODO集約実行

---

## 🧪 テストコマンド例

### I2C通信テスト

```cpp
// ホーミング状態読み取り
Wire.requestFrom(0x08, 2);
char tag = Wire.read();      // 'H'
int status = Wire.read();    // 0 or 1

// 位置制御（1000mm）
Wire.beginTransmission(0x08);
Wire.write('Z');
Wire.write(0xE8);  // LSB
Wire.write(0x03);  // MSB
Wire.endTransmission();
```

### UART通信テスト

```cpp
// 統合データパケット（赤色LED）
Serial1.printf("DATA,F,255,0,0,R,0,0,0,P,0.00,0.00,B,0,H,0.00\n");

// Sparkコマンド（10秒）
Serial1.printf("SPARK,10\n");

// クリア
Serial1.printf("C\n");
```

---

## 📂 プロジェクト構造

```
U1/
├── .claude/
│   ├── instructions.md       # 開発指示書（最重要）
│   └── claude_config.json    # Claude設定
├── .vscode/
│   └── settings.json         # VS Code設定
├── src/
│   ├── main.cpp              # メインロジック
│   ├── mqttClient.cpp        # MQTT通信
│   ├── otaUpdater.cpp        # OTA更新
│   └── globals.h             # グローバル変数
├── platformio.ini            # PlatformIO設定
├── U1_FIRMWARE_OVERVIEW.md   # ファームウェア完全仕様
└── README.md                 # このファイル
```

---

## 🔗 関連ドキュメント

| ドキュメント | 説明 |
|------------|------|
| [.claude/instructions.md](.claude/instructions.md) | **開発指示書（最重要）** |
| [U1_FIRMWARE_OVERVIEW.md](U1_FIRMWARE_OVERVIEW.md) | ファームウェア完全仕様 |
| [../../docs/specs/winch-u1-esp32s3-pinout.md](../../docs/specs/winch-u1-esp32s3-pinout.md) | ピンアサイン仕様 |
| [../../docs/specs/winch-circuit-design.md](../../docs/specs/winch-circuit-design.md) | Winch V2回路設計 |
| [../../notes/v1-analysis.md](../../notes/v1-analysis.md) | V1システム分析 |
| [../../TODO.md](../../TODO.md) | プロジェクト全体TODO |

---

## 📞 サポート

### 質問・問題が発生した場合

1. **[.claude/instructions.md](.claude/instructions.md)** のトラブルシューティングセクションを確認
2. **[U1_FIRMWARE_OVERVIEW.md](U1_FIRMWARE_OVERVIEW.md)** で該当機能の仕様を確認
3. シリアルモニター出力を保存してサポートに共有

---

## ⚠️ 重要な注意事項

- **期限**: 2025-10-24（基板発注締切）
- **I2Cプルアップ抵抗必須**: 4.7kΩ推奨
- **WiFi/MQTT必須**: 起動にはネットワーク接続が必要
- **ホーミング完了待ち**: 制御コマンドはホーミング完了後のみ有効
- **デバイスID取得必須**: MQTT経由でID取得後に本格動作

---

## ✅ 完了基準

- [ ] ピンアサイン確定・ドキュメント更新
- [ ] ファームウェア修正完了
- [ ] ビルド成功
- [ ] ESP32-S3への書き込み成功
- [ ] I2C通信テスト成功
- [ ] UART通信テスト成功
- [ ] システム統合テスト成功
- [ ] TODO.md更新・集約

**すべて完了したら基板発注準備完了！🎉**

---

**開発を開始してください。VS Codeでこのディレクトリを開き、`.claude/instructions.md`を確認してから作業を始めてください。**
