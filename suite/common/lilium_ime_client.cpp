#include "lilium_ime_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef VK_OEM_1
#define VK_OEM_1 0xBA
#endif
#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS 0xBB
#endif
#ifndef VK_OEM_COMMA
#define VK_OEM_COMMA 0xBC
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif
#ifndef VK_OEM_PERIOD
#define VK_OEM_PERIOD 0xBE
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

namespace LiliumIME {

// =============================================================================
// Romaji -> Hiragana / Katakana Engine
// =============================================================================

namespace {

struct RomEntry {
  const char * rom;
  const wchar_t * hira;
};

static constexpr RomEntry kRomTable[] = {
  // 4-char sequences
  { "ltsu", L"っ" }, { "xtsu", L"っ" },
  // 3-char sequences
  { "kya", L"きゃ" }, { "kyi", L"きぃ" }, { "kyu", L"きゅ" }, { "kye", L"きぇ" }, { "kyo", L"きょ" },
  { "gya", L"ぎゃ" }, { "gyi", L"ぎぃ" }, { "gyu", L"ぎゅ" }, { "gye", L"ぎぇ" }, { "gyo", L"ぎょ" },
  { "sha", L"しゃ" }, { "shi", L"し" },   { "shu", L"しゅ" }, { "she", L"しぇ" }, { "sho", L"しょ" },
  { "sya", L"しゃ" }, { "syi", L"しぃ" }, { "syu", L"しゅ" }, { "sye", L"しぇ" }, { "syo", L"しょ" },
  { "zha", L"じゃ" }, { "zhi", L"じぃ" }, { "zhu", L"じゅ" }, { "zhe", L"じぇ" }, { "zho", L"じょ" },
  { "jya", L"じゃ" }, { "jyi", L"じぃ" }, { "jyu", L"じゅ" }, { "jye", L"じぇ" }, { "jyo", L"じょ" },
  { "zya", L"じゃ" }, { "zyi", L"じぃ" }, { "zyu", L"じゅ" }, { "zye", L"じぇ" }, { "zyo", L"じょ" },
  { "cha", L"ちゃ" }, { "chi", L"ち" },   { "chu", L"ちゅ" }, { "che", L"ちぇ" }, { "cho", L"ちょ" },
  { "cya", L"ちゃ" }, { "cyi", L"ちぃ" }, { "cyu", L"ちゅ" }, { "cye", L"ちぇ" }, { "cyo", L"ちょ" },
  { "tya", L"ちゃ" }, { "tyi", L"ちぃ" }, { "tyu", L"ちゅ" }, { "tye", L"ちぇ" }, { "tyo", L"ちょ" },
  { "dya", L"ぢゃ" }, { "dyi", L"ぢぃ" }, { "dyu", L"ぢゅ" }, { "dye", L"ぢぇ" }, { "dyo", L"ぢょ" },
  { "tha", L"てゃ" }, { "thi", L"てぃ" }, { "thu", L"てゅ" }, { "the", L"てぇ" }, { "tho", L"てょ" },
  { "dha", L"でゃ" }, { "dhi", L"でぃ" }, { "dhu", L"でゅ" }, { "dhe", L"でぇ" }, { "dho", L"でょ" },
  { "tsu", L"つ" },   { "tsa", L"つぁ" }, { "tsi", L"つぃ" }, { "tse", L"つぇ" }, { "tso", L"つぉ" },
  { "nya", L"にゃ" }, { "nyi", L"にぃ" }, { "nyu", L"にゅ" }, { "nye", L"にぇ" }, { "nyo", L"にょ" },
  { "hya", L"ひゃ" }, { "hyi", L"ひぃ" }, { "hyu", L"ひゅ" }, { "hye", L"ひぇ" }, { "hyo", L"ひょ" },
  { "bya", L"びゃ" }, { "byi", L"びぃ" }, { "byu", L"びゅ" }, { "bye", L"びぇ" }, { "byo", L"びょ" },
  { "pya", L"ぴゃ" }, { "pyi", L"ぴぃ" }, { "pyu", L"ぴゅ" }, { "pye", L"ぴぇ" }, { "pyo", L"ぴょ" },
  { "fya", L"ふゃ" }, { "fyu", L"ふゅ" }, { "fyo", L"ふょ" },
  { "mya", L"みゃ" }, { "myi", L"みぃ" }, { "myu", L"みゅ" }, { "mye", L"みぇ" }, { "myo", L"みょ" },
  { "rya", L"りゃ" }, { "ryi", L"りぃ" }, { "ryu", L"りゅ" }, { "rye", L"りぇ" }, { "ryo", L"りょ" },
  { "wha", L"うぁ" }, { "whi", L"うぃ" }, { "whe", L"うぇ" }, { "who", L"うぉ" },
  { "xtu", L"っ" },   { "ltu", L"っ" },
  { "xya", L"ゃ" },   { "xyu", L"ゅ" },   { "xyo", L"ょ" },   { "xwa", L"ゎ" },
  { "lya", L"ゃ" },   { "lyu", L"ゅ" },   { "lyo", L"ょ" },   { "lwa", L"ゎ" },
  { "vya", L"ヴゃ" }, { "vyu", L"ヴゅ" }, { "vyo", L"ヴょ" },

  // 2-char sequences
  { "ka", L"か" }, { "ki", L"き" }, { "ku", L"く" }, { "ke", L"け" }, { "ko", L"こ" },
  { "ga", L"が" }, { "gi", L"ぎ" }, { "gu", L"ぐ" }, { "ge", L"げ" }, { "go", L"ご" },
  { "sa", L"さ" }, { "si", L"し" }, { "su", L"す" }, { "se", L"せ" }, { "so", L"そ" },
  { "za", L"ざ" }, { "zi", L"じ" }, { "zu", L"ず" }, { "ze", L"ぜ" }, { "zo", L"ぞ" },
  { "ja", L"じゃ" }, { "ji", L"じ" }, { "ju", L"じゅ" }, { "je", L"じぇ" }, { "jo", L"じょ" },
  { "ta", L"た" }, { "ti", L"ち" }, { "tu", L"つ" }, { "te", L"て" }, { "to", L"と" },
  { "da", L"だ" }, { "di", L"ぢ" }, { "du", L"づ" }, { "de", L"で" }, { "do", L"ど" },
  { "na", L"な" }, { "ni", L"に" }, { "nu", L"ぬ" }, { "ne", L"ね" }, { "no", L"の" },
  { "nn", L"ん" }, { "n'", L"ん" }, { "xn", L"ん" },
  { "ha", L"は" }, { "hi", L"ひ" }, { "hu", L"ふ" }, { "he", L"へ" }, { "ho", L"ほ" },
  { "fa", L"ふぁ" }, { "fi", L"ふぃ" }, { "fu", L"ふ" }, { "fe", L"ふぇ" }, { "fo", L"ふぉ" },
  { "ba", L"ば" }, { "bi", L"び" }, { "bu", L"ぶ" }, { "be", L"べ" }, { "bo", L"ぼ" },
  { "pa", L"ぱ" }, { "pi", L"ぴ" }, { "pu", L"ぷ" }, { "pe", L"ぺ" }, { "po", L"ぽ" },
  { "ma", L"ま" }, { "mi", L"み" }, { "mu", L"む" }, { "me", L"め" }, { "mo", L"も" },
  { "ya", L"や" }, { "yu", L"ゆ" }, { "ye", L"いぇ" }, { "yo", L"よ" },
  { "ra", L"ら" }, { "ri", L"り" }, { "ru", L"る" }, { "re", L"れ" }, { "ro", L"ろ" },
  { "wa", L"わ" }, { "wi", L"うぃ" }, { "wu", L"う" }, { "we", L"うぇ" }, { "wo", L"を" },
  { "va", L"ヴぁ" }, { "vi", L"ヴぃ" }, { "vu", L"ヴ" }, { "ve", L"ヴぇ" }, { "vo", L"ヴぉ" },
  { "xa", L"ぁ" }, { "xi", L"ぃ" }, { "xu", L"ぅ" }, { "xe", L"ぇ" }, { "xo", L"ぉ" },
  { "la", L"ぁ" }, { "li", L"ぃ" }, { "lu", L"ぅ" }, { "le", L"ぇ" }, { "lo", L"ぉ" },
  { "ca", L"か" }, { "cu", L"く" }, { "co", L"こ" }, { "qa", L"くぁ" }, { "qi", L"くぃ" }, { "qu", L"く" }, { "qe", L"くぇ" }, { "qo", L"くぉ" },

  // 1-char vowels
  { "a", L"あ" }, { "i", L"い" }, { "u", L"う" }, { "e", L"え" }, { "o", L"お" }
};

static bool isVowelOrY(char c) {
  return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o' || c == 'y';
}

static bool isConsonantLetter(char c) {
  return (c >= 'a' && c <= 'z') && !isVowelOrY(c) && c != 'n';
}

static const wchar_t * lookupExactRom(const char * rom) {
  for (size_t i = 0; i < sizeof(kRomTable) / sizeof(kRomTable[0]); ++i) {
    if (strcmp(kRomTable[i].rom, rom) == 0) return kRomTable[i].hira;
  }
  return nullptr;
}

static bool hasPrefixRom(const char * prefix) {
  size_t pLen = strlen(prefix);
  if (pLen == 0) return true;
  for (size_t i = 0; i < sizeof(kRomTable) / sizeof(kRomTable[0]); ++i) {
    if (strncmp(kRomTable[i].rom, prefix, pLen) == 0) return true;
  }
  return false;
}

} // namespace

void RomajiEngine::reset() {
  kana[0] = L'\0';
  kanaLen = 0;
  rom[0] = '\0';
  romLen = 0;
}

void RomajiEngine::appendDirect(wchar_t ch) {
  if (kanaLen + 1 < kMaxReadingChars) {
    kana[kanaLen++] = ch;
    kana[kanaLen] = L'\0';
  }
}

void RomajiEngine::flushPendingN() {
  if (romLen == 1 && rom[0] == 'n') {
    appendDirect(L'ん');
    rom[0] = '\0';
    romLen = 0;
  }
}

void RomajiEngine::feedChar(char lowerAscii) {
  // Handle single punctuation directly
  if (lowerAscii == '-') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'ー'); return; }
  if (lowerAscii == ',') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'、'); return; }
  if (lowerAscii == '.') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'。'); return; }
  if (lowerAscii == '[') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'「'); return; }
  if (lowerAscii == ']') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'」'); return; }
  if (lowerAscii == '?') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'？'); return; }
  if (lowerAscii == '!') { flushPendingN(); rom[0] = '\0'; romLen = 0; appendDirect(L'！'); return; }

  if (lowerAscii < 'a' || lowerAscii > 'z') {
    if (lowerAscii == '\'' && romLen == 1 && rom[0] == 'n') {
      appendDirect(L'ん');
      rom[0] = '\0';
      romLen = 0;
      return;
    }
    flushPendingN();
    rom[0] = '\0';
    romLen = 0;
    if (lowerAscii >= '0' && lowerAscii <= '9') {
      appendDirect(static_cast<wchar_t>(lowerAscii));
    }
    return;
  }

  // Handle 'n' + consonant -> emit 'ん' then process new char
  if (romLen == 1 && rom[0] == 'n') {
    if (!isVowelOrY(lowerAscii) && lowerAscii != 'n') {
      appendDirect(L'ん');
      rom[0] = '\0';
      romLen = 0;
    }
  }

  // Handle double consonant sokuon (kk, tt, ss, pp, cc, etc.) -> 'っ'
  if (romLen == 1 && rom[0] == lowerAscii && isConsonantLetter(lowerAscii)) {
    appendDirect(L'っ');
    return; // keep single consonant in rom[0]
  }

  if (romLen + 1 >= static_cast<int>(sizeof(rom))) {
    rom[0] = '\0';
    romLen = 0;
  }
  rom[romLen++] = lowerAscii;
  rom[romLen] = '\0';

  const wchar_t * exact = lookupExactRom(rom);
  if (exact) {
    for (int i = 0; exact[i] != L'\0'; ++i) {
      appendDirect(exact[i]);
    }
    rom[0] = '\0';
    romLen = 0;
    return;
  }

  // If no valid Romaji starts with this buffer, drop oldest char (e.g., typo consonant)
  while (romLen > 0 && !hasPrefixRom(rom)) {
    if (romLen == 1) {
      rom[0] = '\0';
      romLen = 0;
    } else {
      memmove(rom, rom + 1, romLen);
      --romLen;
      const wchar_t * ex2 = lookupExactRom(rom);
      if (ex2) {
        for (int i = 0; ex2[i] != L'\0'; ++i) appendDirect(ex2[i]);
        rom[0] = '\0';
        romLen = 0;
        break;
      }
    }
  }
}

