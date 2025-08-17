


#include "web_draw_server.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <config.h>
#include "app_state.h"

// HTTP on port 80, WS on port 81
static AsyncWebServer http(80);
static AsyncWebSocket ws("/ws");

// Latest 1bpp frame buffer
static uint8_t g_bitmap[DRAW_BITMAP_SIZE];
static volatile bool g_hasFrame = false;
static SemaphoreHandle_t g_bitmapMutex = nullptr;

static void decodeRLEToBuffer(const uint8_t *data, size_t len) {
  if (!g_bitmapMutex) return;
  xSemaphoreTake(g_bitmapMutex, portMAX_DELAY);
  size_t out = 0;
  size_t i = 0;
  while (i + 1 < len && out < DRAW_BITMAP_SIZE) {
    uint8_t count = data[i++];
    uint8_t value = data[i++];
    size_t todo = min((size_t)count, DRAW_BITMAP_SIZE - out);
    memset(g_bitmap + out, value, todo);
    out += todo;
  }
  if (out == DRAW_BITMAP_SIZE) {
    g_hasFrame = true;
  }
  xSemaphoreGive(g_bitmapMutex);
}


const uint8_t *webDrawGetBitmap()
{
  if (!g_bitmapMutex) return nullptr;
  if (!g_hasFrame) return nullptr;
  return g_bitmap;
}

bool webDrawCopyBitmap(uint8_t *out, size_t len)
{
  if (!out || len < DRAW_BITMAP_SIZE || !g_bitmapMutex) return false;
  if (!g_hasFrame) return false;
  xSemaphoreTake(g_bitmapMutex, portMAX_DELAY);
  memcpy(out, g_bitmap, DRAW_BITMAP_SIZE);
  xSemaphoreGive(g_bitmapMutex);
  return true;
}

bool webDrawLoadFromFlash()
{
  if (!SPIFFS.exists("/saved_draw.bin")) return false;
  File file = SPIFFS.open("/saved_draw.bin", FILE_READ);
  if (!file) return false;
  if (file.size() != DRAW_BITMAP_SIZE) { file.close(); return false; }

  if (!g_bitmapMutex) { file.close(); return false; }

  xSemaphoreTake(g_bitmapMutex, portMAX_DELAY);
  size_t bytesRead = file.readBytes((char *)g_bitmap, DRAW_BITMAP_SIZE);
  file.close();

  if (bytesRead == DRAW_BITMAP_SIZE) {
    g_hasFrame = true;
    xSemaphoreGive(g_bitmapMutex);
    return true;
  } else {
    xSemaphoreGive(g_bitmapMutex);
    return false;
  }
}


static void handleRoot(AsyncWebServerRequest *request) {
  http.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
}

static void handleUploadPage(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/upload.html", "text/html");
}

static void handleWifiPage(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/wifi.html", "text/html");
}

static void handleUploadPost(AsyncWebServerRequest *request, 
                              String filename, size_t index, 
                              uint8_t *data, size_t len, bool final) {
  static File uploadFile;
  if (!index) {
    if (SPIFFS.exists("/anim.gif")) SPIFFS.remove("/anim.gif");
    uploadFile = SPIFFS.open("/anim.gif", FILE_WRITE);
  }
  if (uploadFile) uploadFile.write(data, len);
  if (final && uploadFile) {
    uploadFile.close();
    request->redirect("/upload");
  }
}

static void handleSaveDraw(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (len != DRAW_BITMAP_SIZE) {
    request->send(400, "text/plain", "Invalid bitmap size");
    return;
  }
  File file = SPIFFS.open("/saved_draw.bin", FILE_WRITE);
  if (!file) {
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

static void handleLoadDraw(AsyncWebServerRequest *request) {
  if (!SPIFFS.exists("/saved_draw.bin")) {
    request->send(404, "text/plain", "No saved drawing found");
    return;
  }
  File file = SPIFFS.open("/saved_draw.bin", FILE_READ);
  if (!file) {
    request->send(500, "text/plain", "Cannot open saved file");
    return;
  }
  if (file.size() != DRAW_BITMAP_SIZE) {
    file.close();
    request->send(400, "text/plain", "Invalid saved file size");
    return;
  }
  AsyncWebServerResponse *res = request->beginResponse(SPIFFS, "/saved_draw.bin", "application/octet-stream");
  request->send(res);
}

static void handleWifiStatus(AsyncWebServerRequest *request) {
  WifiStatus status = wifiGetStatus();
  String ssid, pass;
  wifiGetStaCredentials(ssid, pass);
  String json = "{";
  json += "\"connected\":" + String(status.connected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + ssid + "\",";
  json += "\"ip\":\"" + status.ip.toString() + "\",";
  json += "\"rssi\":" + String(status.rssi);
  json += "}";
  request->send(200, "application/json", json);
}

static void handleWifiScan(AsyncWebServerRequest *request) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    request->send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (n < 0) { 
    WiFi.scanNetworks(true, true); // bắt đầu scan
    request->send(202, "application/json", "{\"status\":\"started\"}");
    return;
  }

  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
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


static void handleWifiConfigPost(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid", true)) {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing SSID\"}");
    return;
  }
  String ssid = request->getParam("ssid", true)->value();
  String password = request->getParam("password", true) ? request->getParam("password", true)->value() : "";
  if (ssid.length() == 0) {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID cannot be empty\"}");
    return;
  }
  if (ssid.length() > 32) {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID too long\"}");
    return;
  }
  if (password.length() > 64) {
    request->send(400, "application/json", "{\"success\":false,\"message\":\"Password too long\"}");
    return;
  }
  bool success = wifiSaveStaCredentials(ssid, password);
  if (success) {
    wifiSetMode(WifiModeEx::STA);
    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi configuration saved\"}");
  } else {
    request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save configuration\"}");
  }
}

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                      void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_BINARY) {
      changeState(AppState::DRAW);
      decodeRLEToBuffer(data, len);
    }
  }
}

void webDrawServerBegin() {
  if (!g_bitmapMutex) g_bitmapMutex = xSemaphoreCreateMutex();

  http.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  http.on("/", HTTP_GET, handleRoot);
  http.on("/upload", HTTP_GET, handleUploadPage);
  http.onFileUpload(handleUploadPost);
  http.on("/save-draw", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL, handleSaveDraw);
  http.on("/load-draw", HTTP_GET, handleLoadDraw);

  http.on("/wifi-config", HTTP_GET, handleWifiPage);
  http.on("/wifi-config", HTTP_POST, handleWifiConfigPost);
  http.on("/wifi-status", HTTP_GET, handleWifiStatus);
  http.on("/wifi-scan", HTTP_GET, handleWifiScan);

  ws.onEvent(onWsEvent);
  http.addHandler(&ws);

  http.begin();
}

void webDrawServerLoop() {
  // Không cần http.handleClient() nữa
}
