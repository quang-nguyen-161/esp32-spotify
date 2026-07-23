#pragma once
#include <Arduino.h>

// ============================================================
// WifiPortal
// - Saves SSID/password to NVS (Preferences library, namespace "wifi")
// - If no credentials are stored yet (or connection fails), automatically
//   starts an AP + captive portal so the user can enter WiFi credentials
//   from a phone/laptop.
// - The captive portal auto-redirects to the config page on most devices
//   (Android/iOS/Windows) because the DNS server returns the ESP32's IP
//   for EVERY domain.
// ============================================================
namespace WifiPortal {

  static const char* AP_SSID_DEFAULT = "ESP32-Setup";
  static const char* AP_PASS_DEFAULT = ""; // "" = open AP, no password required

  // Tries to connect to WiFi using credentials saved in NVS.
  // If there are none, or the connection fails within connectTimeoutMs,
  // automatically starts the captive portal (blocks until the user submits
  // credentials and the ESP32 connects successfully, or until
  // portalTimeoutMs elapses).
  bool connectOrStartPortal(unsigned long connectTimeoutMs = 15000,
                             unsigned long portalTimeoutMs = 0 /* 0 = no limit */,
                             const char* apSsid = AP_SSID_DEFAULT,
                             const char* apPass = AP_PASS_DEFAULT);

  bool hasCredentials();
  void clearCredentials();
  String getSavedSsid();
}