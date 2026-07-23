#ifndef NOW_PLAYING_H
#define NOW_PLAYING_H

#include <Arduino.h>

/*
  Module: now_playing
  Calls GET {baseUrl}/api/now-playing?user=<id> (metadata JSON) and
  GET {baseUrl}/api/now-playing?user=<id>&art=raw (binary RGB565 64x64) on Vercel.
  Vercel already handles the full Spotify OAuth + token refresh flow (see esp32-plan.md).

  Usage in main.cpp:

    #include "now_playing.h"

    NowPlaying::Track t;
    if (NowPlaying::fetchMeta(baseUrl, userId, apiKey, t)) {
      if (t.isPlaying && t.trackId != lastTrackId) {
        NowPlaying::fetchArtwork(baseUrl, userId, apiKey, artworkBuf, 64 * 64);
        lastTrackId = t.trackId;
      }
    }
*/

namespace NowPlaying {

  struct NextTrack {
    String trackId;
    String trackName;
    String artistName;
    long durationMs = 0;
  };

  struct Track {
    bool isPlaying = false;
    String trackId;
    String trackName;
    String artistName;
    String albumName;
    long progressMs = 0;
    long durationMs = 0;
    bool hasArtwork = false;
    int artworkSize = 0;      // square artwork side length, e.g. 64 (px)
    String dominantColor;     // hex code, e.g. "#a8325f" - used to color the UI bg/border
    int volumePercent = 0;
    String deviceName;
    bool shuffleState = false;
    String repeatState;       // "off" | "track" | "context"
    String contextType;
    bool hasNext = false;
    NextTrack next;
  };

  // Calls GET {baseUrl}/api/now-playing?user=userId, parses the full JSON into outTrack.
  // Returns true if the request + JSON parse succeeded (not necessarily currently playing).
  // If the server returns 429 (rate limited), this function delay()s according to
  // retry_after_seconds and returns false (skip this poll cycle, no error thrown).
  bool fetchMeta(const String& baseUrl, const String& userId, const String& apiKey, Track& outTrack);

  // Calls GET {baseUrl}/api/now-playing?user=userId&art=raw, reads pixelCount*2 bytes of
  // RGB565 (big-endian) into buf. buf must already be allocated with enough uint16_t elements.
  // Returns true if the expected number of bytes was read.
  bool fetchArtwork(const String& baseUrl, const String& userId, const String& apiKey,
                     uint16_t* buf, size_t pixelCount);

  // Converts a hex string "#a8325f" to an RGB565 value (used to draw a background/border
  // based on dominant_color).
  uint16_t hexToRgb565(const String& hex);

} // namespace NowPlaying

#endif // NOW_PLAYING_H
