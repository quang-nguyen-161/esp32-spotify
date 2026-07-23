#include "dashboard.h"
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
#include <Fonts/FreeMonoOblique9pt7b.h>

// ---------------------------------------------------------------------
// dashboard.cpp
// ---------------------------------------------------------------------

// Small monochrome playback glyphs kept as a fallback path (DashIcon
// still supports tinted monochrome variants via addVariant()), even
// though DashPlayback now defaults to the full-color bitmap buttons.
static const unsigned char PROGMEM image_music_next_bits[] = {0x00,0x00,0xe0,0x0f,0x90,0x09,0x8c,0x09,0x82,0x09,0x81,0x89,0x80,0x49,0x80,0x39,0x80,0x39,0x80,0x49,0x81,0x89,0x82,0x09,0x8c,0x09,0x90,0x09,0xe0,0x0f,0x00,0x00};
static const unsigned char PROGMEM image_music_pause_bits[] = {0xf9,0xf0,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0x89,0x10,0xf9,0xf0,0x00,0x00};
static const unsigned char PROGMEM image_music_play_bits[] = {0xc0,0x00,0xe0,0x00,0x98,0x00,0x86,0x00,0x81,0x80,0x80,0x60,0x80,0x18,0x80,0x06,0x80,0x18,0x80,0x60,0x81,0x80,0x86,0x00,0x98,0x00,0xe0,0x00,0xc0,0x00,0x00,0x00};
static const unsigned char PROGMEM image_music_previous_bits[] = {0x00,0x00,0xf0,0x07,0x90,0x09,0x90,0x31,0x90,0x41,0x91,0x81,0x92,0x01,0x9c,0x01,0x9c,0x01,0x92,0x01,0x91,0x81,0x90,0x41,0x90,0x31,0x90,0x09,0xf0,0x07,0x00,0x00};

// Default matches the purple theme background (sampled from the actual
// hardware photo) -- NOT black. DashIcon::draw() fillRect()s this color
// behind every icon before drawing it, so leaving this at 0x0000 paints
// a black square behind every button. Override with
// dashboard_setBackgroundColor() if your theme uses a different color.
uint16_t dash_colorBackground = 0xCD5E;

// Sets the color used to "erase" behind icons/plates on partial redraws
// (see DashIcon::draw's fillRect). Left at the 0x0000 init value, this
// paints black behind every icon instead of the theme's actual purple
// background -- call this once with the real background color (e.g.
// sampled from image_background__3__pixels) before/after dashboard_begin().
void dashboard_setBackgroundColor(uint16_t color, bool redraw) {
  dash_colorBackground = color;
  if (redraw) dashboard_draw();
}

// Guards every DashText::render() call (and anything else that touches
// `gfx`) from firing during global/static construction, i.e. before
// SPI.begin()/tft.init() have run in setup(). Global DashArtwork/DashLyrics
// objects call setText()/setBgColor() from their constructors (to set
// initial captions/plates), which would otherwise write to the TFT over
// SPI/UART before the hardware is initialized -- crashing inside
// Adafruit_GFX/Adafruit_SPITFT during _GLOBAL__sub_I_tft. Call
// dashboard_begin() once, right after tft.init(), before anything else
// touches the dashboard.
static bool dash_hwReady = false;

void dashboard_begin() {
  dash_hwReady = true;
}

// ---------------------------------------------------------------------
// Hue rotation helpers
// ---------------------------------------------------------------------

uint16_t dash_colorMainBase = 0xFFE0; // yellow, matches the Lopaka button accent
int16_t dash_hueRotation = 0;
bool dash_hueRotateBitmaps = false;

uint16_t dash_darkenColor565(uint16_t color, uint8_t pct) {
  if (pct > 100) pct = 100;
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5)  & 0x3F;
  uint8_t b =  color        & 0x1F;
  r = (uint8_t)(r * (100 - pct) / 100);
  g = (uint8_t)(g * (100 - pct) / 100);
  b = (uint8_t)(b * (100 - pct) / 100);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// RGB565 -> float RGB -> HSV, rotate hue, back to RGB565. Straightforward
