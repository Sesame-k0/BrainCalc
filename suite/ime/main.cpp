#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef MOD_ALT
#define MOD_ALT 0x0001
#endif
#ifndef MOD_CONTROL
#define MOD_CONTROL 0x0002
#endif
#ifndef VK_CONVERT
#define VK_CONVERT 0x1C
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif
#ifndef VK_OEM_PERIOD
#define VK_OEM_PERIOD 0xBE
#endif
#ifndef VK_OEM_COMMA
#define VK_OEM_COMMA 0xBC
#endif
#ifndef VK_OEM_2
#define VK_OEM_2 0xBF
#endif
#ifndef VK_OEM_4
#define VK_OEM_4 0xDB
#endif
#ifndef VK_OEM_6
#define VK_OEM_6 0xDD
#endif

namespace {

static constexpr int kOverlayW = 800;
static constexpr int kOverlayH = 164;
static constexpr int kOverlayX = 0;
static constexpr int kOverlayY = 480 - kOverlayH; // 316

static constexpr int kHotKeyCtrlSpace = 101;
static constexpr int kHotKeyAltSpace  = 102;
static constexpr int kHotKeyF11       = 103;
static constexpr int kHotKeyConvert   = 104;
static constexpr UINT_PTR kTimerSaveUser = 1;

static constexpr size_t kMaxDictBytes = 4u * 1024u * 1024u;
static constexpr int kMaxUserLearn   = 128;

static_assert(sizeof(wchar_t) == 2, "Expected 16-bit wchar_t for LIME dictionary");

struct UserLearnEntry {
  wchar_t reading[24];
  wchar_t surface[32];
  uint32_t hits;
};

class MozcDict {
public:
  MozcDict()
    : m_data(nullptr),
      m_dataSize(0),
      m_readingCount(0),
      m_totalPairs(0),
      m_indexOffset(0),
      m_poolOffset(0) {}

  ~MozcDict() {
    close();
  }

  void close() {
    if (m_data) {
      free(m_data);
      m_data = nullptr;
    }
    m_dataSize = 0;
    m_readingCount = 0;
    m_totalPairs = 0;
    m_indexOffset = 0;
    m_poolOffset = 0;
  }

  bool load(const wchar_t * dicPath) {
    close();
    FILE * fp = _wfopen(dicPath, L"rb");
    if (!fp) return false;

    if (fseek(fp, 0, SEEK_END) != 0) {
      fclose(fp);
      return false;
    }
    long fsz = ftell(fp);
    if (fsz < 24 || static_cast<size_t>(fsz) > kMaxDictBytes || fseek(fp, 0, SEEK_SET) != 0) {
      fclose(fp);
      return false;
    }

    uint8_t * buf = static_cast<uint8_t *>(malloc(static_cast<size_t>(fsz)));
    if (!buf) {
      fclose(fp);
      return false;
    }
    size_t got = fread(buf, 1, static_cast<size_t>(fsz), fp);
    fclose(fp);
    if (got != static_cast<size_t>(fsz)) {
      free(buf);
      return false;
    }

    if (memcmp(buf, "LIME", 4) != 0 || readU32(buf + 4) != 2) {
      free(buf);
      return false;
    }

    uint32_t readingCnt = readU32(buf + 8);
    uint32_t totalPairs = readU32(buf + 12);
    uint32_t indexOff = readU32(buf + 16);
    uint32_t poolOff = readU32(buf + 20);
    if (readingCnt == 0 ||
        indexOff < 24 ||
        static_cast<uint64_t>(indexOff) + static_cast<uint64_t>(readingCnt) * 4u > poolOff ||
        poolOff > static_cast<uint32_t>(fsz)) {
      free(buf);
      return false;
    }

    m_data = buf;
    m_dataSize = static_cast<uint32_t>(fsz);
    m_readingCount = readingCnt;
    m_totalPairs = totalPairs;
    m_indexOffset = indexOff;
    m_poolOffset = poolOff;
    return true;
  }

  uint32_t totalPairs() const { return m_totalPairs; }
  bool isLoaded() const { return m_data != nullptr && m_readingCount > 0; }

