import struct, os, sys
from PIL import Image, ImageFont, ImageDraw

script_dir = os.path.dirname(os.path.abspath(__file__))
font_path = os.path.join(script_dir, "font.ttf")

if not os.path.isfile(font_path):
    print("ERROR: font.ttf not found at", font_path)
    sys.exit(1)

font = ImageFont.truetype(font_path, 8)

with open(font_path, "rb") as f:
    data = f.read()

num_tables = struct.unpack(">H", data[4:6])[0]
cmap_offset = 0
for i in range(num_tables):
    if data[12+i*16:16+i*16] == b"cmap":
        cmap_offset = struct.unpack(">I", data[12+i*16+8:12+i*16+12])[0]
        break

num_subtables = struct.unpack(">H", data[cmap_offset+2:cmap_offset+4])[0]
sub_offset = 0
for i in range(num_subtables):
    p_id, e_id, off = struct.unpack(">HHI", data[cmap_offset+4+i*8:cmap_offset+4+i*8+8])
    if p_id == 3 and e_id in (1, 10):
        sub_offset = cmap_offset + off
        break
    elif p_id == 0:
        sub_offset = cmap_offset + off

segCount = struct.unpack(">H", data[sub_offset+6:sub_offset+8])[0] // 2
endCodes = struct.unpack(f">{segCount}H", data[sub_offset+14:sub_offset+14+segCount*2])
startCodes = struct.unpack(f">{segCount}H", data[sub_offset+16+segCount*2:sub_offset+16+segCount*4])

all_chars = []
for s, e in zip(startCodes, endCodes):
    if s == 0xFFFF: continue
    for c in range(s, e + 1):
        all_chars.append(c)

all_chars.sort()
print(f"Total characters to extract: {len(all_chars)}")

im = Image.new("1", (8, 8), 0)
draw = ImageDraw.Draw(im)

glyphs = []
for code in all_chars:
    ch = chr(code)
    w = int(font.getlength(ch))
    if w <= 0:
        w = 4 if code < 0x80 else 8
    elif w > 8:
        w = 8
    
    im.paste(0, (0, 0, 8, 8))
    draw.text((0, 0), ch, font=font, fill=1)
    rows = []
    for y in range(8):
        v = 0
        for x in range(8):
            if im.getpixel((x, y)):
                v |= (1 << (7 - x))
        rows.append(v)
    glyphs.append((code, w, rows))

# Generate misaki_font_data.h
h_path = os.path.join(script_dir, "misaki_font_data.h")
with open(h_path, "w", encoding="utf-8") as f:
    f.write("""#ifndef MISAKI_FONT_DATA_H
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
""")

