#include "spotify_control.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace SpotifyControl {

  static int sendControl(const String& baseUrl, const String& userId, const String& apiKey,
                          const String& action, const String& extraQuery = "") {
    if (baseUrl.length() == 0 || userId.length() == 0 || apiKey.length() == 0) {
      Serial.println("[spotify_control] Missing baseUrl / userId / apiKey.");
      return -1;
    }

    WiFiClientSecure client;
    client.setInsecure(); // see TLS note in now_playing.cpp / esp32-plan.md

    HTTPClient http;
    String url = baseUrl + "/api/control?user=" + userId + "&action=" + action + extraQuery;

    if (!http.begin(client, url)) {
      Serial.println("[spotify_control] http.begin() failed.");
      return -1;
    }
    http.addHeader("x-api-key", apiKey);

    int httpCode = http.GET();

    if (httpCode == 429) {
      String payload = http.getString();
      DynamicJsonDocument rateDoc(256);
      int retryAfter = 5;
      if (!deserializeJson(rateDoc, payload)) {
        retryAfter = rateDoc["retry_after_seconds"] | 5;
      }
      Serial.printf("[spotify_control] Rate limited, waiting %d seconds...\n", retryAfter);
      http.end();
      delay(retryAfter * 1000);
      return httpCode;
    }

    if (httpCode == 404) {
      Serial.println("[spotify_control] No active device (Spotify isn't open anywhere).");
    } else if (httpCode != 200) {
      Serial.printf("[spotify_control] Error on control '%s', HTTP code: %d\n", action.c_str(), httpCode);
      if (httpCode > 0) Serial.println(http.getString());
    } else {
      Serial.printf("[spotify_control] '%s' OK.\n", action.c_str());
    }

    http.end();
    return httpCode;
  }

  int play(const String& baseUrl, const String& userId, const String& apiKey) {
    return sendControl(baseUrl, userId, apiKey, "play");
  }

  int pause(const String& baseUrl, const String& userId, const String& apiKey) {
    return sendControl(baseUrl, userId, apiKey, "pause");
  }

  int next(const String& baseUrl, const String& userId, const String& apiKey) {
    return sendControl(baseUrl, userId, apiKey, "next");
  }

  int previous(const String& baseUrl, const String& userId, const String& apiKey) {
    return sendControl(baseUrl, userId, apiKey, "previous");
  }

  int setVolume(const String& baseUrl, const String& userId, const String& apiKey, int value) {
    value = constrain(value, 0, 100);
    return sendControl(baseUrl, userId, apiKey, "volume", "&value=" + String(value));
  }

  int setShuffle(const String& baseUrl, const String& userId, const String& apiKey, bool state) {
    return sendControl(baseUrl, userId, apiKey, "shuffle", state ? "&state=true" : "&state=false");
  }

  int setRepeat(const String& baseUrl, const String& userId, const String& apiKey, const String& state) {
    // Valid states: "off" | "track" | "context" - not validated here, the backend will report errors
    return sendControl(baseUrl, userId, apiKey, "repeat", "&state=" + state);
  }

  // ---------- ButtonDebouncer ----------
  ButtonDebouncer::ButtonDebouncer(unsigned long minIntervalMs)
    : m_minIntervalMs(minIntervalMs) {}

  bool ButtonDebouncer::shouldTrigger(bool pinIsLow) {
    unsigned long now = millis();

    // Only trigger on the falling edge: from "released" (false) to "pressed" (true)
    bool edgeDetected = pinIsLow && !m_lastState;
    m_lastState = pinIsLow;

    if (!edgeDetected) return false;

    if (now - m_lastTriggerMs < m_minIntervalMs) {
      return false; // too soon after the last trigger, treat as bounce/spam -> ignore
    }

    m_lastTriggerMs = now;
    return true;
  }

} // namespace SpotifyControl
