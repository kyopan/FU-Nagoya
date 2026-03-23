#include "ota_handler.h"
#include <mbedtls/md.h>

#ifdef ENABLE_DEBUG_SERIAL
#define OTA_DEBUG_PRINT(x) Serial.print(x)
#define OTA_DEBUG_PRINTLN(x) Serial.println(x)
#define OTA_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define OTA_DEBUG_PRINT(x)
#define OTA_DEBUG_PRINTLN(x)
#define OTA_DEBUG_PRINTF(...)
#endif

OTAHandler::OTAHandler(PubSubClient& mqtt, const char* deviceMac)
  : _mqtt(mqtt), _deviceMac(deviceMac), _status(OTA_IDLE), _progress(0) {

  // MQTT Topic構築
  _progressTopic = "fu/ota/progress/" + _deviceMac;
  _completeTopic = "fu/ota/complete/" + _deviceMac;
  _errorTopic = "fu/ota/error/" + _deviceMac;
}

// MQTT OTAコマンド受信処理
void OTAHandler::handleOTACommand(const char* payload, unsigned int length) {
  if (_status != OTA_IDLE) {
    OTA_DEBUG_PRINTLN("[OTA] Already in progress - ignoring new request");
    return;
  }

  // JSONパース
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    OTA_DEBUG_PRINTF("[OTA] JSON parse error: %s\n", error.c_str());
    reportProgress(OTA_FAILED, 0, "JSON parse error");
    return;
  }

  // OTA情報抽出
  _updateInfo.firmwareId = doc["firmwareId"] | 0;
  _updateInfo.version = doc["version"] | "";
  _updateInfo.url = doc["url"] | "";
  _updateInfo.checksum = doc["checksum"] | "";
  _updateInfo.fileSize = doc["fileSize"] | 0;
  _updateInfo.timestamp = doc["timestamp"] | 0;

  // 検証
  if (_updateInfo.url.isEmpty() || _updateInfo.fileSize == 0) {
    OTA_DEBUG_PRINTLN("[OTA] Invalid OTA command - missing url or fileSize");
    reportProgress(OTA_FAILED, 0, "Invalid OTA command");
    return;
  }

  OTA_DEBUG_PRINTF("[OTA] Firmware update requested: v%s (%d bytes)\n",
                   _updateInfo.version.c_str(), _updateInfo.fileSize);
  OTA_DEBUG_PRINTF("[OTA] URL: %s\n", _updateInfo.url.c_str());

  // OTA開始（次のprocess()で実行）
  _status = OTA_DOWNLOADING;
  _progress = 0;
  reportProgress(OTA_DOWNLOADING, 0);
}

// OTA更新実行（非ブロッキング）
void OTAHandler::process() {
  if (_status == OTA_DOWNLOADING) {
    if (performOTA()) {
      reportProgress(OTA_SUCCESS, 100);
      OTA_DEBUG_PRINTLN("[OTA] Update successful - restarting...");
      delay(2000);
      ESP.restart();
    } else {
      reportProgress(OTA_FAILED, _progress, "OTA failed");
      _status = OTA_IDLE;
    }
  }
}

// OTA実行
bool OTAHandler::performOTA() {
  OTA_DEBUG_PRINTLN("[OTA] Starting OTA update...");

  bool success = downloadAndFlash(_updateInfo.url.c_str(),
                                  _updateInfo.fileSize,
                                  _updateInfo.checksum.c_str());

  if (success) {
    OTA_DEBUG_PRINTLN("[OTA] Update completed successfully");
    _status = OTA_SUCCESS;
    return true;
  } else {
    OTA_DEBUG_PRINTLN("[OTA] Update failed");
    _status = OTA_FAILED;
    return false;
  }
}

