#include "tiny_llm.h"
#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"
#include "../common/lilium_settings.h"

#include <new>
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

namespace {

static constexpr int kMaxMsgs = 32;
static constexpr int kMaxModels = 16;
static constexpr int kMaxInputChars = 128;
static constexpr int kMaxWrappedPerMsg = 18;
static constexpr int kChatViewTop = 48;
static constexpr int kChatViewBottom = 392;
static constexpr int kCandBarTop = 354;
static constexpr int kCandBarH = 38;
static constexpr int kChatAreaW = LiliumUI::kMainW - 28; // 612

enum DirtyMask : uint32_t {
  kDirtyNone         = 0,
  kDirtyHeader       = 1u << 0,
  kDirtyArea         = 1u << 1,
  kDirtyCandBar      = 1u << 2,
  kDirtyInputBar     = 1u << 3,
  kDirtyButtons      = 1u << 4,
  kDirtySidebar      = 1u << 5,
  kDirtySidebarStats = 1u << 6,
  kDirtyAll          = 0x7Fu
};

struct ChatMsg {
  bool isUser;
  wchar_t text[512];
  int emotionIdx;   // -1 if user, else 0..7
  int tokCount;
  uint32_t genMs;
  bool generating;
};

struct RenderedBubble {
  bool isUser;
  int lineCount;
  wchar_t lines[kMaxWrappedPerMsg][64];
  int boxW;
  int boxH;
  int emotionIdx;
  int tokCount;
  bool generating;
};

struct ModelFileEntry {
  wchar_t name[64];
  wchar_t modelPath[MAX_PATH];
  wchar_t tokPath[MAX_PATH];
  uint32_t sizeBytes;
};

struct ChatState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;

  LiliumLLM::TinyLlmEngine engine;
  ModelFileEntry models[kMaxModels] = {};
  int modelCount = 0;
  int modelIdx = 0;
  wchar_t loadErr[64] = L"";

  ChatMsg msgs[kMaxMsgs] = {};
  RenderedBubble bubbles[kMaxMsgs] = {};
  int msgCount = 0;
  int scrollLinesFromBottom = 0;
  int maxScrollLines = 0;

  bool generating = false;
  int activeAiMsgIdx = -1;
  DWORD genStartTick = 0;
  DWORD firstTokTick = 0;
  int activeEmotion = LiliumLLM::EmoNormal;
  double lastTokPerSec = 0.0;
  int lastGenTokens = 0;

  int tempIdx = 2;    // 0=0.0, 1=0.4, 2=0.7, 3=1.0
  int lenIdx = 1;     // 0=短32, 1=標準64, 2=長96, 3=超160
  int presetIdx = 0;
  int animTick = 0;

  // Direct inline Lilium IME composer (zero popup modal lag)
  LiliumIME::ImeClient ime;
  LiliumIME::RomajiEngine romaji;
  wchar_t inputBuf[kMaxInputChars] = L"";
  int inputCursor = 0;
  int inputMode = 0; // 0=あ(IME), 1=abc, 2=ABC, 3=12x
  bool converting = false;
  wchar_t cands[LiliumIME::kMaxCands][LiliumIME::kMaxCandChars] = {};
  int candCount = 0;
  int candSel = 0;
  int candPageStart = 0;
  int segLen = 0;

  wchar_t lastPromptText[128] = L"";
  uint32_t dirtyFlags = kDirtyAll;

  wchar_t toast[32] = L"";
  DWORD toastExpireTick = 0;
  int lastMin = -1;
  int lastBat = -1;
};

static constexpr float kTemps[4] = { 0.0f, 0.4f, 0.7f, 1.0f };
static constexpr const wchar_t * kTempLabels[4] = { L"0.0", L"0.4", L"0.7", L"1.0" };

static constexpr const wchar_t * kLenLabels[4] = { L"短32", L"標準64", L"長96", L"超160" };
static constexpr int kLenMaxToks[4] = { 32, 64, 96, 160 };
static constexpr int kLenMinToks[4] = { 0, 0, 24, 48 };
static constexpr float kLenRepPens[4] = { 0.0f, 0.0f, 1.22f, 1.22f };

static constexpr const wchar_t * kZundamonPresets[6] = {
  L"おはよう、ずんだもん！",
  L"今日はどんな天気かな？",
  L"好きな食べ物は何？",
  L"電子辞書ってすごいよね！",
  L"勉強がつかれたのだ...",
  L"おやすみ！"
};

static constexpr const wchar_t * kAkebonoPresets[6] = {
  L"こんにちは。",
  L"猫が好き。",
  L"何が好き？",
  L"今日はどんな天気かな？",
  L"勉強がつかれた...",
  L"おやすみ！"
};

static const wchar_t * nextPresetPrompt(ChatState * st) {
  const bool multi = st->engine.isLoaded() && st->engine.isMultiTurn();
  const wchar_t * p = multi ? kAkebonoPresets[st->presetIdx % 6]
                            : kZundamonPresets[st->presetIdx % 6];
  st->presetIdx = (st->presetIdx + 1) % 6;
  return p;
}

static void markDirty(ChatState * st, uint32_t mask, bool fullWindow = false) {
  if (!st) return;
  st->dirtyFlags |= mask;
  st->window.requestRepaint(fullWindow);
}

static void setToast(ChatState * st, const wchar_t * msg) {
  if (!st) return;
  wcsncpy(st->toast, msg ? msg : L"", 31);
  st->toast[31] = L'\0';
  st->toastExpireTick = GetTickCount() + 2600;
}

static const wchar_t * activeStatusText(ChatState * st) {
  if (!st) return L"会話待機";
  if (st->toast[0]) {
    if (static_cast<int32_t>(GetTickCount() - st->toastExpireTick) < 0) return st->toast;
    st->toast[0] = L'\0';
  }
  if (st->generating) return L"生成中...";
  if (st->converting) return L"漢字変換";
  if (st->engine.isLoaded()) return L"会話待機";
  return L"モデル無";
}

static bool containsNoCaseW(const wchar_t * hay, const wchar_t * needle) {
  if (!hay || !needle || !needle[0]) return false;
  size_t hl = wcslen(hay), nl = wcslen(needle);
  if (hl < nl) return false;
  for (size_t i = 0; i + nl <= hl; ++i) {
    if (_wcsnicmp(hay + i, needle, nl) == 0) return true;
  }
  return false;
}

static int modelSortPriority(const wchar_t * name) {
  if (containsNoCaseW(name, L"zundamon")) return 0;
  if (containsNoCaseW(name, L"akebono")) return 1;
  return 2;
}

static bool resolveCompanionTokenizer(const wchar_t * dir, const wchar_t * binName,
                                      wchar_t * outTokPath, size_t maxLen) {
  const wchar_t * dot = wcsrchr(binName, L'.');
  size_t stemLen = dot ? static_cast<size_t>(dot - binName) : wcslen(binName);
  if (stemLen >= 60) stemLen = 59;

  wchar_t stem[64] = L"";
  wcsncpy(stem, binName, stemLen);
  stem[stemLen] = L'\0';

  wchar_t candName[80] = L"";
  _snwprintf(candName, 80, L"%ls.tok", stem);
  candName[79] = L'\0';
  LiliumUI::joinPath(dir, candName, outTokPath, maxLen);
  if (GetFileAttributesW(outTokPath) != INVALID_FILE_ATTRIBUTES) return true;

  _snwprintf(candName, 80, L"%ls_tok.bin", stem);
  candName[79] = L'\0';
  LiliumUI::joinPath(dir, candName, outTokPath, maxLen);
  if (GetFileAttributesW(outTokPath) != INVALID_FILE_ATTRIBUTES) return true;

  LiliumUI::joinPath(dir, L"tok.bin", outTokPath, maxLen);
  if (GetFileAttributesW(outTokPath) != INVALID_FILE_ATTRIBUTES) return true;

  return false;
}

static void scanModels(ChatState * st) {
  if (!st) return;
  st->modelCount = 0;

  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  wchar_t modelDir[MAX_PATH] = L"";
  LiliumUI::joinPath(exeDir, L"models", modelDir, MAX_PATH);

  const wchar_t * searchDirs[2] = { modelDir, exeDir };
  for (int d = 0; d < 2; ++d) {
    wchar_t pat[MAX_PATH] = L"";
    LiliumUI::joinPath(searchDirs[d], L"*.bin", pat, MAX_PATH);
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pat, &fd);
    if (hFind == INVALID_HANDLE_VALUE) continue;
    do {
      if (st->modelCount >= kMaxModels) break;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      if (containsNoCaseW(fd.cFileName, L"tok")) continue;

      wchar_t tokPath[MAX_PATH] = L"";
      if (!resolveCompanionTokenizer(searchDirs[d], fd.cFileName, tokPath, MAX_PATH)) continue;

      ModelFileEntry & e = st->models[st->modelCount++];
      memset(&e, 0, sizeof(e));
      wcsncpy(e.name, fd.cFileName, 63);
      e.name[63] = L'\0';
      LiliumUI::joinPath(searchDirs[d], fd.cFileName, e.modelPath, MAX_PATH);
      wcsncpy(e.tokPath, tokPath, MAX_PATH - 1);
      e.tokPath[MAX_PATH - 1] = L'\0';
      e.sizeBytes = fd.nFileSizeLow;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }

  // Deterministic order: Zundamon 1.7M -> Akebono 10M -> other user models
  for (int i = 0; i + 1 < st->modelCount; ++i) {
    for (int j = i + 1; j < st->modelCount; ++j) {
      int pi = modelSortPriority(st->models[i].name);
      int pj = modelSortPriority(st->models[j].name);
      if (pj < pi || (pj == pi && _wcsicmp(st->models[j].name, st->models[i].name) < 0)) {
        ModelFileEntry tmp = st->models[i];
        st->models[i] = st->models[j];
        st->models[j] = tmp;
      }
    }
  }
}