// (not fixed-point optimized); fine for occasional theme-change redraws.
uint16_t dashboard_rotateHue565(uint16_t color565, int16_t degrees) {
  if (degrees == 0) return color565;

  uint8_t r5 = (color565 >> 11) & 0x1F, g6 = (color565 >> 5) & 0x3F, b5 = color565 & 0x1F;
  float r = r5 / 31.0f, g = g6 / 63.0f, b = b5 / 31.0f;

  float maxc = fmaxf(r, fmaxf(g, b));
  float minc = fminf(r, fminf(g, b));
  float delta = maxc - minc;
  float h = 0.0f, s = (maxc <= 0.0f) ? 0.0f : (delta / maxc), v = maxc;

  if (delta > 1e-6f) {
    if      (maxc == r) h = 60.0f * fmodf(((g - b) / delta), 6.0f);
    else if (maxc == g) h = 60.0f * (((b - r) / delta) + 2.0f);
    else                h = 60.0f * (((r - g) / delta) + 4.0f);
  }
  if (h < 0) h += 360.0f;

  h = fmodf(h + degrees + 360.0f, 360.0f);

  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float rf, gf, bf;
  if      (h <  60.0f) { rf = c; gf = x; bf = 0; }
  else if (h < 120.0f) { rf = x; gf = c; bf = 0; }
  else if (h < 180.0f) { rf = 0; gf = c; bf = x; }
  else if (h < 240.0f) { rf = 0; gf = x; bf = c; }
  else if (h < 300.0f) { rf = x; gf = 0; bf = c; }
  else                 { rf = c; gf = 0; bf = x; }

  uint8_t or5 = (uint8_t)roundf((rf + m) * 31.0f);
  uint8_t og6 = (uint8_t)roundf((gf + m) * 63.0f);
  uint8_t ob5 = (uint8_t)roundf((bf + m) * 31.0f);
  return (uint16_t)((or5 << 11) | (og6 << 5) | ob5);
}

uint16_t dashboard_getMainColor(void) {
  return dashboard_rotateHue565(dash_colorMainBase, dash_hueRotation);
}

void dashboard_setHueRotation(int16_t degrees, bool redraw) {
  dash_hueRotation = ((degrees % 360) + 360) % 360;
  if (redraw) dashboard_draw();
}

void dashboard_setHueRotateBitmaps(bool enabled) {
  dash_hueRotateBitmaps = enabled;
}

// Color key used to mark "no pixel here" in the exported RGB565 assets.
// Pure black (0x0000) is never an intentional fill color in these Lopaka
// exports, so we treat it as transparent and just don't plot it -- that
// lets the real background (or whatever was drawn underneath) show
// through instead of getting stamped over with a black square.
#define DASH_TRANSPARENT_KEY 0x0000

// Draws a full-color RGB565 PROGMEM bitmap, hue-rotating every pixel on
// the fly. Used only when dash_hueRotateBitmaps is enabled -- costs one
// pgm_read_word + one HSV round-trip per pixel, so it's noticeably
// slower than a plain drawRGBBitmap(); fine for an occasional theme
// change, not for something you'd call every frame.
static void drawBitmapHueRotated(int16_t x, int16_t y, const uint16_t *bmp,
                                  int16_t w, int16_t h, int16_t degrees) {
  gfx->startWrite();
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      uint16_t px = pgm_read_word(&bmp[j * w + i]);
      if (px == DASH_TRANSPARENT_KEY) continue; // skip black -> transparent
      gfx->writePixel(x + i, y + j, dashboard_rotateHue565(px, degrees));
    }
  }
  gfx->endWrite();
}

// Plain (non-hue-rotated) keyed blit: same idea, just no HSV round-trip.
// Slower than gfx->drawRGBBitmap() since it can't push a solid block, but
// that's the cost of skipping the black "holes" pixel by pixel.
static void drawBitmapKeyed(int16_t x, int16_t y, const uint16_t *bmp, int16_t w, int16_t h) {
  gfx->startWrite();
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      uint16_t px = pgm_read_word(&bmp[j * w + i]);
      if (px == DASH_TRANSPARENT_KEY) continue; // skip black -> transparent
      gfx->writePixel(x + i, y + j, px);
    }
  }
  gfx->endWrite();
}

// Central place every bitmap asset draw goes through, so hue rotation
// (when enabled) applies uniformly to background/chrome/buttons alike.
static void drawAsset(int16_t x, int16_t y, const uint16_t *bmp, int16_t w, int16_t h) {
  if (dash_hueRotateBitmaps && dash_hueRotation != 0) {
    drawBitmapHueRotated(x, y, bmp, w, h, dash_hueRotation);
  } else {
    drawBitmapKeyed(x, y, bmp, w, h);
  }
}

// ---------------------------------------------------------------------
// DashText
// ---------------------------------------------------------------------

