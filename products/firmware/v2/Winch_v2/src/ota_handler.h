#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Update.h>

// OTA状態
enum OTAStatus {
  OTA_IDLE,
  OTA_DOWNLOADING,
  OTA_FLASHING,
  OTA_SUCCESS,
  OTA_FAILED
};

// OTA更新情報
struct OTAUpdateInfo {
  int firmwareId;
  String version;
  String url;          // Presigned S3 URL
  String checksum;
  int fileSize;
  unsigned long timestamp;
};

class OTAHandler {
public:
  OTAHandler(PubSubClient& mqtt, const char* deviceMac);

  // MQTT OTAコマンド受信処理
  void handleOTACommand(const char* payload, unsigned int length);

  // OTA更新実行（非ブロッキング）
  void process();

  // 現在のOTA状態
  OTAStatus getStatus() const { return _status; }
  int getProgress() const { return _progress; }

  // 進捗報告
  void reportProgress(OTAStatus status, int progress, const char* error = nullptr);

private:
  PubSubClient& _mqtt;
  String _deviceMac;
  String _progressTopic;
  String _completeTopic;
  String _errorTopic;

  OTAStatus _status;
  int _progress;
  OTAUpdateInfo _updateInfo;

  // OTA実行
  bool performOTA();
  bool downloadAndFlash(const char* url, int expectedSize, const char* expectedChecksum);
  String calculateChecksum(uint8_t* data, size_t length);
};

#endif // OTA_HANDLER_H