  int lookupExact(const wchar_t * reading, wchar_t outCands[][LiliumIME::kMaxCandChars], int maxOut) const {
    if (!isLoaded() || !reading || !reading[0] || maxOut <= 0) return 0;

    int lo = 0;
    int hi = static_cast<int>(m_readingCount) - 1;
    while (lo <= hi) {
      int mid = lo + (hi - lo) / 2;
      uint32_t relOff = readU32(m_data + m_indexOffset + static_cast<size_t>(mid) * 4u);
      size_t pos = static_cast<size_t>(m_poolOffset) + relOff;
      if (pos >= m_dataSize) return 0;

      uint8_t rLen = m_data[pos++];
      if (pos + static_cast<size_t>(rLen) * 2u + 1u > m_dataSize) return 0;

      int cmp = compareUtf16(m_data + pos, rLen, reading);
      if (cmp == 0) {
        pos += static_cast<size_t>(rLen) * 2u;
        uint8_t candCnt = m_data[pos++];
        int copied = 0;
        for (int c = 0; c < candCnt; ++c) {
          if (pos >= m_dataSize) break;
          uint8_t sLen = m_data[pos++];
          if (pos + static_cast<size_t>(sLen) * 2u > m_dataSize) break;
          if (copied < maxOut) {
            int copyLen = (sLen < LiliumIME::kMaxCandChars) ? sLen : (LiliumIME::kMaxCandChars - 1);
            for (int j = 0; j < copyLen; ++j) {
              outCands[copied][j] = static_cast<wchar_t>(readU16(m_data + pos + static_cast<size_t>(j) * 2u));
            }
            outCands[copied][copyLen] = L'\0';
            ++copied;
          }
          pos += static_cast<size_t>(sLen) * 2u;
        }
        return copied;
      }
      if (cmp < 0) {
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    return 0;
  }

private:
  static uint16_t readU16(const uint8_t * p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
  }
  static uint32_t readU32(const uint8_t * p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
  }
  static int compareUtf16(const uint8_t * recUtf16, uint8_t recLen, const wchar_t * target) {
    int i = 0;
    while (i < recLen && target[i] != L'\0') {
      uint16_t a = readU16(recUtf16 + static_cast<size_t>(i) * 2u);
      uint16_t b = static_cast<uint16_t>(target[i]);
      if (a != b) return (a < b) ? -1 : 1;
      ++i;
    }
    if (i < recLen) return 1;
    if (target[i] != L'\0') return -1;
    return 0;
  }

  uint8_t * m_data;
  uint32_t m_dataSize;
  uint32_t m_readingCount;
  uint32_t m_totalPairs;
  uint32_t m_indexOffset;
  uint32_t m_poolOffset;
};

struct ImeDaemonState {
  HWND hDaemon = NULL;
  HWND hOverlay = NULL;
  HDC memDC = NULL;
  HBITMAP dib = NULL;
  HGDIOBJ oldBmp = NULL;
  uint32_t * pixels = nullptr;

  HANDLE hMap = NULL;
  LiliumIME::ImeSharedMem * shm = nullptr;

  MozcDict dict;
  wchar_t userDatPath[MAX_PATH] = L"";
  UserLearnEntry userLearn[kMaxUserLearn] = {};
  int userLearnCount = 0;
  bool userLearnDirty = false;

  // Floating overlay state for external apps
  bool overlayVisible = false;
  bool overlayChromeDrawn = false;
  int lastOverlayMode = -1;
  bool lastOverlayConv = false;
  int lastOverlayCandSel = -1;
  int lastOverlayCandPage = -1;
  int lastOverlayCandCount = -1;
  wchar_t lastOverlayComp[64] = L"";
  wchar_t lastOverlayPre[64] = L"";
  HWND targetHwnd = NULL;
  int inputMode = 0; // 0=あ, 1=abc, 2=ABC, 3=12x
  LiliumIME::RomajiEngine romaji;
  wchar_t compBuf[64] = L"";
  bool converting = false;
  wchar_t cands[LiliumIME::kMaxCands][LiliumIME::kMaxCandChars] = {};
  int candCount = 0;
  int candSel = 0;
  int candPageStart = 0;
  int segLen = 0;
};

static ImeDaemonState * g_ime = nullptr;

static void appendUniqueCand(wchar_t out[][LiliumIME::kMaxCandChars], int & cnt, int maxCnt, const wchar_t * s) {
  if (!s || !s[0] || cnt >= maxCnt) return;
  for (int i = 0; i < cnt; ++i) {
    if (wcscmp(out[i], s) == 0) return;
  }
  wcsncpy(out[cnt], s, LiliumIME::kMaxCandChars - 1);
  out[cnt][LiliumIME::kMaxCandChars - 1] = L'\0';
  ++cnt;
}

static void loadUserHistory(ImeDaemonState * st) {
  FILE * fp = _wfopen(st->userDatPath, L"rb");
  if (!fp) return;
  char line[256] = {};
  while (st->userLearnCount < kMaxUserLearn && fgets(line, sizeof(line), fp) != NULL) {
    char * tab = strchr(line, '\t');
    if (!tab) continue;
    *tab = '\0';
    char * surf = tab + 1;
    size_t sl = strlen(surf);
    while (sl > 0 && (surf[sl - 1] == '\n' || surf[sl - 1] == '\r')) surf[--sl] = '\0';
    if (!line[0] || !surf[0]) continue;
    UserLearnEntry & ue = st->userLearn[st->userLearnCount++];
    MultiByteToWideChar(CP_UTF8, 0, line, -1, ue.reading, 23);
    ue.reading[23] = L'\0';
    MultiByteToWideChar(CP_UTF8, 0, surf, -1, ue.surface, 31);
    ue.surface[31] = L'\0';
    ue.hits = 1;
  }
  fclose(fp);
}

static void saveUserHistory(const ImeDaemonState * st) {
  if (!st->userDatPath[0]) return;
  FILE * fp = _wfopen(st->userDatPath, L"wb");
  if (!fp) return;
  for (int i = 0; i < st->userLearnCount; ++i) {
    char rBuf[96] = {}, sBuf[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, st->userLearn[i].reading, -1, rBuf, sizeof(rBuf) - 1, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, st->userLearn[i].surface, -1, sBuf, sizeof(sBuf) - 1, NULL, NULL);
    if (rBuf[0] && sBuf[0]) fprintf(fp, "%s\t%s\n", rBuf, sBuf);
  }
  fclose(fp);
}

static void markUserLearnDirty(ImeDaemonState * st) {
  if (!st) return;
  st->userLearnDirty = true;
  if (st->hDaemon) {
    SetTimer(st->hDaemon, kTimerSaveUser, 5000, NULL);
  }
}

static void flushUserHistoryIfDirty(ImeDaemonState * st) {
  if (!st || !st->userLearnDirty) return;
  saveUserHistory(st);
  st->userLearnDirty = false;
}

static void recordUserChoice(ImeDaemonState * st, const wchar_t * reading, const wchar_t * surface) {
  if (!reading || !reading[0] || !surface || !surface[0]) return;
  if (wcscmp(reading, L"、") == 0 || wcscmp(reading, L"。") == 0) return;
  for (int i = 0; i < st->userLearnCount; ++i) {
    if (wcscmp(st->userLearn[i].reading, reading) == 0 &&
        wcscmp(st->userLearn[i].surface, surface) == 0) {
      ++st->userLearn[i].hits;
      UserLearnEntry moved = st->userLearn[i];
      for (int j = i; j > 0; --j) st->userLearn[j] = st->userLearn[j - 1];
      st->userLearn[0] = moved;
      markUserLearnDirty(st);
      return;
    }
  }
  int limit = (st->userLearnCount < kMaxUserLearn) ? st->userLearnCount : (kMaxUserLearn - 1);
  for (int j = limit; j > 0; --j) st->userLearn[j] = st->userLearn[j - 1];
  wcsncpy(st->userLearn[0].reading, reading, 23);
  st->userLearn[0].reading[23] = L'\0';
  wcsncpy(st->userLearn[0].surface, surface, 31);
  st->userLearn[0].surface[31] = L'\0';
  st->userLearn[0].hits = 1;
  if (st->userLearnCount < kMaxUserLearn) ++st->userLearnCount;
  markUserLearnDirty(st);
}

static int queryDictWithClause(ImeDaemonState * st, const wchar_t * reading,
                               wchar_t outCands[][LiliumIME::kMaxCandChars], int & outSegLen) {
  int cnt = 0;
  int rLen = static_cast<int>(wcslen(reading));
  outSegLen = rLen;
  if (rLen <= 0) return 0;

  if (wcscmp(reading, L"、") == 0) {
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, L",");
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, L"、");
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, L"，");
    outSegLen = 1;
    return cnt;
  }
  if (wcscmp(reading, L"。") == 0) {
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, L".");
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, L"。");
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, L"．");
    outSegLen = 1;
    return cnt;
  }

  // 1. User learned top hits for full reading
  for (int i = 0; i < st->userLearnCount; ++i) {
    if (wcscmp(st->userLearn[i].reading, reading) == 0) {
      appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, st->userLearn[i].surface);
    }
  }

  // 2. Exact match in Mozc dictionary
  wchar_t tmp[LiliumIME::kMaxCands][LiliumIME::kMaxCandChars] = {};
  int exactN = st->dict.lookupExact(reading, tmp, LiliumIME::kMaxCands);
  for (int i = 0; i < exactN; ++i) {
    appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, tmp[i]);
  }

  // 3. Longest-prefix segmentation fallback for multi-clause / compound hiragana strings
  if (cnt == 0 && rLen >= 2) {
    int bestPref = 0;
    wchar_t prefCands[LiliumIME::kMaxCands][LiliumIME::kMaxCandChars] = {};
    int prefCnt = 0;
    for (int len = rLen - 1; len >= 1; --len) {
      wchar_t sub[LiliumIME::kMaxReadingChars] = L"";
      wcsncpy(sub, reading, static_cast<size_t>(len));
      sub[len] = L'\0';
      int foundN = st->dict.lookupExact(sub, prefCands, LiliumIME::kMaxCands);
      if (foundN > 0) {
        bestPref = len;
        prefCnt = foundN;
        break;
      }
    }
    if (bestPref > 0 && prefCnt > 0) {
      // Also offer greedy whole-sentence join as candidate #1
      wchar_t joined[LiliumIME::kMaxCandChars] = L"";
      int pos = 0;
      while (pos < rLen) {
        int seg = 1;
        wchar_t piece[LiliumIME::kMaxCandChars] = L"";
        for (int len = (rLen - pos < 15 ? rLen - pos : 15); len >= 1; --len) {
          wchar_t sub[LiliumIME::kMaxReadingChars] = L"";
          wcsncpy(sub, reading + pos, static_cast<size_t>(len));
          sub[len] = L'\0';
          wchar_t one[2][LiliumIME::kMaxCandChars] = {};
          if (st->dict.lookupExact(sub, one, 1) > 0) {
            wcsncpy(piece, one[0], LiliumIME::kMaxCandChars - 1);
            seg = len;
            break;
          }
        }
        if (!piece[0]) {
          piece[0] = reading[pos];
          piece[1] = L'\0';
        }
        size_t curL = wcslen(joined);
        if (curL + wcslen(piece) + 1 < LiliumIME::kMaxCandChars) {
          wcscat(joined, piece);
        }
        pos += seg;
      }
      if (joined[0] && wcscmp(joined, reading) != 0) {
        appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, joined);
        outSegLen = rLen;
      } else {
        outSegLen = bestPref;
      }
      for (int i = 0; i < prefCnt; ++i) {
        appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, prefCands[i]);
      }
    }
  }

  // 4. Always provide Hiragana & Katakana candidates
  appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, reading);
  wchar_t kata[LiliumIME::kMaxCandChars] = L"";
  LiliumIME::RomajiEngine::toKatakana(reading, kata, LiliumIME::kMaxCandChars);
  appendUniqueCand(outCands, cnt, LiliumIME::kMaxCands, kata);

  return cnt;
}