DashText::DashText(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint8_t textSize, uint16_t textColor, uint16_t bgColor)
  : _x(x), _y(y), _w(w), _h(h), _textSize(textSize),
    _color(textColor), _bgColor(bgColor), _text(""),
    _textWidthPx(0), _needsScroll(false), _scrollX(0), _lastStepMs(0),
    _stepIntervalMs(DASH_TEXT_DEFAULT_INTERVAL_MS), _pxPerStep(DASH_TEXT_DEFAULT_STEP_PX),
    _canvas(nullptr), _dirty(true) {
  allocCanvas();
}

DashText::~DashText() { delete _canvas; }

void DashText::allocCanvas() {
  delete _canvas;
  _canvas = new GFXcanvas16(_w, _h);
}

void DashText::setSize(int16_t w, int16_t h) {
  _w = w; _h = h;
  allocCanvas();
  measure();
  _dirty = true;
  render();
}

void DashText::setPos(int16_t x, int16_t y) { _x = x; _y = y; _dirty = true; }

void DashText::setTextColor(uint16_t color) { _color = color; _dirty = true; render(); }
void DashText::setBgColor(uint16_t color)   { _bgColor = color; _dirty = true; render(); }

// Assigns a custom Adafruit GFX font (pass nullptr to go back to the
// built-in default font). Must re-measure since glyph metrics change
// with the font, then force a repaint.
void DashText::setFont(const GFXfont *font) {
  _font = font;
  measure();
  _dirty = true;
  render();
}

void DashText::setScrollSpeed(uint16_t pxPerStep, uint16_t stepIntervalMs) {
  _pxPerStep = pxPerStep;
  _stepIntervalMs = stepIntervalMs;
}

void DashText::measure() {
  if (!_canvas) return;
  int16_t x1, y1;
  uint16_t w, h;
  _canvas->setFont(_font);
  _canvas->setTextSize(_textSize);
  _canvas->getTextBounds(_text, 0, 0, &x1, &y1, &w, &h);
  _textWidthPx = (int16_t)w;
  _needsScroll = _textWidthPx > _w;
}

void DashText::setText(const String &text) {
  if (_text == text) return; // no change -> no redraw, no SPI write
  _text = text;
  _scrollX = 0;
  _lastStepMs = millis();
  measure();
  _dirty = true;
  render();
}

void DashText::render() {
  if (!_canvas) return;
  if (!dash_hwReady) return; // hardware not initialized yet (e.g. called from a global object's constructor)
  if (!_dirty) return;       // nothing changed since last paint -> skip the SPI write entirely

  _canvas->fillScreen(_bgColor);
  _canvas->setFont(_font);
  _canvas->setTextColor(_color);
  _canvas->setTextWrap(false);
  _canvas->setTextSize(_textSize);

  if (_needsScroll) {
    _canvas->setCursor(-_scrollX, 0);
    _canvas->print(_text);
    _canvas->setCursor(-_scrollX + _textWidthPx + DASH_TEXT_SCROLL_GAP_PX, 0);
    _canvas->print(_text);
  } else {
    _canvas->setCursor(0, 0);
    _canvas->print(_text);
  }

  gfx->drawRGBBitmap(_x, _y, _canvas->getBuffer(), _w, _h);
  _dirty = false; // clean until the next state change
}

void DashText::draw() { render(); }

void DashText::update() {
  if (!_needsScroll) return;
  unsigned long now = millis();
  if (now - _lastStepMs < _stepIntervalMs) return;
  _lastStepMs = now;
  _dirty = true; // scroll position changed -> this instance needs a repaint

  _scrollX += _pxPerStep;
  if (_scrollX >= _textWidthPx + DASH_TEXT_SCROLL_GAP_PX) _scrollX = 0;
  render();
}

// ---------------------------------------------------------------------
// DashIcon
// ---------------------------------------------------------------------

DashIcon::DashIcon(int16_t x, int16_t y, uint16_t color)
  : _x(x), _y(y), _color(color), _hover(false),
    _activeVariant(0), _variantCount(0), _maxW(0), _maxH(0) {}

uint8_t DashIcon::addVariant(const unsigned char *bits, int16_t w, int16_t h) {
  if (_variantCount >= DASH_ICON_MAX_VARIANTS) return _variantCount - 1;
  _variants[_variantCount] = Variant{false, bits, nullptr, w, h};
  if (w > _maxW) _maxW = w;
  if (h > _maxH) _maxH = h;
  return _variantCount++;
}

