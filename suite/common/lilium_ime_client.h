#ifndef LILIUM_SUITE_COMMON_LILIUM_IME_CLIENT_H
#define LILIUM_SUITE_COMMON_LILIUM_IME_CLIENT_H

#include "lilium_ui.h"

namespace LiliumIME {

static constexpr const wchar_t * kDaemonClassName = L"LiliumIMEDaemonClass";
static constexpr const wchar_t * kDaemonWindowTitle = L"Lilium IME Daemon";
static constexpr const wchar_t * kShmName = L"LiliumIME_SHM_V1";

static constexpr UINT kMsgImeQuery       = WM_APP + 1; // wParam = maxCands, lParam = 0 -> returns candCount
static constexpr UINT kMsgImeLearn       = WM_APP + 2; // learns (reading, chosen)
static constexpr UINT kMsgImePing        = WM_APP + 3; // returns 1 and writes dictEntries
static constexpr UINT kMsgImeShowOverlay = WM_APP + 4; // wParam = target HWND (0 -> foreground)

static constexpr uint32_t kShmMagic = 0x4C494D45U; // 'LIME'
static constexpr uint32_t kShmVersion = 1U;
static constexpr int kMaxReadingChars = 48;
static constexpr int kMaxCandChars = 48;
static constexpr int kMaxCands = 24;

#pragma pack(push, 1)
struct ImeSharedMem {
  uint32_t magic;
  uint32_t version;
  uint32_t dictEntries;
  wchar_t reading[kMaxReadingChars];
  wchar_t chosen[kMaxCandChars];
  int32_t segLen;
  int32_t candCount;
  wchar_t candidates[kMaxCands][kMaxCandChars];
};
#pragma pack(pop)

// Romaji -> Hiragana / Katakana incremental engine
struct RomajiEngine {
  wchar_t kana[kMaxReadingChars];
  int kanaLen;
  char rom[8];
  int romLen;

  void reset();
  bool hasContent() const { return kanaLen > 0 || romLen > 0; }
  void feedChar(char lowerAscii);
  bool backspace();
  void flushPendingN();
  void appendDirect(wchar_t ch);
  void getPreeditDisplay(wchar_t * out, size_t maxLen) const;

  static void toKatakana(const wchar_t * hiragana, wchar_t * out, size_t maxLen);
};

// Shared-memory + SendMessage client to resident LiliumIME daemon
class ImeClient {
public:
  ImeClient();
  ~ImeClient();

  bool connectOrLaunch(bool allowLaunch = true);
  void close();
  bool isAlive();
  uint32_t dictEntryCount();

  // Converts Hiragana `reading` into candidate list.
  // Falls back gracefully to Hiragana/Katakana if daemon is unavailable.
  int queryCandidates(const wchar_t * reading, wchar_t outCands[kMaxCands][kMaxCandChars], int & outSegLen);
  void learnChoice(const wchar_t * reading, const wchar_t * surface);
  bool showExternalOverlay(HWND targetHwnd);

private:
  HANDLE m_hMap;
  ImeSharedMem * m_shm;
  HWND findDaemonWindow();
};

// Reusable modal text input dialog with integrated Lilium IME (for Filer, TimeTable, Editor)
class TextInputModal {
public:
  static constexpr int kMaxBuf = 128;

  TextInputModal();

  void open(const wchar_t * title, const wchar_t * hint, const wchar_t * initialText, int maxChars);
  void close();
  bool isOpen() const { return m_open; }
  const wchar_t * text() const { return m_buf; }

  void render(LiliumUI::Canvas & c, bool drawSidebarGuide = true) const;
  // Returns true if key consumed; sets outConfirmed when user submits, outClosed when dialog closes.
  bool handleKey(WPARAM vkCode, bool isShift, bool & outConfirmed, bool & outClosed);
  bool handleClick(int x, int y, bool & outConfirmed, bool & outClosed);

private:
  bool m_open;
  wchar_t m_title[64];
  wchar_t m_hint[96];
  wchar_t m_buf[kMaxBuf];
  int m_maxChars;
  int m_cursor;
  int m_inputMode; // 0=あ(IME), 1=abc, 2=ABC, 3=12x
  int m_btnFocus;  // 0=確定, 1=キャンセル

  RomajiEngine m_romaji;
  bool m_converting;
  wchar_t m_cands[kMaxCands][kMaxCandChars];
  int m_candCount;
  int m_candSel;
  int m_candPageStart;
  int m_segLen;
  ImeClient m_ime;

  void insertStrAtCursor(const wchar_t * str);
  void deleteCharBeforeCursor();
  void startConversion();
  void commitSelectedCandidate();
  void commitRawPreedit();
  void commitKatakanaPreedit();
  void cancelPreeditOrConv();
  wchar_t map12xSymbol(WPARAM vkCode) const;
};

} // namespace LiliumIME

#endif // LILIUM_SUITE_COMMON_LILIUM_IME_CLIENT_H
