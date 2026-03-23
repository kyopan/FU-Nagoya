# AWS S3 自動アップロード設定

## 概要

ビルド成功後、ファームウェアバイナリ（`.bin`）を自動的にAWS S3にアップロードします。

## ⚡ クイックスタート

### AWS認証情報の設定（初回のみ）

AWS認証情報は既に設定済みです：
- `~/.aws/credentials` - AWSアクセスキー
- `~/.aws/config` - リージョン設定

### 通常の使用方法

**✨ 最も簡単な方法：クリーンビルド（推奨）**

S3設定は `platformio.ini` に組み込まれているため、環境変数不要：

```bash
cd "/Users/kyopan/Dropbox (個人)/Obsidian/kyopan/projects/fu/products/firmware/v2/Test/ESP32-S3/DevKitC/mqtt_plus_test"

# クリーンビルド（確実にS3アップロード）
pio run --target clean && pio run
```

⚠️ **重要**: ソースコードに変更がない場合、`pio run` だけではビルドがスキップされ、S3アップロードも実行されません。クリーンビルドを使用してください。

**代替方法A: ヘルパースクリプトを使用**

```bash
./build_and_upload.sh
./build_and_upload.sh clean  # クリーンビルド
```

**代替方法B: 環境変数で上書き**

platformio.iniの設定を一時的に上書きする場合：

```bash
export S3_BUCKET="別のバケット名"
export S3_PREFIX="別のプレフィックス/"
export AWS_REGION="別のリージョン"
pio run
```

### アップロード結果の確認

ブラウザでS3バケットを確認:
```
https://ap-northeast-1.console.aws.amazon.com/s3/buckets/comone-fragmented-unity-fw?prefix=winch_v2/
```

## S3設定の変更方法

`platformio.ini` の以下の部分を編集：

```ini
; AWS S3 Upload Settings
custom_s3_bucket = comone-fragmented-unity-fw  ; S3バケット名
custom_s3_prefix = winch_v2/                   ; S3キープレフィックス
custom_aws_region = ap-northeast-1             ; AWSリージョン
```

例：Tube V2 Unit1用に変更する場合：
```ini
custom_s3_bucket = comone-fragmented-unity-fw
custom_s3_prefix = tube_v2_unit1/
custom_aws_region = ap-northeast-1
```

## 機能

- ✅ ビルド成功後に自動アップロード
- ✅ タイムスタンプ付きバージョン管理
- ✅ `latest` バージョンも同時アップロード
- ✅ メタデータ付与（プロジェクト名、ビルド時刻、ボード情報）
- ✅ エラーハンドリング

## 前提条件

### 1. boto3のインストール

```bash
pip install boto3
```

### 2. AWS認証情報の設定

以下のいずれかの方法で認証情報を設定:

#### 方法A: AWS CLIで設定（推奨）

```bash
aws configure
```

プロンプトに従って入力:
- AWS Access Key ID
- AWS Secret Access Key
- Default region name: `ap-northeast-1`
- Default output format: `json`

#### 方法B: 環境変数で設定

```bash
export AWS_ACCESS_KEY_ID="your-access-key"
export AWS_SECRET_ACCESS_KEY="your-secret-key"
export AWS_DEFAULT_REGION="ap-northeast-1"
```

#### 方法C: IAMロール（EC2インスタンス上で実行する場合）

EC2インスタンスにS3アクセス権限のあるIAMロールをアタッチ。

### 3. 必要なIAM権限

S3バケットへのアップロード権限が必要:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject",
        "s3:PutObjectAcl"
      ],
      "Resource": "arn:aws:s3:::your-bucket-name/fu/firmware/*"
    }
  ]
}
```

## 環境変数の設定

### 必須環境変数

| 変数名 | 説明 | 例 |
|--------|------|-----|
| `S3_BUCKET` | S3バケット名 | `my-firmware-bucket` |

### オプション環境変数

| 変数名 | 説明 | デフォルト値 |
|--------|------|-------------|
| `S3_PREFIX` | S3キープレフィックス | `` (空文字列) |
| `AWS_REGION` | AWSリージョン | `ap-northeast-1` |

### 環境変数の設定方法

#### macOS/Linux (一時的)

```bash
export S3_BUCKET="my-firmware-bucket"
export S3_PREFIX="fu/firmware/esp32s3/"
export AWS_REGION="ap-northeast-1"
```

#### macOS/Linux (永続的)

`~/.zshrc` または `~/.bashrc` に追加:

```bash
# FU Project - S3 Upload Settings
export S3_BUCKET="my-firmware-bucket"
export S3_PREFIX="fu/firmware/esp32s3/"
export AWS_REGION="ap-northeast-1"
```

設定後、ターミナルを再起動または:

```bash
source ~/.zshrc  # または source ~/.bashrc
```

#### Windows (PowerShell)

一時的:
```powershell
$env:S3_BUCKET = "my-firmware-bucket"
$env:S3_PREFIX = "fu/firmware/esp32s3/"
$env:AWS_REGION = "ap-northeast-1"
```

永続的（ユーザー環境変数）:
```powershell
[System.Environment]::SetEnvironmentVariable("S3_BUCKET", "my-firmware-bucket", "User")
[System.Environment]::SetEnvironmentVariable("S3_PREFIX", "fu/firmware/esp32s3/", "User")
[System.Environment]::SetEnvironmentVariable("AWS_REGION", "ap-northeast-1", "User")
```

## 使用方法

### 通常のビルド（アップロード有効）

```bash
cd /path/to/mqtt_plus_test
pio run
```

ビルド成功後、自動的にS3にアップロードされます。

### アップロード先の確認

アップロードされるファイル:

1. **バージョン付き**: `s3://bucket/prefix/firmware_YYYYMMDD_HHMMSS.bin`
2. **最新版**: `s3://bucket/prefix/firmware_latest.bin`