uint8_t DashIcon::addColorVariant(const uint16_t *rgb565Bits, int16_t w, int16_t h) {
  if (_variantCount >= DASH_ICON_MAX_VARIANTS) return _variantCount - 1;
  _variants[_variantCount] = Variant{true, nullptr, rgb565Bits, w, h};
  if (w > _maxW) _maxW = w;
  if (h > _maxH) _maxH = h;
  return _variantCount++;
}

void DashIcon::setVariant(uint8_t index) {
  if (index < _variantCount) _activeVariant = index;
}

void DashIcon::setHover(bool hovered) { _hover = hovered; }

uint16_t DashIcon::effectiveColor() const {
  return _hover ? dash_darkenColor565(_color, DASH_HOVER_DARKEN_PCT) : _color;
}

void DashIcon::draw(uint16_t bgColor) {
  if (_variantCount == 0) return;

  gfx->fillRect(_x, _y - DASH_HOVER_LIFT_PX, _maxW, _maxH + DASH_HOVER_LIFT_PX, bgColor);

  const Variant &v = _variants[_activeVariant];
  int16_t drawY = _y - (_hover ? DASH_HOVER_LIFT_PX : 0);

  if (v.isColor) {
    // Full-color bitmap: drawn as-is (no tint), black pixels skipped so
    // the fillRect background above shows through instead of a black
    // square. Hover feedback is the position lift only -- no per-pixel
    // darken here to keep redraws cheap.
    drawBitmapKeyed(_x, drawY, v.colorBits, v.w, v.h);
  } else {
    gfx->drawBitmap(_x, drawY, v.monoBits, v.w, v.h, effectiveColor());
  }
}

// ---------------------------------------------------------------------
// DashWindow
// ---------------------------------------------------------------------

DashWindow::DashWindow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t titleBarH)
  : titleText(x + 4, y + 2, w - 4 - 42, titleBarH - 4, 1, 0x0000, 0xFFFF),
    _x(x), _y(y), _w(w), _h(h), _titleBarH(titleBarH),
    _titleBarColor(0xF81F), _borderColor(0x0000), _controlsColor(0x0000),
    _showMinimize(true), _showMaximize(true), _showClose(true) {}

void DashWindow::setColors(uint16_t titleBarColor, uint16_t borderColor, uint16_t controlsColor) {
  _titleBarColor = titleBarColor;
  _borderColor = borderColor;
  _controlsColor = controlsColor;
}

void DashWindow::setControlsVisible(bool minimize, bool maximize, bool close) {
  _showMinimize = minimize;
  _showMaximize = maximize;
  _showClose = close;
}

void DashWindow::setBackgroundBitmap(const uint16_t *rgb565Bits, int16_t bmpW, int16_t bmpH) {
  _bgBitmap = rgb565Bits;
  _bmpW = bmpW;
  _bmpH = bmpH;
}

void DashWindow::draw(uint16_t bgColor) {
  if (!_bgBitmap) return; // no bitmap chrome assigned -> nothing to draw (fallback removed)

  if (dash_hueRotateBitmaps && dash_hueRotation != 0) {
    // Reuse the same rotated draw path as other assets.
    gfx->startWrite();
    for (int16_t j = 0; j < _bmpH; j++) {
      for (int16_t i = 0; i < _bmpW; i++) {
        uint16_t px = pgm_read_word(&_bgBitmap[j * _bmpW + i]);
        if (px == DASH_TRANSPARENT_KEY) continue; // skip black -> transparent
        gfx->writePixel(_x + i, _y + j, dashboard_rotateHue565(px, dash_hueRotation));
      }
    }
    gfx->endWrite();
  } else {
    drawBitmapKeyed(_x, _y, _bgBitmap, _bmpW, _bmpH);
  }
}

// ---------------------------------------------------------------------
// Layout constants (from the bitmap-based Lopaka export)
// ---------------------------------------------------------------------
static const int16_t BG_X = 0, BG_Y = 0, BG_W = 320, BG_H = 240;
static const int16_t WIN_SKY_X = 3,   WIN_SKY_Y = 9,   WIN_SKY_W = 137, WIN_SKY_H = 156;
static const int16_t WIN_HEARTLIST_X = 145, WIN_HEARTLIST_Y = 6,  WIN_HEARTLIST_W = 155, WIN_HEARTLIST_H = 105;
static const int16_t WIN_PLAYER_X = 145, WIN_PLAYER_Y = 115, WIN_PLAYER_W = 155, WIN_PLAYER_H = 77; // playlist plate

