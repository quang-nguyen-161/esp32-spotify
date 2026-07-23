#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "dashboard_assets.h"

// ---------------------------------------------------------------------
// dashboard.h
// Class-based music player dashboard (DashElement -> DashText/DashIcon/
// DashWindow -> DashArtwork/DashLyrics/DashPlayback), now with the
// full-color (RGB565) Lopaka bitmap assets wired in:
//   - background grid          -> drawn full-screen in dashboard_draw()
//   - window chrome bitmaps    -> DashWindow::setBackgroundBitmap()
//   - color playback buttons   -> DashIcon::addColorVariant()
// Plus a hue-rotation helper to re-theme the whole dashboard (flat
// colors always; bitmaps too, if you enable per-pixel rotation).
// ---------------------------------------------------------------------

extern Adafruit_ST7789 *gfx;

uint16_t dash_darkenColor565(uint16_t color, uint8_t pct);

// ---- Hue rotation helpers (get/change the dashboard's main color) ----
// Rotates an RGB565 color's hue by `degrees` (can be negative, wraps).
uint16_t dashboard_rotateHue565(uint16_t color565, int16_t degrees);

// The dashboard's reference "main color" (before rotation). Defaults to
// the yellow button accent used across the bitmap assets.
extern uint16_t dash_colorMainBase;

// Current hue rotation offset in degrees (0 = no change from base colors).
extern int16_t dash_hueRotation;

// Returns the CURRENT effective main color (base rotated by dash_hueRotation).
uint16_t dashboard_getMainColor(void);

// Sets the rotation offset and re-applies it to every flat dashboard
// color (frame/text/progress/icon tint colors). Does NOT recolor the
// RGB565 bitmap assets themselves (that requires per-pixel rotation,
// see dashboard_setHueRotationBitmaps() below) unless enabled.
// redraw=true also triggers an immediate dashboard_draw().
void dashboard_setHueRotation(int16_t degrees, bool redraw = true);

// Also rotate the hue of the bitmap assets themselves (background,
// window chrome, buttons) when redrawing. This is per-pixel and costs
// noticeably more CPU/time per redraw -- off by default.
extern bool dash_hueRotateBitmaps;
void dashboard_setHueRotateBitmaps(bool enabled);

#define DASH_HOVER_LIFT_PX 3
#define DASH_HOVER_DARKEN_PCT 40
#define DASH_ICON_MAX_VARIANTS 3

#define DASH_TEXT_SCROLL_GAP_PX 20
#define DASH_TEXT_DEFAULT_STEP_PX 1
#define DASH_TEXT_DEFAULT_INTERVAL_MS 40

// ---------------------------------------------------------------------
// DashElement (shared base): priority + direction, reserved for future
// layout/animation logic. Every class below inherits this.
// ---------------------------------------------------------------------
enum DashPriority { DASH_PRIORITY_PRIMARY, DASH_PRIORITY_SECONDARY, DASH_PRIORITY_TERTIARY };
enum DashDirection { DASH_DIR_LEFT, DASH_DIR_RIGHT, DASH_DIR_UP, DASH_DIR_DOWN };

class DashElement {
public:
  void setPriority(DashPriority p) { _priority = p; }
  DashPriority priority() const { return _priority; }
  void setDirection(DashDirection d) { _direction = d; }
  DashDirection direction() const { return _direction; }
protected:
  DashPriority _priority = DASH_PRIORITY_PRIMARY;
  DashDirection _direction = DASH_DIR_LEFT;
};

// ---------------------------------------------------------------------
// DashText (sub-class): scrolling text container, own w/h/text/colors.
// ---------------------------------------------------------------------
class DashText : public DashElement {
public:
  DashText(int16_t x, int16_t y, int16_t w, int16_t h,
           uint8_t textSize = 1, uint16_t textColor = 0xFFFF, uint16_t bgColor = 0x0000);
  ~DashText();

  void setText(const String &text);
  const String &text() const { return _text; }

  void setSize(int16_t w, int16_t h);
  void setPos(int16_t x, int16_t y);
  int16_t width() const { return _w; }
  int16_t height() const { return _h; }

  void setTextColor(uint16_t color);
  uint16_t textColor() const { return _color; }
  void setBgColor(uint16_t color);
  uint16_t bgColor() const { return _bgColor; }