static void initOverlayBackbuffer(ImeDaemonState * st) {
  HDC hdcScreen = GetDC(NULL);
  st->memDC = CreateCompatibleDC(hdcScreen);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = kOverlayW;
  bmi.bmiHeader.biHeight = -kOverlayH;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void * bits = nullptr;
  st->dib = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (st->dib) {
    st->oldBmp = SelectObject(st->memDC, st->dib);
    st->pixels = static_cast<uint32_t *>(bits);
  }
  ReleaseDC(NULL, hdcScreen);
}

static void freeOverlayBackbuffer(ImeDaemonState * st) {
  if (st->memDC && st->oldBmp) SelectObject(st->memDC, st->oldBmp);
  if (st->dib) DeleteObject(st->dib);
  if (st->memDC) DeleteDC(st->memDC);
  st->dib = NULL;
  st->memDC = NULL;
  st->pixels = nullptr;
}

static void expandDirtyRect(RECT * rc, LONG l, LONG t, LONG r, LONG b) {
  if (!rc) return;
  if (IsRectEmpty(rc)) {
    SetRect(rc, l, t, r, b);
  } else {
    if (l < rc->left) rc->left = l;
    if (t < rc->top) rc->top = t;
    if (r > rc->right) rc->right = r;
    if (b > rc->bottom) rc->bottom = b;
  }
}

