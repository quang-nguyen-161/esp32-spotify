#include "dashboard.h"

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

uint16_t dash_colorBackground = 0x0000;

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
      gfx->writePixel(x + i, y + j, dashboard_rotateHue565(px, degrees));
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
    gfx->drawRGBBitmap(x, y, bmp, w, h);
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
    _canvas(nullptr) {
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
  render();
}

void DashText::setPos(int16_t x, int16_t y) { _x = x; _y = y; }

void DashText::setTextColor(uint16_t color) { _color = color; render(); }
void DashText::setBgColor(uint16_t color)   { _bgColor = color; render(); }

void DashText::setScrollSpeed(uint16_t pxPerStep, uint16_t stepIntervalMs) {
  _pxPerStep = pxPerStep;
  _stepIntervalMs = stepIntervalMs;
}

void DashText::measure() {
  if (!_canvas) return;
  int16_t x1, y1;
  uint16_t w, h;
  _canvas->setTextSize(_textSize);
  _canvas->getTextBounds(_text, 0, 0, &x1, &y1, &w, &h);
  _textWidthPx = (int16_t)w;
  _needsScroll = _textWidthPx > _w;
}

void DashText::setText(const String &text) {
  _text = text;
  _scrollX = 0;
  _lastStepMs = millis();
  measure();
  render();
}

void DashText::render() {
  if (!_canvas) return;

  _canvas->fillScreen(_bgColor);
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
}

void DashText::draw() { render(); }

void DashText::update() {
  if (!_needsScroll) return;
  unsigned long now = millis();
  if (now - _lastStepMs < _stepIntervalMs) return;
  _lastStepMs = now;

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
    // Full-color bitmap: drawn as-is (no tint). Hover feedback is the
    // position lift only -- no per-pixel darken here to keep redraws cheap.
    gfx->drawRGBBitmap(_x, drawY, v.colorBits, v.w, v.h);
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
  if (_bgBitmap) {
    if (dash_hueRotateBitmaps && dash_hueRotation != 0) {
      // Reuse the same rotated draw path as other assets.
      gfx->startWrite();
      for (int16_t j = 0; j < _bmpH; j++) {
        for (int16_t i = 0; i < _bmpW; i++) {
          uint16_t px = pgm_read_word(&_bgBitmap[j * _bmpW + i]);
          gfx->writePixel(_x + i, _y + j, dashboard_rotateHue565(px, dash_hueRotation));
        }
      }
      gfx->endWrite();
    } else {
      gfx->drawRGBBitmap(_x, _y, _bgBitmap, _bmpW, _bmpH);
    }
    return; // bitmap chrome replaces primitive drawing + title text entirely
  }

  // ---- Fallback: primitive-drawn chrome (no bitmap asset assigned) ----
  gfx->drawRect(_x, _y, _w, _h, _borderColor);
  gfx->fillRect(_x + 1, _y + 1, _w - 2, _titleBarH - 1, _titleBarColor);

  titleText.setBgColor(_titleBarColor);
  titleText.draw();

  int16_t boxSize = _titleBarH - 6;
  int16_t boxY = _y + 3;
  int16_t cursorX = _x + _w - 4 - boxSize;

  if (_showClose) {
    gfx->drawRect(cursorX, boxY, boxSize, boxSize, _controlsColor);
    gfx->drawLine(cursorX + 2, boxY + 2, cursorX + boxSize - 3, boxY + boxSize - 3, _controlsColor);
    gfx->drawLine(cursorX + boxSize - 3, boxY + 2, cursorX + 2, boxY + boxSize - 3, _controlsColor);
    cursorX -= boxSize + 3;
  }
  if (_showMaximize) {
    gfx->drawRect(cursorX, boxY, boxSize, boxSize, _controlsColor);
    gfx->drawRect(cursorX + 2, boxY + 2, boxSize - 4, boxSize - 4, _controlsColor);
    cursorX -= boxSize + 3;
  }
  if (_showMinimize) {
    gfx->drawRect(cursorX, boxY, boxSize, boxSize, _controlsColor);
    gfx->drawLine(cursorX + 2, boxY + boxSize - 3, cursorX + boxSize - 3, boxY + boxSize - 3, _controlsColor);
  }

  gfx->fillRect(contentX(), contentY(), contentW(), contentH(), bgColor);
}

