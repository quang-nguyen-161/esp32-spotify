#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>

/*
  Module: wifi_portal
  Purpose: Captive portal that lets the user enter:
    - WiFi SSID / Password
    - Vercel endpoint base URL (e.g. https://your-app.vercel.app)
    - Spotify User ID (account identifier on the Vercel backend, e.g. "xuan123")
    - ESP32 API key (used to authenticate with Vercel via the x-api-key header)

  Spotify Client ID/Secret/Refresh Token no longer live on the ESP32 -
  the Vercel proxy handles the entire OAuth flow; the ESP32 just calls
  a single simple HTTPS endpoint. (See plan.md: Spotify -> Vercel -> ESP32)

  Usage in main.cpp:

    #include "wifi_portal.h"

    void setup() {
      Serial.begin(115200);
      bool connected = WifiPortal::begin();
      if (connected) {
        // ... main code, use WifiPortal::getVercelBaseUrl() / getApiKey() ...
      }
    }

    void loop() {
      WifiPortal::loop(); // REQUIRED - handles the captive portal while in AP mode
    }
*/

namespace WifiPortal {

  // Call in setup(). Returns true if WiFi connected successfully.
  // Returns false if currently in captive portal mode (must call loop() continuously).
  bool begin();

  // Call in loop() - handles DNS + HTTP requests while in AP mode.
  void loop();

  // Whether currently in AP/captive portal mode
  bool isInPortalMode();

  // Clear all saved config, used when the user wants to reconfigure from scratch
  void resetConfig();

  // Getters for the saved config
  String getWifiSsid();
  String getVercelBaseUrl(); // e.g. "https://your-app.vercel.app" (no trailing slash)
  String getUserId();        // Spotify account identifier on the backend, e.g. "xuan123"
  String getApiKey();        // value sent in the x-api-key header

} // namespace WifiPortal

#endif // WIFI_PORTAL_H