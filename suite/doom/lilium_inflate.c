#include "lilium_inflate.h"
#include <string.h>

typedef struct {
  const uint8_t * src;
  size_t srcLen;
  size_t srcPos;
  uint32_t bitBuf;
  int bitCnt;
} BitReader;

typedef struct {
  uint16_t counts[16];
  uint16_t symbols[288];
} HuffTable;

static int needBits(BitReader * br, int n) {
  while (br->bitCnt < n) {
    if (br->srcPos >= br->srcLen) return 0;
    br->bitBuf |= ((uint32_t)br->src[br->srcPos++]) << br->bitCnt;
    br->bitCnt += 8;
  }
  return 1;
}

static int readBits(BitReader * br, int n, uint32_t * out) {
  if (n == 0) {
    *out = 0;
    return 1;
  }
  if (!needBits(br, n)) return 0;
  *out = br->bitBuf & ((1U << n) - 1U);
  br->bitBuf >>= n;
  br->bitCnt -= n;
  return 1;
}

static int buildHuffTable(HuffTable * ht, const uint8_t * lengths, int numSyms) {
  uint16_t offs[16];
  int left;
  int len;
  int sym;

  memset(ht->counts, 0, sizeof(ht->counts));
  for (sym = 0; sym < numSyms; ++sym) {
    if (lengths[sym] > 15) return 0;
    ht->counts[lengths[sym]]++;
  }
  if (ht->counts[0] == numSyms) {
    return 1; // Empty table (valid for unused dist table)
  }

  left = 1;
  for (len = 1; len <= 15; ++len) {
    left <<= 1;
    left -= ht->counts[len];
    if (left < 0) return 0;
  }

  offs[1] = 0;
  for (len = 1; len < 15; ++len) {
    offs[len + 1] = offs[len] + ht->counts[len];
  }

  for (sym = 0; sym < numSyms; ++sym) {
    if (lengths[sym] != 0) {
      ht->symbols[offs[lengths[sym]]++] = (uint16_t)sym;
    }
  }
  return 1;
}

static int decodeSym(BitReader * br, const HuffTable * ht, int * outSym) {
  int len;
  int code = 0;
  int first = 0;
  int index = 0;

  for (len = 1; len <= 15; ++len) {
    uint32_t bit = 0;
    if (!readBits(br, 1, &bit)) return 0;
    code |= (int)bit;
    int cnt = ht->counts[len];
    if (code - cnt < first) {
      *outSym = ht->symbols[index + (code - first)];
      return 1;
    }
    index += cnt;
    first += cnt;
    first <<= 1;
    code <<= 1;
  }
  return 0;
}

static void initFixedTables(HuffTable * litLen, HuffTable * dist) {
  uint8_t lengths[288];
  int i;
  for (i = 0; i <= 143; ++i) lengths[i] = 8;
  for (; i <= 255; ++i) lengths[i] = 9;
  for (; i <= 279; ++i) lengths[i] = 7;
  for (; i <= 287; ++i) lengths[i] = 8;
  buildHuffTable(litLen, lengths, 288);

  for (i = 0; i < 32; ++i) lengths[i] = 5;
  buildHuffTable(dist, lengths, 32);
}

static int decodeBlock(BitReader * br, const HuffTable * litLen, const HuffTable * dist,
                       uint8_t * dst, size_t dstLen, size_t * outPos) {
  static const uint16_t kLenBase[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
  };
  static const uint8_t kLenExtra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
  };
  static const uint16_t kDistBase[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
  };
  static const uint8_t kDistExtra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
  };

  for (;;) {
    int sym = 0;
    if (!decodeSym(br, litLen, &sym)) return -1;
    if (sym < 256) {
      if (*outPos >= dstLen) return -2;
      dst[(*outPos)++] = (uint8_t)sym;
    } else if (sym == 256) {
      return 0;
    } else {
      int lidx = sym - 257;
      uint32_t extra = 0;
      uint32_t len;
      int dsym = 0;
      uint32_t d;
      size_t i;

      if (lidx < 0 || lidx >= 29) return -3;
      if (!readBits(br, kLenExtra[lidx], &extra)) return -4;
      len = kLenBase[lidx] + extra;

      if (!decodeSym(br, dist, &dsym)) return -5;
      if (dsym < 0 || dsym >= 30) return -6;
      if (!readBits(br, kDistExtra[dsym], &extra)) return -7;
      d = kDistBase[dsym] + extra;

      if (d == 0 || (size_t)d > *outPos || *outPos + len > dstLen) return -8;
      for (i = 0; i < len; ++i) {
        dst[*outPos] = dst[*outPos - d];
        (*outPos)++;
      }
    }
  }
}

