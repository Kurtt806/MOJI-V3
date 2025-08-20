#pragma once
#include <Arduino.h>

void otaBegin();   // khởi tạo cả ArduinoOTA và HTTP OTA
void otaLoop();    // chạy trong loop() để xử lý ArduinoOTA
bool otaHttpUpdate(const char *fwInfoUrl); // gọi khi cần check OTA qua server
// Tải và ghi image partition (ví dụ: spiffs) từ URL -> partition label
bool otaHttpUpdateFS(const char *fsUrl, const char *partLabel);

int otaGetState();
int otaGetProgress();
const char *otaGetMessage();