#include "lyrics.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace Lyrics {

  static String urlEncodeBasic(const String& s) {
    // Basic URL-encoding: good enough for track/artist names (letters, digits, spaces).
    // If names contain other special characters (&, +, accented characters...),
    // a more thorough encoder would be needed.
    String out = s;
    out.replace(" ", "%20");
    out.replace("&", "%26");
    return out;
  }

  bool fetch(const String& baseUrl, const String& apiKey,
             const String& track, const String& artist, const String& album,
             int durationSec, LyricsData& out) {
    if (baseUrl.length() == 0 || apiKey.length() == 0 || track.length() == 0) {
      Serial.println("[lyrics] Missing required parameters.");
      return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // see TLS note in now_playing.cpp

    HTTPClient http;
    String url = baseUrl + "/api/lyrics?track=" + urlEncodeBasic(track) +
                 "&artist=" + urlEncodeBasic(artist) +
                 "&album=" + urlEncodeBasic(album) +
                 "&duration=" + String(durationSec);

    if (!http.begin(client, url)) {
      Serial.println("[lyrics] http.begin() failed.");
      return false;
    }
    http.addHeader("x-api-key", apiKey);

    int httpCode = http.GET();

    if (httpCode == 429) {
      String payload = http.getString();
      http.end();
      DynamicJsonDocument rateDoc(256);
      int retryAfter = 5;
      if (!deserializeJson(rateDoc, payload)) {
        retryAfter = rateDoc["retry_after_seconds"] | 5;
      }
      Serial.printf("[lyrics] Rate limited, waiting %d seconds...\n", retryAfter);
      delay(retryAfter * 1000);
      return false;
    }

    if (httpCode != 200) {
      Serial.printf("[lyrics] Error calling API, HTTP code: %d\n", httpCode);
      http.end();
      return false;
    }

    String payload = http.getString();
    http.end();

    // Full-song lyrics can be long (many lines), so this needs a bigger buffer
    // than the other APIs
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("[lyrics] JSON parse error: ");
      Serial.println(err.c_str());
      return false;
    }

    out.lines.clear();
    out.found = doc["found"] | false;
    out.synced = doc["synced"] | false;

    if (out.found && doc.containsKey("lines")) {
      JsonArray linesArr = doc["lines"];
      out.lines.reserve(linesArr.size());
      for (JsonObject lineObj : linesArr) {
        Line l;
        l.timeMs = lineObj["time_ms"].isNull() ? -1 : lineObj["time_ms"].as<long>();
        l.text = lineObj["text"] | "";
        out.lines.push_back(l);
      }
    }

    return true;
  }

  int findCurrentLine(const LyricsData& data, long progressMs) {
    if (!data.found || !data.synced || data.lines.empty()) return -1;

    int result = -1;
    for (size_t i = 0; i < data.lines.size(); i++) {
      if (data.lines[i].timeMs < 0) continue; // line has no timestamp, skip
      if (data.lines[i].timeMs <= progressMs) {
        result = (int)i; // this line has already started, keep looking for a closer one
      } else {
        break; // remaining lines have a timestamp greater than progress, stop here
      }
    }
    return result;
  }

} // namespace Lyrics