// Song-name / artist bitmap plates (cover-art panel)
static const int16_t SONG_PLATE_X = 3,  SONG_PLATE_Y = 169, SONG_PLATE_W = 138, SONG_PLATE_H = 31;
static const int16_t ARTIST_PLATE_X = 24, ARTIST_PLATE_Y = 202, ARTIST_PLATE_W = 94, ARTIST_PLATE_H = 23;
static const int16_t SONG_TEXT_X = 15, SONG_TEXT_Y = 187;
static const int16_t ARTIST_TEXT_X = 37, ARTIST_TEXT_Y = 216;

// "Next:" label + next-song-name, floating over the playlist plate
static const int16_t NEXT_LABEL_X = 156, NEXT_LABEL_Y = 148;
static const int16_t NEXT_SONG_X = 155, NEXT_SONG_Y = 170;

// Transport buttons
static const int16_t BTN_REWIND_X = 185, BTN_REWIND_Y = 216, BTN_REWIND_W = 16, BTN_REWIND_H = 13;
static const int16_t BTN_FORWARD_X = 251, BTN_FORWARD_Y = 215, BTN_FORWARD_W = 16, BTN_FORWARD_H = 13;
static const int16_t BTN_PLAYPAUSE_X = 212, BTN_Y = 211, BTN_W = 25, BTN_H = 21;

// Progress bar: track + fill + thumb
static const int16_t PROGRESS_X = 145, PROGRESS_Y = 195, PROGRESS_W = 153, PROGRESS_H = 13;
static const int16_t PROGRESS_FILL_X = 148, PROGRESS_FILL_Y = 198, PROGRESS_FILL_W = 90, PROGRESS_FILL_H = 6;
static const int16_t PROGRESS_THUMB_Y = 193, PROGRESS_THUMB_W = 15, PROGRESS_THUMB_H = 15;

static const int16_t DASH_LYRICS_LINE_H = 18;
static const int16_t DASH_LYRICS_LINE_W = 120;

// ---------------------------------------------------------------------
// DashArtwork
// chrome = WIN_SKY bitmap. songName sits over the yellow-button bitmap
// (button art already gives it a nice background); artistName sits over
// the search-bar bitmap just above it. Adjust freely with setPos()/etc.
// if your asset proportions differ.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// DashArtwork
// chrome = cover-art bitmap. songName/artistName are each drawn on top
// of their own bitmap "plate" (image_song_name_pixels / image_artist_pixels)
// rather than a flat color background.
// ---------------------------------------------------------------------

DashArtwork::DashArtwork(int16_t x, int16_t y, int16_t w, int16_t h)
  : chrome(WIN_SKY_X, WIN_SKY_Y, WIN_SKY_W, WIN_SKY_H),
    songName(SONG_TEXT_X, SONG_TEXT_Y, SONG_PLATE_W - (SONG_TEXT_X - SONG_PLATE_X) - 4, 14, 1, 0x0000, 0xFFFF),
    artistName(ARTIST_TEXT_X, ARTIST_TEXT_Y, ARTIST_PLATE_W - (ARTIST_TEXT_X - ARTIST_PLATE_X) - 4, 12, 1, 0x0000, 0xFFFF),
    _x(x), _y(y), _w(w), _h(h), _frameColor(0xFFFF) {
  chrome.setBackgroundBitmap(image_cover_art_pixels, WIN_SKY_W, WIN_SKY_H);
  // Text plates draw directly over their bitmaps, so give the DashText
  // elements a transparent-looking (match-the-plate) background; callers
  // can override via setColors()/setBgColor() if the plate art differs.
  songName.setBgColor(0x0000);
  artistName.setBgColor(0x0000);
  songName.setFont(&FreeMonoBoldOblique9pt7b);
  artistName.setFont(&FreeMonoOblique9pt7b);
  setSongPlate(image_song_name__1__pixels, SONG_PLATE_X, SONG_PLATE_Y, SONG_PLATE_W, SONG_PLATE_H);
  setArtistPlate(image_artist__1__pixels, ARTIST_PLATE_X, ARTIST_PLATE_Y, ARTIST_PLATE_W, ARTIST_PLATE_H);
}

void DashArtwork::setSongPlate(const uint16_t *bmp, int16_t x, int16_t y, int16_t w, int16_t h) {
  _songPlateBmp = bmp;
  _songPlateX = x; _songPlateY = y; _songPlateW = w; _songPlateH = h;
}