static void renderOverlay(ImeDaemonState * st, bool fullRedraw, RECT * outDirty) {
  if (!st || !st->pixels) return;
  if (outDirty) SetRectEmpty(outDirty);
  if (!st->overlayChromeDrawn) fullRedraw = true;

  LiliumUI::Canvas c = {};
  c.pixels = st->pixels;
  c.width = kOverlayW;
  c.height = kOverlayH;
  c.clearDirty();

  if (fullRedraw) {
    c.drawBox(0, 0, kOverlayW, kOverlayH, LiliumUI::kColWhite, LiliumUI::kColTealDark);
    c.fillRect(0, 0, kOverlayW, 34, LiliumUI::kColTeal);
    c.drawText(14, 0, 260, 34, LiliumUI::kColWhite, L"LILIUM IME (外部入力)", 2, LiliumUI::Align::Left, true);

    wchar_t dictStat[40];
    _snwprintf(dictStat, 40, L"辞書:%lu語", static_cast<unsigned long>(st->dict.totalPairs()));
    c.drawText(402, 0, 238, 34, LiliumUI::kColWhite, dictStat, 2, LiliumUI::Align::Right, true);

    c.drawBox(650, 4, 68, 26, LiliumUI::kColWhite, LiliumUI::kColTealDark);
    c.drawText(650, 4, 68, 26, LiliumUI::kColTealDeep, L"確 定", 2, LiliumUI::Align::Center, true);
    c.drawBox(724, 4, 68, 26, LiliumUI::kColDangerBg, LiliumUI::kColDangerBorder);
    c.drawText(724, 4, 68, 26, LiliumUI::kColDangerText, L"閉じる", 2, LiliumUI::Align::Center, true);

    c.drawText(14, 132, 772, 26, LiliumUI::kColMuted,
               L"[F2]あ/abc/ABC/12x   [1-8/←→]候補   [Enter]送信確定   [Esc]キャンセル",
               2, LiliumUI::Align::Left, true);

    st->overlayChromeDrawn = true;
    expandDirtyRect(outDirty, 0, 0, kOverlayW, kOverlayH);
  }

  static constexpr const wchar_t * kModeLbls[4] = { L"[F2] あ", L"[F2] abc", L"[F2] ABC", L"[F2] 12x" };
  if (fullRedraw || st->lastOverlayMode != st->inputMode) {
    c.drawBox(280, 4, 110, 26, LiliumUI::kColTealSoft, LiliumUI::kColWhite);
    c.drawText(280, 4, 110, 26, LiliumUI::kColTealDeep, kModeLbls[st->inputMode], 2, LiliumUI::Align::Center, true);
    st->lastOverlayMode = st->inputMode;
    expandDirtyRect(outDirty, 280, 4, 390, 30);
  }

  wchar_t pre[64] = L"";
  if (st->converting && st->candCount > 0) {
    wcsncpy(pre, st->cands[st->candSel], 63);
  } else {
    st->romaji.getPreeditDisplay(pre, 64);
  }

  bool prevConv = st->lastOverlayConv;
  if (fullRedraw ||
      prevConv != st->converting ||
      wcscmp(st->lastOverlayComp, st->compBuf) != 0 ||
      wcscmp(st->lastOverlayPre, pre) != 0) {
    c.drawBox(12, 42, 776, 38, 0xF7F9FB, LiliumUI::kColTealDark);
    int tx = 20;
    if (st->compBuf[0]) {
      int cw = c.measureWidth(st->compBuf, 2);
      c.drawText(tx, 42, 760, 38, LiliumUI::kColText, st->compBuf, 2, LiliumUI::Align::Left, true);
      tx += cw;
    }
    if (pre[0]) {
      int pw = c.measureWidth(pre, 2);
      c.fillRect(tx, 47, pw + 2, 28, st->converting ? LiliumUI::kColTealSoft : 0xFFF3D6);
      c.drawText(tx, 42, pw + 12, 38, st->converting ? LiliumUI::kColTealDeep : 0x8A4E00,
                 pre, 2, LiliumUI::Align::Left, true);
      c.fillRect(tx, 75, pw, 2, LiliumUI::kColTealDark);
    } else {
      c.fillRect(tx, 49, 2, 24, LiliumUI::kColTealDark);
    }
    wcsncpy(st->lastOverlayComp, st->compBuf, 63);
    st->lastOverlayComp[63] = L'\0';
    wcsncpy(st->lastOverlayPre, pre, 63);
    st->lastOverlayPre[63] = L'\0';
    st->lastOverlayConv = st->converting;
    expandDirtyRect(outDirty, 12, 42, 788, 80);
  }

  bool candChanged = fullRedraw ||
                     (prevConv != st->converting) ||
                     (st->converting &&
                      (st->lastOverlayCandCount != st->candCount ||
                       st->lastOverlayCandSel != st->candSel ||
                       st->lastOverlayCandPage != st->candPageStart));
  if (candChanged) {
    c.drawBox(12, 86, 776, 40, LiliumUI::kColRowAlt, LiliumUI::kColBorder);
    if (st->converting && st->candCount > 0) {
      int cx = 20;
      for (int i = 0; i < 8 && (st->candPageStart + i) < st->candCount; ++i) {
        int idx = st->candPageStart + i;
        bool sel = (idx == st->candSel);
        wchar_t pill[64];
        _snwprintf(pill, 64, L"%d.%ls", i + 1, st->cands[idx]);
        pill[63] = L'\0';
        int pw = c.measureWidth(pill, 2) + 16;
        if (cx + pw > 780) break;
        c.drawBox(cx, 91, pw, 30, sel ? LiliumUI::kColTeal : LiliumUI::kColWhite, LiliumUI::kColTealDark);
        c.drawText(cx, 91, pw, 30, sel ? LiliumUI::kColWhite : LiliumUI::kColText,
                   pill, 2, LiliumUI::Align::Center, true);
        cx += pw + 8;
      }
    } else {
      c.drawText(20, 86, 760, 40, LiliumUI::kColMuted,
                 L"文字入力 -> [Space]Mozc変換 -> [Enter]外部アプリへ送信  ([F7]カタカナ / [Esc]中止)",
                 2, LiliumUI::Align::Left, true);
    }
    st->lastOverlayCandCount = st->candCount;
    st->lastOverlayCandSel = st->candSel;
    st->lastOverlayCandPage = st->candPageStart;
    expandDirtyRect(outDirty, 12, 86, 788, 126);
  }
}

