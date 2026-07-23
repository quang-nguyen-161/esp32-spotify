#pragma once
#include <Arduino.h>

// ============================================================
// SpotifyAuth
// - Once the ESP32 has WiFi (STA), runs a temporary web server on the
//   ESP32's IP address so the user can log in to Spotify (Authorization
//   Code Flow), then saves client_id / client_secret / refresh_token to
//   NVS (namespace "spotify"). ApiResolve::begin() will use this
//   refresh_token.
// - redirect_uri MUST be registered exactly as shown in the Spotify
//   Developer Dashboard, in the form: http://<ESP32_IP>/callback
//   (e.g. http://192.168.1.50/callback)
// ============================================================
namespace SpotifyAuth {

  // Whether client_id/secret/refresh_token already exist in NVS
  bool hasTokens();

  // Reads credentials saved in NVS. Returns false if nothing is stored yet.
  bool loadCredentials(String& clientId, String& clientSecret, String& refreshToken);

  // Saves (or updates) client_id / client_secret in NVS.
  // Call this before running runAuthPortal() for the first time (e.g. via
  // Serial input, hardcoded in code, or added to wifi_portal's form).
  void saveAppCredentials(const String& clientId, const String& clientSecret);

  // Clears all stored Spotify data (to log in again from scratch)
  void clearTokens();

  // Runs a blocking web server on the current STA IP, showing a
  // "Log in with Spotify" button. Once the user logs in successfully and
  // Spotify redirects the callback to /callback, this function exchanges
  // the code for a refresh_token, saves it to NVS, then returns true.
  // Returns false if timeoutMs elapses first.
  //
  // Default scope is enough for api_resolve.cpp: user-top-read,
  // user-read-currently-playing, user-read-playback-state
  bool runAuthPortal(unsigned long timeoutMs = 300000,
                      const String& scope = "user-top-read user-read-currently-playing user-read-playback-state");
}