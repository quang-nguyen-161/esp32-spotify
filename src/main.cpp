/*
  ESP32 + ST7789 TFT — Spotify Now Playing dashboard

  Wiring (custom pinout):
    TFT_GND  -> GPIO 32  (driven LOW in software)
    TFT_VCC  -> GPIO 33  (driven HIGH in software)
    TFT_CS   -> GPIO 12
    TFT_RST  -> GPIO 27
    TFT_DC   -> GPIO 14
    TFT_MOSI -> GPIO 26
    TFT_SCLK -> GPIO 25

  NOTE: GPIO32/33 are normal output-capable GPIOs (unlike 34/35), so
  driving them LOW/HIGH for GND/VCC actually works electrically here.
  Still, a GPIO can only source ~12mA, so this only suits small,
  low-current TFT panels -- check your display's current draw if unsure.

  NOTE ALSO: GPIO12 is a strapping pin (affects flash voltage mode at
  boot if pulled high externally). Using it as TFT_CS should be fine in
  most cases since CS is driven by software after boot, but if you see
  boot issues, move CS to a different pin.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "dashboard.h"
#include "wifi_portal.h"
#include "now_playing.h"
#include "spotify_control.h"
#include "lyrics.h"
#include "text_utils.h"

// ---- Pin definitions ----
#define TFT_GND   32   // driven LOW
#define TFT_VCC   33   // driven HIGH
#define TFT_CS    12
#define TFT_RST   27
#define TFT_DC    14
#define TFT_MOSI  26
#define TFT_SCLK  25

// Use hardware SPI (VSPI) with explicit pins
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
Adafruit_ST7789 *gfx = &tft; // required: defines the extern dashboard.cpp uses

// ---------- Now Playing state ----------
static String lastTrackId = "";
static uint16_t artworkBuf[120 * 120]; // RGB565 buffer, 8192 bytes
static unsigned long lastPollMs = 0;
static const unsigned long POLL_INTERVAL_MS = 2000; // esp32-plan.md: 3-5s is reasonable

// ---------- Lyrics state ----------
static Lyrics::LyricsData currentLyrics;
static int lastLyricLineIdx = -1;

// Flat String buffer handed to dash_lyrics.setLyricsSource(), which only
// stores a pointer + count (doesn't copy), so this needs to stay alive
// for as long as the lyrics are on screen. Cap chosen generously; bump
// if a track legitimately has more synced lines than this.
#define MAX_LYRIC_LINES 200
static String lyricTextsBuf[MAX_LYRIC_LINES];
static uint16_t lyricTextsCount = 0;

// ---------- Physical buttons (optional - change pins to match your wiring) ----------
#define BTN_PLAY_PAUSE 4
#define BTN_NEXT       16
#define BTN_PREV       17

static SpotifyControl::ButtonDebouncer btnPlayPauseDebouncer(300);
static SpotifyControl::ButtonDebouncer btnNextDebouncer(300);
static SpotifyControl::ButtonDebouncer btnPrevDebouncer(300);
static bool s_isPlayingCache = false; // used to decide whether to send "play" or "pause"

// ---------- Helper: push fetched lyrics into the dashboard's lyric source ----------
static void updateLyricsSource() {
  lyricTextsCount = 0;
  if (currentLyrics.found) {
    size_t n = currentLyrics.lines.size();
    if (n > MAX_LYRIC_LINES) n = MAX_LYRIC_LINES;
    for (size_t i = 0; i < n; i++) {
      lyricTextsBuf[i] = TextUtils::stripDiacriticsUtf8(currentLyrics.lines[i].text);
    }
    lyricTextsCount = (uint16_t)n;
  }
  dash_lyrics.setLyricsSource(lyricTextsBuf, lyricTextsCount);
  lastLyricLineIdx = -1;
}

// ---------- Helper: display track info + artwork on the dashboard widgets ----------
static void showNowPlaying(const NowPlaying::Track& t) {
  if (t.trackId != lastTrackId) {
    // Set the dashboard background color based on dominant_color before
    // drawing anything else (so we still get a nice background even if
    // the artwork fetch fails). redraw=false: we do one full dashboard_draw()
    // below once song/artist/artwork/lyrics are all updated.
    if (t.dominantColor.length() > 0) {
      uint16_t bgColor = NowPlaying::hexToRgb565(t.dominantColor);
      dashboard_setBackgroundColor(bgColor, false);
    }

    if (t.hasArtwork) {
      bool ok = NowPlaying::fetchArtwork(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(),
                                          WifiPortal::getApiKey(), artworkBuf, 120 * 120);
      if (ok) {
        dash_artwork.chrome.setBackgroundBitmap(artworkBuf, 120, 120);
      } else {
        Serial.println("Failed to fetch artwork, keeping the previous cover art.");
      }
    }

    // Lyrics are only fetched once per track change, same as artwork
    int durationSec = t.durationMs / 1000;
    bool lyricsOk = Lyrics::fetch(WifiPortal::getVercelBaseUrl(), WifiPortal::getApiKey(),
                                   t.trackName, t.artistName, t.albumName, durationSec, currentLyrics);
    if (!lyricsOk || !currentLyrics.found) {
      currentLyrics.lines.clear();
      currentLyrics.found = false;
    }
    updateLyricsSource();

    // Strip diacritics before drawing: the dashboard's DashText fonts
    // (see esp32-plan_2.md, "UTF-8 / Vietnamese text note") have no
    // Vietnamese glyphs.
    dash_artwork.setSongName(TextUtils::stripDiacriticsUtf8(t.trackName));
    dash_artwork.setArtist(TextUtils::stripDiacriticsUtf8(t.artistName));

    lastTrackId = t.trackId;
    dashboard_draw(); // full repaint: new bg color / artwork / song / artist / lyrics all changed together
  }

  // Playback state + progress bar refresh every poll, track change or not
  dash_playback.setState(t.isPlaying ? DASH_PLAYING : DASH_PAUSED);
  if (t.durationMs > 0) {
    dash_playback.setProgress((float)t.progressMs / (float)t.durationMs);
  }

  // Update the currently active lyric line (no API call needed, just compare progress_ms)
  if (currentLyrics.found && currentLyrics.synced) {
    int idx = Lyrics::findCurrentLine(currentLyrics, t.progressMs);
    if (idx >= 0 && idx != lastLyricLineIdx) {
      dash_lyrics.setLyricsCursor((uint16_t)idx);
      lastLyricLineIdx = idx;
    }
  }

  dashboard_update(); // repaints only what's dirty (scrolling text, progress thumb, etc.)
}

static void showIdleScreen() {
  dash_artwork.setSongName("Not Playing");
  dash_artwork.setArtist("");
  dash_playback.setState(DASH_PAUSED);
  dash_playback.setProgress(0.0f);
  lyricTextsCount = 0;
  dash_lyrics.setLyricsSource(lyricTextsBuf, 0);
  lastTrackId = ""; // force re-fetching artwork/lyrics once playback resumes
  lastLyricLineIdx = -1;
  dashboard_draw();
}

// ---------- Poll now-playing + update the screen ----------
static void pollNowPlaying() {
  NowPlaying::Track t;
  bool ok = NowPlaying::fetchMeta(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(),
                                   WifiPortal::getApiKey(), t);
  if (!ok) {
    Serial.println("Poll now-playing failed (temporary WiFi/Vercel issue?).");
    return; // keep the current screen, retry on the next poll
  }

  s_isPlayingCache = t.isPlaying;

  if (t.isPlaying) {
    showNowPlaying(t);
  } else {
    showIdleScreen();
  }
}

// ---------- Read buttons and send control commands (debounced) ----------
static void handleButtons() {
  bool playPausePressed = digitalRead(BTN_PLAY_PAUSE) == LOW;
  bool nextPressed = digitalRead(BTN_NEXT) == LOW;
  bool prevPressed = digitalRead(BTN_PREV) == LOW;

  if (btnPlayPauseDebouncer.shouldTrigger(playPausePressed)) {
    if (s_isPlayingCache) {
      SpotifyControl::pause(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(), WifiPortal::getApiKey());
    } else {
      SpotifyControl::play(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(), WifiPortal::getApiKey());
    }
  }

  if (btnNextDebouncer.shouldTrigger(nextPressed)) {
    SpotifyControl::next(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(), WifiPortal::getApiKey());
  }

  if (btnPrevDebouncer.shouldTrigger(prevPressed)) {
    SpotifyControl::previous(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(), WifiPortal::getApiKey());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Connect WiFi + configure the Vercel proxy (Spotify OAuth is handled separately
  // by Vercel, see plan.md). If there's no config yet or the connection fails,
  // this function automatically starts the captive portal (AP "ESP32-Setup") and
  // waits for the user to fill in the web form.
  // While in portal mode, WifiPortal::loop() must be called continuously in loop() below.
  bool wifiConnected = WifiPortal::begin();

  if (!wifiConnected) {
    Serial.println("In captive portal mode. Connect to WiFi \"ESP32-Setup\" to configure.");
    return; // don't init the TFT until WiFi is up, to avoid blocking the portal
  }

  Serial.println("WiFi + Vercel config ready. Base URL: " + WifiPortal::getVercelBaseUrl());

  // Physical buttons (INPUT_PULLUP: button wired to GND, reads LOW when pressed)
  pinMode(BTN_PLAY_PAUSE, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  Serial.println("ST7789 dashboard starting...");

  // Drive "power pins" for the display (GPIO32/33 are valid outputs).
  pinMode(TFT_GND, OUTPUT);
  digitalWrite(TFT_GND, LOW);
  pinMode(TFT_VCC, OUTPUT);
  digitalWrite(TFT_VCC, HIGH);
  delay(50); // let power settle before talking to the display

  // Init SPI with custom pins
  SPI.begin(TFT_SCLK, -1 /* MISO not used */, TFT_MOSI, TFT_CS);

  // Init display - 240x320 per the dashboard's arduino_gfx/320x240 layout
  tft.init(240, 320);

  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  dashboard_begin();
  Serial.println("Display initialized.");

  // Poll once immediately at startup to show data right away, instead of waiting
  // for the first poll cycle inside loop()
  pollNowPlaying();
  lastPollMs = millis();
}

void loop() {
  // Required: handle the captive portal (DNS + HTTP) while in AP mode.
  // Does nothing once actually connected to WiFi.
  WifiPortal::loop();

  if (WifiPortal::isInPortalMode()) {
    return; // wait for the user to finish configuring, ESP32 will auto-restart after save
  }

  // Read buttons every loop iteration (debounced internally, won't spam the API)
  handleButtons();

  // Poll now-playing periodically (non-blocking, uses millis() instead of delay())
  unsigned long now = millis();
  if (now - lastPollMs >= POLL_INTERVAL_MS) {
    pollNowPlaying();
    lastPollMs = now;
  }

  // Keep scrolling text / animated widgets ticking every loop, independent
  // of the (slower) now-playing poll interval.
  dashboard_update();
}