static bool loadActiveModel(ChatState * st) {
  st->loadErr[0] = L'\0';
  st->presetIdx = 0;
  st->lastPromptText[0] = L'\0';
  st->lastTokPerSec = 0.0;
  st->lastGenTokens = 0;
  if (st->modelCount <= 0) {
    wcsncpy(st->loadErr, L"apps\\models\\ に Q8 .bin と .tok を配置してください", 63);
    st->loadErr[63] = L'\0';
    return false;
  }
  if (st->modelIdx < 0 || st->modelIdx >= st->modelCount) st->modelIdx = 0;
  const ModelFileEntry & m = st->models[st->modelIdx];
  if (!st->engine.load(m.modelPath, m.tokPath, st->loadErr, 64)) {
    return false;
  }
  st->tempIdx = st->engine.isMultiTurn() ? 0 : 2;
  return true;
}

static void pushWelcomeMessage(ChatState * st) {
  st->msgCount = 0;
  st->scrollLinesFromBottom = 0;
  ChatMsg & m = st->msgs[st->msgCount++];
  memset(&m, 0, sizeof(m));
  m.isUser = false;
  m.emotionIdx = LiliumLLM::EmoHappy;
  st->activeEmotion = st->engine.isLoaded() ? LiliumLLM::EmoHappy : LiliumLLM::EmoTroubled;
  if (st->engine.isLoaded()) {
    const LiliumLLM::ModelInfo & mi = st->engine.info();
    double mParams = static_cast<double>(mi.paramCount) / 1000000.0;
    const wchar_t * modeDesc = mi.promptStyle == 1 ? L"連続会話・記憶対応" : L"1往復高速・感情タグ付";
    const wchar_t * switchHint = (st->modelCount > 1) ? L" / [F9]モデル切替" : L"";
    _snwprintf(m.text, 512,
               L"[%ls] (約%.1fM / %ls) 準備完了！\n"
               L"そのまま入力し[Enter]送信、[F3]例文 / [F8]長短%ls。",
               mi.displayName, mParams, modeDesc, switchHint);
  } else {
    _snwprintf(m.text, 512,
               L"モデル未検出: %ls\n"
               L"apps\\models\\ にモデルとトークナイザを配置してください。",
               st->loadErr[0] ? st->loadErr : L"(missing)");
  }
  m.text[511] = L'\0';
}

static ChatMsg * appendMsg(ChatState * st, bool isUser, const wchar_t * text) {
  if (!st) return nullptr;
  if (st->msgCount >= kMaxMsgs) {
    memmove(&st->msgs[0], &st->msgs[1], sizeof(ChatMsg) * (kMaxMsgs - 1));
    --st->msgCount;
    if (st->activeAiMsgIdx > 0) --st->activeAiMsgIdx;
  }
  ChatMsg & m = st->msgs[st->msgCount++];
  memset(&m, 0, sizeof(m));
  m.isUser = isUser;
  m.emotionIdx = isUser ? -1 : LiliumLLM::EmoNormal;
  if (text) {
    wcsncpy(m.text, text, 511);
    m.text[511] = L'\0';
  }
  return &m;
}

static void startChatPrompt(ChatState * st, const wchar_t * promptW) {
  if (!st || !promptW || !promptW[0]) return;
  if (!st->engine.isLoaded()) {
    setToast(st, L"モデル無");
    return;
  }
  if (st->generating) {
    st->engine.abortGeneration();
    st->generating = false;
    if (st->activeAiMsgIdx >= 0 && st->activeAiMsgIdx < st->msgCount) {
      st->msgs[st->activeAiMsgIdx].generating = false;
    }
  }

  wcsncpy(st->lastPromptText, promptW, 127);
  st->lastPromptText[127] = L'\0';

  appendMsg(st, true, promptW);
  ChatMsg * ai = appendMsg(st, false, L"");
  if (!ai) return;
  ai->generating = true;
  st->activeAiMsgIdx = st->msgCount - 1;
  st->scrollLinesFromBottom = 0;

  if (!st->engine.beginPrompt(promptW, kTemps[st->tempIdx],
                              kLenMaxToks[st->lenIdx],
                              kLenMinToks[st->lenIdx],
                              kLenRepPens[st->lenIdx])) {
    ai->generating = false;
    wcsncpy(ai->text, L"(変換エラー)", 511);
    ai->text[511] = L'\0';
    return;
  }

  if (st->engine.didContextAutoReset()) {
    setToast(st, L"文脈リセット");
  }

  st->generating = true;
  st->genStartTick = GetTickCount();
  st->firstTokTick = 0;
  st->lastGenTokens = 0;
  st->window.setTimerInterval(15);
  LiliumSettings::suiteLog(L"Chat", L"Begin prompt model=%ls len=%d kv=%d temp=%.1f maxTok=%d",
                           st->engine.info().displayName,
                           st->engine.promptTokenCount(),
                           st->engine.contextUsed(),
                           static_cast<double>(kTemps[st->tempIdx]),
                           kLenMaxToks[st->lenIdx]);
}

static void stopGeneration(ChatState * st) {
  if (!st || !st->generating) return;
  st->engine.abortGeneration();
  st->generating = false;
  st->window.setTimerInterval(500);
  if (st->activeAiMsgIdx >= 0 && st->activeAiMsgIdx < st->msgCount) {
    ChatMsg & ai = st->msgs[st->activeAiMsgIdx];
    ai.generating = false;
    if (!ai.text[0]) {
      wcsncpy(ai.text, L"(中断しました)", 511);
      ai.text[511] = L'\0';
    }
  }
}

// =============================================================================
// Direct Inline IME helpers
// =============================================================================

static void insertInputStr(ChatState * st, const wchar_t * str) {
  if (!st || !str || !str[0]) return;
  int len = static_cast<int>(wcslen(st->inputBuf));
  int add = static_cast<int>(wcslen(str));
  int room = (kMaxInputChars - 1) - len;
  if (room <= 0) return;
  if (add > room) add = room;
  memmove(st->inputBuf + st->inputCursor + add, st->inputBuf + st->inputCursor,
          static_cast<size_t>(len - st->inputCursor + 1) * sizeof(wchar_t));
  memcpy(st->inputBuf + st->inputCursor, str, static_cast<size_t>(add) * sizeof(wchar_t));
  st->inputCursor += add;
}

static void deleteInputBeforeCursor(ChatState * st) {
  if (!st || st->inputCursor <= 0) return;
  int len = static_cast<int>(wcslen(st->inputBuf));
  memmove(st->inputBuf + st->inputCursor - 1, st->inputBuf + st->inputCursor,
          static_cast<size_t>(len - st->inputCursor + 1) * sizeof(wchar_t));
  --st->inputCursor;
}

static void commitChatRawPreedit(ChatState * st) {
  if (!st) return;
  st->romaji.flushPendingN();
  wchar_t disp[LiliumIME::kMaxReadingChars] = L"";
  st->romaji.getPreeditDisplay(disp, LiliumIME::kMaxReadingChars);
  if (disp[0]) insertInputStr(st, disp);
  st->romaji.reset();
  st->converting = false;
  st->candCount = 0;
}

static void commitChatKatakana(ChatState * st) {
  if (!st) return;
  st->romaji.flushPendingN();
  if (st->romaji.kanaLen > 0) {
    wchar_t kata[LiliumIME::kMaxReadingChars] = L"";
    LiliumIME::RomajiEngine::toKatakana(st->romaji.kana, kata, LiliumIME::kMaxReadingChars);
    insertInputStr(st, kata);
  }
  st->romaji.reset();
  st->converting = false;
  st->candCount = 0;
}

static void startChatConversion(ChatState * st) {
  if (!st) return;
  st->romaji.flushPendingN();
  if (st->romaji.kanaLen <= 0) return;
  int seg = 0;
  int n = st->ime.queryCandidates(st->romaji.kana, st->cands, seg);
  if (n > 0) {
    st->converting = true;
    st->candCount = n;
    st->candSel = 0;
    st->candPageStart = 0;
    st->segLen = (seg > 0 && seg <= st->romaji.kanaLen) ? seg : st->romaji.kanaLen;
  }
}