// ---------------------------------------------------------------------
// Layout constants (from the bitmap-based Lopaka export)
// ---------------------------------------------------------------------
static const int16_t BG_X = -6, BG_Y = -1, BG_W = 321, BG_H = 240;
static const int16_t WIN_SKY_X = 9,   WIN_SKY_Y = 5,   WIN_SKY_W = 144, WIN_SKY_H = 164;
static const int16_t WIN_HEARTLIST_X = 164, WIN_HEARTLIST_Y = 10,  WIN_HEARTLIST_W = 145, WIN_HEARTLIST_H = 100;
static const int16_t WIN_PLAYER_X = 164, WIN_PLAYER_Y = 118, WIN_PLAYER_W = 145, WIN_PLAYER_H = 66;
static const int16_t SEARCH_BAR_X = 11, SEARCH_BAR_Y = 176, SEARCH_BAR_W = 143, SEARCH_BAR_H = 27;
static const int16_t BTN_YELLOW_X = 21, BTN_YELLOW_Y = 209, BTN_YELLOW_W = 123, BTN_YELLOW_H = 20;
static const int16_t BTN_REWIND_X = 188, BTN_PLAYPAUSE_X = 224, BTN_FORWARD_X = 267;
static const int16_t BTN_Y = 194, BTN_W = 25, BTN_H = 24;
static const int16_t PROGRESS_X = 191, PROGRESS_Y = 181, PROGRESS_W = 110, PROGRESS_H = 6;
static const int16_t DASH_LYRICS_LINE_H = 15;
static const int16_t DASH_LYRICS_LINE_W = 130;

// ---------------------------------------------------------------------
// DashArtwork
// chrome = WIN_SKY bitmap. songName sits over the yellow-button bitmap
// (button art already gives it a nice background); artistName sits over
// the search-bar bitmap just above it. Adjust freely with setPos()/etc.
// if your asset proportions differ.
// ---------------------------------------------------------------------

DashArtwork::DashArtwork(int16_t x, int16_t y, int16_t w, int16_t h)
  : chrome(WIN_SKY_X, WIN_SKY_Y, WIN_SKY_W, WIN_SKY_H),
    songName(BTN_YELLOW_X + 4, BTN_YELLOW_Y + 3, BTN_YELLOW_W - 8, BTN_YELLOW_H - 6, 1, 0x0000, 0xFFE0),
    artistName(SEARCH_BAR_X + 4, SEARCH_BAR_Y + 8, SEARCH_BAR_W - 8, 12, 1, 0x0000, 0xFFFF),
    _x(x), _y(y), _w(w), _h(h), _frameColor(0xFFFF) {
  chrome.setBackgroundBitmap(image_window_sky_pixels, WIN_SKY_W, WIN_SKY_H);
}

void DashArtwork::setColors(uint16_t frameColor, uint16_t songColor, uint16_t artistColor) {
  _frameColor = frameColor;
  songName.setTextColor(songColor);
  artistName.setTextColor(artistColor);
}