bool RomajiEngine::backspace() {
  if (romLen > 0) {
    rom[--romLen] = '\0';
    return true;
  }
  if (kanaLen > 0) {
    kana[--kanaLen] = L'\0';
    return true;
  }
  return false;
}

void RomajiEngine::getPreeditDisplay(wchar_t * out, size_t maxLen) const {
  if (!out || maxLen == 0) return;
  size_t pos = 0;
  for (int i = 0; i < kanaLen && pos + 1 < maxLen; ++i) {
    out[pos++] = kana[i];
  }
  for (int i = 0; i < romLen && pos + 1 < maxLen; ++i) {
    out[pos++] = static_cast<wchar_t>(rom[i]);
  }
  out[pos] = L'\0';
}

void RomajiEngine::toKatakana(const wchar_t * hiragana, wchar_t * out, size_t maxLen) {
  if (!hiragana || !out || maxLen == 0) return;
  size_t i = 0;
  for (; hiragana[i] != L'\0' && i + 1 < maxLen; ++i) {
    wchar_t c = hiragana[i];
    if (c >= 0x3041 && c <= 0x3096) {
      out[i] = static_cast<wchar_t>(c + 0x60);
    } else {
      out[i] = c;
    }
  }
  out[i] = L'\0';
}

// =============================================================================
// IPC Client to LiliumIME Daemon
// =============================================================================