static HWND findEditChildWindow(HWND parent, int depth) {
  if (!parent || depth > 4) return NULL;
  for (HWND ch = GetWindow(parent, GW_CHILD); ch != NULL; ch = GetWindow(ch, GW_HWNDNEXT)) {
    wchar_t cls[64] = L"";
    GetClassNameW(ch, cls, 64);
    if (_wcsnicmp(cls, L"EDIT", 4) == 0 || _wcsnicmp(cls, L"RichEdit", 8) == 0) {
      return ch;
    }
    HWND nested = findEditChildWindow(ch, depth + 1);
    if (nested) return nested;
  }
  return NULL;
}

static void postCharsToWindowEdit(HWND target, const wchar_t * text) {
  if (!target || !IsWindow(target) || !text) return;
  HWND editHwnd = findEditChildWindow(target, 0);
  HWND dest = editHwnd ? editHwnd : target;
  for (size_t i = 0; text[i] != L'\0'; ++i) {
    PostMessageW(dest, WM_CHAR, static_cast<WPARAM>(text[i]), 1);
  }
}

static void injectTextToTargetWindow(HWND target, const wchar_t * text) {
  if (!text || !text[0]) return;

  // Copy to clipboard too as reliable WinCE/PC fallback
  if (OpenClipboard(NULL)) {
    EmptyClipboard();
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
      void * p = GlobalLock(hg);
      if (p) {
        memcpy(p, text, bytes);
        GlobalUnlock(hg);
        SetClipboardData(CF_UNICODETEXT, hg);
      } else {
        GlobalFree(hg);
      }
    }
    CloseClipboard();
  }

  if (target && IsWindow(target)) {
    SetForegroundWindow(target);
    Sleep(35);
  }

#ifndef _WIN32_WCE
  HWND fgNow = GetForegroundWindow();
  bool targetHasFocus = (!target) || (fgNow == target) || (IsWindow(target) && IsChild(target, fgNow));
  if (targetHasFocus) {
    size_t len = wcslen(text);
    for (size_t i = 0; i < len; ++i) {
      INPUT inp[2] = {};
      inp[0].type = INPUT_KEYBOARD;
      inp[0].ki.wScan = text[i];
      inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
      inp[1].type = INPUT_KEYBOARD;
      inp[1].ki.wScan = text[i];
      inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      SendInput(2, inp, sizeof(INPUT));
    }
  } else {
    postCharsToWindowEdit(target, text);
  }
#else
  HWND fgNow = GetForegroundWindow();
  bool sent = false;
  if (target && IsWindow(target) && (fgNow == target || IsChild(target, fgNow))) {
    HMODULE hCore = GetModuleHandleW(L"coredll.dll");
    if (hCore) {
      typedef BOOL (WINAPI * PFN_PostKeybdMessage)(HWND, UINT, UINT, UINT, UINT *, UINT *);
      PFN_PostKeybdMessage pPostKeybd = reinterpret_cast<PFN_PostKeybdMessage>(
          reinterpret_cast<void *>(GetProcAddress(hCore, TEXT("PostKeybdMessage"))));
      if (pPostKeybd) {
        size_t len = wcslen(text);
        if (len > 0 && len <= 63) {
          UINT shifts[64] = {};
          UINT chars[64] = {};
          for (size_t i = 0; i < len; ++i) {
            shifts[i] = 0x0080; // KeyStateDownFlag
            chars[i] = static_cast<UINT>(text[i]);
          }
          sent = (pPostKeybd(fgNow, 0, 0x0080, static_cast<UINT>(len), shifts, chars) != FALSE);
        }
      }
    }
  }
  if (!sent) {
    postCharsToWindowEdit(target, text);
  }
#endif
}

