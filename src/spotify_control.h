#ifndef SPOTIFY_CONTROL_H
#define SPOTIFY_CONTROL_H

#include <Arduino.h>

/*
  Module: spotify_control
  Calls GET {baseUrl}/api/control?user=<id>&action=<...> on Vercel to control playback.
  See esp32-plan.md, section "/api/control".

  Usage in main.cpp:

    #include "spotify_control.h"

    SpotifyControl::play(baseUrl, userId, apiKey);
    SpotifyControl::pause(baseUrl, userId, apiKey);
    SpotifyControl::next(baseUrl, userId, apiKey);
    SpotifyControl::previous(baseUrl, userId, apiKey);
    SpotifyControl::setVolume(baseUrl, userId, apiKey, 50);
    SpotifyControl::setShuffle(baseUrl, userId, apiKey, true);
    SpotifyControl::setRepeat(baseUrl, userId, apiKey, "track");

  Notes:
    - Requires an active device (a Spotify app open somewhere) for control to work,
      otherwise you'll get an HTTP 404.
    - Physical buttons should be debounced (see ButtonDebouncer) to avoid spamming the API.
*/

namespace SpotifyControl {

  // Send a simple control request with no extra parameters.
  // Returns the HTTP status code (200 = OK, 404 = no active device, other = error).
  int play(const String& baseUrl, const String& userId, const String& apiKey);
  int pause(const String& baseUrl, const String& userId, const String& apiKey);
  int next(const String& baseUrl, const String& userId, const String& apiKey);
  int previous(const String& baseUrl, const String& userId, const String& apiKey);

  // value: 0-100
  int setVolume(const String& baseUrl, const String& userId, const String& apiKey, int value);

  // state: true/false
  int setShuffle(const String& baseUrl, const String& userId, const String& apiKey, bool state);

  // state: "off" | "track" | "context"
  int setRepeat(const String& baseUrl, const String& userId, const String& apiKey, const String& state);

  // ---------- Debounce helper for physical buttons ----------
  // Use one instance per button. Call shouldTrigger() every time you read the pin;
  // it filters out contact bounce and rate-limits how fast commands can be sent.
  class ButtonDebouncer {
  public:
    explicit ButtonDebouncer(unsigned long minIntervalMs = 300);

    // pinIsLow: current pin state (e.g. digitalRead(pin) == LOW means pressed,
    // depending on your wiring). Returns true EXACTLY ONCE per valid button press
    // (after debounce + rate limiting).
    bool shouldTrigger(bool pinIsLow);

  private:
    unsigned long m_minIntervalMs;
    unsigned long m_lastTriggerMs = 0;
    bool m_lastState = false; // false = released, true = pressed
  };

} // namespace SpotifyControl

#endif // SPOTIFY_CONTROL_H
