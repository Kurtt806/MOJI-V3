#include "ota_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <config.h>
#if defined(ESP_PLATFORM)
#include <esp_partition.h>
#endif

static volatile int ota_state = 0;     // 0=IDLE,1=CHECK,2=DOWNLOADING,3=SUCCESS,4=FAILED
static volatile int ota_progress = -1; // 0..100 or -1
static char ota_msg[64] = {0};

static void ota_setState(int s)
{
  ota_state = s;
}
static void ota_setProgress(int p)
{
  ota_progress = p;
}
static void ota_setMsg(const char *m)
{
  if (!m)
  {
    ota_msg[0] = '\0';
    return;
  }
  strncpy(ota_msg, m, sizeof(ota_msg) - 1);
  ota_msg[sizeof(ota_msg) - 1] = '\0';
}

// New APIs for status reporting used by drawSetting()
int otaGetState()
{
  return ota_state;
}
int otaGetProgress()
{
  return ota_progress;
}
const char *otaGetMessage()
{
  return ota_msg;
}

void otaBegin()
{

  // --- Local OTA (ArduinoOTA) ---
  ArduinoOTA.setHostname(DEFAULT_OTA_HOSTNAME);
  if (String(DEFAULT_OTA_PASSWORD).length() > 0)
  {
    ArduinoOTA.setPassword(DEFAULT_OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});

  ArduinoOTA.begin();

  // init ota status
  ota_setState(0);
  ota_setProgress(-1);
  ota_setMsg("Idle");
}

void otaLoop()
{
  if (WiFi.isConnected())
  {
    ArduinoOTA.handle(); // xử lý OTA local
  }
}

// --- OTA từ server HTTP ---
bool otaHttpUpdate(const char *fwInfoUrl)
{
  if (!WiFi.isConnected())
  {
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("WiFi disconnected");
    return false;
  }

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(fwInfoUrl))
  {
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("http begin fail");
    return false;
  }

  ota_setState(1); // CHECK
  ota_setProgress(0);
  ota_setMsg("Checking");

  int code = http.GET();
  if (code != HTTP_CODE_OK)
  {
    Serial.printf("[HTTP OTA] Lỗi HTTP code %d\n", code);
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("HTTP meta failed");
    http.end();
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, http.getStream()))
  {
    Serial.println("[HTTP OTA] Lỗi parse JSON");
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("JSON parse fail");
    http.end();
    return false;
  }
  http.end();

  String newVer = doc["version"].as<String>();
  String url = doc["url"].as<String>();

  // So sánh version (hỗ trợ semantic version, bỏ tiền tố 'v')
  auto normalize = [](String s) -> String
  {
    s.trim();
    if (s.length() && (s[0] == 'v' || s[0] == 'V'))
      s = s.substring(1);
    return s;
  };
  auto cmpVer = [&](const String &a_, const String &b_) -> int
  {
    String a = normalize(a_), b = normalize(b_);
    int ia = 0, ib = 0;
    while (ia < a.length() || ib < b.length())
    {
      long va = 0, vb = 0;
      while (ia < a.length() && isDigit(a[ia]))
      {
        va = va * 10 + (a[ia] - '0');
        ia++;
      }
      if (ia < a.length() && a[ia] == '.')
        ia++;
      while (ib < b.length() && isDigit(b[ib]))
      {
        vb = vb * 10 + (b[ib] - '0');
        ib++;
      }
      if (ib < b.length() && b[ib] == '.')
        ib++;
      if (va < vb)
        return -1;
      if (va > vb)
        return 1;
    }
    return 0;
  };

  if (cmpVer(newVer, String(CURRENT_VERSION)) <= 0)
  {
    String s = "Đang chạy bản mới nhất: " + String(CURRENT_VERSION);
    Serial.println("[HTTP OTA] " + s);
    ota_setState(0);
    ota_setProgress(-1);
    ota_setMsg(s.c_str());
    return false;
  }

  {
    String s = "New Fw: " + newVer;
    Serial.println("[HTTP OTA] " + s);
    ota_setMsg(s.c_str());
  }

  // --- Tải firmware ---
  HTTPClient httpFw;
  WiFiClientSecure *client = new WiFiClientSecure();
  client->setInsecure();

  httpFw.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!httpFw.begin(*client, url))
  {
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("httpFW begin fail");
    delete client;
    return false;
  }

  int fwCode = httpFw.GET();
  if (fwCode != HTTP_CODE_OK)
  {
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("FW HTTP failed");
    httpFw.end();
    delete client;
    return false;
  }

  int len = httpFw.getSize();
  char oledMsg[32];

  WiFiClient *stream = httpFw.getStreamPtr();

  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN))
  {
    ota_setState(4);
    ota_setProgress(-1);
    snprintf(oledMsg, sizeof(oledMsg), "FW size: %d bytes", len);
    ota_setMsg(oledMsg); // Giả sử hàm này sẽ hiển thị lên OLED
    httpFw.end();
    delete client;
    return false;
  }

  ota_setState(2); // DOWNLOADING
  ota_setProgress(0);
  ota_setMsg("Downloading");

  // Read and write in chunks to provide progress
  const int bufSize = 512; // lower memory footprint
  uint8_t *buf = (uint8_t *)malloc(bufSize);
  if (!buf)
  {
    Serial.println("[HTTP OTA] No memory");
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("No memory");
    httpFw.end();
    delete client;
    return false;
  }

  size_t written = 0;
  int remaining = len;
  unsigned long lastProgressMs = 0;

  while (true)
  {
    int available = stream->available();
    if (available <= 0)
    {
      if (len > 0 && written >= (size_t)len)
        break; // done
      if (!stream->connected())
        break;
      delay(10);
      continue;
    }
    int toRead = available;
    if (toRead > bufSize)
      toRead = bufSize;
    if (len > 0 && toRead > remaining)
      toRead = remaining;

    int r = stream->readBytes((char *)buf, toRead);
    if (r <= 0)
      break;

    size_t w = Update.write(buf, r);
    if (w != (size_t)r)
    {
      Serial.println("[HTTP OTA] Write error");
      ota_setState(4);
      ota_setProgress(-1);
      ota_setMsg("Write error");
      free(buf);
      httpFw.end();
      return false;
    }
    written += w;
    if (len > 0)
    {
      remaining = len - (int)written;
      int prog = (int)((written * 100) / (size_t)len);
      // throttle progress updates to not spam
      if (millis() - lastProgressMs > 200)
      {
        ota_setProgress(prog);
        lastProgressMs = millis();
      }
    }
  }

  free(buf);

  if (Update.end())
  {
    if (Update.isFinished())
    {
      Serial.println("[HTTP OTA] Update OK, reboot...");
      ota_setState(3);
      ota_setProgress(100);
      ota_setMsg("Update OK");
      httpFw.end();
      delete client;
      delay(100);
      ESP.restart();
      return true;
    }
    else
    {
      Serial.println("[HTTP OTA] Update chưa hoàn tất");
      ota_setState(4);
      ota_setProgress(-1);
      ota_setMsg("Update not finished");
    }
  }
  else
  {
    Serial.printf("[HTTP OTA] Update lỗi: %s\n", Update.errorString());
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg(Update.errorString());
  }

  httpFw.end();
  delete client;
  return false;
}

