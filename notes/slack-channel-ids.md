# Slack チャンネルID一覧

**最終更新**: 2025-10-17

## 利用可能なチャンネル

| チャンネル名 | チャンネルID | 用途 |
|-------------|-------------|------|
| **fragmented_unity** | `C0806K7E9RD` | FU-Nagoya プロジェクト本番チャンネル |
| **api_test** | `C09JBAYQZ7Z` | API動作テスト用チャンネル |

## 投稿方法

### Python3使用（推奨）

```python
import urllib.request
import json

token = "xoxb-..."  # SLACK_BOT_TOKEN
channel = "C09JBAYQZ7Z"  # api_test

message = {
    "channel": channel,
    "text": "メッセージ本文",
    "username": "CIC"
}

url = "https://slack.com/api/chat.postMessage"
data = json.dumps(message).encode('utf-8')

req = urllib.request.Request(url, data=data)
req.add_header("Authorization", f"Bearer {token}")
req.add_header("Content-Type", "application/json; charset=utf-8")

with urllib.request.urlopen(req) as response:
    result = json.loads(response.read().decode())
    print(result)
```

## メンション

- suzukishohei: `<@U07QCFP3V1U>`

## 参考

- [CIC Slack Bot人格設定](../../docs/rules/slack-bot-personality.md)
- [Slack API Documentation](https://api.slack.com/methods/chat.postMessage)