void DashArtwork::setArtistPlate(const uint16_t *bmp, int16_t x, int16_t y, int16_t w, int16_t h) {
  _artistPlateBmp = bmp;
  _artistPlateX = x; _artistPlateY = y; _artistPlateW = w; _artistPlateH = h;
}

void DashArtwork::setColors(uint16_t frameColor, uint16_t songColor, uint16_t artistColor) {
  _frameColor = frameColor;
  songName.setTextColor(songColor);
  artistName.setTextColor(artistColor);
}

void DashArtwork::draw(uint16_t bgColor) {
  chrome.draw(bgColor);
  if (_songPlateBmp) drawAsset(_songPlateX, _songPlateY, _songPlateBmp, _songPlateW, _songPlateH);
  if (_artistPlateBmp) drawAsset(_artistPlateX, _artistPlateY, _artistPlateBmp, _artistPlateW, _artistPlateH);
  songName.draw();
  artistName.draw();
}

void DashArtwork::update() {
  songName.update();
  artistName.update();
}

// ---------------------------------------------------------------------
// DashLyrics
// chrome = WIN_HEARTLIST bitmap (already has heart bullets + dotted
// lines baked in for 6 rows). Text lines sit on top with a WHITE
// background to blend with the bitmap's white list area -- change via
// setColors()/each line's setBgColor() if your asset's white differs.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// DashLyrics
// chrome = lyrics-window bitmap. Only 4 lyric lines now (was 5).
// playlistLabel repurposed as the "Next:" caption; nextSongLabel is the
// next-track title -- both float over the DashPlayback playlist plate,
// which is why their x/y no longer derive from this window's (x,y).
// ---------------------------------------------------------------------

DashLyrics::DashLyrics(int16_t x, int16_t y)
  : chrome(WIN_HEARTLIST_X, WIN_HEARTLIST_Y, WIN_HEARTLIST_W, WIN_HEARTLIST_H),
    lines{
      DashText(x, y + 0 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 14, 1, 0x0000, 0xFFFF),
      DashText(x, y + 1 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 14, 1, 0x0000, 0xFFFF),
      DashText(x, y + 2 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 14, 1, 0x0000, 0xFFFF),
      DashText(x, y + 3 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 14, 1, 0x0000, 0xFFFF)
    },
    playlistLabel(NEXT_LABEL_X, NEXT_LABEL_Y, 40, 12, 1, 0x0000, 0xFFFF),
    nextSongLabel(NEXT_SONG_X, NEXT_SONG_Y, DASH_LYRICS_LINE_W, 14, 1, 0x0000, 0xFFFF) {
  chrome.setBackgroundBitmap(image_lyrics_pixels, WIN_HEARTLIST_W, WIN_HEARTLIST_H);
  playlistLabel.setText("Next:");
  playlistLabel.setFont(&FreeMonoOblique9pt7b);
  nextSongLabel.setFont(&FreeMonoOblique9pt7b);
}

void DashLyrics::setLine(uint8_t index, const String &text) {
  if (index < LINE_COUNT) lines[index].setText(text);
}

uint8_t DashLyrics::setWrappedLines(uint8_t startIndex, const String &text) {
  for (uint8_t i = startIndex; i < LINE_COUNT; i++) lines[i].setText("");

  uint8_t idx = startIndex;
  String current = "";
  int start = 0;
  int len = text.length();

  gfx->setTextSize(1);

  while (start < len && idx < LINE_COUNT) {
    int spaceIdx = text.indexOf(' ', start);
    String word = (spaceIdx == -1) ? text.substring(start) : text.substring(start, spaceIdx);
    String candidate = current.length() == 0 ? word : (current + " " + word);

    int16_t x1, y1;
    uint16_t w, h;
    gfx->getTextBounds(candidate, 0, 0, &x1, &y1, &w, &h);

    if (w > DASH_LYRICS_LINE_W && current.length() > 0) {
      lines[idx].setText(current);
      idx++;
      current = word;
    } else {
      current = candidate;
    }
    start = (spaceIdx == -1) ? len : spaceIdx + 1;
  }

  if (idx < LINE_COUNT && current.length() > 0) {
    lines[idx].setText(current);
    idx++;
  }
  return idx;
}

void DashLyrics::setColors(uint16_t lyricsColor, uint16_t playlistColor, uint16_t nextSongColor) {
  for (uint8_t i = 0; i < LINE_COUNT; i++) lines[i].setTextColor(lyricsColor);
  playlistLabel.setTextColor(playlistColor);
  nextSongLabel.setTextColor(nextSongColor);
}

