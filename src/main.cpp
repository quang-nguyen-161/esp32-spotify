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

static const String songLyrics[] = {
  "First line", "Second line", "Third line", "Fourth line", "Fifth line", "Sixth line"
};

void setup() {
  Serial.begin(115200);
  delay(200);
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

  dashboard_begin();
  
  Serial.println("Display initialized.");

  // ---- Dashboard demo wiring ----
  dash_artwork.setSongName("Creep");
  dash_artwork.setArtist("Radiohead");
  dash_lyrics.setLyricsSource(songLyrics, sizeof(songLyrics) / sizeof(songLyrics[0]));
  dash_playback.setState(DASH_PLAYING);
  dash_playback.setProgress(0.4f);
  dashboard_draw();

}

void loop() {
  
}