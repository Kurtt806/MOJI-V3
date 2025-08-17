#include "ota_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <config.h>


void otaBegin() {

  // --- Local OTA (ArduinoOTA) ---
  ArduinoOTA.setHostname(DEFAULT_OTA_HOSTNAME);
  if (String(DEFAULT_OTA_PASSWORD).length() > 0) {
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
  });
  ArduinoOTA.onEnd([]() {
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  });
  ArduinoOTA.onError([](ota_error_t error) {
  });

  ArduinoOTA.begin();
}

void otaLoop() {
  if (WiFi.isConnected()) {
    ArduinoOTA.handle();  // xử lý OTA local
  }
}

// --- OTA từ server HTTP ---
bool otaHttpUpdate(const char *fwInfoUrl) {
  if (!WiFi.isConnected()) {
    Serial.println("[HTTP OTA] WiFi chưa kết nối");
    return false;
  }

  HTTPClient http;
  Serial.printf("[HTTP OTA] Kiểm tra firmware: %s\n", fwInfoUrl);
  if (!http.begin(fwInfoUrl)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[HTTP OTA] Lỗi HTTP code %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, http.getStream())) {
    Serial.println("[HTTP OTA] Lỗi parse JSON");
    http.end();
    return false;
  }
  http.end();

  String newVer = doc["version"].as<String>();
  String url    = String(SERVER_BASE_URL) + doc["url"].as<String>();

  if (newVer == CURRENT_VERSION) {
    Serial.println("[HTTP OTA] Đang chạy bản mới nhất: " + newVer);
    return false;
  }

  Serial.println("[HTTP OTA] Có bản mới: " + newVer + " tải từ " + url);

  HTTPClient httpFw;
  if (!httpFw.begin(url)) return false;
  int fwCode = httpFw.GET();
  if (fwCode != HTTP_CODE_OK) {
    Serial.printf("[HTTP OTA] Lỗi tải firmware code %d\n", fwCode);
    httpFw.end();
    return false;
  }

  int len = httpFw.getSize();
  WiFiClient * stream = httpFw.getStreamPtr();

  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) {
    Serial.println("[HTTP OTA] Update.begin() fail");
    httpFw.end();
    return false;
  }

  size_t written = Update.writeStream(*stream);
  if (written > 0) {
    Serial.printf("[HTTP OTA] Đã ghi %d byte\n", (int)written);
  }

  if (Update.end()) {
    if (Update.isFinished()) {
      Serial.println("[HTTP OTA] Update OK, reboot...");
      ESP.restart();
      return true;
    } else {
      Serial.println("[HTTP OTA] Update chưa hoàn tất");
    }
  } else {
    Serial.printf("[HTTP OTA] Update lỗi: %s\n", Update.errorString());
  }

  httpFw.end();
  return false;
}