static void commitChatCandidate(ChatState * st) {
  if (!st || !st->converting || st->candCount <= 0) return;
  const wchar_t * chosen = st->cands[st->candSel];
  int segLen = (st->segLen > 0 && st->segLen <= st->romaji.kanaLen) ? st->segLen : st->romaji.kanaLen;

  wchar_t segReading[LiliumIME::kMaxReadingChars] = L"";
  wcsncpy(segReading, st->romaji.kana, static_cast<size_t>(segLen));
  segReading[segLen] = L'\0';
  st->ime.learnChoice(segReading, chosen);
  insertInputStr(st, chosen);

  int rem = st->romaji.kanaLen - segLen;
  if (rem > 0) {
    memmove(st->romaji.kana, st->romaji.kana + segLen, static_cast<size_t>(rem + 1) * sizeof(wchar_t));
    st->romaji.kanaLen = rem;
    st->converting = false;
    st->candCount = 0;
    startChatConversion(st);
  } else {
    st->romaji.reset();
    st->converting = false;
    st->candCount = 0;
    st->candSel = 0;
    st->candPageStart = 0;
    st->segLen = 0;
  }
}

static wchar_t mapChat12xSymbol(WPARAM vkCode) {
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
    case VK_PRIOR: return L'、';
    case VK_NEXT: return L'。';
    default: return L'\0';
  }
}

// =============================================================================
// Rendering (100% Scale=2 Normal Font + Pixel-Art Edamame Emotion Icon)
// =============================================================================

static int wrapBubbleText(LiliumUI::Canvas & c, const wchar_t * src, int maxPx,
                          wchar_t outLines[kMaxWrappedPerMsg][64]) {
  int lineCount = 0;
  if (!src || !src[0]) {
    wcscpy(outLines[0], L" ");
    return 1;
  }
  int curLen = 0;
  int curW = 0;
  outLines[0][0] = L'\0';

  for (size_t i = 0; src[i] != L'\0'; ++i) {
    wchar_t ch = src[i];
    if (ch == L'\r') continue;
    if (ch == L'\n') {
      outLines[lineCount][curLen] = L'\0';
      if (++lineCount >= kMaxWrappedPerMsg) return kMaxWrappedPerMsg;
      curLen = 0;
      curW = 0;
      outLines[lineCount][0] = L'\0';
      continue;
    }
    wchar_t tmp[2] = { ch, L'\0' };
    int cw = c.measureWidth(tmp, 2);
    if (curLen > 0 && (curW + cw > maxPx || curLen >= 60)) {
      outLines[lineCount][curLen] = L'\0';
      if (++lineCount >= kMaxWrappedPerMsg) return kMaxWrappedPerMsg;
      curLen = 0;
      curW = 0;
    }
    outLines[lineCount][curLen++] = ch;
    outLines[lineCount][curLen] = L'\0';
    curW += cw;
  }
  if (lineCount < kMaxWrappedPerMsg) ++lineCount;
  return lineCount;
}

static void renderEmotionMark(LiliumUI::Canvas & c, int x, int y, int emoIdx, bool thinking, int tick) {
  if (thinking) {
    int activeDot = tick % 3;
    for (int d = 0; d < 3; ++d) {
      c.fillRect(x + 25 + d * 8, y + 12, 5, 5,
                 (d == activeDot) ? LiliumUI::kColTealDark : LiliumUI::kColBorderStrong);
    }
    return;
  }

  switch (emoIdx) {
    case LiliumLLM::EmoHappy:
      c.fillRect(x + 34, y + 8,  4, 4, 0xE85A70);
      c.fillRect(x + 41, y + 8,  4, 4, 0xE85A70);
      c.fillRect(x + 33, y + 10, 13, 4, 0xE85A70);
      c.fillRect(x + 35, y + 14, 9,  3, 0xE85A70);
      c.fillRect(x + 38, y + 17, 3,  2, 0xE85A70);
      c.fillRect(x + 18, y + 8,  2,  6, 0xE6A218);
      c.fillRect(x + 16, y + 10, 6,  2, 0xE6A218);
      break;
    case LiliumLLM::EmoSmug:
      c.fillRect(x + 38, y + 6,  3, 14, 0xE69A12);
      c.fillRect(x + 32, y + 12, 15, 3, 0xE69A12);
      c.fillRect(x + 36, y + 10, 7, 7, 0xFFD24A);
      c.fillRect(x + 22, y + 8,  2, 6, 0xE69A12);
      c.fillRect(x + 20, y + 10, 6, 2, 0xE69A12);
      break;
    case LiliumLLM::EmoSurprised:
      c.fillRect(x + 38, y + 6,  5, 11, 0xE04434);
      c.fillRect(x + 38, y + 19, 5, 4,  0xE04434);
      c.fillRect(x + 29, y + 9,  4, 2,  0xE68A1E);
      c.fillRect(x + 47, y + 9,  4, 2,  0xE68A1E);
      break;
    case LiliumLLM::EmoSad:
      c.fillRect(x + 26, y + 7,  2, 7, 0x6AA8E8);
      c.fillRect(x + 33, y + 9,  2, 7, 0x6AA8E8);
      c.fillRect(x + 41, y + 7,  2, 7, 0x6AA8E8);
      c.fillRect(x + 38, y + 16, 4, 3, 0x3578D4);
      c.fillRect(x + 36, y + 19, 8, 5, 0x3578D4);
      c.fillRect(x + 38, y + 20, 2, 2, 0xD8EEFF);
      break;
    case LiliumLLM::EmoAngry:
      c.fillRect(x + 33, y + 10, 5, 2, 0xDC382E);
      c.fillRect(x + 36, y + 6,  2, 5, 0xDC382E);
      c.fillRect(x + 41, y + 10, 5, 2, 0xDC382E);
      c.fillRect(x + 41, y + 6,  2, 5, 0xDC382E);
      c.fillRect(x + 33, y + 16, 5, 2, 0xDC382E);
      c.fillRect(x + 36, y + 17, 2, 5, 0xDC382E);
      c.fillRect(x + 41, y + 16, 5, 2, 0xDC382E);
      c.fillRect(x + 41, y + 17, 2, 5, 0xDC382E);
      break;
    case LiliumLLM::EmoTroubled:
      c.fillRect(x + 37, y + 7,  6, 2, 0x3578D4);
      c.fillRect(x + 41, y + 9,  3, 4, 0x3578D4);
      c.fillRect(x + 38, y + 12, 4, 3, 0x3578D4);
      c.fillRect(x + 38, y + 17, 3, 3, 0x3578D4);
      c.fillRect(x + 25, y + 9,  4, 6, 0x5BA2F0);
      c.fillRect(x + 24, y + 13, 6, 4, 0x5BA2F0);
      break;
    case LiliumLLM::EmoSleepy:
      c.fillRect(x + 35, y + 6,  10, 2, 0x4A5CB0);
      c.fillRect(x + 41, y + 8,  3,  2, 0x4A5CB0);
      c.fillRect(x + 38, y + 10, 4,  2, 0x4A5CB0);
      c.fillRect(x + 35, y + 12, 10, 2, 0x4A5CB0);
      c.fillRect(x + 24, y + 11, 7,  2, 0x6E7ECF);
      c.fillRect(x + 27, y + 13, 3,  2, 0x6E7ECF);
      c.fillRect(x + 24, y + 15, 7,  2, 0x6E7ECF);
      break;
    default:
      c.fillRect(x + 38, y + 8,  2, 10, LiliumUI::kColTealDark);
      c.fillRect(x + 34, y + 12, 10, 2, LiliumUI::kColTealDark);
      c.fillRect(x + 37, y + 11, 4,  4, LiliumUI::kColTeal);
      break;
  }
}

static void renderEdamameEmotion(LiliumUI::Canvas & c, int x, int y, int emoIdx, bool thinking, int tick) {
  c.drawBox(x, y, 56, 56, 0xF1FAF3, LiliumUI::kColTealDark);
  c.drawThickFrame(x, y, 56, 56, 2, LiliumUI::kColTealDark);

  // Pixel-art Edamame pod (branch + tilted 3-bean green pod + glossy highlights)
  static constexpr uint32_t kOutline = 0x195E32;
  static constexpr uint32_t kPodBody = 0x4BC260;
  static constexpr uint32_t kBeanCol = 0x7AE38A;
  static constexpr uint32_t kShine   = 0xD8FCE0;

  c.fillRect(x + 7,  y + 20, 6, 3, kOutline);
  c.fillRect(x + 9,  y + 22, 5, 3, kPodBody);

  c.fillRect(x + 11, y + 21, 14, 15, kOutline);
  c.fillRect(x + 21, y + 26, 15, 16, kOutline);
  c.fillRect(x + 31, y + 31, 15, 16, kOutline);

  c.fillRect(x + 13, y + 23, 11, 11, kPodBody);
  c.fillRect(x + 23, y + 28, 12, 12, kPodBody);
  c.fillRect(x + 33, y + 33, 11, 12, kPodBody);

  c.fillRect(x + 14, y + 25, 8,  8,  kBeanCol);
  c.fillRect(x + 25, y + 30, 8,  8,  kBeanCol);
  c.fillRect(x + 35, y + 35, 8,  8,  kBeanCol);
  c.fillRect(x + 15, y + 26, 3,  2,  kShine);
  c.fillRect(x + 26, y + 31, 3,  2,  kShine);
  c.fillRect(x + 36, y + 36, 3,  2,  kShine);

  c.fillRect(x + 23, y + 27, 2,  3,  kOutline);
  c.fillRect(x + 33, y + 32, 2,  3,  kOutline);

  renderEmotionMark(c, x, y, emoIdx, thinking, tick);
}