int lilium_inflate_raw(const uint8_t * src, size_t srcLen, uint8_t * dst, size_t dstLen) {
  static const uint8_t kClenOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
  };
  BitReader br;
  size_t outPos = 0;
  uint32_t bfinal = 0;

  if (!src || !dst) return -1;
  br.src = src;
  br.srcLen = srcLen;
  br.srcPos = 0;
  br.bitBuf = 0;
  br.bitCnt = 0;

  do {
    uint32_t btype = 0;
    if (!readBits(&br, 1, &bfinal)) return -10;
    if (!readBits(&br, 2, &btype)) return -11;

    if (btype == 0) {
      uint16_t len, nlen;
      // Discard remaining bits in partial byte
      br.bitBuf = 0;
      br.bitCnt = 0;
      if (br.srcPos + 4 > br.srcLen) return -12;
      len = (uint16_t)(br.src[br.srcPos] | (br.src[br.srcPos + 1] << 8));
      nlen = (uint16_t)(br.src[br.srcPos + 2] | (br.src[br.srcPos + 3] << 8));
      br.srcPos += 4;
      if (len != (uint16_t)(~nlen)) return -13;
      if (br.srcPos + len > br.srcLen || outPos + len > dstLen) return -14;
      if (len > 0) {
        memcpy(dst + outPos, br.src + br.srcPos, len);
        br.srcPos += len;
        outPos += len;
      }
    } else if (btype == 1) {
      HuffTable litLen, dist;
      initFixedTables(&litLen, &dist);
      if (decodeBlock(&br, &litLen, &dist, dst, dstLen, &outPos) != 0) return -15;
    } else if (btype == 2) {
      uint32_t hlit = 0, hdist = 0, hclen = 0;
      uint8_t clens[19];
      uint8_t lens[288 + 32];
      HuffTable clTable, litLen, dist;
      uint32_t i, idx;

      if (!readBits(&br, 5, &hlit) || !readBits(&br, 5, &hdist) || !readBits(&br, 4, &hclen)) {
        return -16;
      }
      hlit += 257;
      hdist += 1;
      hclen += 4;
      if (hlit > 286 || hdist > 30) return -17;

      memset(clens, 0, sizeof(clens));
      for (i = 0; i < hclen; ++i) {
        uint32_t v = 0;
        if (!readBits(&br, 3, &v)) return -18;
        clens[kClenOrder[i]] = (uint8_t)v;
      }
      if (!buildHuffTable(&clTable, clens, 19)) return -19;

      idx = 0;
      while (idx < hlit + hdist) {
        int sym = 0;
        uint8_t val = 0;
        uint32_t rep = 1, extra = 0;
        if (!decodeSym(&br, &clTable, &sym)) return -20;
        if (sym < 16) {
          lens[idx++] = (uint8_t)sym;
          continue;
        }
        if (sym == 16) {
          if (idx == 0) return -21;
          val = lens[idx - 1];
          if (!readBits(&br, 2, &extra)) return -22;
          rep = 3 + extra;
        } else if (sym == 17) {
          val = 0;
          if (!readBits(&br, 3, &extra)) return -23;
          rep = 3 + extra;
        } else {
          val = 0;
          if (!readBits(&br, 7, &extra)) return -24;
          rep = 11 + extra;
        }
        if (idx + rep > hlit + hdist) return -25;
        while (rep--) {
          lens[idx++] = val;
        }
      }
      if (lens[256] == 0) return -26;
      if (!buildHuffTable(&litLen, lens, (int)hlit)) return -27;
      if (!buildHuffTable(&dist, lens + hlit, (int)hdist)) return -28;
      if (decodeBlock(&br, &litLen, &dist, dst, dstLen, &outPos) != 0) return -29;
    } else {
      return -30;
    }
  } while (!bfinal);

  return (int)outPos;
}
