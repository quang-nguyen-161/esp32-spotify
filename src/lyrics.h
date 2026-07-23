#ifndef LYRICS_H
#define LYRICS_H

#include <Arduino.h>
#include <vector>

/*
  Module: lyrics
  Calls GET {baseUrl}/api/lyrics?track=..&artist=..&album=..&duration=.. on Vercel
  (Vercel proxies to lrclib.net, with caching). See esp32-plan.md, section "/api/lyrics".

  IMPORTANT: only call this once when track_id changes (same as artwork), NOT on every poll.
  Once you have lines[], compare progress_ms (from now-playing) against each line's time_ms
  to determine the currently active line - no need to call the API again.

  Usage in main.cpp:

    #include "lyrics.h"

    Lyrics::LyricsData lyr;
    if (Lyrics::fetch(baseUrl, apiKey, "Levitating", "Dua Lipa", "Future Nostalgia", 203, lyr)) {
      if (lyr.found && lyr.synced) {
        int idx = Lyrics::findCurrentLine(lyr, progressMs);
        if (idx >= 0) Serial.println(lyr.lines[idx].text);
      }
    }
*/

namespace Lyrics {

  struct Line {
    long timeMs = 0;   // -1 if there's no timestamp (plain lyrics)
    String text;
  };

  struct LyricsData {
    bool found = false;
    bool synced = false;
    std::vector<Line> lines;
  };

  // Calls the lyrics API. track/artist/album are basically URL-encoded (spaces -> %20).
  // durationSec: song duration in seconds (duration_ms / 1000 from now-playing).
  // Returns true if the request + JSON parse succeeded (also check out.found for whether
  // any lyrics were actually returned).
  // If rate limited (429), this delay()s according to retry_after_seconds and returns false.
  bool fetch(const String& baseUrl, const String& apiKey,
             const String& track, const String& artist, const String& album,
             int durationSec, LyricsData& out);

  // Finds the lyric line currently active at progressMs (based on each line's timeMs).
  // Returns the index into out.lines, or -1 if there's no active line yet / lyrics aren't synced.
  int findCurrentLine(const LyricsData& data, long progressMs);

} // namespace Lyrics

#endif // LYRICS_H
