#include "otaUpdater.h"

String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

void execOTA() {
  Serial.println("Connecting to: " + String(host));
  OTAprogress = true;

  // 0〜60秒のランダムディレイを挟んで一斉接続を防ぐ
  //   unsigned long delayMs = random(0, 60000);
  unsigned long delayMs = deviceIdNumber * 2000;
  Serial.printf("Waiting %lu ms before starting OTA...\n", delayMs);
  delay(delayMs);

  WiFiClient http;
  if (http.connect(host.c_str(), port)) {
    Serial.println("Fetching Bin: " + String(bin));
    http.print(String("GET ") + bin + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Cache-Control: no-cache\r\n" +
               "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (http.available() == 0) {
      if (millis() - timeout > 10000) {
        Serial.println("http Timeout !");
        http.stop();
        OTAprogress = false;
        return;
      }
    }

    while (http.available()) {
      String line = http.readStringUntil('\n');
      line.trim();
      if (!line.length()) {
        // ヘッダ終了
        break;
      }
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }
      if (line.startsWith("Content-Length: ")) {
        contentLength = atol((getHeaderValue(line, "Content-Length: ")).c_str());
        Serial.println("Got " + String(contentLength) + " bytes from server");
      }
      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        Serial.println("Got " + contentType + " payload.");
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    }
  } else {
    Serial.println("Connection to " + String(host) + " failed. Please check your setup");
    if (retryCount < maxRetry) {
      retryCount++;
      execOTA();
    } else {
      Serial.println("Max retries reached. OTA failed.");
      ESP.restart();
    }
  }

  Serial.println("contentLength : " + String(contentLength) +
                 ", isValidContentType : " + String(isValidContentType));

  if (contentLength && isValidContentType) {
    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      Serial.println("Begin OTA. This may take a while...");
      size_t written = Update.writeStream(http);

      if (written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
      } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
      }

      if (Update.end()) {
        Serial.println("OTA done!");
        if (Update.isFinished()) {
          Serial.println("Update successfully completed. Rebooting.");
          ESP.restart();
        } else {
          Serial.println("Update not finished? Something went wrong!");
          ESP.restart();
        }
      } else {
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
        ESP.restart();
      }
    } else {
      Serial.println("Not enough space to begin OTA");
      http.clear();
    }
  } else {
    Serial.println("There was no content in the response");
    http.clear();
  }
}