static void openOverlay(ImeDaemonState * st, HWND explicitTarget) {
  HWND fg = explicitTarget ? explicitTarget : GetForegroundWindow();
  if (fg == st->hDaemon || fg == st->hOverlay) fg = NULL;
  st->targetHwnd = fg;
  st->compBuf[0] = L'\0';
  st->romaji.reset();
  st->converting = false;
  st->candCount = 0;
  st->candSel = 0;
  st->candPageStart = 0;
  st->overlayVisible = true;
  st->overlayChromeDrawn = false;
  renderOverlay(st, true, nullptr);
  ShowWindow(st->hOverlay, SW_SHOW);
  SetWindowPos(st->hOverlay, HWND_TOPMOST, kOverlayX, kOverlayY, kOverlayW, kOverlayH, SWP_SHOWWINDOW);
  SetForegroundWindow(st->hOverlay);
  InvalidateRect(st->hOverlay, NULL, FALSE);
}

static void closeOverlay(ImeDaemonState * st, bool commit) {
  wchar_t finalTxt[64] = L"";
  if (commit) {
    wcsncpy(finalTxt, st->compBuf, 63);
    finalTxt[63] = L'\0';
    if (st->converting && st->candCount > 0) {
      wcsncat(finalTxt, st->cands[st->candSel], 63 - wcslen(finalTxt));
    } else if (st->romaji.hasContent()) {
      st->romaji.flushPendingN();
      wchar_t pre[64] = L"";
      st->romaji.getPreeditDisplay(pre, 64);
      wcsncat(finalTxt, pre, 63 - wcslen(finalTxt));
    }
  }
  HWND target = st->targetHwnd;
  st->overlayVisible = false;
  ShowWindow(st->hOverlay, SW_HIDE);
  if (commit && finalTxt[0]) {
    injectTextToTargetWindow(target, finalTxt);
  } else if (target && IsWindow(target)) {
    SetForegroundWindow(target);
  }
}

static void overlayCommitCurrentSeg(ImeDaemonState * st) {
  if (st->converting && st->candCount > 0) {
    const wchar_t * chosen = st->cands[st->candSel];
    int slen = (st->segLen > 0 && st->segLen <= st->romaji.kanaLen) ? st->segLen : st->romaji.kanaLen;
    wchar_t segR[LiliumIME::kMaxReadingChars] = L"";
    wcsncpy(segR, st->romaji.kana, static_cast<size_t>(slen));
    segR[slen] = L'\0';
    recordUserChoice(st, segR, chosen);
    wcsncat(st->compBuf, chosen, 63 - wcslen(st->compBuf));
    int rem = st->romaji.kanaLen - slen;
    if (rem > 0) {
      memmove(st->romaji.kana, st->romaji.kana + slen, static_cast<size_t>(rem + 1) * sizeof(wchar_t));
      st->romaji.kanaLen = rem;
      st->candCount = queryDictWithClause(st, st->romaji.kana, st->cands, st->segLen);
      st->candSel = 0;
      st->candPageStart = 0;
    } else {
      st->romaji.reset();
      st->converting = false;
      st->candCount = 0;
    }
  } else if (st->romaji.hasContent()) {
    st->romaji.flushPendingN();
    wchar_t pre[64] = L"";
    st->romaji.getPreeditDisplay(pre, 64);
    wcsncat(st->compBuf, pre, 63 - wcslen(st->compBuf));
    st->romaji.reset();
  }
}

