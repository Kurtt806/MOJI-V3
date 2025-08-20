#include "web_server.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <config.h>
#include "app_state.h"

// accessors implemented in main.cpp
extern void setTextFontIndex(uint8_t idx);
extern uint8_t getTextFontIndex();
extern const char *textFontNames[];
extern const uint8_t textFontCount;

// HTTP on port 80, WS on port 81
static AsyncWebServer http(80);
static AsyncWebSocket ws("/ws");

// Latest 1bpp frame buffer
static uint8_t g_bitmap[DRAW_BITMAP_SIZE];
static volatile bool g_hasFrame = false;
static SemaphoreHandle_t g_bitmapMutex = nullptr;

static void decodeRLEToBuffer(const uint8_t *data, size_t len)
{
  if (!g_bitmapMutex)
    return;
  xSemaphoreTake(g_bitmapMutex, portMAX_DELAY);
  size_t out = 0;
  size_t i = 0;
  while (i + 1 < len && out < DRAW_BITMAP_SIZE)
  {
    uint8_t count = data[i++];
    uint8_t value = data[i++];
    size_t todo = min((size_t)count, DRAW_BITMAP_SIZE - out);
    memset(g_bitmap + out, value, todo);
    out += todo;
  }
  if (out == DRAW_BITMAP_SIZE)
  {
    g_hasFrame = true;
  }
  xSemaphoreGive(g_bitmapMutex);
}

const uint8_t *webDrawGetBitmap()
{
  if (!g_bitmapMutex)
    return nullptr;
  if (!g_hasFrame)
    return nullptr;
  return g_bitmap;
}

bool webDrawCopyBitmap(uint8_t *out, size_t len)
{
  if (!out || len < DRAW_BITMAP_SIZE || !g_bitmapMutex)
    return false;
  if (!g_hasFrame)
    return false;
  xSemaphoreTake(g_bitmapMutex, portMAX_DELAY);
  memcpy(out, g_bitmap, DRAW_BITMAP_SIZE);
  xSemaphoreGive(g_bitmapMutex);
  return true;
}

bool webDrawLoadFromFlash()
{
  if (!SPIFFS.exists("/saved_draw.bin"))
    return false;
  File file = SPIFFS.open("/saved_draw.bin", FILE_READ);
  if (!file)
    return false;
  if (file.size() != DRAW_BITMAP_SIZE)
  {
    file.close();
    return false;
  }

  if (!g_bitmapMutex)
  {
    file.close();
    return false;
  }

  xSemaphoreTake(g_bitmapMutex, portMAX_DELAY);
  size_t bytesRead = file.readBytes((char *)g_bitmap, DRAW_BITMAP_SIZE);
  file.close();

  if (bytesRead == DRAW_BITMAP_SIZE)
  {
    g_hasFrame = true;
    xSemaphoreGive(g_bitmapMutex);
    return true;
  }
  else
  {
    xSemaphoreGive(g_bitmapMutex);
    return false;
  }
}

static void handleRoot(AsyncWebServerRequest *request)
{
  http.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
}

static void handleUploadPage(AsyncWebServerRequest *request)
{
  request->send(SPIFFS, "/load-gif.html", "text/html");
}

// Simple in-memory mapping to remember destination file per original upload filename
struct UploadMapEntry {
  bool used = false;
  String orig;            // original client filename
  String dest;            // destination path we chose
  AsyncWebServerRequest *req = nullptr;
};

static UploadMapEntry uploadMap[8]; // small fixed map (handles concurrent uploads up to 8)

static int findUploadMapping(const String &orig)
{
  for (int i = 0; i < (int)(sizeof(uploadMap) / sizeof(uploadMap[0])); ++i)
  {
    if (uploadMap[i].used && uploadMap[i].orig == orig)
      return i;
  }
  return -1;
}

static int addUploadMapping(const String &orig, const String &dest, AsyncWebServerRequest *req)
{
  for (int i = 0; i < (int)(sizeof(uploadMap) / sizeof(uploadMap[0])); ++i)
  {
    if (!uploadMap[i].used)
    {
      uploadMap[i].used = true;
      uploadMap[i].orig = orig;
      uploadMap[i].dest = dest;
      uploadMap[i].req = req;
      return i;
    }
  }
  return -1;
}

static void removeUploadMappingAt(int idx)
{
  if (idx < 0 || idx >= (int)(sizeof(uploadMap) / sizeof(uploadMap[0])))
    return;
  uploadMap[idx].used = false;
  uploadMap[idx].orig = String();
  uploadMap[idx].dest = String();
  uploadMap[idx].req = nullptr;
}