void DashLyrics::setActiveLine(int8_t index, uint16_t highlightColor) {
  _activeLineIndex = index;
  _highlightColor = highlightColor;
}
void DashLyrics::setActiveLine(int8_t index) { _activeLineIndex = index; }

void DashLyrics::setLyricsSource(const String *lyricLines, uint16_t count) {
  _lyricSource = lyricLines;
  _lyricCount = count;
  _lyricCursor = 0;
  setActiveLine(ACTIVE_LINE_INDEX);
  refreshLyricsWindow();
}

void DashLyrics::setLyricsCursor(uint16_t index) {
  if (!_lyricSource) return;
  if (index >= _lyricCount) index = _lyricCount > 0 ? _lyricCount - 1 : 0;
  _lyricCursor = index;
  refreshLyricsWindow();
}

void DashLyrics::nextLyric() {
  if (!_lyricSource) return;
  if (_lyricCursor + 1 < _lyricCount) _lyricCursor++;
  refreshLyricsWindow();
}

void DashLyrics::previousLyric() {
  if (!_lyricSource) return;
  if (_lyricCursor > 0) _lyricCursor--;
  refreshLyricsWindow();
}

void DashLyrics::refreshLyricsWindow() {
  if (!_lyricSource) return;
  for (uint8_t row = 0; row < LINE_COUNT; row++) {
    int32_t srcIndex = (int32_t)_lyricCursor + ((int32_t)row - (int32_t)ACTIVE_LINE_INDEX);
    if (srcIndex >= 0 && srcIndex < (int32_t)_lyricCount) setLine(row, _lyricSource[srcIndex]);
    else setLine(row, "");
  }
}

void DashLyrics::draw(uint16_t bgColor) {
  chrome.draw(bgColor);

  for (uint8_t i = 0; i < LINE_COUNT; i++) {
    uint16_t lineBg = ((int8_t)i == _activeLineIndex) ? _highlightColor : lines[i].bgColor();
    lines[i].setBgColor(lineBg);
    lines[i].draw();
  }
  // NOTE: playlistLabel ("Next:") and nextSongLabel are drawn by
  // DashPlayback::draw() instead, since they float on top of the
  // playlist plate bitmap which DashPlayback owns and draws later.
}

void DashLyrics::update() {
  for (uint8_t i = 0; i < LINE_COUNT; i++) lines[i].update();
  playlistLabel.update();
  nextSongLabel.update();
}

// ---------------------------------------------------------------------
// DashPlayback
// chrome = WIN_PLAYER (equalizer) bitmap. Buttons default to the
// full-color Lopaka button art (addColorVariant), positioned per the
// original bitmap export's BTN_* coordinates (separate from the chrome
// window -- they sit below it).
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// DashPlayback
// chrome = playlist plate bitmap (occupies the slot the old WIN_PLAYER
// equalizer window used to). Buttons/progress bar are the new smaller
// Lopaka bitmaps; the progress bar also has a thumb bitmap whose x is
// recomputed each draw() from the current progress value.
// ---------------------------------------------------------------------

DashPlayback::DashPlayback(int16_t progressX, int16_t progressY, int16_t progressW, int16_t progressH,
                            int16_t prevX, int16_t playPauseX, int16_t nextX, int16_t iconY)
  : chrome(WIN_PLAYER_X, WIN_PLAYER_Y, WIN_PLAYER_W, WIN_PLAYER_H),
    prev(prevX, BTN_REWIND_Y), playPause(playPauseX, BTN_Y), next(nextX, BTN_FORWARD_Y),
    _progX(progressX), _progY(progressY), _progW(progressW), _progH(progressH),
    _progressColor(0xFFFF), _progress(0.0f), _state(DASH_PAUSED), _iconsInitialized(false) {
  chrome.setBackgroundBitmap(image_playlist_pixels, WIN_PLAYER_W, WIN_PLAYER_H);
  setThumbBitmap(image_current_play_pixels, PROGRESS_THUMB_Y, PROGRESS_THUMB_W, PROGRESS_THUMB_H);
}

void DashPlayback::setThumbBitmap(const uint16_t *bmp, int16_t y, int16_t w, int16_t h) {
  _thumbBmp = bmp;
  _thumbY = y; _thumbW = w; _thumbH = h;
}

