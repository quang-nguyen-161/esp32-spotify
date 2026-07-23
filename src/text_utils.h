#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <Arduino.h>

/*
  Module: text_utils
  Why this module is needed (see esp32-plan_2.md, "UTF-8 / Vietnamese text note"):
    - The server already normalizes text to clean UTF-8 (NFC), so the bytes
      received are correct.
    - BUT the default Adafruit_GFX / TFT_eSPI fonts only include ASCII glyphs -
      no a-breve, a-circumflex, e-circumflex, o-circumflex, o-horn, u-horn, d-stroke, etc.
      Drawing correct UTF-8 with such a font just produces blank boxes or garbage.
    - The simplest fix (no custom font / U8g2 needed) is to strip diacritics,
      converting Vietnamese accented characters back to their base ASCII letter
      before drawing.

  Usage:
    String clean = TextUtils::stripDiacriticsUtf8(rawUtf8String);
    tft.println(clean);
*/

namespace TextUtils {

  // Takes a UTF-8 string (which may contain Vietnamese accented characters) and
  // returns a plain ASCII string (a-z, A-Z, 0-9, punctuation, spaces...) with
  // diacritics stripped.
  // Any other UTF-8 characters (non-Vietnamese, e.g. emoji, CJK characters) are
  // replaced with '?' to avoid drawing garbage on screen.
  String stripDiacriticsUtf8(const String& utf8Str);

} // namespace TextUtils

#endif // TEXT_UTILS_H