  void setScrollSpeed(uint16_t pxPerStep, uint16_t stepIntervalMs);
  bool isScrolling() const { return _needsScroll; }

  void update();
  void draw();

private:
  int16_t _x, _y, _w, _h;
  uint8_t _textSize;
  uint16_t _color, _bgColor;
  String _text;
  int16_t _textWidthPx;
  bool _needsScroll;
  int16_t _scrollX;
  unsigned long _lastStepMs;
  uint16_t _stepIntervalMs, _pxPerStep;
  GFXcanvas16 *_canvas;

  void allocCanvas();
  void measure();
  void render();
};

// ---------------------------------------------------------------------
// DashIcon (sub-class): stateful icon/button. Supports TWO kinds of
// sub-icons ("variants"):
//   - addVariant()      : monochrome bitmap, tinted with setColor()/hover
//   - addColorVariant() : full-color RGB565 bitmap (e.g. the
//                          Lopaka playback button art) drawn as-is; hover
//                          feedback is position-lift only (no darken,
//                          since darkening a full-color image per pixel
//                          on every hover toggle is comparatively costly).
// ---------------------------------------------------------------------
class DashIcon : public DashElement {
public:
  DashIcon(int16_t x, int16_t y, uint16_t color = 0xFFFF);

  uint8_t addVariant(const unsigned char *bits, int16_t w, int16_t h);       // monochrome, tinted
  uint8_t addColorVariant(const uint16_t *rgb565Bits, int16_t w, int16_t h); // full-color, as-is
  void setVariant(uint8_t index);
  uint8_t variant() const { return _activeVariant; }

  void setColor(uint16_t color) { _color = color; }
  uint16_t color() const { return _color; }

  void setHover(bool hovered);
  bool hovered() const { return _hover; }

  uint16_t effectiveColor() const;
  void draw(uint16_t bgColor);

private:
  int16_t _x, _y;
  uint16_t _color;
  bool _hover;
  uint8_t _activeVariant;
  uint8_t _variantCount;
  int16_t _maxW, _maxH;

  struct Variant {
    bool isColor;
    const unsigned char *monoBits;
    const uint16_t *colorBits;
    int16_t w, h;
  };
  Variant _variants[DASH_ICON_MAX_VARIANTS];
};

// ---------------------------------------------------------------------
// DashWindow (sub-class): retro window chrome. Either drawn from
// primitives (border + title bar + controls, original behavior) OR, if
// setBackgroundBitmap() is given a full-color RGB565 asset, drawn as
// that bitmap directly (matching the Lopaka window-frame art).
// ---------------------------------------------------------------------
class DashWindow : public DashElement {
public:
  DashWindow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t titleBarH = 16);

  void setTitle(const String &title) { titleText.setText(title); }
  void setTitleBarColor(uint16_t color) { _titleBarColor = color; }
  void setBorderColor(uint16_t color) { _borderColor = color; }
  void setControlsColor(uint16_t color) { _controlsColor = color; }
  void setColors(uint16_t titleBarColor, uint16_t borderColor, uint16_t controlsColor);
  void setControlsVisible(bool minimize, bool maximize, bool close);

  // When set, draw() renders this bitmap instead of the primitive chrome.
  void setBackgroundBitmap(const uint16_t *rgb565Bits, int16_t bmpW, int16_t bmpH);
  bool hasBackgroundBitmap() const { return _bgBitmap != nullptr; }

  void draw(uint16_t bgColor);
  void update() { titleText.update(); }

  int16_t contentX() const { return _x + 1; }
  int16_t contentY() const { return _y + _titleBarH + 1; }
  int16_t contentW() const { return _w - 2; }
  int16_t contentH() const { return _h - _titleBarH - 2; }

  DashText titleText;

private:
  int16_t _x, _y, _w, _h, _titleBarH;
  uint16_t _titleBarColor, _borderColor, _controlsColor;
  bool _showMinimize, _showMaximize, _showClose;

  const uint16_t *_bgBitmap = nullptr;
  int16_t _bmpW = 0, _bmpH = 0;
};

