#include "wifi_manager.h"
#include <WiFi.h>
#include <config.h>
#include <Preferences.h>

static Preferences preferences;
static WifiModeEx g_mode = WifiModeEx::OFF;
static String g_staSsid = DEFAULT_STA_SSID;
static String g_staPass = DEFAULT_STA_PASS;
static String g_apSsid  = DEFAULT_AP_SSID;
static String g_apPass  = DEFAULT_AP_PASS;
static WifiStatus g_status{WifiModeEx::OFF, false, IPAddress(), 0};
static SemaphoreHandle_t g_mutex;
static TaskHandle_t g_taskHandle = nullptr;

static void applyModeLocked(WifiModeEx mode) {
  // assumes g_mutex held
  if (g_mode == mode) return;

  // Bring WiFi down first
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  g_status = {mode, false, IPAddress(), 0};

  if (mode == WifiModeEx::OFF) {
    // nothing else
    g_mode = mode;
    return;
  }

  if (mode == WifiModeEx::AP) {
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(g_apSsid.c_str(), g_apPass.c_str());
    if (ok) {
      g_status.ip = WiFi.softAPIP();
    }
    g_status.connected = ok; // AP up treated as connected
    g_mode = mode;
    return;
  }

  if (mode == WifiModeEx::STA) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    if (g_staSsid.length()) {
      WiFi.begin(g_staSsid.c_str(), g_staPass.c_str());
    }
    g_mode = mode;
    return;
  }
}

static void WifiTask(void *pv) {
  for (;;) {
    // update status
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    WifiModeEx mode = g_mode;
    if (mode == WifiModeEx::STA) {
      wl_status_t st = WiFi.status();
      bool conn = (st == WL_CONNECTED);
      g_status.connected = conn;
      if (conn) {
        g_status.ip = WiFi.localIP();
        g_status.rssi = WiFi.RSSI();
      } else {
        g_status.ip = IPAddress();
        g_status.rssi = 0;
      }
    } else if (mode == WifiModeEx::AP) {
      g_status.connected = true;
      g_status.ip = WiFi.softAPIP();
      g_status.rssi = 0;
    } else {
      g_status.connected = false;
      g_status.ip = IPAddress();
      g_status.rssi = 0;
    }
    xSemaphoreGive(g_mutex);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void wifiManagerBegin() {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  
  // Load saved WiFi credentials from flash
  wifiLoadStaCredentials();
  
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  applyModeLocked(WifiModeEx::STA);
  xSemaphoreGive(g_mutex);

  if (!g_taskHandle) {
    xTaskCreatePinnedToCore(WifiTask, "WifiTask", 4096, nullptr, 1, &g_taskHandle, 0);
  }
}

void wifiSetMode(WifiModeEx mode) {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  applyModeLocked(mode);
  xSemaphoreGive(g_mutex);
}

void wifiSetStaCredentials(const String &ssid, const String &pass) {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_staSsid = ssid;
  g_staPass = pass;
  xSemaphoreGive(g_mutex);
}

void wifiSetApCredentials(const String &ssid, const String &pass) {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_apSsid = ssid;
  g_apPass = pass;
  xSemaphoreGive(g_mutex);
}

WifiStatus wifiGetStatus() {
  WifiStatus s;
  if (!g_mutex) return {WifiModeEx::OFF, false, IPAddress(), 0};
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  s = g_status;
  xSemaphoreGive(g_mutex);
  return s;
}

bool wifiSaveStaCredentials(const String &ssid, const String &pass) {
  preferences.begin("wifi", false);
  bool success = true;
  
  if (!preferences.putString("sta_ssid", ssid)) {
    success = false;
  }
  if (!preferences.putString("sta_pass", pass)) {
    success = false;
  }
  
  preferences.end();
  
  if (success) {
    // Update runtime credentials
    if (!g_mutex) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_staSsid = ssid;
    g_staPass = pass;
    xSemaphoreGive(g_mutex);
  }
  
  return success;
}

void wifiLoadStaCredentials() {
  preferences.begin("wifi", true); // read-only
  
  String savedSsid = preferences.getString("sta_ssid", "");
  String savedPass = preferences.getString("sta_pass", "");
  
  preferences.end();
  
  if (savedSsid.length() > 0) {
    g_staSsid = savedSsid;
    g_staPass = savedPass;
  }
}

void wifiGetStaCredentials(String &ssid, String &pass) {
  if (!g_mutex) {
    ssid = "";
    pass = "";
    return;
  }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  ssid = g_staSsid;
  pass = g_staPass;
  xSemaphoreGive(g_mutex);
}
