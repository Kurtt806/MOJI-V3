// Simple WiFi manager for ESP32-C3 with STA/AP/OFF modes
#pragma once

#include <Arduino.h>

enum class WifiModeEx : uint8_t { OFF = 0, STA, AP };

struct WifiStatus {
  WifiModeEx mode;
  bool connected;       // valid for STA
  IPAddress ip;         // STA: local IP when connected, AP: softAP IP
  int rssi;             // valid for STA when connected
};

// Call once in setup, creates a task internally
void wifiManagerBegin();

// Request a mode change, thread-safe
void wifiSetMode(WifiModeEx mode);

// Optional: set credentials before switching to STA or AP
void wifiSetStaCredentials(const String &ssid, const String &pass);
void wifiSetApCredentials(const String &ssid, const String &pass);

// Save STA credentials to flash permanently
bool wifiSaveStaCredentials(const String &ssid, const String &pass);

// Load STA credentials from flash
void wifiLoadStaCredentials();

// Get current STA credentials
void wifiGetStaCredentials(String &ssid, String &pass);

// Get latest status snapshot (non-blocking)
WifiStatus wifiGetStatus();
