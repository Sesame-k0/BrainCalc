#ifndef MISAKI_FONT_DATA_H
#define MISAKI_FONT_DATA_H

#include <stdint.h>
#include <stddef.h>

namespace MisakiFont {

struct Glyph {
  uint16_t code;
  uint8_t  width;
  uint8_t  rows[8];
};

extern const size_t kGlyphCount;
extern const Glyph kGlyphs[];

// Fast O(1) ASCII lookup table for 0x20..0x7E
extern const int16_t kAsciiGlyphIndex[96];

const Glyph * findGlyph(uint16_t code);

int measureCharWidth(uint16_t code, int scale);
int measureTextWidth(const wchar_t * text, int len, int scale);

void drawString(uint32_t * dst, int screenW, int screenH,
                int x, int y, const wchar_t * text, int len,
                uint32_t color, int scale);

} // namespace MisakiFont

#endif // MISAKI_FONT_DATA_H