static LRESULT CALLBACK daemonWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  ImeDaemonState * st = g_ime;
  switch (msg) {
    case LiliumIME::kMsgImePing:
      if (st && st->shm) st->shm->dictEntries = st->dict.totalPairs();
      return 1;

    case LiliumIME::kMsgImeQuery: {
      if (!st || !st->shm) return 0;
      int segLen = 0;
      int n = queryDictWithClause(st, st->shm->reading, st->shm->candidates, segLen);
      int maxReq = (wParam > 0 && wParam <= LiliumIME::kMaxCands) ? static_cast<int>(wParam) : LiliumIME::kMaxCands;
      if (n > maxReq) n = maxReq;
      st->shm->magic = LiliumIME::kShmMagic;
      st->shm->version = LiliumIME::kShmVersion;
      st->shm->dictEntries = st->dict.totalPairs();
      st->shm->candCount = n;
      st->shm->segLen = segLen;
      return n;
    }

    case LiliumIME::kMsgImeLearn:
      if (st && st->shm) recordUserChoice(st, st->shm->reading, st->shm->chosen);
      return 1;

    case LiliumIME::kMsgImeShowOverlay:
      if (st) openOverlay(st, reinterpret_cast<HWND>(wParam));
      return 1;

    case WM_HOTKEY:
      if (st) {
        if (st->overlayVisible) closeOverlay(st, false);
        else openOverlay(st, NULL);
      }
      return 0;

    case WM_TIMER:
      if (wParam == kTimerSaveUser) {
        KillTimer(hWnd, kTimerSaveUser);
        flushUserHistoryIfDirty(st);
      }
      return 0;

    case WM_CLOSE:
      flushUserHistoryIfDirty(st);
      DestroyWindow(hWnd);
      return 0;

    case WM_DESTROY:
      KillTimer(hWnd, kTimerSaveUser);
      flushUserHistoryIfDirty(st);
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

static LRESULT CALLBACK overlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  ImeDaemonState * st = g_ime;
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      if (st && st->memDC) {
        int x = ps.rcPaint.left;
        int y = ps.rcPaint.top;
        int w = ps.rcPaint.right - x;
        int h = ps.rcPaint.bottom - y;
        if (w > 0 && h > 0) {
          BitBlt(hdc, x, y, w, h, st->memDC, x, y, SRCCOPY);
        }
      }
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_KEYDOWN: {
      if (!st) return 0;
      if (wParam == VK_F2) {
        overlayCommitCurrentSeg(st);
        st->inputMode = (st->inputMode + 1) % 4;
      } else if (wParam == VK_ESCAPE) {
        if (st->converting || st->romaji.hasContent()) {
          st->converting = false;
          st->candCount = 0;
          st->romaji.reset();
        } else {
          closeOverlay(st, false);
          return 0;
        }
      } else if (wParam == VK_RETURN || wParam == VK_F3) {
        if (st->converting || st->romaji.hasContent()) {
          overlayCommitCurrentSeg(st);
        } else {
          closeOverlay(st, true);
          return 0;
        }
      } else if (wParam == VK_BACK) {
        if (st->converting) {
          st->converting = false;
          st->candCount = 0;
        } else if (!st->romaji.backspace()) {
          size_t l = wcslen(st->compBuf);
          if (l > 0) st->compBuf[l - 1] = L'\0';
        }
      } else if (wParam == VK_SPACE) {
        if (st->converting && st->candCount > 0) {
          st->candSel = (st->candSel + 1) % st->candCount;
          st->candPageStart = (st->candSel / 8) * 8;
        } else if (st->inputMode == 0 && st->romaji.hasContent()) {
          st->romaji.flushPendingN();
          st->candCount = queryDictWithClause(st, st->romaji.kana, st->cands, st->segLen);
          st->converting = (st->candCount > 0);
          st->candSel = 0;
          st->candPageStart = 0;
        } else {
          wcsncat(st->compBuf, L" ", 63 - wcslen(st->compBuf));
        }
      } else if (st->converting && (wParam == VK_LEFT || wParam == VK_RIGHT)) {
        int dir = (wParam == VK_RIGHT) ? 1 : -1;
        st->candSel = (st->candSel + st->candCount + dir) % st->candCount;
        st->candPageStart = (st->candSel / 8) * 8;
      } else if (st->converting && wParam >= '1' && wParam <= '8') {
        int pick = st->candPageStart + static_cast<int>(wParam - '1');
        if (pick < st->candCount) {
          st->candSel = pick;
          overlayCommitCurrentSeg(st);
        }
      } else if (wParam == VK_F7 && st->romaji.hasContent()) {
        st->romaji.flushPendingN();
        wchar_t kata[64] = L"";
        LiliumIME::RomajiEngine::toKatakana(st->romaji.kana, kata, 64);
        wcsncat(st->compBuf, kata, 63 - wcslen(st->compBuf));
        st->romaji.reset();
        st->converting = false;
        st->candCount = 0;
      } else if (st->inputMode == 0) {
        bool isShift = ((GetKeyState(VK_SHIFT) & 0x8000) != 0 ||
                        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
#ifdef _WIN32_WCE
        if ((GetKeyState(VK_CAPITAL) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_CAPITAL) & 0x8000) != 0) {
          isShift = true;
        }
#endif
        if (wParam == VK_PRIOR || (isShift && wParam == 'N')) {
          if (st->converting) overlayCommitCurrentSeg(st);
          st->romaji.feedChar(',');
        } else if (wParam == VK_NEXT || (isShift && wParam == 'M')) {
          if (st->converting) overlayCommitCurrentSeg(st);
          st->romaji.feedChar('.');
        } else if (wParam >= 'A' && wParam <= 'Z') {
          if (st->converting) overlayCommitCurrentSeg(st);
          else if (st->romaji.romLen == 0 && st->romaji.kanaLen == 1 &&
                   (st->romaji.kana[0] == L'、' || st->romaji.kana[0] == L'。')) {
            overlayCommitCurrentSeg(st);
          }
          st->romaji.feedChar(static_cast<char>(wParam - 'A' + 'a'));
        } else if (wParam >= '0' && wParam <= '9') {
          if (st->converting) overlayCommitCurrentSeg(st);
          else if (st->romaji.romLen == 0 && st->romaji.kanaLen == 1 &&
                   (st->romaji.kana[0] == L'、' || st->romaji.kana[0] == L'。')) {
            overlayCommitCurrentSeg(st);
          }
          st->romaji.feedChar(static_cast<char>(wParam));
        } else if (wParam == VK_OEM_MINUS) {
          if (st->converting) overlayCommitCurrentSeg(st);
          st->romaji.feedChar('-');
        } else if (wParam == VK_OEM_PERIOD) {
          if (st->converting) overlayCommitCurrentSeg(st);
          st->romaji.feedChar('.');
        } else if (wParam == VK_OEM_COMMA) {
          if (st->converting) overlayCommitCurrentSeg(st);
          st->romaji.feedChar(',');
        }
      } else {
        wchar_t ch = 0;
        if (wParam >= 'A' && wParam <= 'Z') {
          ch = static_cast<wchar_t>((st->inputMode == 2) ? wParam : (wParam - 'A' + 'a'));
        } else if (wParam >= '0' && wParam <= '9') {
          ch = static_cast<wchar_t>(wParam);
        } else if (wParam == VK_PRIOR) {
          ch = L',';
        } else if (wParam == VK_NEXT) {
          ch = L'.';
        }
        if (ch) {
          size_t l = wcslen(st->compBuf);
          if (l + 1 < 64) {
            st->compBuf[l] = ch;
            st->compBuf[l + 1] = L'\0';
          }
        }
      }
      RECT dirty = {};
      renderOverlay(st, false, &dirty);
      if (!IsRectEmpty(&dirty)) InvalidateRect(hWnd, &dirty, FALSE);
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (!st) return 0;
      int x = static_cast<short>(LOWORD(lParam));
      int y = static_cast<short>(HIWORD(lParam));
      if (y >= 4 && y < 30) {
        if (x >= 280 && x < 390) {
          overlayCommitCurrentSeg(st);
          st->inputMode = (st->inputMode + 1) % 4;
          RECT dirty = {};
          renderOverlay(st, false, &dirty);
          if (!IsRectEmpty(&dirty)) InvalidateRect(hWnd, &dirty, FALSE);
        } else if (x >= 650 && x < 718) {
          closeOverlay(st, true);
        } else if (x >= 724 && x < 792) {
          closeOverlay(st, false);
        }
      }
      return 0;
    }

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  HINSTANCE hInst = GetModuleHandleW(NULL);
  const wchar_t * cmd = GetCommandLineW();
  bool daemonArg = (cmd && wcsstr(cmd, L"--daemon") != nullptr);

  HWND existing = FindWindowW(LiliumIME::kDaemonClassName, NULL);
  if (existing && IsWindow(existing)) {
    if (!daemonArg) {
      PostMessageW(existing, LiliumIME::kMsgImeShowOverlay, 0, 0);
    }
    return 0;
  }

  void * raw = calloc(1, sizeof(ImeDaemonState));
  if (!raw) return 1;
  ImeDaemonState * st = new (raw) ImeDaemonState();
  g_ime = st;

  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  wchar_t dicPath[MAX_PATH] = L"";
  LiliumUI::joinPath(exeDir, L"lilium_ime.dic", dicPath, MAX_PATH);
  if (GetFileAttributesW(dicPath) == INVALID_FILE_ATTRIBUTES) {
    wchar_t rootDir[MAX_PATH] = L"";
    LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
    LiliumUI::joinPath(rootDir, L"apps\\lilium_ime.dic", dicPath, MAX_PATH);
  }
  st->dict.load(dicPath);
  LiliumUI::joinPath(exeDir, L"ime_user.dat", st->userDatPath, MAX_PATH);
  loadUserHistory(st);

  st->hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                sizeof(LiliumIME::ImeSharedMem), LiliumIME::kShmName);
  if (st->hMap) {
    st->shm = static_cast<LiliumIME::ImeSharedMem *>(
        MapViewOfFile(st->hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LiliumIME::ImeSharedMem)));
    if (st->shm) {
      memset(st->shm, 0, sizeof(LiliumIME::ImeSharedMem));
      st->shm->magic = LiliumIME::kShmMagic;
      st->shm->version = LiliumIME::kShmVersion;
      st->shm->dictEntries = st->dict.totalPairs();
    }
  }

  WNDCLASSW wcDaemon = {};
  wcDaemon.lpfnWndProc = daemonWndProc;
  wcDaemon.hInstance = hInst;
  wcDaemon.lpszClassName = LiliumIME::kDaemonClassName;
  RegisterClassW(&wcDaemon);

  WNDCLASSW wcOver = {};
  wcOver.lpfnWndProc = overlayWndProc;
  wcOver.hInstance = hInst;
  wcOver.lpszClassName = L"LiliumIMEOverlayClass";
  wcOver.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassW(&wcOver);