void DashArtwork::draw(uint16_t bgColor) {
  chrome.draw(bgColor);
  drawAsset(SEARCH_BAR_X, SEARCH_BAR_Y, image_search_bar_pixels, SEARCH_BAR_W, SEARCH_BAR_H);
  drawAsset(BTN_YELLOW_X, BTN_YELLOW_Y, image_btn_yellow_rect_pixels, BTN_YELLOW_W, BTN_YELLOW_H);
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

DashLyrics::DashLyrics(int16_t x, int16_t y)
  : chrome(WIN_HEARTLIST_X, WIN_HEARTLIST_Y, WIN_HEARTLIST_W, WIN_HEARTLIST_H),
    lines{
      DashText(x, y + 0 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 12, 1, 0x0000, 0xFFFF),
      DashText(x, y + 1 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 12, 1, 0x0000, 0xFFFF),
      DashText(x, y + 2 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 12, 1, 0x0000, 0xFFFF),
      DashText(x, y + 3 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 12, 1, 0x0000, 0xFFFF),
      DashText(x, y + 4 * DASH_LYRICS_LINE_H, DASH_LYRICS_LINE_W, 12, 1, 0x0000, 0xFFFF)
    },
    playlistLabel(x - 2, y + 118, DASH_LYRICS_LINE_W + 20, 12, 1, 0x0000, 0xFFFF),
    nextSongLabel(x + 1, y + 130, DASH_LYRICS_LINE_W + 20, 12, 1, 0x0000, 0xFFFF) {
  chrome.setBackgroundBitmap(image_window_heartlist_pixels, WIN_HEARTLIST_W, WIN_HEARTLIST_H);
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
  playlistLabel.draw();
  nextSongLabel.draw();
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

DashPlayback::DashPlayback(int16_t progressX, int16_t progressY, int16_t progressW, int16_t progressH,
                            int16_t prevX, int16_t playPauseX, int16_t nextX, int16_t iconY)
  : chrome(WIN_PLAYER_X, WIN_PLAYER_Y, WIN_PLAYER_W, WIN_PLAYER_H),
    prev(prevX, iconY), playPause(playPauseX, iconY), next(nextX, iconY),
    _progX(progressX), _progY(progressY), _progW(progressW), _progH(progressH),
    _progressColor(0xFFFF), _progress(0.0f), _state(DASH_PAUSED), _iconsInitialized(false) {
  chrome.setBackgroundBitmap(image_window_musicplayer_pixels, WIN_PLAYER_W, WIN_PLAYER_H);
}

void DashPlayback::initIconsIfNeeded() {
  if (_iconsInitialized) return;
  _iconsInitialized = true;

  // Full-color Lopaka button art (default). Swap to addVariant(...) with
  // the monochrome image_music_*_bits above if you'd rather have tinted
  // icons that respond to setIconsColor().
  prev.addColorVariant(image_btn_rewind_full_pixels, BTN_W, BTN_H);
  next.addColorVariant(image_btn_forward_pixels, BTN_W, BTN_H);
  playPause.addColorVariant(image_wc_play_blue_pixels, BTN_W, BTN_H);   // variant 0 = play
  playPause.addColorVariant(image_wc_pause_yellow_pixels, BTN_W, BTN_H); // variant 1 = pause
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

  gfx->drawRect(_progX, _progY, _progW, _progH, _progressColor);
  int16_t fillW = (int16_t)((_progW - 2) * _progress);
  gfx->fillRect(_progX + 1, _progY + 1, _progW - 2, _progH - 2, bgColor);
  if (fillW > 0) gfx->fillRect(_progX + 1, _progY + 1, fillW, _progH - 2, _progressColor);

  prev.draw(bgColor);
  playPause.draw(bgColor);
  next.draw(bgColor);
}

// ---------------------------------------------------------------------
// Top-level dashboard instances
// ---------------------------------------------------------------------

DashArtwork  dash_artwork(WIN_SKY_X, WIN_SKY_Y, WIN_SKY_W, WIN_SKY_H);
DashLyrics   dash_lyrics(WIN_HEARTLIST_X + 30, WIN_HEARTLIST_Y + 15);
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
  drawAsset(BG_X, BG_Y, image_bg_grid_large_pixels, BG_W, BG_H);
  dash_artwork.draw(dash_colorBackground);
  dash_lyrics.draw(dash_colorBackground);
  dash_playback.draw(dash_colorBackground);
}

void dashboard_update(void) {
  dash_artwork.update();
  dash_lyrics.update();
}