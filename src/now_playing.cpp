#include "now_playing.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace NowPlaying {

  // Vercel only serves HTTPS, so WiFiClientSecure is required.
  // setInsecure() = skip verifying the CA chain (traffic is still TLS-encrypted,
  // just without validating the server identity).
  // TODO (esp32-plan.md): replace with a real cert (setCACert) for a production build.
  static WiFiClientSecure makeSecureClient() {
    WiFiClientSecure client;
    client.setInsecure();
    return client;
  }

  bool fetchMeta(const String& baseUrl, const String& userId, const String& apiKey, Track& outTrack) {
    if (baseUrl.length() == 0 || userId.length() == 0 || apiKey.length() == 0) {
      Serial.println("[now_playing] Missing baseUrl / userId / apiKey.");
      return false;
    }

    WiFiClientSecure client = makeSecureClient();
    HTTPClient http;
    String url = baseUrl + "/api/now-playing?user=" + userId;

    if (!http.begin(client, url)) {
      Serial.println("[now_playing] http.begin() failed (bad URL?).");
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
      Serial.printf("[now_playing] Rate limited, waiting %d seconds...\n", retryAfter);
      delay(retryAfter * 1000);
      return false; // skip this poll cycle
    }

    if (httpCode != 200) {
      Serial.printf("[now_playing] Error calling now-playing, HTTP code: %d\n", httpCode);
      if (httpCode > 0) Serial.println(http.getString());
      http.end();
      return false;
    }

    String payload = http.getString();
    http.end();

    // The JSON can be fairly large (track + next), so the buffer needs to be big enough
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.print("[now_playing] JSON parse error: ");
      Serial.println(err.c_str());
      return false;
    }

    outTrack.isPlaying     = doc["playing"] | false;
    outTrack.trackId       = doc["track_id"] | "";
    outTrack.trackName     = doc["track"] | "";
    outTrack.artistName    = doc["artist"] | "";
    outTrack.albumName     = doc["album"] | "";
    outTrack.progressMs    = doc["progress_ms"] | 0;
    outTrack.durationMs    = doc["duration_ms"] | 0;
    outTrack.hasArtwork    = doc["has_artwork"] | false;
    outTrack.artworkSize   = doc["artwork_size"] | 0;
    outTrack.dominantColor = doc["dominant_color"] | "";
    outTrack.volumePercent = doc["volume_percent"] | 0;
    outTrack.deviceName    = doc["device_name"] | "";
    outTrack.shuffleState  = doc["shuffle_state"] | false;
    outTrack.repeatState   = doc["repeat_state"] | "off";
    outTrack.contextType   = doc["context_type"] | "";

    outTrack.hasNext = doc.containsKey("next") && !doc["next"].isNull();
    if (outTrack.hasNext) {
      JsonObject nextObj = doc["next"];
      outTrack.next.trackId     = nextObj["track_id"] | "";
      outTrack.next.trackName   = nextObj["track"] | "";
      outTrack.next.artistName  = nextObj["artist"] | "";
      outTrack.next.durationMs  = nextObj["duration_ms"] | 0;
    }

    return true;
  }

  bool fetchArtwork(const String& baseUrl, const String& userId, const String& apiKey,
                     uint16_t* buf, size_t pixelCount) {
    if (baseUrl.length() == 0 || userId.length() == 0 || apiKey.length() == 0 || buf == nullptr) {
      Serial.println("[now_playing] Missing parameters for fetchArtwork.");
      return false;
    }

    WiFiClientSecure client = makeSecureClient();
    HTTPClient http;
    String url = baseUrl + "/api/now-playing?user=" + userId + "&art=raw";

    if (!http.begin(client, url)) {
      Serial.println("[now_playing] http.begin() failed while fetching artwork.");
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
      Serial.printf("[now_playing] Artwork rate limited, waiting %d seconds...\n", retryAfter);
      delay(retryAfter * 1000);
      return false;
    }

    if (httpCode != 200) {
      Serial.printf("[now_playing] Error fetching artwork, HTTP code: %d\n", httpCode);
      http.end();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t bytesToRead = pixelCount * 2; // RGB565 = 2 bytes/pixel
    size_t bytesRead = 0;
    uint8_t* rawBuf = reinterpret_cast<uint8_t*>(buf);

    unsigned long startMs = millis();
    const unsigned long timeoutMs = 8000;

    while (bytesRead < bytesToRead && http.connected() && (millis() - startMs) < timeoutMs) {
      size_t avail = stream->available();
      if (avail) {
        size_t toRead = min(avail, bytesToRead - bytesRead);
        size_t n = stream->readBytes(rawBuf + bytesRead, toRead);
        bytesRead += n;
      } else {
        delay(1);
      }
    }
    http.end();

    if (bytesRead != bytesToRead) {
      Serial.printf("[now_playing] Artwork incomplete: read %u/%u bytes.\n",
                     (unsigned)bytesRead, (unsigned)bytesToRead);
      return false;
    }

    // RGB565 from the server is big-endian. If colors look off on a specific screen,
    // try byte-swapping each pixel:
    // for (size_t i = 0; i < pixelCount; i++) buf[i] = (buf[i] >> 8) | (buf[i] << 8);
    return true;
  }

  uint16_t hexToRgb565(const String& hex) {
    // hex format: "#a8325f" or "a8325f"
    String h = hex;
    if (h.startsWith("#")) h.remove(0, 1);
    if (h.length() != 6) return 0x0000; // fallback: black

    long rgb = strtol(h.c_str(), nullptr, 16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    // RGB888 -> RGB565: 5 bits R, 6 bits G, 5 bits B
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

} // namespace NowPlaying