// Find next sequential anim_NNN.gif filename in SPIFFS
// Use a simple index file to persist last used number: /anim_index.txt
static String nextAnimFilename()
{
  int idx = 0;
  if (SPIFFS.exists("/anim_index.txt"))
  {
    File f = SPIFFS.open("/anim_index.txt", FILE_READ);
    if (f)
    {
      String s = f.readString();
      idx = s.toInt();
      f.close();
    }
  }
  idx++;
  // write back
  File wf = SPIFFS.open("/anim_index.txt", FILE_WRITE);
  if (wf)
  {
    wf.print(String(idx));
    wf.close();
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "/anim_%03d.gif", idx);
  return String(buf);
}

static void handleWifiPage(AsyncWebServerRequest *request)
{
  request->send(SPIFFS, "/wifi.html", "text/html");
}

static void handleUploadPost(AsyncWebServerRequest *request,
                             String filename, size_t index,
                             uint8_t *data, size_t len, bool final)
{
  // Support multiple GIF uploads from client. We assign a sequential dest filename
  // on the first chunk for this filename and store mapping in uploadMap.
  int mapIndex = findUploadMapping(filename);
  String destPath;

  if (index == 0)
  {
    // create a new destination filename
    destPath = nextAnimFilename();
    int added = addUploadMapping(filename, destPath, request);
    if (added < 0)
    {
      // map full
      request->send(500, "text/plain", "Upload map full");
      return;
    }
    mapIndex = added;
    // remove any existing file at that dest just in case
    if (SPIFFS.exists(destPath))
      SPIFFS.remove(destPath);
  }
  else
  {
    if (mapIndex >= 0)
      destPath = uploadMap[mapIndex].dest;
    else
    {
      // unknown stream, reject
      request->send(400, "text/plain", "Unknown upload stream");
      return;
    }
  }

  File uploadFile;
  if (index == 0)
    uploadFile = SPIFFS.open(destPath, FILE_WRITE);
  else
    uploadFile = SPIFFS.open(destPath, FILE_APPEND);

  if (uploadFile)
  {
    uploadFile.write(data, len);
    uploadFile.close();
  }

  if (final)
  {
    // final chunk: respond OK for this file and free mapping
    request->send(200, "text/plain", destPath.c_str());
    // mark mapping free
    if (mapIndex >= 0)
      removeUploadMappingAt(mapIndex);
    // set GIF state (display last uploaded)
    changeState(AppState::GIF);
  }
}

static void handleSaveDraw(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (len != DRAW_BITMAP_SIZE)
  {
    request->send(400, "text/plain", "Invalid bitmap size");
    return;
  }
  File file = SPIFFS.open("/saved_draw.bin", FILE_WRITE);
  if (!file)
  {
    request->send(500, "text/plain", "Cannot open file for writing");
    return;
  }
  size_t written = file.write(data, len);
  file.close();
  if (written == len)
    request->send(200, "text/plain", "Saved successfully");
  else
    request->send(500, "text/plain", "Write error");
}
static void handleLoadDraw(AsyncWebServerRequest *request)
{
  if (!SPIFFS.exists("/saved_draw.bin"))
  {
    request->send(404, "text/plain", "No saved drawing found");
    return;
  }
  File file = SPIFFS.open("/saved_draw.bin", FILE_READ);
  if (!file)
  {
    request->send(500, "text/plain", "Cannot open saved file");
    return;
  }
  if (file.size() != DRAW_BITMAP_SIZE)
  {
    file.close();
    request->send(400, "text/plain", "Invalid saved file size");
    return;
  }
  AsyncWebServerResponse *res = request->beginResponse(SPIFFS, "/saved_draw.bin", "application/octet-stream");
  request->send(res);
}