// Download a filesystem image (spiffs/littlefs) from URL and write to partition by label
bool otaHttpUpdateFS(const char *fsUrl, const char *partLabel)
{
  if (!WiFi.isConnected())
  {
    Serial.println("[HTTP OTA FS] WiFi chưa kết nối");
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("WiFi disconnected");
    return false;
  }

  Serial.printf("[HTTP OTA FS] Tải image FS từ: %s -> label=%s\n", fsUrl, partLabel);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(fsUrl))
  {
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("http begin fail");
    return false;
  }

  ota_setState(1); // CHECK
  ota_setProgress(0);
  ota_setMsg("Checking FS image");

  int code = http.GET();
  if (code != HTTP_CODE_OK)
  {
    Serial.printf("[HTTP OTA FS] Lỗi HTTP code %d\n", code);
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("HTTP FS meta failed");
    http.end();
    return false;
  }

  int len = http.getSize();
  WiFiClient *stream = http.getStreamPtr();

  // Locate partition by label
#if defined(ESP_PLATFORM)
  const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partLabel);
#else
  const esp_partition_t *part = nullptr;
#endif

  if (!part)
  {
    Serial.println("[HTTP OTA FS] Không tìm thấy partition");
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("Partition not found");
    http.end();
    return false;
  }

#if defined(ESP_PLATFORM)
  // Erase partition (full)
  esp_err_t err = esp_partition_erase_range(part, 0, part->size);
  if (err != ESP_OK)
  {
    Serial.printf("[HTTP OTA FS] Lỗi erase partition: %d\n", err);
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("Partition erase fail");
    http.end();
    return false;
  }
#endif

  ota_setState(2); // DOWNLOADING
  ota_setProgress(0);
  ota_setMsg("Downloading FS");

  const int bufSize = 1024;
  uint8_t *buf = (uint8_t *)malloc(bufSize);
  if (!buf)
  {
    Serial.println("[HTTP OTA FS] No memory");
    ota_setState(4);
    ota_setProgress(-1);
    ota_setMsg("No memory");
    http.end();
    return false;
  }

  size_t written = 0;
  int remaining = len;
  unsigned long lastProgressMs = 0;

#if defined(ESP_PLATFORM)
  size_t offset = 0;
#endif

  while (true)
  {
    int available = stream->available();
    if (available <= 0)
    {
      if (len > 0 && written >= (size_t)len)
        break;
      if (!stream->connected())
        break;
      delay(10);
      continue;
    }
    int toRead = available;
    if (toRead > bufSize)
      toRead = bufSize;
    if (len > 0 && toRead > remaining)
      toRead = remaining;

    int r = stream->readBytes((char *)buf, toRead);
    if (r <= 0)
      break;

#if defined(ESP_PLATFORM)
    err = esp_partition_write(part, offset, buf, r);
    if (err != ESP_OK)
    {
      Serial.printf("[HTTP OTA FS] Write partition error: %d\n", err);
      ota_setState(4);
      ota_setProgress(-1);
      ota_setMsg("Partition write fail");
      free(buf);
      http.end();
      return false;
    }
    offset += r;
#else
    // If esp_partition API not available, try using SPIFFS file as fallback
    // (Not implemented here)
#endif

    written += r;
    if (len > 0)
    {
      remaining = len - (int)written;
      int prog = (int)((written * 100) / (size_t)len);
      if (millis() - lastProgressMs > 200)
      {
        ota_setProgress(prog);
        lastProgressMs = millis();
      }
    }
  }

  free(buf);

  http.end();

  // Finalize
#if defined(ESP_PLATFORM)
  Serial.println("[HTTP OTA FS] Write complete, restarting...");
  ota_setState(3);
  ota_setProgress(100);
  ota_setMsg("FS update OK");
  delay(200);
  ESP.restart();
  return true;
#else
  ota_setState(4);
  ota_setProgress(-1);
  ota_setMsg("Platform not supported");
  return false;
#endif
}