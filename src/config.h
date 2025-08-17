// ==================== CONFIGURATION ====================
#include <Arduino.h>

#define BUTTON_PIN 3
#define I2C_SDA 8
#define I2C_SCL 9

#define I2C_FREQ 400000
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

#define BITMAP_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

// ===== WiFi defaults =====
// Note: Change these to your local WiFi credentials if you want STA mode to auto-connect.
#ifndef DEFAULT_STA_SSID
#define DEFAULT_STA_SSID "Dang khoa"
#endif
#ifndef DEFAULT_STA_PASS
#define DEFAULT_STA_PASS "12345678"
#endif

#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID "MOJI-V3"
#endif
#ifndef DEFAULT_AP_PASS
#define DEFAULT_AP_PASS "12345678"
#endif

// ===== OTA defaults =====
#ifndef DEFAULT_OTA_HOSTNAME
#define DEFAULT_OTA_HOSTNAME "MOJI-V3"
#endif
#ifndef DEFAULT_OTA_PASSWORD
#define DEFAULT_OTA_PASSWORD ""
#endif
// #define SERVER_BASE_URL   "http://ota.vi3d.io.vn"
#define SERVER_BASE_URL   "http://ota.vi3d.io.vn/mojiv3/"
#define CURRENT_VERSION   "3.0.2"

// ===== device =====
#define LOGO "MOJI V3"
#define VERSION CURRENT_VERSION
#define MAC_KEY 46667666739340 // đen