/*
  ESP32 + ST7789 TFT color test
  Using Adafruit GFX + Adafruit ST7789 library (most popular combo for ST7789 on Arduino/PlatformIO)

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
Adafruit_ST7789 *gfx = &tft;
// Basic colors to cycle through
struct ColorEntry {
  const char *name;
  uint16_t color;
};

ColorEntry colors[] = {
  {"RED",     ST77XX_RED},
  {"GREEN",   ST77XX_GREEN},
  {"BLUE",    ST77XX_BLUE},
  {"YELLOW",  ST77XX_YELLOW},
  {"CYAN",    ST77XX_CYAN},
  {"MAGENTA", ST77XX_MAGENTA},
  {"WHITE",   ST77XX_WHITE},
  {"BLACK",   ST77XX_BLACK},
};

const int numColors = sizeof(colors) / sizeof(colors[0]);

// ---------- Now Playing state ----------
static String lastTrackId = "";
static uint16_t artworkBuf[120 * 120]; // RGB565 buffer, 8192 bytes
static unsigned long lastPollMs = 0;
static const unsigned long POLL_INTERVAL_MS = 4000; // esp32-plan.md: 3-5s is reasonable

// ---------- Lyrics state ----------
static Lyrics::LyricsData currentLyrics;
static int lastLyricLineIdx = -1;

// ---------- Physical buttons (optional - change pins to match your wiring) ----------
#define BTN_PLAY_PAUSE 4
#define BTN_NEXT       16
#define BTN_PREV       17

static SpotifyControl::ButtonDebouncer btnPlayPauseDebouncer(300);
static SpotifyControl::ButtonDebouncer btnNextDebouncer(300);
static SpotifyControl::ButtonDebouncer btnPrevDebouncer(300);
static bool s_isPlayingCache = false; // used to decide whether to send "play" or "pause"

// ---------- Helper: display track info + artwork ----------
static void showNowPlaying(const NowPlaying::Track& t) {
  if (t.trackId != lastTrackId) {
    // Set the background color based on dominant_color before drawing the artwork
    // (so we still get a nice background even if the artwork fetch fails)
    if (t.dominantColor.length() > 0) {
      uint16_t bgColor = NowPlaying::hexToRgb565(t.dominantColor);
      tft.fillScreen(bgColor);
    }

    if (t.hasArtwork) {
      bool ok = NowPlaying::fetchArtwork(WifiPortal::getVercelBaseUrl(), WifiPortal::getUserId(),
                                          WifiPortal::getApiKey(), artworkBuf, 120 * 120);
      if (ok) {
        tft.drawRGBBitmap(0, 0, artworkBuf, 120, 120);
      } else {
        Serial.println("Failed to fetch artwork, keeping the background color.");
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
    lastLyricLineIdx = -1;

    lastTrackId = t.trackId;
  }

  // Draw track info text (rough position, adjust to match the actual dashboard layout).
  // Strip diacritics before drawing: the default Adafruit_GFX font has no Vietnamese
  // glyphs (see esp32-plan_2.md, "UTF-8 / Vietnamese text note")
  String trackNameAscii = TextUtils::stripDiacriticsUtf8(t.trackName);
  String artistNameAscii = TextUtils::stripDiacriticsUtf8(t.artistName);

  tft.fillRect(0, 140, tft.width(), 60, ST77XX_BLACK);
  tft.setCursor(0, 140);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println(trackNameAscii);
  tft.setTextColor(ST77XX_CYAN);
  tft.println(artistNameAscii);

  // Update the currently active lyric line (no API call needed, just compare progress_ms)
  if (currentLyrics.found && currentLyrics.synced) {
    int idx = Lyrics::findCurrentLine(currentLyrics, t.progressMs);
    if (idx >= 0 && idx != lastLyricLineIdx) {
      String lineAscii = TextUtils::stripDiacriticsUtf8(currentLyrics.lines[idx].text);
      tft.fillRect(0, 360, tft.width(), 20, ST77XX_BLACK);
      tft.setCursor(0, 360);
      tft.setTextColor(ST77XX_YELLOW);
      tft.println(lineAscii);
      lastLyricLineIdx = idx;
    }
  }
}

static void showIdleScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 100);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println("Not Playing");
  lastTrackId = ""; // force re-fetching artwork once playback resumes
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

  Serial.println("ST7789 color test starting...");

  // Drive "power pins" for the display (GPIO32/33 are valid outputs).
  pinMode(TFT_GND, OUTPUT);
  digitalWrite(TFT_GND, LOW);
  pinMode(TFT_VCC, OUTPUT);
  digitalWrite(TFT_VCC, HIGH);
  delay(50); // let power settle before talking to the display

  // Init SPI with custom pins
  SPI.begin(TFT_SCLK, -1 /* MISO not used */, TFT_MOSI, TFT_CS);

  // Init display - common 1.3"/1.54" ST7789 modules are 240x240
  // Change to 240x320 if you have that variant (uncomment tft.init line below accordingly)
  tft.init(240, 320);           // 240x240 square display
  // tft.init(240, 320);         // uncomment for 240x320 display instead

  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

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
}