static void handleWifiStatus(AsyncWebServerRequest *request)
{
  WifiStatus status = wifiGetStatus();
  String ssid, pass;
  wifiGetStaCredentials(ssid, pass);

  // memory / filesystem info
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t heapSize = ESP.getHeapSize();
  uint32_t maxAlloc = ESP.getMaxAllocHeap();
  size_t fsTotal = SPIFFS.totalBytes();
  size_t fsUsed = SPIFFS.usedBytes();
  // flash / sketch info
  uint32_t flashSize = 0;
  uint32_t sketchSize = 0;
  uint32_t freeSketch = 0;
#if defined(ESP)
  flashSize = ESP.getFlashChipSize();
  sketchSize = ESP.getSketchSize();
  freeSketch = ESP.getFreeSketchSpace();
#endif

  String json = "{";
  json += "\"connected\":" + String(status.connected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + ssid + "\",";
  json += "\"ip\":\"" + status.ip.toString() + "\",";
  json += "\"rssi\":" + String(status.rssi) + ",";
  json += "\"freeHeap\":" + String(freeHeap) + ",";
  json += "\"heapSize\":" + String(heapSize) + ",";
  json += "\"maxAllocHeap\":" + String(maxAlloc) + ",";
  json += "\"fsTotal\":" + String(fsTotal) + ",";
  json += "\"fsUsed\":" + String(fsUsed);
  json += ",\"flashSize\":" + String(flashSize);
  json += ",\"sketchSize\":" + String(sketchSize);
  json += ",\"freeSketchSpace\":" + String(freeSketch);
  json += "}";
  request->send(200, "application/json", json);
}
static void handleWifiScan(AsyncWebServerRequest *request)
{
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING)
  {
    request->send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (n < 0)
  {
    WiFi.scanNetworks(true, true); // bắt đầu scan
    request->send(202, "application/json", "{\"status\":\"started\"}");
    return;
  }

  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++)
  {
    if (i > 0)
      json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"encryption\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]}";
  WiFi.scanDelete();
  request->send(200, "application/json", json);
}
static void handleWifiConfigPost(AsyncWebServerRequest *request)
{
  if (!request->hasParam("ssid", true))
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing SSID\"}");
    return;
  }
  String ssid = request->getParam("ssid", true)->value();
  String password = request->getParam("password", true) ? request->getParam("password", true)->value() : "";
  if (ssid.length() == 0)
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID cannot be empty\"}");
    return;
  }
  if (ssid.length() > 32)
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID too long\"}");
    return;
  }
  if (password.length() > 64)
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"Password too long\"}");
    return;
  }
  bool success = wifiSaveStaCredentials(ssid, password);
  if (success)
  {
    wifiSetMode(WifiModeEx::STA);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi configuration saved\"}");
  }
  else
  {
    request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save configuration\"}");
  }
}

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                      void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->opcode == WS_BINARY)
    {
      changeState(AppState::DRAW);
      decodeRLEToBuffer(data, len);
    }
  }
}

#include <Update.h>
#include <esp_partition.h>

static void handleSystemInfo(AsyncWebServerRequest *request)
{
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t heapSize = ESP.getHeapSize();
  uint32_t maxAlloc = ESP.getMaxAllocHeap();
  size_t fsTotal = SPIFFS.totalBytes();
  size_t fsUsed = SPIFFS.usedBytes();

  // flash / sketch info
  uint32_t flashSize = 0;
  uint32_t sketchSize = 0;
  uint32_t freeSketch = 0;
#if defined(ESP)
  flashSize = ESP.getFlashChipSize();
  sketchSize = ESP.getSketchSize();
  freeSketch = ESP.getFreeSketchSpace();
#endif

  // Get OTA and program partition info
  const esp_partition_t* ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  const esp_partition_t* ota1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

  String json = "{";
  json += "\"freeHeap\":" + String(freeHeap) + ",";
  json += "\"heapSize\":" + String(heapSize) + ",";
  json += "\"maxAllocHeap\":" + String(maxAlloc) + ",";
  json += "\"fsTotal\":" + String(fsTotal) + ",";
  json += "\"fsUsed\":" + String(fsUsed);

  json += ",\"flashSize\":" + String(flashSize);
  json += ",\"sketchSize\":" + String(sketchSize);
  json += ",\"freeSketchSpace\":" + String(freeSketch);

  // Add OTA partition info
  if (ota0) {
    json += ",\"ota0_size\":" + String(ota0->size);
  // emit address as a decimal number (valid JSON) instead of an invalid 0x literal
  json += ",\"ota0_addr\":" + String(ota0->address);
    json += ",\"ota0_label\":\"" + String((const char*)ota0->label) + "\"";
  }
  if (ota1) {
    json += ",\"ota1_size\":" + String(ota1->size);
  // emit address as a decimal number (valid JSON)
  json += ",\"ota1_addr\":" + String(ota1->address);
    json += ",\"ota1_label\":\"" + String((const char*)ota1->label) + "\"";
  }

  json += "}";
  request->send(200, "application/json", json);
}