### アップロードをスキップする場合

`S3_BUCKET` 環境変数を未設定にするか、一時的に無効化:

```bash
unset S3_BUCKET
pio run
```

または

```bash
S3_BUCKET="" pio run
```

## 出力例

ビルド成功時の出力:

```
Building in release mode
...
Linking .pio/build/esp32-s3-devkitc-1/firmware.bin
Building .pio/build/esp32-s3-devkitc-1/firmware.bin
============================================================
AWS S3 Upload Configuration:
  Bucket:       my-firmware-bucket
  Region:       ap-northeast-1
  Source:       .pio/build/esp32-s3-devkitc-1/firmware.bin
  S3 Key:       fu/firmware/esp32s3/firmware_20251216_143022.bin
  Project:      esp32-s3-devkitc-1
============================================================
Uploading to s3://my-firmware-bucket/fu/firmware/esp32s3/firmware_20251216_143022.bin...
Uploading latest version to s3://my-firmware-bucket/fu/firmware/esp32s3/firmware_latest.bin...
✅ Upload successful!
   Versioned: s3://my-firmware-bucket/fu/firmware/esp32s3/firmware_20251216_143022.bin
   Latest:    s3://my-firmware-bucket/fu/firmware/esp32s3/firmware_latest.bin
============================================================
```

## トラブルシューティング

### VS Code PlatformIO拡張機能でboto3エラーが出る

**症状**: VS CodeのBuildボタンから実行すると以下のエラーが表示される
```
ERROR: boto3 not installed. ImportError: No module named 'boto3'
```

**原因**: VS Code PlatformIO拡張機能が異なるPython環境を使用している

**解決済み**: 自動的にsubprocess fallbackが実行されます
- boto3インポートに失敗した場合、自動的に正しいPython環境（PlatformIO Python）でS3アップロードを実行
- 以下のメッセージが表示されますが、正常動作です：
  ```
  INFO: Using alternative Python environment for S3 upload...
  ```
- その後、通常通りS3アップロードが実行されます

**推奨**: ターミナルから実行する方がシンプルです
```bash
pio run
```

### boto3がインストールされていない

**エラー**:
```
ERROR: boto3 not installed. Install with: pip install boto3
```

**解決方法**:
```bash
pip install boto3
```

### AWS認証情報が見つからない

**エラー**:
```
ERROR: AWS credentials not found.
       Configure credentials in ~/.aws/credentials or use IAM role
```

**解決方法**:
1. `aws configure` を実行
2. または環境変数 `AWS_ACCESS_KEY_ID` と `AWS_SECRET_ACCESS_KEY` を設定

### S3バケットへのアクセス権限がない

**エラー**:
```
ERROR: AWS S3 upload failed (AccessDenied)
       Access Denied
```

**解決方法**:
1. IAMユーザーに適切なS3権限を付与
2. バケット名とプレフィックスが正しいか確認

### S3_BUCKET環境変数が未設定

**警告** (ビルドは成功):
```
WARNING: S3_BUCKET not set. Skipping S3 upload.
```

**解決方法**:
```bash
export S3_BUCKET="your-bucket-name"
```

## ファイル構成

```
mqtt_plus_test/
├── platformio.ini              # extra_scripts設定を含む
├── scripts/
│   ├── upload_to_s3.py         # S3アップロードスクリプト（メイン）
│   └── upload_to_s3_wrapper.py # Subprocess fallback用ラッパー
├── .vscode/
│   └── settings.json           # VS Code PlatformIO拡張機能設定
└── S3_UPLOAD_README.md         # このファイル
```

### スクリプトの動作

1. **通常動作**: `upload_to_s3.py` がboto3を直接インポートしてS3アップロード実行
2. **Fallback動作**: boto3インポートに失敗した場合、`upload_to_s3_wrapper.py` をPlatformIOのPython環境で実行
   - VS Code PlatformIO拡張機能使用時に自動的にfallbackが実行されます
   - ユーザーは何もする必要がありません

## セキュリティ注意事項

⚠️ **AWS認証情報をGitリポジトリにコミットしないこと**

- `.gitignore`に以下を追加推奨:
  ```
  # AWS credentials
  .aws/
  credentials
  config
  ```

- 環境変数や`~/.aws/credentials`で管理すること
- プロジェクトファイルにハードコードしないこと

## 参考リンク

- [PlatformIO Scripting](https://docs.platformio.org/en/latest/scripting/index.html)
- [boto3 Documentation](https://boto3.amazonaws.com/v1/documentation/api/latest/index.html)
- [AWS S3 Documentation](https://docs.aws.amazon.com/s3/)