void DashPlayback::initIconsIfNeeded() {
  if (_iconsInitialized) return;
  _iconsInitialized = true;

  // Full-color Lopaka button art (default). Swap to addVariant(...) with
  // the monochrome image_music_*_bits above if you'd rather have tinted
  // icons that respond to setIconsColor().
  prev.addColorVariant(image_skip_back_pixels, BTN_REWIND_W, BTN_REWIND_H);
  next.addColorVariant(image_skip_next_pixels, BTN_FORWARD_W, BTN_FORWARD_H);
  playPause.addColorVariant(image_resume_pixels, BTN_W, BTN_H); // variant 0 = paused (show "resume")
  playPause.addColorVariant(image_pause_pixels, BTN_W, BTN_H);  // variant 1 = playing (show "pause")
}

void DashPlayback::setState(DashPlayState state) {
  _state = state;
  playPause.setVariant(state == DASH_PLAYING ? 1 : 0);
}

void DashPlayback::setProgress(float pct01) {
  if (pct01 < 0.0f) pct01 = 0.0f;
  if (pct01 > 1.0f) pct01 = 1.0f;
  _progress = pct01;
}

void DashPlayback::setIconsColor(uint16_t color) {
  // Only affects icons using monochrome addVariant() bitmaps; full-color
  // addColorVariant() bitmaps are drawn as-is and ignore this.
  prev.setColor(color);
  playPause.setColor(color);
  next.setColor(color);
}

void DashPlayback::draw(uint16_t bgColor) {
  initIconsIfNeeded();

  chrome.draw(bgColor);

  drawAsset(_progX, _progY, image_emty_bar_pixels, _progW, _progH);
  int16_t fillW = (int16_t)(PROGRESS_FILL_W * _progress);
  if (fillW > 0) drawAsset(PROGRESS_FILL_X, PROGRESS_FILL_Y, image_progression_bar_pixels, fillW, PROGRESS_FILL_H);

  if (_thumbBmp) {
    int16_t thumbTravel = PROGRESS_FILL_W - _thumbW;
    if (thumbTravel < 0) thumbTravel = 0;
    int16_t thumbX = PROGRESS_FILL_X + (int16_t)(thumbTravel * _progress);
    drawAsset(thumbX, _thumbY, _thumbBmp, _thumbW, _thumbH);
  }

  prev.draw(bgColor);
  playPause.draw(bgColor);
  next.draw(bgColor);
}

// ---------------------------------------------------------------------
// Top-level dashboard instances
// ---------------------------------------------------------------------

DashArtwork  dash_artwork(WIN_SKY_X, WIN_SKY_Y, WIN_SKY_W, WIN_SKY_H);
DashLyrics   dash_lyrics(WIN_HEARTLIST_X + 11, WIN_HEARTLIST_Y + 21);
DashPlayback dash_playback(PROGRESS_X, PROGRESS_Y, PROGRESS_W, PROGRESS_H,
                            BTN_REWIND_X, BTN_PLAYPAUSE_X, BTN_FORWARD_X, BTN_Y);

void dashboard_setColorScheme(uint16_t mainColor, bool redraw) {
  dash_artwork.setColors(mainColor, mainColor, mainColor);
  dash_lyrics.setColors(mainColor, mainColor, mainColor);
  dash_playback.setProgressColor(mainColor);
  dash_playback.setIconsColor(mainColor);
  if (redraw) dashboard_draw();
}

void dashboard_draw(void) {
  drawAsset(BG_X, BG_Y, image_background__3__pixels, BG_W, BG_H);
  dash_artwork.draw(dash_colorBackground);
  dash_lyrics.draw(dash_colorBackground);
  dash_playback.draw(dash_colorBackground);
  // "Next:" caption + next-song title float over the playlist plate that
  // dash_playback.chrome just drew, so they must be drawn last.
  dash_lyrics.playlistLabel.draw();
  dash_lyrics.nextSongLabel.draw();
}

void dashboard_updateArtwork(void) {
  dash_artwork.update();
}

void dashboard_updateLyrics(void) {
  dash_lyrics.update();
}

void dashboard_updatePlayback(void) {
  // DashPlayback has no scroll/animation state today, so nothing to
  // advance here yet -- kept as its own function so callers can treat
  // all three primary components uniformly, and so it's ready to wire
  // up once DashPlayback gains animated state (e.g. a smooth progress
  // bar tween or an icon animation).
}

void dashboard_update(void) {
  dashboard_updateArtwork();
  dashboard_updateLyrics();
  dashboard_updatePlayback();
}