static void renderBrainDictEmotion(LiliumUI::Canvas & c, int x, int y, int emoIdx, bool thinking, int tick) {
  c.drawBox(x, y, 56, 56, 0xEEF6FA, LiliumUI::kColTealDark);
  c.drawThickFrame(x, y, 56, 56, 2, LiliumUI::kColTealDark);

  // Pixel-art SHARP Brain clamshell electronic dictionary
  static constexpr uint32_t kShell  = 0x253545;
  static constexpr uint32_t kDeck   = 0xDCE5EE;
  static constexpr uint32_t kHinge  = 0x5A6A7A;
  static constexpr uint32_t kLcdBg  = 0xD8F6F2;
  static constexpr uint32_t kKeyCol = 0x4F6070;

  // Upper display shell & bezel
  c.fillRect(x + 9,  y + 22, 38, 14, kShell);
  c.fillRect(x + 11, y + 24, 34, 10, kLcdBg);
  c.fillRect(x + 11, y + 24, 34, 2,  LiliumUI::kColTeal);
  c.fillRect(x + 14, y + 28, 22, 2,  LiliumUI::kColTealDark);
  c.fillRect(x + 14, y + 31, 16, 2,  LiliumUI::kColMuted);

  // Hinge bar
  c.fillRect(x + 10, y + 36, 36, 2, kHinge);

  // Lower silver keyboard deck
  c.fillRect(x + 5,  y + 38, 46, 14, kShell);
  c.fillRect(x + 7,  y + 39, 42, 11, kDeck);
  c.fillRect(x + 9,  y + 39, 38, 2,  LiliumUI::kColTeal);

  // Mini keycap rows (QWERTY + bottom Spacebar)
  for (int col = 0; col < 6; ++col) {
    c.fillRect(x + 9 + col * 6, y + 42, 4, 2, kKeyCol);
    c.fillRect(x + 9 + col * 6, y + 45, 4, 2, kKeyCol);
  }
  c.fillRect(x + 9,  y + 48, 8,  2, kKeyCol);
  c.fillRect(x + 19, y + 48, 18, 2, LiliumUI::kColWhite);
  c.fillRect(x + 39, y + 48, 8,  2, kKeyCol);

  renderEmotionMark(c, x, y, emoIdx, thinking, tick);
}

static void renderChatArea(LiliumUI::Canvas & c, ChatState * st) {
  const bool candVis = (st->converting && st->candCount > 0);
  const int ax = 14, ay = kChatViewTop, aw = kChatAreaW;
  const int ah = (candVis ? kCandBarTop - 4 : kChatViewBottom) - kChatViewTop;
  c.drawBox(ax, ay, aw, ah, LiliumUI::kColWhite, LiliumUI::kColBorder);

  const int bubbleMaxTextW = 484;
  int totalH = 10;
  wchar_t waitBuf[32] = L"";
  for (int i = 0; i < st->msgCount; ++i) {
    const ChatMsg & m = st->msgs[i];
    RenderedBubble & b = st->bubbles[i];
    b.isUser = m.isUser;
    b.emotionIdx = m.emotionIdx;
    b.tokCount = m.tokCount;
    b.generating = m.generating;

    const wchar_t * txt = m.text;
    if (!txt[0]) {
      if (i == st->activeAiMsgIdx && st->generating &&
          st->engine.prefillTotal() > 0 &&
          st->engine.prefillProgress() + 1 < st->engine.prefillTotal()) {
        _snwprintf(waitBuf, 32, L"読込中 %d/%d...",
                   st->engine.prefillProgress() + 1, st->engine.prefillTotal());
        waitBuf[31] = L'\0';
        txt = waitBuf;
      } else {
        txt = m.generating ? L"考え中..." : L" ";
      }
    }
    b.lineCount = wrapBubbleText(c, txt, bubbleMaxTextW, b.lines);

    int maxLineW = 0;
    for (int l = 0; l < b.lineCount; ++l) {
      int lw = c.measureWidth(b.lines[l], 2);
      if (lw > maxLineW) maxLineW = lw;
    }
    int minW = b.isUser ? 64 : 210;
    b.boxW = maxLineW + 24;
    if (b.boxW < minW) b.boxW = minW;
    if (b.boxW > aw - 24) b.boxW = aw - 24;
    int footerH = (!b.isUser) ? 22 : 0;
    b.boxH = 12 + b.lineCount * 22 + footerH;
    totalH += b.boxH + 10;
  }

  const int linePx = 22;
  int maxScroll = (totalH > ah) ? ((totalH - ah + linePx - 1) / linePx) : 0;
  st->maxScrollLines = maxScroll;
  if (st->scrollLinesFromBottom > maxScroll) st->scrollLinesFromBottom = maxScroll;
  if (st->scrollLinesFromBottom < 0) st->scrollLinesFromBottom = 0;

  int scrollOffsetPx = (totalH > ah) ? (totalH - ah - st->scrollLinesFromBottom * linePx) : 0;
  if (scrollOffsetPx < 0) scrollOffsetPx = 0;

  int curY = ay + 8 - scrollOffsetPx;
  for (int i = 0; i < st->msgCount; ++i) {
    const RenderedBubble & b = st->bubbles[i];
    int by = curY;
    int bh = b.boxH;
    curY += bh + 10;

    if (by + bh <= ay + 2 || by >= ay + ah - 2) continue;

    int bx = b.isUser ? (ax + aw - 12 - b.boxW) : (ax + 12);
    uint32_t bg = b.isUser ? LiliumUI::kColTealSoft : 0xF8FAFC;
    uint32_t bd = b.isUser ? LiliumUI::kColTeal : LiliumUI::kColBorderStrong;

    int drawY = (by < ay + 2) ? (ay + 2) : by;
    int drawEnd = (by + bh > ay + ah - 2) ? (ay + ah - 2) : (by + bh);
    if (drawEnd <= drawY + 4) continue;
    c.drawBox(bx, drawY, b.boxW, drawEnd - drawY, bg, bd);

    for (int l = 0; l < b.lineCount; ++l) {
      int ly = by + 6 + l * 22;
      if (ly < ay + 4 || ly + 20 > ay + ah - 4) continue;
      c.drawText(bx + 12, ly, b.boxW - 24, 20,
                 b.isUser ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 b.lines[l], 2, LiliumUI::Align::Left, true);
    }

    if (!b.isUser) {
      int fy = by + 8 + b.lineCount * 22;
      if (fy >= ay + 4 && fy + 20 <= ay + ah - 4) {
        wchar_t tag[36];
        if (b.generating) {
          _snwprintf(tag, 36, L"[生成中 %d]", b.tokCount);
        } else if (b.tokCount > 0) {
          _snwprintf(tag, 36, L"[%ls] %dtok", LiliumLLM::emotionLabel(b.emotionIdx), b.tokCount);
        } else {
          _snwprintf(tag, 36, L"[%ls]", LiliumLLM::emotionLabel(b.emotionIdx));
        }
        tag[35] = L'\0';
        c.drawText(bx + 12, fy, b.boxW - 24, 18, LiliumUI::kColTealDark,
                   tag, 2, LiliumUI::Align::Right, true);
      }
    }
  }
}

static void renderCandidateBar(LiliumUI::Canvas & c, ChatState * st) {
  if (!st->converting || st->candCount <= 0) return;
  const int cx0 = 14, cy0 = kCandBarTop, cw = kChatAreaW, ch = kCandBarH;
  c.drawBox(cx0, cy0, cw, ch, LiliumUI::kColRowAlt, LiliumUI::kColTealDark);

  wchar_t pgBuf[20];
  _snwprintf(pgBuf, 20, L"%d/%d", st->candSel + 1, st->candCount);
  pgBuf[19] = L'\0';
  c.drawText(cx0 + cw - 78, cy0, 72, ch, LiliumUI::kColMuted, pgBuf, 2, LiliumUI::Align::Right, true);

  int px = cx0 + 8;
  const int maxX = cx0 + cw - 82;
  for (int i = 0; i < 8 && (st->candPageStart + i) < st->candCount; ++i) {
    int idx = st->candPageStart + i;
    bool sel = (idx == st->candSel);
    wchar_t pillTxt[48];
    _snwprintf(pillTxt, 48, L"%d.%ls", i + 1, st->cands[idx]);
    pillTxt[47] = L'\0';
    int pw = c.measureWidth(pillTxt, 2) + 14;
    if (pw < 56) pw = 56;
    if (px + pw > maxX) break;
    c.drawBox(px, cy0 + 4, pw, ch - 8,
              sel ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              sel ? LiliumUI::kColTealDark : LiliumUI::kColBorderStrong);
    c.drawText(px, cy0 + 4, pw, ch - 8,
               sel ? LiliumUI::kColWhite : LiliumUI::kColText,
               pillTxt, 2, LiliumUI::Align::Center, true);
    px += pw + 6;
  }
}