// GIF speed control endpoints
extern void gifPlayerSetSpeed(float speed);
extern float gifPlayerGetSpeed();

static void handleGifSpeed(AsyncWebServerRequest *request)
{
  request->send(SPIFFS, "/gif-speed.html", "text/html");
}
static void handleGifSpeedPost(AsyncWebServerRequest *request)
{
  if (!request->hasParam("speed", true))
  {
    request->send(400, "application/json", "{\"error\":\"missing speed\"}");
    return;
  }
  String val = request->getParam("speed", true)->value();
  float s = val.toFloat();
  if (s <= 0)
  {
    request->send(400, "application/json", "{\"error\":\"invalid speed\"}");
    return;
  }
  gifPlayerSetSpeed(s);
  request->send(200, "application/json", "{\"speed\":" + String(s, 2) + "}");
}

// Display text handlers: save / load the text to SPIFFS so device can show it
static void handleDisplayTextPost(AsyncWebServerRequest *request)
{
  if (!request->hasParam("text", true))
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"missing text\"}");
    return;
  }
  String txt = request->getParam("text", true)->value();
  // Limit length to avoid excessively large files
  if (txt.length() > 200)
    txt = txt.substring(0, 200);

  File f = SPIFFS.open("/display_text.txt", FILE_WRITE);
  if (!f)
  {
    request->send(500, "application/json", "{\"success\":false,\"message\":\"cannot open file\"}");
    return;
  }
  f.print(txt);
  f.close();

  request->send(200, "application/json", "{\"success\":true}");
}
static void handleDisplayTextGet(AsyncWebServerRequest *request)
{
  if (!SPIFFS.exists("/display_text.txt"))
  {
    request->send(200, "text/plain", "");
    return;
  }
  request->send(SPIFFS, "/display_text.txt", "text/plain");
}

// List available text fonts and current selection
static void handleTextFontsGet(AsyncWebServerRequest *request)
{
  String json = "{";
  json += "\"count\":" + String(textFontCount) + ",";
  json += "\"current\":" + String(getTextFontIndex()) + ",";
  json += "\"names\":[";
  for (int i = 0; i < textFontCount; i++)
  {
    if (i) json += ",";
    json += "\"" + String(textFontNames[i]) + "\"";
  }
  json += "]}";
  request->send(200, "application/json", json);
}

static void handleTextFontsPost(AsyncWebServerRequest *request)
{
  if (!request->hasParam("index", true))
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"missing index\"}");
    return;
  }
  int idx = request->getParam("index", true)->value().toInt();
  if (idx < 0 || idx >= (int)textFontCount)
  {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"invalid index\"}");
    return;
  }
  setTextFontIndex((uint8_t)idx);
  request->send(200, "application/json", "{\"success\":true}");
}

void webDrawServerBegin()
{
  if (!g_bitmapMutex)
    g_bitmapMutex = xSemaphoreCreateMutex();

  SPIFFS.begin(true);

  http.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  http.on("/", HTTP_GET, handleRoot);
  http.on("/load-gif", HTTP_GET, handleUploadPage);
  http.on("/save-gif", HTTP_POST, [](AsyncWebServerRequest *req) {}, handleUploadPost, NULL);
  http.on("/load-draw", HTTP_GET, handleLoadDraw);
  http.on("/save-draw", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL, handleSaveDraw);

  http.on("/wifi-config", HTTP_GET, handleWifiPage);
  http.on("/wifi-config", HTTP_POST, handleWifiConfigPost);
  http.on("/wifi-status", HTTP_GET, handleWifiStatus);
  http.on("/wifi-scan", HTTP_GET, handleWifiScan);

  // new system info endpoint
  http.on("/system-info", HTTP_GET, handleSystemInfo);

  // GIF speed endpoints
  http.on("/gif-speed", HTTP_GET, handleGifSpeed);
  http.on("/gif-speed", HTTP_POST, handleGifSpeedPost);

  // Display text endpoints
  http.on("/display-text", HTTP_GET, handleDisplayTextGet);
  http.on("/display-text", HTTP_POST, handleDisplayTextPost);

  // Text fonts endpoints
  http.on("/text-fonts", HTTP_GET, handleTextFontsGet);
  http.on("/text-fonts", HTTP_POST, handleTextFontsPost);

  ws.onEvent(onWsEvent);
  http.addHandler(&ws);

  http.begin();
}

void webDrawServerLoop()
{
  // Không cần http.handleClient() nữa
}