// HTTPSダウンロード＆フラッシュ書き込み
bool OTAHandler::downloadAndFlash(const char* url, int expectedSize, const char* expectedChecksum) {
  OTA_DEBUG_PRINTF("[OTA] URL length: %d\n", strlen(url));
  OTA_DEBUG_PRINTF("[OTA] Expected size: %d bytes\n", expectedSize);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(30000);  // 30秒タイムアウト

  OTA_DEBUG_PRINTLN("[OTA] Sending HTTP GET request...");
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    OTA_DEBUG_PRINTF("[OTA] HTTP GET failed: %d\n", httpCode);
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength != expectedSize) {
    OTA_DEBUG_PRINTF("[OTA] Size mismatch: expected %d, got %d\n", expectedSize, contentLength);
    http.end();
    return false;
  }

  // Update.h初期化
  if (!Update.begin(contentLength)) {
    OTA_DEBUG_PRINTF("[OTA] Update.begin failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  OTA_DEBUG_PRINTF("[OTA] Downloading %d bytes...\n", contentLength);
  reportProgress(OTA_DOWNLOADING, 0);

  // ストリームダウンロード＆書き込み
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buff[512];
  int lastProgress = 0;

  _status = OTA_FLASHING;

  while (http.connected() && written < contentLength) {
    size_t available = stream->available();
    if (available) {
      int c = stream->readBytes(buff, min(available, sizeof(buff)));
      if (c > 0) {
        if (Update.write(buff, c) != c) {
          OTA_DEBUG_PRINTLN("[OTA] Write failed");
          http.end();
          return false;
        }
        written += c;

        // 進捗報告（10%刻み）
        int progress = (written * 100) / contentLength;
        if (progress >= lastProgress + 10 || progress == 100) {
          _progress = progress;
          reportProgress(OTA_FLASHING, progress);
          lastProgress = progress;
          OTA_DEBUG_PRINTF("[OTA] Progress: %d%% (%d/%d bytes)\n",
                           progress, written, contentLength);
        }
      }
    }
    delay(1);  // WDTフィード
  }

  http.end();

  if (written != contentLength) {
    OTA_DEBUG_PRINTF("[OTA] Incomplete download: %d/%d\n", written, contentLength);
    return false;
  }

  // チェックサム検証（オプション）
  // TODO: ダウンロードしたデータのチェックサム計算と検証

  // フラッシュ完了
  if (!Update.end(true)) {
    OTA_DEBUG_PRINTF("[OTA] Update.end failed: %s\n", Update.errorString());
    return false;
  }

  if (!Update.isFinished()) {
    OTA_DEBUG_PRINTLN("[OTA] Update not finished");
    return false;
  }

  OTA_DEBUG_PRINTLN("[OTA] Firmware flashed successfully");
  return true;
}

// 進捗報告（MQTT）
void OTAHandler::reportProgress(OTAStatus status, int progress, const char* error) {
  if (!_mqtt.connected()) {
    return;
  }

  JsonDocument doc;

  // ステータス変換
  switch (status) {
    case OTA_DOWNLOADING:
      doc["status"] = "downloading";
      break;
    case OTA_FLASHING:
      doc["status"] = "flashing";
      break;
    case OTA_SUCCESS:
      doc["status"] = "success";
      break;
    case OTA_FAILED:
      doc["status"] = "failed";
      break;
    default:
      doc["status"] = "pending";
  }

  doc["progress"] = progress;
  if (error) {
    doc["error_message"] = error;
  }

  char buffer[256];
  serializeJson(doc, buffer, sizeof(buffer));

  // 進捗報告
  _mqtt.publish(_progressTopic.c_str(), buffer);

  // 完了/エラー通知
  if (status == OTA_SUCCESS) {
    _mqtt.publish(_completeTopic.c_str(), buffer);
  } else if (status == OTA_FAILED) {
    _mqtt.publish(_errorTopic.c_str(), buffer);
  }

  OTA_DEBUG_PRINTF("[OTA] Progress reported: %s\n", buffer);
}

// MD5チェックサム計算（未使用だが将来用）
String OTAHandler::calculateChecksum(uint8_t* data, size_t length) {
  uint8_t hash[16];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_MD5;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, data, length);
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  char checksum[33];
  for (int i = 0; i < 16; i++) {
    sprintf(&checksum[i * 2], "%02x", hash[i]);
  }
  checksum[32] = '\0';

  return String(checksum);
}