static void renderInputBar(LiliumUI::Canvas & c, ChatState * st) {
  const int ix = 14, iy = 396, iw = kChatAreaW, ih = 36;
  c.drawBox(ix, iy, iw, ih, LiliumUI::kColWhite, LiliumUI::kColTealDark);

  static constexpr const wchar_t * kModeLabels[4] = {
    L"あ(IME)", L"abc", L"ABC", L"12x"
  };
  uint32_t mBg = (st->inputMode == 0) ? 0xFFF3D6 : LiliumUI::kColTealSoft;
  uint32_t mBd = (st->inputMode == 0) ? 0xCC7A00 : LiliumUI::kColTealDark;
  uint32_t mTx = (st->inputMode == 0) ? 0x8A4E00 : LiliumUI::kColTealDeep;
  c.drawBox(ix + 4, iy + 4, 88, ih - 8, mBg, mBd);
  c.drawText(ix + 4, iy + 4, 88, ih - 8, mTx, kModeLabels[st->inputMode], 2, LiliumUI::Align::Center, true);

  wchar_t pre[LiliumIME::kMaxReadingChars] = L"";
  if (st->converting && st->candCount > 0) {
    wcsncpy(pre, st->cands[st->candSel], LiliumIME::kMaxReadingChars - 1);
  } else {
    st->romaji.getPreeditDisplay(pre, LiliumIME::kMaxReadingChars);
  }
  pre[LiliumIME::kMaxReadingChars - 1] = L'\0';

  const int textAreaX = ix + 100;
  const int textAreaMaxW = (ix + iw - 8) - textAreaX; // ~504 px

  if (!st->inputBuf[0] && !pre[0]) {
    c.drawText(textAreaX, iy, textAreaMaxW, ih, LiliumUI::kColLightMuted,
               L"そのまま入力 → [Space]変換 / [Ent]送信",
               2, LiliumUI::Align::Left, true);
    c.fillRect(textAreaX, iy + 8, 2, ih - 16, LiliumUI::kColTealDark);
    return;
  }

  wchar_t leftPart[kMaxInputChars + 4] = L"";
  int copyL = (st->inputCursor < kMaxInputChars) ? st->inputCursor : kMaxInputChars - 1;
  wcsncpy(leftPart, st->inputBuf, static_cast<size_t>(copyL));
  leftPart[copyL] = L'\0';
  const wchar_t * rightPart = st->inputBuf + st->inputCursor;

  int preW = pre[0] ? c.measureWidth(pre, 2) + 4 : 0;
  int maxLeftW = textAreaMaxW - preW - 8;
  const wchar_t * visLeft = leftPart;
  while (*visLeft && c.measureWidth(visLeft, 2) > maxLeftW) {
    ++visLeft;
  }

  int tx = textAreaX;
  if (visLeft[0]) {
    int lw = c.measureWidth(visLeft, 2);
    c.drawText(tx, iy, lw + 4, ih, LiliumUI::kColText, visLeft, 2, LiliumUI::Align::Left, true);
    tx += lw;
  }
  if (pre[0]) {
    int pw = c.measureWidth(pre, 2);
    c.fillRect(tx, iy + 5, pw + 2, ih - 10, st->converting ? LiliumUI::kColTealSoft : 0xFFF3D6);
    c.drawText(tx, iy, pw + 8, ih, st->converting ? LiliumUI::kColTealDeep : 0x8A4E00,
               pre, 2, LiliumUI::Align::Left, true);
    c.fillRect(tx, iy + ih - 6, pw, 2, st->converting ? LiliumUI::kColTealDark : 0xCC7A00);
    tx += pw + 3;
  } else {
    c.fillRect(tx, iy + 8, 2, ih - 16, LiliumUI::kColTealDark);
    tx += 4;
  }
  int remW = (ix + iw - 8) - tx;
  if (rightPart[0] && remW > 16) {
    c.drawText(tx, iy, remW, ih, LiliumUI::kColText, rightPart, 2, LiliumUI::Align::Left, true);
  }
}

static void renderBottomButtons(LiliumUI::Canvas & c, ChatState * st) {
  const wchar_t * btns[6] = {
    st->generating ? L"F1:停止" : L"F1:再送",
    L"F2:あ/A",
    L"F3:例文",
    L"F4:温度",
    L"F8:長短",
    L"F9:切替"
  };
  for (int b = 0; b < 6; ++b) {
    int bx = 14 + b * 102;
    int bw = (b == 5) ? (kChatAreaW - 5 * 102) : 99;
    int by = 438;
    int bh = 34;
    uint32_t bg = LiliumUI::kColWhite;
    uint32_t bd = LiliumUI::kColBorder;
    uint32_t fg = LiliumUI::kColText;
    if (b == 0 && st->generating) {
      bg = LiliumUI::kColDangerBg;
      bd = LiliumUI::kColDangerBorder;
      fg = LiliumUI::kColDangerText;
    } else if (b == 2) {
      bg = LiliumUI::kColTealSoft;
      bd = LiliumUI::kColTealDark;
      fg = LiliumUI::kColTealDeep;
    } else if (b == 4 && st->lenIdx != 1) {
      bg = 0xFFF6E5;
      bd = 0xE0A83C;
      fg = 0x8A5A00;
    } else if (b == 5) {
      if (st->modelCount > 1) {
        bg = LiliumUI::kColTealHeader;
        bd = LiliumUI::kColTealBorder;
        fg = LiliumUI::kColTealDeep;
      } else {
        fg = LiliumUI::kColLightMuted;
      }
    }
    c.drawBox(bx, by, bw, bh, bg, bd);
    c.drawText(bx + 2, by, bw - 4, bh, fg, btns[b], 2, LiliumUI::Align::Center, true);
  }
}

static void renderSidebarStatsBlock(LiliumUI::Canvas & c, ChatState * st) {
  const int sx = LiliumUI::kSidebarX + 6;
  const int sw = LiliumUI::kSidebarW - 12;

  c.fillRect(sx + 2, 98, sw - 4, 220, LiliumUI::kColWhite);
  const int ix = sx + (sw - 56) / 2;
  if (!st->engine.isLoaded() || st->engine.hasEmotionTags()) {
    renderEdamameEmotion(c, ix, 102, st->activeEmotion, st->generating, st->animTick);
  } else {
    renderBrainDictEmotion(c, ix, 102, st->activeEmotion, st->generating, st->animTick);
  }

  wchar_t emoBuf[24];
  _snwprintf(emoBuf, 24, L"【%ls】", LiliumLLM::emotionLabel(st->activeEmotion));
  emoBuf[23] = L'\0';
  c.drawText(sx + 4, 162, sw - 8, 22, LiliumUI::kColTealDark, emoBuf, 2, LiliumUI::Align::Center, true);

  c.fillRect(sx + 8, 190, sw - 16, 1, LiliumUI::kColBorder);

  int totMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totMB, availMB, usedPct);

  wchar_t l1[28], l2[28], l3[28], l4[28], l5[28];
  _snwprintf(l1, 28, L"%.1ftok/s", st->lastTokPerSec);
  if (st->engine.isLoaded()) {
    _snwprintf(l2, 28, L"文脈 %d/%d", st->engine.contextUsed(), st->engine.info().seqLen);
  } else {
    _snwprintf(l2, 28, L"生成 %dtok", st->lastGenTokens);
  }
  _snwprintf(l3, 28, L"温度%ls %ls", kTempLabels[st->tempIdx], kLenLabels[st->lenIdx]);
  _snwprintf(l4, 28, L"RAM %dMB", availMB);
  if (st->engine.isLoaded()) {
    _snwprintf(l5, 28, L"Q8 %.1fMB", static_cast<double>(st->engine.info().fileBytes) / (1024.0 * 1024.0));
  } else {
    wcsncpy(l5, L"モデル無", 27);
  }
  l1[27] = l2[27] = l3[27] = l4[27] = l5[27] = L'\0';

  c.drawText(sx + 6, 196, sw - 12, 22, LiliumUI::kColTealDeep, l1, 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 220, sw - 12, 22, LiliumUI::kColText,     l2, 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 244, sw - 12, 22, LiliumUI::kColText,     l3, 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 268, sw - 12, 22, LiliumUI::kColMuted,    l4, 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 292, sw - 12, 22, LiliumUI::kColMuted,    l5, 2, LiliumUI::Align::Left, true);
}