// ---------------------------------------------------------------------
// DashArtwork (main class): sky-preview window + song name + artist.
// Chrome is the WIN_SKY bitmap; song name / artist sit below it, over
// the search-bar and yellow-button bitmaps from the same asset set.
// ---------------------------------------------------------------------
class DashArtwork : public DashElement {
public:
  DashArtwork(int16_t x, int16_t y, int16_t w, int16_t h);

  void setSongName(const String &name) { songName.setText(name); }
  void setArtist(const String &artist) { artistName.setText(artist); }

  void setFrameColor(uint16_t color) { _frameColor = color; }
  void setColors(uint16_t frameColor, uint16_t songColor, uint16_t artistColor);

  void draw(uint16_t bgColor);
  void update();

  DashWindow chrome; // WIN_SKY bitmap chrome
  DashText songName;
  DashText artistName;

private:
  int16_t _x, _y, _w, _h;
  uint16_t _frameColor;
};

// ---------------------------------------------------------------------
// DashLyrics (main class): heart-list window + lyric lines/labels.
// ---------------------------------------------------------------------
class DashLyrics : public DashElement {
public:
  static const uint8_t LINE_COUNT = 5;
  static const uint8_t ACTIVE_LINE_INDEX = 2; // 0-based -> "line 3"

  DashLyrics(int16_t x, int16_t y);

  void setLine(uint8_t index, const String &text);
  void setPlaylistLabel(const String &text) { playlistLabel.setText(text); }
  void setNextSongLabel(const String &text) { nextSongLabel.setText(text); }

  uint8_t setWrappedLines(uint8_t startIndex, const String &text);
  uint8_t setLyrics(const String &text) { return setWrappedLines(0, text); }

  void setColors(uint16_t lyricsColor, uint16_t playlistColor, uint16_t nextSongColor);

  void setActiveLine(int8_t index, uint16_t highlightColor);
  void setActiveLine(int8_t index);
  void clearActiveLine() { _activeLineIndex = -1; }
  int8_t activeLine() const { return _activeLineIndex; }

  void setLyricsSource(const String *lyricLines, uint16_t count);
  void setLyricsCursor(uint16_t index);
  void nextLyric();
  void previousLyric();
  uint16_t lyricsCursor() const { return _lyricCursor; }

  void draw(uint16_t bgColor);
  void update();

  DashWindow chrome; // WIN_HEARTLIST bitmap chrome
  DashText lines[LINE_COUNT];
  DashText playlistLabel;
  DashText nextSongLabel;

private:
  int8_t _activeLineIndex = ACTIVE_LINE_INDEX;
  uint16_t _highlightColor = 0xFFE0;

  const String *_lyricSource = nullptr;
  uint16_t _lyricCount = 0;
  uint16_t _lyricCursor = 0;

  void refreshLyricsWindow();
};

// ---------------------------------------------------------------------
// DashPlayback (main class): equalizer window + prev/play-pause/next
// buttons, now drawn from the full-color Lopaka button bitmaps.
// ---------------------------------------------------------------------
enum DashPlayState { DASH_PAUSED, DASH_PLAYING };

class DashPlayback : public DashElement {
public:
  DashPlayback(int16_t progressX, int16_t progressY, int16_t progressW, int16_t progressH,
               int16_t prevX, int16_t playPauseX, int16_t nextX, int16_t iconY);

  void setState(DashPlayState state);
  DashPlayState state() const { return _state; }

  void setProgress(float pct01);
  void setProgressColor(uint16_t color) { _progressColor = color; }
  void setIconsColor(uint16_t color); // only affects monochrome icon variants, if used

  void draw(uint16_t bgColor);

  DashWindow chrome; // WIN_PLAYER (equalizer) bitmap chrome
  DashIcon prev;
  DashIcon playPause;
  DashIcon next;

private:
  int16_t _progX, _progY, _progW, _progH;
  uint16_t _progressColor;
  float _progress;
  DashPlayState _state;
  bool _iconsInitialized;

  void initIconsIfNeeded();
};

// ---------------------------------------------------------------------
// Top-level dashboard instances + global background color
// ---------------------------------------------------------------------
extern uint16_t dash_colorBackground;

extern DashArtwork dash_artwork;
extern DashLyrics dash_lyrics;
extern DashPlayback dash_playback;

void dashboard_setColorScheme(uint16_t mainColor, bool redraw = true);
void dashboard_update(void);
void dashboard_draw(void);