ImeClient::ImeClient() : m_hMap(NULL), m_shm(nullptr) {}

ImeClient::~ImeClient() {
  close();
}

void ImeClient::close() {
  if (m_shm) {
    UnmapViewOfFile(m_shm);
    m_shm = nullptr;
  }
  if (m_hMap) {
    CloseHandle(m_hMap);
    m_hMap = NULL;
  }
}

HWND ImeClient::findDaemonWindow() {
  return FindWindowW(kDaemonClassName, NULL);
}

bool ImeClient::connectOrLaunch(bool allowLaunch) {
  if (!m_hMap) {
    // WinCE 6.0 uses CreateFileMappingW on both server and client (no OpenFileMappingW in coredll)
    m_hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                sizeof(ImeSharedMem), kShmName);
    if (m_hMap) {
      m_shm = static_cast<ImeSharedMem *>(MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ImeSharedMem)));
      if (!m_shm) {
        CloseHandle(m_hMap);
        m_hMap = NULL;
      }
    }
  }

  HWND hDaemon = findDaemonWindow();
  if (!hDaemon && allowLaunch) {
    wchar_t rootDir[MAX_PATH] = L"";
    LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
    wchar_t imeExe[MAX_PATH] = L"";
    LiliumUI::joinPath(rootDir, L"apps\\LiliumIME.exe", imeExe, MAX_PATH);
    if (GetFileAttributesW(imeExe) == INVALID_FILE_ATTRIBUTES) {
      wchar_t exeDir[MAX_PATH] = L"";
      LiliumUI::getExeDir(exeDir, MAX_PATH);
      LiliumUI::joinPath(exeDir, L"LiliumIME.exe", imeExe, MAX_PATH);
    }
    if (GetFileAttributesW(imeExe) != INVALID_FILE_ATTRIBUTES) {
      PROCESS_INFORMATION pi = {};
#ifdef _WIN32_WCE
      wchar_t cmdArgs[32] = L"--daemon";
      if (CreateProcessW(imeExe, cmdArgs, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
#else
      wchar_t cmdLine[MAX_PATH + 32] = L"";
      _snwprintf(cmdLine, MAX_PATH + 31, L"\"%ls\" --daemon", imeExe);
      STARTUPINFOW si = {};
      si.cb = sizeof(si);
      if (CreateProcessW(imeExe, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
#endif
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        for (int waitMs = 0; waitMs < 900; waitMs += 30) {
          hDaemon = findDaemonWindow();
          if (hDaemon) break;
          Sleep(30);
        }
      }
    }
  }

  return (m_shm != nullptr && hDaemon != NULL && IsWindow(hDaemon));
}

bool ImeClient::isAlive() {
  HWND h = findDaemonWindow();
  if (!h || !IsWindow(h)) return false;
  return SendMessageW(h, kMsgImePing, 0, 0) == 1;
}

uint32_t ImeClient::dictEntryCount() {
  if (!connectOrLaunch(false)) return 0;
  HWND h = findDaemonWindow();
  if (!h) return 0;
  if (SendMessageW(h, kMsgImePing, 0, 0) == 1 && m_shm->magic == kShmMagic) {
    return m_shm->dictEntries;
  }
  return 0;
}

int ImeClient::queryCandidates(const wchar_t * reading, wchar_t outCands[kMaxCands][kMaxCandChars], int & outSegLen) {
  outSegLen = 0;
  if (!reading || !reading[0]) return 0;

  if (connectOrLaunch(true) && m_shm) {
    HWND h = findDaemonWindow();
    if (h && IsWindow(h)) {
      m_shm->magic = kShmMagic;
      m_shm->version = kShmVersion;
      wcsncpy(m_shm->reading, reading, kMaxReadingChars - 1);
      m_shm->reading[kMaxReadingChars - 1] = L'\0';
      m_shm->candCount = 0;
      m_shm->segLen = 0;

      LRESULT res = SendMessageW(h, kMsgImeQuery, static_cast<WPARAM>(kMaxCands), 0);
      if (res > 0 && m_shm->candCount > 0) {
        int n = m_shm->candCount;
        if (n > kMaxCands) n = kMaxCands;
        for (int i = 0; i < n; ++i) {
          wcsncpy(outCands[i], m_shm->candidates[i], kMaxCandChars - 1);
          outCands[i][kMaxCandChars - 1] = L'\0';
        }
        outSegLen = m_shm->segLen;
        return n;
      }
    }
  }

  // Fallback if daemon unavailable: Hiragana + Katakana
  wcsncpy(outCands[0], reading, kMaxCandChars - 1);
  outCands[0][kMaxCandChars - 1] = L'\0';
  RomajiEngine::toKatakana(reading, outCands[1], kMaxCandChars);
  outSegLen = static_cast<int>(wcslen(reading));
  return (_wcsicmp(outCands[0], outCands[1]) == 0) ? 1 : 2;
}

void ImeClient::learnChoice(const wchar_t * reading, const wchar_t * surface) {
  if (!reading || !reading[0] || !surface || !surface[0]) return;
  if (!connectOrLaunch(false) || !m_shm) return;
  HWND h = findDaemonWindow();
  if (!h || !IsWindow(h)) return;
  m_shm->magic = kShmMagic;
  m_shm->version = kShmVersion;
  wcsncpy(m_shm->reading, reading, kMaxReadingChars - 1);
  m_shm->reading[kMaxReadingChars - 1] = L'\0';
  wcsncpy(m_shm->chosen, surface, kMaxCandChars - 1);
  m_shm->chosen[kMaxCandChars - 1] = L'\0';
  SendMessageW(h, kMsgImeLearn, 0, 0);
}

bool ImeClient::showExternalOverlay(HWND targetHwnd) {
  if (!connectOrLaunch(true)) return false;
  HWND h = findDaemonWindow();
  if (!h || !IsWindow(h)) return false;
  PostMessageW(h, kMsgImeShowOverlay, reinterpret_cast<WPARAM>(targetHwnd), 0);
  return true;
}

// =============================================================================
// Reusable TextInputModal Widget
// =============================================================================

TextInputModal::TextInputModal()
  : m_open(false),
    m_maxChars(64),
    m_cursor(0),
    m_inputMode(0),
    m_btnFocus(0),
    m_converting(false),
    m_candCount(0),
    m_candSel(0),
    m_candPageStart(0),
    m_segLen(0) {
  m_title[0] = L'\0';
  m_hint[0] = L'\0';
  m_buf[0] = L'\0';
  m_romaji.reset();
  memset(m_cands, 0, sizeof(m_cands));
}

void TextInputModal::open(const wchar_t * title, const wchar_t * hint, const wchar_t * initialText, int maxChars) {
  m_open = true;
  wcsncpy(m_title, title ? title : L"テキスト入力", 63);
  m_title[63] = L'\0';
  wcsncpy(m_hint, hint ? hint : L"", 95);
  m_hint[95] = L'\0';
  m_maxChars = (maxChars > 0 && maxChars < kMaxBuf) ? maxChars : (kMaxBuf - 1);

  if (initialText) {
    wcsncpy(m_buf, initialText, static_cast<size_t>(m_maxChars));
    m_buf[m_maxChars] = L'\0';
  } else {
    m_buf[0] = L'\0';
  }
  m_cursor = static_cast<int>(wcslen(m_buf));
  m_inputMode = 0; // Default to Lilium IME (あ)
  m_btnFocus = 0;
  m_romaji.reset();
  m_converting = false;
  m_candCount = 0;
  m_candSel = 0;
  m_candPageStart = 0;
  m_segLen = 0;
  m_ime.connectOrLaunch(true);
}

void TextInputModal::close() {
  m_open = false;
  m_romaji.reset();
  m_converting = false;
  m_candCount = 0;
}

void TextInputModal::insertStrAtCursor(const wchar_t * str) {
  if (!str || !str[0]) return;
  int len = static_cast<int>(wcslen(m_buf));
  int addLen = static_cast<int>(wcslen(str));
  int room = m_maxChars - len;
  if (room <= 0) return;
  if (addLen > room) addLen = room;
  memmove(m_buf + m_cursor + addLen, m_buf + m_cursor,
          static_cast<size_t>(len - m_cursor + 1) * sizeof(wchar_t));
  memcpy(m_buf + m_cursor, str, static_cast<size_t>(addLen) * sizeof(wchar_t));
  m_cursor += addLen;
}

void TextInputModal::deleteCharBeforeCursor() {
  if (m_cursor <= 0) return;
  int len = static_cast<int>(wcslen(m_buf));
  memmove(m_buf + m_cursor - 1, m_buf + m_cursor,
          static_cast<size_t>(len - m_cursor + 1) * sizeof(wchar_t));
  --m_cursor;
}

void TextInputModal::startConversion() {
  m_romaji.flushPendingN();
  if (m_romaji.kanaLen <= 0) return;
  int seg = 0;
  int n = m_ime.queryCandidates(m_romaji.kana, m_cands, seg);
  if (n > 0) {
    m_converting = true;
    m_candCount = n;
    m_candSel = 0;
    m_candPageStart = 0;
    m_segLen = (seg > 0 && seg <= m_romaji.kanaLen) ? seg : m_romaji.kanaLen;
  }
}

void TextInputModal::commitSelectedCandidate() {
  if (!m_converting || m_candCount <= 0) return;
  const wchar_t * chosen = m_cands[m_candSel];
  int segLen = (m_segLen > 0 && m_segLen <= m_romaji.kanaLen) ? m_segLen : m_romaji.kanaLen;

  // Learn full-segment or exact match choice
  wchar_t segReading[kMaxReadingChars] = L"";
  wcsncpy(segReading, m_romaji.kana, static_cast<size_t>(segLen));
  segReading[segLen] = L'\0';
  m_ime.learnChoice(segReading, chosen);

  insertStrAtCursor(chosen);

  int rem = m_romaji.kanaLen - segLen;
  if (rem > 0) {
    memmove(m_romaji.kana, m_romaji.kana + segLen, static_cast<size_t>(rem + 1) * sizeof(wchar_t));
    m_romaji.kanaLen = rem;
    m_converting = false;
    m_candCount = 0;
    startConversion();
  } else {
    m_romaji.reset();
    m_converting = false;
    m_candCount = 0;
    m_candSel = 0;
    m_candPageStart = 0;
    m_segLen = 0;
  }
}

void TextInputModal::commitRawPreedit() {
  m_romaji.flushPendingN();
  wchar_t disp[kMaxReadingChars] = L"";
  m_romaji.getPreeditDisplay(disp, kMaxReadingChars);
  if (disp[0]) insertStrAtCursor(disp);
  m_romaji.reset();
  m_converting = false;
  m_candCount = 0;
}

void TextInputModal::commitKatakanaPreedit() {
  m_romaji.flushPendingN();
  if (m_romaji.kanaLen > 0) {
    wchar_t kata[kMaxReadingChars] = L"";
    RomajiEngine::toKatakana(m_romaji.kana, kata, kMaxReadingChars);
    insertStrAtCursor(kata);
  }
  m_romaji.reset();
  m_converting = false;
  m_candCount = 0;
}

void TextInputModal::cancelPreeditOrConv() {
  if (m_converting) {
    m_converting = false;
    m_candCount = 0;
    m_candSel = 0;
    m_candPageStart = 0;
  } else {
    m_romaji.reset();
  }
}

wchar_t TextInputModal::map12xSymbol(WPARAM vkCode) const {
  if (vkCode >= '0' && vkCode <= '9') return static_cast<wchar_t>(vkCode);
  switch (vkCode) {
    case 'Q': return L'1'; case 'W': return L'2'; case 'E': return L'3';
    case 'R': return L'4'; case 'T': return L'5'; case 'Y': return L'6';
    case 'U': return L'7'; case 'I': return L'8'; case 'O': return L'9';
    case 'P': return L'0';
    case 'A': return L'+'; case 'S': return L'-'; case 'D': return L'*';
    case 'F': return L'/'; case 'G': return L'=';
    case 'H': return L'('; case 'J': return L')'; case 'K': return L'['; case 'L': return L']';
    case 'Z': return L'#'; case 'X': return L'_'; case 'C': return L':'; case 'V': return L'!'; case 'B': return L'?';
    case 'N': return L','; case 'M': return L'.';
    case VK_OEM_MINUS: return L'-';
    case VK_OEM_PERIOD: return L'.';
    case VK_OEM_COMMA: return L',';
    case VK_OEM_PLUS: return L'+';
    case VK_OEM_2: return L'/';
    case VK_OEM_4: return L'[';
    case VK_OEM_6: return L']';
    case VK_OEM_1: return L':';
    default: return L'\0';
  }
}

void TextInputModal::render(LiliumUI::Canvas & c, bool drawSidebarGuide) const {
  if (!m_open) return;
  c.darkenScreen();

  const int mx = 36, my = 52, mw = 568, mh = 360;
  c.drawBox(mx, my, mw, mh, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawThickFrame(mx, my, mw, mh, 2, LiliumUI::kColTealDark);
  c.fillRect(mx, my, mw, 38, LiliumUI::kColTeal);
  c.drawText(mx + 14, my, 360, 38, LiliumUI::kColWhite, m_title, 2, LiliumUI::Align::Left, true);

  // Input mode pill top-right
  static constexpr const wchar_t * kModeLabels[4] = {
    L"[F2] あ (IME)", L"[F2] abc", L"[F2] ABC", L"[F2] 12x記号"
  };
  uint32_t mBg = (m_inputMode == 0) ? 0xFFF3D6 : LiliumUI::kColTealSoft;
  uint32_t mBd = (m_inputMode == 0) ? 0xCC7A00 : LiliumUI::kColWhite;
  uint32_t mTx = (m_inputMode == 0) ? 0x8A4E00 : LiliumUI::kColTealDeep;
  c.drawBox(mx + mw - 160, my + 6, 148, 26, mBg, mBd);
  c.drawText(mx + mw - 160, my + 6, 148, 26, mTx, kModeLabels[m_inputMode], 2, LiliumUI::Align::Center, true);

  if (m_hint[0]) {
    c.drawText(mx + 16, my + 46, mw - 32, 22, LiliumUI::kColMuted, m_hint, 2, LiliumUI::Align::Left, true);
  }

  // Editable text box with inline IME composition underline
  c.drawBox(mx + 16, my + 74, mw - 32, 48, 0xF8FAFC, LiliumUI::kColTealDark);
  c.drawThickFrame(mx + 16, my + 74, mw - 32, 48, 2, LiliumUI::kColTealDark);

  wchar_t pre[kMaxReadingChars] = L"";
  if (m_converting && m_candCount > 0) {
    wcsncpy(pre, m_cands[m_candSel], kMaxReadingChars - 1);
  } else {
    m_romaji.getPreeditDisplay(pre, kMaxReadingChars);
  }

  wchar_t leftPart[kMaxBuf + 4] = L"";
  int copyL = (m_cursor < kMaxBuf) ? m_cursor : kMaxBuf - 1;
  wcsncpy(leftPart, m_buf, static_cast<size_t>(copyL));
  leftPart[copyL] = L'\0';

  const wchar_t * rightPart = m_buf + m_cursor;

  int tx = mx + 26;
  int ty = my + 90;
  if (leftPart[0]) {
    int lw = c.measureWidth(leftPart, 2);
    c.drawText(tx, my + 74, mw - 52, 48, LiliumUI::kColText, leftPart, 2, LiliumUI::Align::Left, true);
    tx += lw;
  }
  if (pre[0]) {
    int pw = c.measureWidth(pre, 2);
    c.fillRect(tx, my + 82, pw + 2, 30, m_converting ? LiliumUI::kColTealSoft : 0xFFF3D6);
    c.drawText(tx, my + 74, pw + 16, 48, m_converting ? LiliumUI::kColTealDeep : 0x8A4E00,
               pre, 2, LiliumUI::Align::Left, true);
    c.fillRect(tx, ty + 18, pw, 2, m_converting ? LiliumUI::kColTealDark : 0xCC7A00);
    tx += pw + 2;
  } else {
    c.fillRect(tx, my + 84, 2, 28, LiliumUI::kColTealDark);
    tx += 3;
  }
  if (rightPart[0]) {
    c.drawText(tx, my + 74, (mx + mw - 24) - tx, 48, LiliumUI::kColText,
               rightPart, 2, LiliumUI::Align::Left, true);
  }

  // Candidate strip / Mozc status box (y = my+132 .. my+258)
  c.drawBox(mx + 16, my + 132, mw - 32, 126, LiliumUI::kColRowAlt, LiliumUI::kColBorder);
  if (m_converting && m_candCount > 0) {
    c.drawText(mx + 26, my + 138, 420, 22, LiliumUI::kColTealDeep,
               L"変換候補 ([1-8]決定 / [←→][Spc]移動):", 2, LiliumUI::Align::Left, true);
    wchar_t pgBuf[24];
    _snwprintf(pgBuf, 24, L"%d / %d", m_candSel + 1, m_candCount);
    c.drawText(mx + mw - 126, my + 138, 100, 22, LiliumUI::kColMuted, pgBuf, 2, LiliumUI::Align::Right, true);

    // Draw up to 4 candidates cleanly on row 1, next 4 on row 2
    int drawn = 0;
    for (int row = 0; row < 2; ++row) {
      int cx = mx + 26;
      int cy = my + 168 + row * 40;
      for (int col = 0; col < 4 && (m_candPageStart + drawn) < m_candCount; ++col) {
        int idx = m_candPageStart + drawn;
        bool sel = (idx == m_candSel);
        wchar_t pillTxt[64];
        _snwprintf(pillTxt, 64, L"%d.%ls", drawn + 1, m_cands[idx]);
        pillTxt[63] = L'\0';
        int pw = c.measureWidth(pillTxt, 2) + 18;
        if (pw < 76) pw = 76;
        if (cx + pw > mx + mw - 24) break;
        c.drawBox(cx, cy, pw, 32,
                  sel ? LiliumUI::kColTeal : LiliumUI::kColWhite,
                  sel ? LiliumUI::kColTealDark : LiliumUI::kColBorderStrong);
        c.drawText(cx, cy, pw, 32,
                   sel ? LiliumUI::kColWhite : LiliumUI::kColText,
                   pillTxt, 2, LiliumUI::Align::Center, true);
        cx += pw + 8;
        ++drawn;
      }
    }
  } else if (m_romaji.hasContent()) {
    c.drawText(mx + 26, my + 168, mw - 52, 26, LiliumUI::kColTealDeep,
               L"[Space]: Mozc漢字変換   [F7]: カタカナ   [Enter]: ひらがな確定",
               2, LiliumUI::Align::Center, true);
  } else {
    c.drawText(mx + 26, my + 160, mw - 52, 26, LiliumUI::kColTextSub,
               L"ローマ字で日本語入力 (あ) / [F2]で abc・ABC・12x記号 に切替",
               2, LiliumUI::Align::Center, true);
    c.drawText(mx + 26, my + 194, mw - 52, 24, LiliumUI::kColMuted,
               L"[←→]: カーソル移動   [BS]: 1文字削除   [F4]: 全消去",
               2, LiliumUI::Align::Center, true);
  }

  // Confirm / Cancel Buttons (my+274 .. my+314)
  const int bx0 = mx + (mw - (160 * 2 + 24)) / 2;
  const int bx1 = bx0 + 184;
  const int by = my + 272;
  c.drawBox(bx0, by, 160, 40,
            (m_btnFocus == 0) ? LiliumUI::kColTeal : LiliumUI::kColWhite,
            LiliumUI::kColTealDark);
  c.drawText(bx0, by, 160, 40,
             (m_btnFocus == 0) ? LiliumUI::kColWhite : LiliumUI::kColTealDeep,
             L"決　定", 2, LiliumUI::Align::Center, true);

  c.drawBox(bx1, by, 160, 40,
            (m_btnFocus == 1) ? LiliumUI::kColCharcoal : LiliumUI::kColWhite,
            LiliumUI::kColBorderStrong);
  c.drawText(bx1, by, 160, 40,
             (m_btnFocus == 1) ? LiliumUI::kColWhite : LiliumUI::kColText,
             L"キャンセル", 2, LiliumUI::Align::Center, true);

  c.drawText(mx + 14, my + 324, mw - 28, 24, LiliumUI::kColMuted,
             L"[F2]入力モード [Spc]変換 [F7]カナ [Ent]確定 [Esc]戻る",
             2, LiliumUI::Align::Center, true);

  if (drawSidebarGuide) {
    LiliumUI::renderSidebar(c, L"文字入力 (IME)", L"Lilium IME",
                            L"【操作ガイド】\n[F2]: あ/abc/ABC/12x\n[Spc]: 漢字変換\n[1-8]: 候補選択\n[F7]: カタカナ\n[Ent]: 確定/決定\n[F4]: 全消去\n[Esc]: 戻る",
                            L"[F2]モード切替", L"[Spc]漢字変換", L"戻　る", false);
  }
}

bool TextInputModal::handleKey(WPARAM vkCode, bool isShift, bool & outConfirmed, bool & outClosed) {
  outConfirmed = false;
  outClosed = false;
  if (!m_open) return false;

  if (vkCode == VK_F2) {
    if (m_romaji.hasContent()) commitRawPreedit();
    m_inputMode = (m_inputMode + 1) % 4;
    return true;
  }

  if (vkCode == VK_F4) {
    cancelPreeditOrConv();
    m_buf[0] = L'\0';
    m_cursor = 0;
    return true;
  }

  if (m_converting) {
    if (vkCode == VK_SPACE || vkCode == VK_RIGHT || vkCode == VK_DOWN) {
      m_candSel = (m_candSel + 1) % m_candCount;
      if (m_candSel < m_candPageStart) m_candPageStart = (m_candSel / 8) * 8;
      if (m_candSel >= m_candPageStart + 8) m_candPageStart = (m_candSel / 8) * 8;
      return true;
    }
    if (vkCode == VK_LEFT || vkCode == VK_UP) {
      m_candSel = (m_candSel + m_candCount - 1) % m_candCount;
      m_candPageStart = (m_candSel / 8) * 8;
      return true;
    }
    if (vkCode >= '1' && vkCode <= '8') {
      int pick = m_candPageStart + static_cast<int>(vkCode - '1');
      if (pick < m_candCount) {
        m_candSel = pick;
        commitSelectedCandidate();
      }
      return true;
    }
    if (vkCode == VK_F7) {
      commitKatakanaPreedit();
      return true;
    }
    if (vkCode == VK_RETURN || vkCode == VK_F3) {
      commitSelectedCandidate();
      return true;
    }
    if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      cancelPreeditOrConv();
      return true;
    }
    // Any typing key auto-commits current candidate then falls through to insert
    commitSelectedCandidate();
  }

  // Handle ESC / BackSpace / Enter
  if (vkCode == VK_ESCAPE) {
    if (m_romaji.hasContent()) {
      m_romaji.reset();
    } else {
      close();
      outClosed = true;
    }
    return true;
  }
  if (vkCode == VK_BACK) {
    if (m_romaji.backspace()) return true;
    deleteCharBeforeCursor();
    return true;
  }
  if (vkCode == VK_RETURN || vkCode == VK_F3) {
    if (m_romaji.hasContent()) {
      commitRawPreedit();
      return true;
    }
    if (m_btnFocus == 1) {
      close();
      outClosed = true;
    } else {
      close();
      outConfirmed = true;
      outClosed = true;
    }
    return true;
  }
  if (vkCode == VK_F7 && m_romaji.hasContent()) {
    commitKatakanaPreedit();
    return true;
  }
  if (vkCode == VK_SPACE) {
    if (m_inputMode == 0 && m_romaji.hasContent()) {
      startConversion();
    } else {
      insertStrAtCursor(L" ");
    }
    return true;
  }
  if (vkCode == VK_LEFT) {
    if (m_romaji.hasContent()) return true;
    if (m_cursor > 0) --m_cursor;
    return true;
  }
  if (vkCode == VK_RIGHT) {
    if (m_romaji.hasContent()) return true;
    int len = static_cast<int>(wcslen(m_buf));
    if (m_cursor < len) ++m_cursor;
    return true;
  }
  if (vkCode == VK_TAB) {
    m_btnFocus = (m_btnFocus == 0) ? 1 : 0;
    return true;
  }

  if (m_inputMode == 0) {
    if (vkCode >= 'A' && vkCode <= 'Z') {
      m_romaji.feedChar(static_cast<char>(vkCode - 'A' + 'a'));
      return true;
    }
    if (vkCode == VK_OEM_MINUS) { m_romaji.feedChar('-'); return true; }
    if (vkCode == VK_OEM_PERIOD) { m_romaji.feedChar('.'); return true; }
    if (vkCode == VK_OEM_COMMA) { m_romaji.feedChar(','); return true; }
    if (vkCode == VK_OEM_4) { m_romaji.feedChar('['); return true; }
    if (vkCode == VK_OEM_6) { m_romaji.feedChar(']'); return true; }
    if (vkCode >= '0' && vkCode <= '9') {
      m_romaji.feedChar(static_cast<char>(vkCode));
      return true;
    }
    return true;
  }

  if (m_inputMode == 1 || m_inputMode == 2) {
    if (vkCode >= 'A' && vkCode <= 'Z') {
      bool upper = (m_inputMode == 2) ^ isShift;
      wchar_t ch[2] = {
        static_cast<wchar_t>(upper ? vkCode : (vkCode - 'A' + 'a')),
        L'\0'
      };
      insertStrAtCursor(ch);
      return true;
    }
    if (vkCode >= '0' && vkCode <= '9') {
      wchar_t ch[2] = { static_cast<wchar_t>(vkCode), L'\0' };
      insertStrAtCursor(ch);
      return true;
    }
    if (vkCode == VK_OEM_MINUS) { insertStrAtCursor(isShift ? L"_" : L"-"); return true; }
    if (vkCode == VK_OEM_PERIOD) { insertStrAtCursor(L"."); return true; }
    if (vkCode == VK_OEM_COMMA) { insertStrAtCursor(L","); return true; }
    if (vkCode == VK_OEM_2) { insertStrAtCursor(isShift ? L"?" : L"/"); return true; }
    return true;
  }

  // Mode 3: 12x symbols & digits
  wchar_t sym = map12xSymbol(vkCode);
  if (sym) {
    wchar_t ch[2] = { sym, L'\0' };
    insertStrAtCursor(ch);
  }
  return true;
}

bool TextInputModal::handleClick(int x, int y, bool & outConfirmed, bool & outClosed) {
  outConfirmed = false;
  outClosed = false;
  if (!m_open) return false;

  const int mx = 36, my = 52, mw = 568;

  // Bottom-right sidebar 戻る button closes modal cleanly
  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    close();
    outClosed = true;
    return true;
  }

  // Mode toggle pill
  if (x >= mx + mw - 160 && x < mx + mw - 12 && y >= my + 6 && y < my + 32) {
    if (m_romaji.hasContent()) commitRawPreedit();
    m_inputMode = (m_inputMode + 1) % 4;
    return true;
  }

  // Confirm / Cancel buttons
  const int bx0 = mx + (mw - (160 * 2 + 24)) / 2;
  const int bx1 = bx0 + 184;
  const int by = my + 272;
  if (y >= by && y < by + 40) {
    if (x >= bx0 && x < bx0 + 160) {
      if (m_converting) commitSelectedCandidate();
      else if (m_romaji.hasContent()) commitRawPreedit();
      close();
      outConfirmed = true;
      outClosed = true;
      return true;
    }
    if (x >= bx1 && x < bx1 + 160) {
      close();
      outClosed = true;
      return true;
    }
  }
  return true;
}

} // namespace LiliumIME