static void renderChatSidebarFull(LiliumUI::Canvas & c, ChatState * st) {
  c.fillRect(LiliumUI::kSidebarX, 0, LiliumUI::kSidebarW, LiliumUI::kScreenH, LiliumUI::kColSidebar);
  c.fillRect(LiliumUI::kSidebarX, 0, 1, LiliumUI::kScreenH, LiliumUI::kColBorder);

  LiliumUI::renderSidebarStatus(c, activeStatusText(st));

  const int sx = LiliumUI::kSidebarX + 6;
  const int sw = LiliumUI::kSidebarW - 12;
  c.drawBox(sx, 70, sw, 356, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
  c.drawBox(sx, 70, sw, 26, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);

  const wchar_t * hdrTitle = st->engine.isLoaded() ? st->engine.info().displayName : L"ローカルLLM";
  c.drawText(sx + 2, 70, sw - 4, 26, LiliumUI::kColTealDeep, hdrTitle, 2, LiliumUI::Align::Center, true);

  renderSidebarStatsBlock(c, st);

  c.fillRect(sx + 8, 320, sw - 16, 1, LiliumUI::kColBorder);
  const wchar_t * helpTxt = (st->engine.isLoaded() && st->engine.isMultiTurn())
                                ? L"10M 記憶会話\n[F8]長短切替\n[F9]モデル替"
                                : L"1.7M 高速AI\n[F8]長短切替\n[F9]10M切替";
  c.drawWrappedText(sx + 6, 326, sw - 12, 386, LiliumUI::kColMuted, helpTxt, 2, 4);

  c.drawBox(sx + 6, 390, sw - 12, 28, LiliumUI::kColRowAlt, LiliumUI::kColBorderStrong);
  c.drawText(sx + 8, 390, sw - 16, 28, LiliumUI::kColTextSub, L"F12:履歴消去", 2, LiliumUI::Align::Center, true);

  c.drawBox(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
            LiliumUI::kColBtnBg, LiliumUI::kColBtnBorder);
  c.drawText(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
             LiliumUI::kColText, L"戻 る", 2, LiliumUI::Align::Center, true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (c.fullRedraw) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
    LiliumUI::renderTopHeader(c, L"Lilium Chat", activeStatusText(st));
    renderChatArea(c, st);
    renderCandidateBar(c, st);
    renderInputBar(c, st);
    renderBottomButtons(c, st);
    renderChatSidebarFull(c, st);
    st->dirtyFlags = kDirtyNone;
    return;
  }

  uint32_t d = st->dirtyFlags;
  st->dirtyFlags = kDirtyNone;
  if (d == kDirtyNone) return;

  if (d & kDirtyHeader) {
    LiliumUI::renderTopHeader(c, L"Lilium Chat", activeStatusText(st));
    LiliumUI::renderSidebarStatus(c, activeStatusText(st));
  }
  if (d & kDirtyArea) renderChatArea(c, st);
  if (d & kDirtyCandBar) renderCandidateBar(c, st);
  if (d & kDirtyInputBar) renderInputBar(c, st);
  if (d & kDirtyButtons) renderBottomButtons(c, st);
  if (d & kDirtySidebar) renderChatSidebarFull(c, st);
  else if (d & kDirtySidebarStats) renderSidebarStatsBlock(c, st);
}

// =============================================================================
// Input Dispatch & Streaming Loop
// =============================================================================

static void onKeyDown(WPARAM vkCode, bool isShift, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (vkCode == VK_HOME || vkCode == VK_F10) {
    stopGeneration(st);
    st->window.requestQuit();
    return;
  }

  if (vkCode == VK_F1) {
    if (st->generating) {
      stopGeneration(st);
    } else if (st->lastPromptText[0]) {
      startChatPrompt(st, st->lastPromptText);
    } else {
      startChatPrompt(st, nextPresetPrompt(st));
    }
    markDirty(st, kDirtyHeader | kDirtyArea | kDirtyButtons | kDirtySidebarStats);
    return;
  }

  if (vkCode == VK_F2) {
    uint32_t dirty = kDirtyInputBar;
    if (st->converting) {
      commitChatCandidate(st);
      dirty |= kDirtyArea | kDirtyHeader;
    } else if (st->romaji.hasContent()) {
      commitChatRawPreedit(st);
    }
    st->inputMode = (st->inputMode + 1) % 4;
    markDirty(st, dirty);
    return;
  }

  if (vkCode == VK_F3) {
    startChatPrompt(st, nextPresetPrompt(st));
    markDirty(st, kDirtyHeader | kDirtyArea | kDirtyButtons | kDirtySidebarStats);
    return;
  }

  if (vkCode == VK_F4) {
    st->tempIdx = (st->tempIdx + 1) % 4;
    markDirty(st, kDirtySidebarStats);
    return;
  }

  if (vkCode == VK_F8) {
    st->lenIdx = (st->lenIdx + 1) % 4;
    wchar_t tMsg[32];
    _snwprintf(tMsg, 32, L"長短:%ls", kLenLabels[st->lenIdx]);
    tMsg[31] = L'\0';
    setToast(st, tMsg);
    markDirty(st, kDirtyHeader | kDirtyButtons | kDirtySidebarStats);
    return;
  }

  if (vkCode == VK_F9) {
    if (st->modelCount > 1) {
      stopGeneration(st);
      st->modelIdx = (st->modelIdx + 1) % st->modelCount;
      loadActiveModel(st);
      pushWelcomeMessage(st);
      setToast(st, st->engine.isLoaded() ? st->engine.info().displayName : L"読込失敗");
      markDirty(st, kDirtyAll, true);
    } else {
      setToast(st, L"他モデル無");
      markDirty(st, kDirtyHeader);
    }
    return;
  }

  if (vkCode == VK_F12 || vkCode == VK_F5 ||
      (vkCode == VK_F7 && !st->converting && !st->romaji.hasContent() && !st->inputBuf[0])) {
    stopGeneration(st);
    st->engine.resetConversation();
    st->presetIdx = 0;
    pushWelcomeMessage(st);
    setToast(st, L"履歴消去");
    markDirty(st, kDirtyHeader | kDirtyArea | kDirtyButtons | kDirtySidebarStats);
    return;
  }

  // Handle active kanji candidate bar first (only touches input bar + candidate strip)
  if (st->converting) {
    if (vkCode == VK_SPACE || vkCode == VK_RIGHT || vkCode == VK_DOWN) {
      st->candSel = (st->candSel + 1) % st->candCount;
      st->candPageStart = (st->candSel / 8) * 8;
      markDirty(st, kDirtyInputBar | kDirtyCandBar);
      return;
    }
    if (vkCode == VK_LEFT || vkCode == VK_UP) {
      st->candSel = (st->candSel + st->candCount - 1) % st->candCount;
      st->candPageStart = (st->candSel / 8) * 8;
      markDirty(st, kDirtyInputBar | kDirtyCandBar);
      return;
    }
    if (vkCode >= '1' && vkCode <= '8') {
      int pick = st->candPageStart + static_cast<int>(vkCode - '1');
      if (pick < st->candCount) {
        st->candSel = pick;
        commitChatCandidate(st);
      }
      uint32_t d = kDirtyInputBar | (st->converting ? kDirtyCandBar : (kDirtyArea | kDirtyHeader));
      markDirty(st, d);
      return;
    }
    if (vkCode == VK_F7) {
      commitChatKatakana(st);
      markDirty(st, kDirtyArea | kDirtyInputBar | kDirtyHeader);
      return;
    }
    if (vkCode == VK_RETURN) {
      commitChatCandidate(st);
      uint32_t d = kDirtyInputBar | (st->converting ? kDirtyCandBar : (kDirtyArea | kDirtyHeader));
      markDirty(st, d);
      return;
    }
    if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->converting = false;
      st->candCount = 0;
      st->candSel = 0;
      st->candPageStart = 0;
      markDirty(st, kDirtyArea | kDirtyInputBar | kDirtyHeader);
      return;
    }
    commitChatCandidate(st);
    markDirty(st, kDirtyArea);
  }

  if (vkCode == VK_ESCAPE) {
    if (st->romaji.hasContent()) {
      st->romaji.reset();
      markDirty(st, kDirtyInputBar);
    } else if (st->inputBuf[0]) {
      st->inputBuf[0] = L'\0';
      st->inputCursor = 0;
      markDirty(st, kDirtyInputBar);
    } else if (st->generating) {
      stopGeneration(st);
      markDirty(st, kDirtyHeader | kDirtyArea | kDirtyButtons | kDirtySidebarStats);
    } else {
      st->window.requestQuit();
    }
    return;
  }

  if (vkCode == VK_BACK) {
    if (st->romaji.backspace()) {
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (st->inputCursor > 0) {
      deleteInputBeforeCursor(st);
      markDirty(st, kDirtyInputBar);
    }
    return;
  }

  if (vkCode == VK_F7 && st->romaji.hasContent()) {
    commitChatKatakana(st);
    markDirty(st, kDirtyInputBar);
    return;
  }

  if (vkCode == VK_RETURN) {
    if (st->romaji.hasContent()) {
      commitChatRawPreedit(st);
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (st->inputBuf[0]) {
      wchar_t msg[kMaxInputChars] = L"";
      wcsncpy(msg, st->inputBuf, kMaxInputChars - 1);
      msg[kMaxInputChars - 1] = L'\0';
      st->inputBuf[0] = L'\0';
      st->inputCursor = 0;
      startChatPrompt(st, msg);
      markDirty(st, kDirtyHeader | kDirtyArea | kDirtyInputBar | kDirtyButtons | kDirtySidebarStats);
    } else {
      setToast(st, L"入力後Ent");
      markDirty(st, kDirtyHeader);
    }
    return;
  }

  if (vkCode == VK_SPACE) {
    if (st->inputMode == 0 && st->romaji.hasContent()) {
      startChatConversion(st);
      markDirty(st, kDirtyInputBar | (st->converting ? (kDirtyCandBar | kDirtyHeader) : kDirtyNone));
    } else {
      insertInputStr(st, L" ");
      markDirty(st, kDirtyInputBar);
    }
    return;
  }

  if (vkCode == VK_UP) {
    if (st->scrollLinesFromBottom < st->maxScrollLines) {
      st->scrollLinesFromBottom += 2;
      if (st->scrollLinesFromBottom > st->maxScrollLines) st->scrollLinesFromBottom = st->maxScrollLines;
      markDirty(st, kDirtyArea);
    }
    return;
  }
  if (vkCode == VK_DOWN) {
    if (st->scrollLinesFromBottom > 0) {
      st->scrollLinesFromBottom -= 2;
      if (st->scrollLinesFromBottom < 0) st->scrollLinesFromBottom = 0;
      markDirty(st, kDirtyArea);
    }
    return;
  }
  if (vkCode == VK_LEFT) {
    if (!st->romaji.hasContent() && st->inputCursor > 0) {
      --st->inputCursor;
      markDirty(st, kDirtyInputBar);
    }
    return;
  }
  if (vkCode == VK_RIGHT) {
    int len = static_cast<int>(wcslen(st->inputBuf));
    if (!st->romaji.hasContent() && st->inputCursor < len) {
      ++st->inputCursor;
      markDirty(st, kDirtyInputBar);
    }
    return;
  }

  // Inline text input
  if (st->inputMode == 0) {
    if (vkCode == VK_PRIOR || (isShift && vkCode == 'N')) {
      st->romaji.feedChar(',');
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (vkCode == VK_NEXT || (isShift && vkCode == 'M')) {
      st->romaji.feedChar('.');
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (vkCode >= 'A' && vkCode <= 'Z') {
      if (st->romaji.romLen == 0 && st->romaji.kanaLen == 1 &&
          (st->romaji.kana[0] == L'、' || st->romaji.kana[0] == L'。')) {
        commitChatRawPreedit(st);
      }
      st->romaji.feedChar(static_cast<char>(vkCode - 'A' + 'a'));
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (vkCode >= '0' && vkCode <= '9') {
      if (st->romaji.romLen == 0 && st->romaji.kanaLen == 1 &&
          (st->romaji.kana[0] == L'、' || st->romaji.kana[0] == L'。')) {
        commitChatRawPreedit(st);
      }
      st->romaji.feedChar(static_cast<char>(vkCode));
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (vkCode == VK_OEM_MINUS) { st->romaji.feedChar('-'); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_PERIOD) { st->romaji.feedChar('.'); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_COMMA) { st->romaji.feedChar(','); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_4) { st->romaji.feedChar('['); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_6) { st->romaji.feedChar(']'); markDirty(st, kDirtyInputBar); return; }
    return;
  }

  if (st->inputMode == 1 || st->inputMode == 2) {
    if (vkCode >= 'A' && vkCode <= 'Z') {
      bool upper = (st->inputMode == 2) ^ isShift;
      wchar_t ch[2] = { static_cast<wchar_t>(upper ? vkCode : (vkCode - 'A' + 'a')), L'\0' };
      insertInputStr(st, ch);
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (vkCode >= '0' && vkCode <= '9') {
      wchar_t ch[2] = { static_cast<wchar_t>(vkCode), L'\0' };
      insertInputStr(st, ch);
      markDirty(st, kDirtyInputBar);
      return;
    }
    if (vkCode == VK_OEM_MINUS) { insertInputStr(st, isShift ? L"_" : L"-"); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_PERIOD || vkCode == VK_NEXT) { insertInputStr(st, L"."); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_COMMA || vkCode == VK_PRIOR) { insertInputStr(st, L","); markDirty(st, kDirtyInputBar); return; }
    if (vkCode == VK_OEM_2) { insertInputStr(st, isShift ? L"?" : L"/"); markDirty(st, kDirtyInputBar); return; }
    return;
  }

  wchar_t sym = mapChat12xSymbol(vkCode);
  if (sym) {
    wchar_t ch[2] = { sym, L'\0' };
    insertInputStr(st, ch);
    markDirty(st, kDirtyInputBar);
  }
}

static void onClick(int x, int y, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    onKeyDown(VK_F10, false, userData);
    return;
  }

  // Click sidebar model header to switch models
  if (x >= LiliumUI::kSidebarX + 6 && x < LiliumUI::kSidebarX + LiliumUI::kSidebarW - 6 &&
      y >= 70 && y < 96) {
    onKeyDown(VK_F9, false, userData);
    return;
  }

  // Click sidebar F12 clear history button
  if (x >= LiliumUI::kSidebarX + 12 && x < LiliumUI::kSidebarX + LiliumUI::kSidebarW - 12 &&
      y >= 390 && y < 418) {
    onKeyDown(VK_F12, false, userData);
    return;
  }

  // Click candidate pills while converting
  if (st->converting && st->candCount > 0 && y >= kCandBarTop && y < kCandBarTop + kCandBarH &&
      x >= 14 && x < 14 + kChatAreaW) {
    int px = 22;
    const int maxX = 14 + kChatAreaW - 82;
    for (int i = 0; i < 8 && (st->candPageStart + i) < st->candCount; ++i) {
      int idx = st->candPageStart + i;
      wchar_t pillTxt[48];
      _snwprintf(pillTxt, 48, L"%d.%ls", i + 1, st->cands[idx]);
      pillTxt[47] = L'\0';
      int pw = ::MisakiFont::measureTextWidth(pillTxt, static_cast<int>(wcslen(pillTxt)), 2) + 14;
      if (pw < 56) pw = 56;
      if (px + pw > maxX) break;
      if (x >= px && x < px + pw) {
        st->candSel = idx;
        commitChatCandidate(st);
        uint32_t d = kDirtyInputBar | (st->converting ? kDirtyCandBar : (kDirtyArea | kDirtyHeader));
        markDirty(st, d);
        return;
      }
      px += pw + 6;
    }
    return;
  }

  // Click mode badge on left of inline input bar (x=18..106, y=396..432) -> cycle mode
  if (y >= 396 && y < 432 && x >= 14 && x < 14 + kChatAreaW) {
    if (x < 108) {
      onKeyDown(VK_F2, false, userData);
    } else if (st->inputBuf[0] && !st->romaji.hasContent() && !st->converting) {
      onKeyDown(VK_RETURN, false, userData);
    }
    return;
  }

  // Bottom 6 action buttons (y = 438..472)
  if (y >= 438 && y <= 472 && x >= 14 && x < 14 + kChatAreaW) {
    int b = (x - 14) / 102;
    if (b > 5) b = 5;
    static constexpr WPARAM kBtnVks[6] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F8, VK_F9 };
    if (b >= 0 && b < 6) onKeyDown(kBtnVks[b], false, userData);
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (!st->generating) {
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    int batPct = 100;
    bool chg = false;
    LiliumUI::queryBattery(batPct, chg);
    if (tm.wMinute != st->lastMin || batPct != st->lastBat) {
      st->lastMin = tm.wMinute;
      st->lastBat = batPct;
      LiliumUI::renderSidebarStatus(c, activeStatusText(st));
    }
    return;
  }

  ++st->animTick;
  DWORD tSlice = GetTickCount();
  bool gotTextTok = false;
  bool anyDone = false;

  for (int iter = 0; iter < 32; ++iter) {
    wchar_t delta[32] = L"";
    bool isGen = false;
    int emo = -1;
    bool done = false;
    st->engine.stepToken(delta, 32, isGen, emo, done);

    if (isGen && st->firstTokTick == 0) {
      st->firstTokTick = GetTickCount();
    }
    if (st->activeAiMsgIdx >= 0 && st->activeAiMsgIdx < st->msgCount) {
      ChatMsg & ai = st->msgs[st->activeAiMsgIdx];
      if (isGen && delta[0]) {
        size_t curLen = wcslen(ai.text);
        size_t addLen = wcslen(delta);
        if (curLen + addLen < 511) {
          wcscat(ai.text, delta);
        }
      }
      ai.tokCount = st->engine.generatedTokenCount();
      if (emo >= 0) {
        ai.emotionIdx = emo;
        st->activeEmotion = emo;
      }
    }

    if (done) {
      anyDone = true;
      break;
    }
    if (isGen) {
      gotTextTok = true;
      break;
    }
    if ((GetTickCount() - tSlice) >= 35) break;
  }

  if (anyDone) {
    st->generating = false;
    st->window.setTimerInterval(500);
    DWORD elapsedMs = GetTickCount() - st->genStartTick;
    int genToks = st->engine.generatedTokenCount();
    st->lastGenTokens = genToks;
    st->lastTokPerSec = (elapsedMs > 0 && genToks > 0)
                            ? (static_cast<double>(genToks) * 1000.0 / static_cast<double>(elapsedMs))
                            : 0.0;
    if (st->activeAiMsgIdx >= 0 && st->activeAiMsgIdx < st->msgCount) {
      ChatMsg & ai = st->msgs[st->activeAiMsgIdx];
      ai.generating = false;
      ai.genMs = elapsedMs;
      if (!ai.text[0]) {
        wcsncpy(ai.text, L"...", 511);
        ai.text[511] = L'\0';
      }
      if (!st->engine.hasEmotionTags()) {
        int fbEmo = LiliumLLM::inferFallbackEmotion(ai.text);
        ai.emotionIdx = fbEmo;
        st->activeEmotion = fbEmo;
      }
    }
    st->dirtyFlags |= kDirtyHeader | kDirtyArea | kDirtyButtons | kDirtySidebarStats;
    if (st->converting) st->dirtyFlags |= kDirtyCandBar;
    onRender(c, st);
  } else if (gotTextTok) {
    DWORD genMs = (st->firstTokTick > 0) ? (GetTickCount() - st->firstTokTick) : 0;
    int genToks = st->engine.generatedTokenCount();
    st->lastGenTokens = genToks;
    if (genMs >= 50 && genToks > 0) {
      st->lastTokPerSec = static_cast<double>(genToks) * 1000.0 / static_cast<double>(genMs);
    }
    st->dirtyFlags |= kDirtyArea | kDirtySidebarStats;
    if (st->converting) st->dirtyFlags |= kDirtyCandBar;
    onRender(c, st);
  } else if ((st->animTick % 2) == 0) {
    st->dirtyFlags |= kDirtyArea | kDirtySidebarStats;
    if (st->converting) st->dirtyFlags |= kDirtyCandBar;
    onRender(c, st);
  }
}

static bool onCloseRequest(void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);
  stopGeneration(st);
  return true;
}

static void runPromptSync(LiliumLLM::TinyLlmEngine & eng, const wchar_t * promptW,
                          float temp, int maxToks, wchar_t * outReply, size_t maxReply,
                          int & outEmo, int minToks = 0, float repPen = 0.0f) {
  outReply[0] = L'\0';
  outEmo = LiliumLLM::EmoNormal;
  if (!eng.beginPrompt(promptW, temp, maxToks, minToks, repPen)) return;
  bool done = false;
  while (!done) {
    wchar_t delta[32] = L"";
    bool isGen = false;
    int e = -1;
    eng.stepToken(delta, 32, isGen, e, done);
    if (delta[0] && wcslen(outReply) + wcslen(delta) + 1 < maxReply) {
      wcscat(outReply, delta);
    }
    if (e >= 0) outEmo = e;
  }
  if (!eng.hasEmotionTags()) {
    outEmo = LiliumLLM::inferFallbackEmotion(outReply);
  }
}

static int runSelfTest(bool testAll) {
  ChatState * st = static_cast<ChatState *>(calloc(1, sizeof(ChatState)));
  if (!st) return 2;
  new (st) ChatState();
  scanModels(st);
  if (st->modelCount <= 0) {
    fprintf(stderr, "No models found\n");
    st->~ChatState();
    free(st);
    return 1;
  }

  if (!testAll) {
    st->modelIdx = 0;
    if (!loadActiveModel(st)) {
      fprintf(stderr, "Load failed: modelCount=%d\n", st->modelCount);
      st->~ChatState();
      free(st);
      return 1;
    }
    const wchar_t * prompt = L"おはよう、ずんだもん！";
    wchar_t reply[512] = L"";
    int emo = LiliumLLM::EmoNormal;
    DWORD t0 = GetTickCount();
    runPromptSync(st->engine, prompt, 0.0f, 48, reply, 512, emo);
    DWORD dt = GetTickCount() - t0;

    char utf8[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, reply, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
    char emoUtf8[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, LiliumLLM::emotionLabel(emo), -1, emoUtf8, sizeof(emoUtf8) - 1, NULL, NULL);
    printf("REPLY=%s EMO=%s TOKS=%d MS=%lu\n",
           utf8, emoUtf8, st->engine.generatedTokenCount(), static_cast<unsigned long>(dt));
    fflush(stdout);

    int rc = (reply[0] != L'\0') ? 0 : 1;
    st->~ChatState();
    free(st);
    return rc;
  }

  int rc = 0;
  for (int m = 0; m < st->modelCount; ++m) {
    st->modelIdx = m;
    if (!loadActiveModel(st)) {
      fprintf(stderr, "Load failed for model %d\n", m);
      rc = 1;
      break;
    }
    char mNameUtf8[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, st->engine.info().displayName, -1, mNameUtf8, 63, NULL, NULL);

    if (!st->engine.isMultiTurn()) {
      wchar_t reply[512] = L"";
      int emo = LiliumLLM::EmoNormal;
      DWORD t0 = GetTickCount();
      runPromptSync(st->engine, L"おはよう、ずんだもん！", 0.0f, 48, reply, 512, emo);
      DWORD dt = GetTickCount() - t0;
      char utf8[1024] = {};
      WideCharToMultiByte(CP_UTF8, 0, reply, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
      char emoUtf8[64] = {};
      WideCharToMultiByte(CP_UTF8, 0, LiliumLLM::emotionLabel(emo), -1, emoUtf8, sizeof(emoUtf8) - 1, NULL, NULL);
      printf("MODEL=%s REPLY=%s EMO=%s TOKS=%d MS=%lu\n",
             mNameUtf8, utf8, emoUtf8, st->engine.generatedTokenCount(), static_cast<unsigned long>(dt));
      if (!reply[0]) rc = 1;
    } else {
      st->engine.resetConversation();
      wchar_t r1[512] = L"", r2[512] = L"", r3[512] = L"";
      int e1 = 0, e2 = 0, e3 = 0;
      DWORD t0 = GetTickCount();
      runPromptSync(st->engine, L"こんにちは。", 0.0f, 48, r1, 512, e1);
      runPromptSync(st->engine, L"猫が好き。",   0.0f, 48, r2, 512, e2);
      runPromptSync(st->engine, L"何が好き？",   0.0f, 48, r3, 512, e3);
      DWORD dt = GetTickCount() - t0;

      char u1[512] = {}, u2[512] = {}, u3[512] = {};
      WideCharToMultiByte(CP_UTF8, 0, r1, -1, u1, sizeof(u1) - 1, NULL, NULL);
      WideCharToMultiByte(CP_UTF8, 0, r2, -1, u2, sizeof(u2) - 1, NULL, NULL);
      WideCharToMultiByte(CP_UTF8, 0, r3, -1, u3, sizeof(u3) - 1, NULL, NULL);
      printf("MODEL=%s TURN1=%s TURN2=%s TURN3=%s CTX=%d/%d MS=%lu\n",
             mNameUtf8, u1, u2, u3,
             st->engine.contextUsed(), st->engine.info().seqLen,
             static_cast<unsigned long>(dt));
      if (!r1[0] || !r2[0] || !wcsstr(r3, L"猫")) rc = 1;

      st->engine.resetConversation();
      wchar_t rLong[512] = L"";
      int eLong = 0;
      runPromptSync(st->engine, L"電子辞書ってすごいよね！", 0.4f, 96, rLong, 512, eLong, 24, 1.22f);
      int longToks = st->engine.generatedTokenCount();
      char uLong[1024] = {};
      WideCharToMultiByte(CP_UTF8, 0, rLong, -1, uLong, sizeof(uLong) - 1, NULL, NULL);
      printf("MODEL=%s LONG_TOKS=%d LONG_REPLY=%s\n", mNameUtf8, longToks, uLong);
      if (longToks < 24 || !rLong[0]) rc = 1;
    }
    fflush(stdout);
  }

  st->~ChatState();
  free(st);
  return rc;
}

} // namespace

int main(int argc, char * argv[]) {
  if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
    return runSelfTest(false);
  }
  if (argc > 1 && strcmp(argv[1], "--selftest-all") == 0) {
    return runSelfTest(true);
  }

  void * raw = calloc(1, sizeof(ChatState));
  if (!raw) return 1;
  ChatState * st = new (raw) ChatState();
  st->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  st->ime.connectOrLaunch(true);
  scanModels(st);
  loadActiveModel(st);
  pushWelcomeMessage(st);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.onCloseRequest = onCloseRequest;
  cb.timerIntervalMs = 500;
  cb.timerAlwaysFire = true;

  int rc = 1;
  if (st->window.create(L"LiliumChatWinClass",
                        L"Lilium Chat (Local LLM)",
                        cb, st, st->manageTaskBar)) {
    rc = st->window.runLoop();
  }

  stopGeneration(st);
  st->~ChatState();
  free(raw);
  return rc;
}