# Generate misaki_font_data.cpp
cpp_path = os.path.join(script_dir, "misaki_font_data.cpp")
with open(cpp_path, "w", encoding="utf-8") as f:
    f.write("""#include "misaki_font_data.h"
#include <string.h>

namespace MisakiFont {

const size_t kGlyphCount = """ + str(len(glyphs)) + """;

const Glyph kGlyphs[kGlyphCount] = {
""")
    for g in glyphs:
        c, w, r = g
        f.write(f"  {{ 0x{c:04X}, {w}, {{ 0x{r[0]:02X}, 0x{r[1]:02X}, 0x{r[2]:02X}, 0x{r[3]:02X}, 0x{r[4]:02X}, 0x{r[5]:02X}, 0x{r[6]:02X}, 0x{r[7]:02X} }} }},\n")
    f.write("""};

// Fast direct lookup for ASCII 0x20 - 0x7E
const int16_t kAsciiGlyphIndex[96] = {
""")
    code_to_idx = { g[0]: i for i, g in enumerate(glyphs) }
    ascii_indices = [code_to_idx.get(c, -1) for c in range(0x20, 0x80)]
    for i in range(0, 96, 16):
        chunk = ascii_indices[i:i+16]
        f.write("  " + ", ".join(f"{idx:4d}" for idx in chunk) + ",\n")
    f.write("""};

const Glyph * findGlyph(uint16_t code) {
  if (code >= 0x20 && code <= 0x7E) {
    int16_t idx = kAsciiGlyphIndex[code - 0x20];
    if (idx >= 0) {
      return &kGlyphs[idx];
    }
  }

  // Binary search
  int left = 0;
  int right = static_cast<int>(kGlyphCount) - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    uint16_t midCode = kGlyphs[mid].code;
    if (midCode == code) {
      return &kGlyphs[mid];
    }
    if (midCode < code) {
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return nullptr;
}

int measureCharWidth(uint16_t code, int scale) {
  if (code == 0) return 0;
  const Glyph * g = findGlyph(code);
  if (g) {
    return static_cast<int>(g->width) * scale;
  }
  // Default to half-width for ASCII, full-width otherwise
  return (code < 0x80 ? 4 : 8) * scale;
}

int measureTextWidth(const wchar_t * text, int len, int scale) {
  if (!text) return 0;
  int total = 0;
  int count = (len < 0) ? static_cast<int>(wcslen(text)) : len;
  for (int i = 0; i < count; i++) {
    total += measureCharWidth(static_cast<uint16_t>(text[i]), scale);
  }
  return total;
}

void drawString(uint32_t * dst, int screenW, int screenH,
                int x, int y, const wchar_t * text, int len,
                uint32_t color, int scale) {
  if (!dst || !text) return;
  int count = (len < 0) ? static_cast<int>(wcslen(text)) : len;
  int curX = x;
  int charH = 8 * scale;

  for (int i = 0; i < count; i++) {
    uint16_t code = static_cast<uint16_t>(text[i]);
    const Glyph * g = findGlyph(code);
    int cw = (g ? static_cast<int>(g->width) : (code < 0x80 ? 4 : 8)) * scale;

    if (g && (curX + cw > 0) && (curX < screenW) && (y + charH > 0) && (y < screenH)) {
      if (scale == 1) {
        for (int r = 0; r < 8; r++) {
          int py = y + r;
          if (py < 0 || py >= screenH) continue;
          uint8_t rowBits = g->rows[r];
          if (!rowBits) continue;
          uint32_t * rowPtr = dst + py * screenW;
          for (int c = 0; c < g->width; c++) {
            int px = curX + c;
            if (px >= 0 && px < screenW) {
              if ((rowBits & (0x80 >> c)) != 0) {
                rowPtr[px] = color;
              }
            }
          }
        }
      } else if (scale == 2) {
        for (int r = 0; r < 8; r++) {
          uint8_t rowBits = g->rows[r];
          if (!rowBits) continue;
          int py0 = y + r * 2;
          int py1 = py0 + 1;
          uint32_t * rowPtr0 = (py0 >= 0 && py0 < screenH) ? (dst + py0 * screenW) : nullptr;
          uint32_t * rowPtr1 = (py1 >= 0 && py1 < screenH) ? (dst + py1 * screenW) : nullptr;
          if (!rowPtr0 && !rowPtr1) continue;

          for (int c = 0; c < g->width; c++) {
            if ((rowBits & (0x80 >> c)) != 0) {
              int px0 = curX + c * 2;
              int px1 = px0 + 1;
              if (rowPtr0) {
                if (px0 >= 0 && px0 < screenW) rowPtr0[px0] = color;
                if (px1 >= 0 && px1 < screenW) rowPtr0[px1] = color;
              }
              if (rowPtr1) {
                if (px0 >= 0 && px0 < screenW) rowPtr1[px0] = color;
                if (px1 >= 0 && px1 < screenW) rowPtr1[px1] = color;
              }
            }
          }
        }
      }
    }

    curX += cw;
  }
}

} // namespace MisakiFont
""")

print("Successfully generated misaki_font_data.h and misaki_font_data.cpp")
