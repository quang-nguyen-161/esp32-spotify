#include "text_utils.h"

namespace TextUtils {

  // Maps a Vietnamese (accented) Unicode codepoint to its base ASCII character.
  // Returns 0 if this isn't a Vietnamese accented character this module knows about.
  static char mapVietnameseCodepoint(uint32_t cp) {
    switch (cp) {
      // ---- a (Latin-1 Supplement + Latin Extended-A) ----
      case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x0103:
        return 'a';
      case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x0102:
        return 'A';

      // ---- e ----
      case 0x00E8: case 0x00E9: case 0x00EA:
        return 'e';
      case 0x00C8: case 0x00C9: case 0x00CA:
        return 'E';

      // ---- i ----
      case 0x00EC: case 0x00ED:
        return 'i';
      case 0x00CC: case 0x00CD:
        return 'I';

      // ---- o ----
      case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x01A1:
        return 'o';
      case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x01A0:
        return 'O';

      // ---- u ----
      case 0x00F9: case 0x00FA: case 0x01B0:
        return 'u';
      case 0x00D9: case 0x00DA: case 0x01AF:
        return 'U';

      // ---- y ----
      case 0x00FD:
        return 'y';
      case 0x00DD:
        return 'Y';

      // ---- d ----
      case 0x0111:
        return 'd';
      case 0x0110:
        return 'D';

      // ---- Latin Extended Additional (U+1EA0 - U+1EF9): Vietnamese diacritic combos ----
      // a
      case 0x1EA1: case 0x1EA3: case 0x1EA5: case 0x1EA7: case 0x1EA9:
      case 0x1EAB: case 0x1EAD: case 0x1EAF: case 0x1EB1: case 0x1EB3:
      case 0x1EB5: case 0x1EB7:
        return 'a';
      case 0x1EA0: case 0x1EA2: case 0x1EA4: case 0x1EA6: case 0x1EA8:
      case 0x1EAA: case 0x1EAC: case 0x1EAE: case 0x1EB0: case 0x1EB2:
      case 0x1EB4: case 0x1EB6:
        return 'A';

      // e
      case 0x1EB9: case 0x1EBB: case 0x1EBD: case 0x1EBF: case 0x1EC1:
      case 0x1EC3: case 0x1EC5: case 0x1EC7:
        return 'e';
      case 0x1EB8: case 0x1EBA: case 0x1EBC: case 0x1EBE: case 0x1EC0:
      case 0x1EC2: case 0x1EC4: case 0x1EC6:
        return 'E';

      // i
      case 0x1EC9: case 0x1ECB:
        return 'i';
      case 0x1EC8: case 0x1ECA:
        return 'I';

      // o
      case 0x1ECD: case 0x1ECF: case 0x1ED1: case 0x1ED3: case 0x1ED5:
      case 0x1ED7: case 0x1ED9: case 0x1EDB: case 0x1EDD: case 0x1EDF:
      case 0x1EE1: case 0x1EE3:
        return 'o';
      case 0x1ECC: case 0x1ECE: case 0x1ED0: case 0x1ED2: case 0x1ED4:
      case 0x1ED6: case 0x1ED8: case 0x1EDA: case 0x1EDC: case 0x1EDE:
      case 0x1EE0: case 0x1EE2:
        return 'O';

      // u
      case 0x1EE5: case 0x1EE7: case 0x1EE9: case 0x1EEB: case 0x1EED:
      case 0x1EEF: case 0x1EF1:
        return 'u';
      case 0x1EE4: case 0x1EE6: case 0x1EE8: case 0x1EEA: case 0x1EEC:
      case 0x1EEE: case 0x1EF0:
        return 'U';

      // y
      case 0x1EF3: case 0x1EF5: case 0x1EF7: case 0x1EF9:
        return 'y';
      case 0x1EF2: case 0x1EF4: case 0x1EF6: case 0x1EF8:
        return 'Y';

      default:
        return 0; // not a Vietnamese accented character this function recognizes
    }
  }

  String stripDiacriticsUtf8(const String& utf8Str) {
    String out;
    out.reserve(utf8Str.length()); // the result is always <= the length of the original string

    size_t i = 0;
    size_t len = utf8Str.length();

    while (i < len) {
      uint8_t b0 = (uint8_t)utf8Str[i];

      if (b0 < 0x80) {
        // 1-byte: plain ASCII, keep as-is
        out += (char)b0;
        i += 1;
        continue;
      }

      // Determine how many bytes this UTF-8 character uses, based on the leading byte
      int numBytes = 0;
      uint32_t cp = 0;

      if ((b0 & 0xE0) == 0xC0) {         // 110xxxxx -> 2 bytes
        numBytes = 2;
        cp = b0 & 0x1F;
      } else if ((b0 & 0xF0) == 0xE0) {  // 1110xxxx -> 3 bytes
        numBytes = 3;
        cp = b0 & 0x0F;
      } else if ((b0 & 0xF8) == 0xF0) {  // 11110xxx -> 4 bytes
        numBytes = 4;
        cp = b0 & 0x07;
      } else {
        // Invalid leading byte for UTF-8 (e.g. a stray continuation byte) -> skip it
        i += 1;
        continue;
      }

      if (i + numBytes > len) {
        // String was truncated mid multi-byte character -> stop here to avoid reading garbage
        break;
      }

      bool validSequence = true;
      for (int k = 1; k < numBytes; k++) {
        uint8_t bk = (uint8_t)utf8Str[i + k];
        if ((bk & 0xC0) != 0x80) { // continuation bytes must be 10xxxxxx
          validSequence = false;
          break;
        }
        cp = (cp << 6) | (bk & 0x3F);
      }

      if (!validSequence) {
        i += 1;
        continue;
      }

      char mapped = mapVietnameseCodepoint(cp);
      if (mapped != 0) {
        out += mapped;
      } else {
        // Other Unicode character (not a recognized Vietnamese accented letter) -> use '?'
        // to avoid drawing garbage/blank boxes with an ASCII-only font.
        out += '?';
      }

      i += numBytes;
    }

    return out;
  }

} // namespace TextUtils