#ifdef _WIN32_WCE
  DWORD daemonEx = 0;
  DWORD overEx = WS_EX_TOPMOST;
#else
  DWORD daemonEx = WS_EX_TOOLWINDOW;
  DWORD overEx = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
#endif

  st->hDaemon = CreateWindowExW(
      daemonEx, LiliumIME::kDaemonClassName, LiliumIME::kDaemonWindowTitle,
      WS_POPUP, 0, 0, 1, 1, NULL, NULL, hInst, NULL);
  st->hOverlay = CreateWindowExW(
      overEx, L"LiliumIMEOverlayClass", L"Lilium IME",
      WS_POPUP, kOverlayX, kOverlayY, kOverlayW, kOverlayH, NULL, NULL, hInst, NULL);

  initOverlayBackbuffer(st);

  if (st->hDaemon) {
    RegisterHotKey(st->hDaemon, kHotKeyCtrlSpace, MOD_CONTROL, VK_SPACE);
    RegisterHotKey(st->hDaemon, kHotKeyAltSpace, MOD_ALT, VK_SPACE);
#ifdef _WIN32_WCE
    RegisterHotKey(st->hDaemon, kHotKeyF11, 0, VK_F11);
    RegisterHotKey(st->hDaemon, kHotKeyConvert, 0, VK_CONVERT);
#endif
  }

  if (!daemonArg) {
    openOverlay(st, NULL);
  }

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (st->hDaemon) {
    UnregisterHotKey(st->hDaemon, kHotKeyCtrlSpace);
    UnregisterHotKey(st->hDaemon, kHotKeyAltSpace);
#ifdef _WIN32_WCE
    UnregisterHotKey(st->hDaemon, kHotKeyF11);
    UnregisterHotKey(st->hDaemon, kHotKeyConvert);
#endif
  }
  freeOverlayBackbuffer(st);
  flushUserHistoryIfDirty(st);
  if (st->hOverlay) DestroyWindow(st->hOverlay);
  if (st->shm) UnmapViewOfFile(st->shm);
  if (st->hMap) CloseHandle(st->hMap);

  g_ime = nullptr;
  st->~ImeDaemonState();
  free(raw);
  return 0;
}
