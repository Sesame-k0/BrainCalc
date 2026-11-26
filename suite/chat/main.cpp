#include "tiny_llm.h"
#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"
#include "../common/lilium_settings.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace {

static constexpr int kMaxMsgs = 32;
static constexpr int kMaxModels = 16;
static constexpr int kMaxWrappedPerMsg = 14;
static constexpr int kChatViewTop = 48;
static constexpr int kChatViewBottom = 392;
static constexpr int kChatAreaW = LiliumUI::kMainW - 28; // 612

struct ChatMsg {
  bool isUser;
  wchar_t text[384];
  int emotionIdx;   // -1 if none/user, else 0..7
  int tokCount;
  uint32_t genMs;
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
  int presetIdx = 0;
  int animTick = 0;

  bool modalOpen = false;
  LiliumIME::TextInputModal inputModal;
  wchar_t lastPromptText[128] = L"";

  wchar_t toast[64] = L"";
  DWORD toastExpireTick = 0;
  int lastMin = -1;
  int lastBat = -1;
};

static constexpr float kTemps[4] = { 0.0f, 0.4f, 0.7f, 1.0f };
static constexpr const wchar_t * kTempLabels[4] = { L"0.0(固定)", L"0.4", L"0.7", L"1.0(自由)" };

static constexpr const wchar_t * kPresetPrompts[6] = {
  L"おはよう、ずんだもん！",
  L"今日はどんな天気かな？",
  L"好きな食べ物は何？",
  L"電子辞書ってすごいよね！",
  L"勉強がつかれたのだ...",
  L"おやすみ！"
};

static void setToast(ChatState * st, const wchar_t * msg) {
  if (!st) return;
  wcsncpy(st->toast, msg ? msg : L"", 63);
  st->toast[63] = L'\0';
  st->toastExpireTick = GetTickCount() + 2800;
}

static const wchar_t * activeStatusText(ChatState * st) {
  if (!st) return L"待機中";
  if (st->toast[0]) {
    if (static_cast<int32_t>(GetTickCount() - st->toastExpireTick) < 0) return st->toast;
    st->toast[0] = L'\0';
  }
  if (st->generating) return L"生成中 (INT8推論)";
  if (st->engine.isLoaded()) return L"会話待機 (ローカルLLM)";
  return L"モデル未検出";
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
      LiliumUI::joinPath(searchDirs[d], L"tok.bin", tokPath, MAX_PATH);
      if (GetFileAttributesW(tokPath) == INVALID_FILE_ATTRIBUTES) continue;

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

  // Prefer base_q8 first if present
  for (int i = 1; i < st->modelCount; ++i) {
    if (containsNoCaseW(st->models[i].name, L"base_q8") &&
        !containsNoCaseW(st->models[0].name, L"base_q8")) {
      ModelFileEntry tmp = st->models[0];
      st->models[0] = st->models[i];
      st->models[i] = tmp;
      break;
    }
  }
}

static bool loadActiveModel(ChatState * st) {
  st->loadErr[0] = L'\0';
  if (st->modelCount <= 0) {
    wcsncpy(st->loadErr, L"apps\\models\\ に Q8 .bin と tok.bin を配置してください", 63);
    st->loadErr[63] = L'\0';
    return false;
  }
  if (st->modelIdx < 0 || st->modelIdx >= st->modelCount) st->modelIdx = 0;
  const ModelFileEntry & m = st->models[st->modelIdx];
  return st->engine.load(m.modelPath, m.tokPath, st->loadErr, 64);
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
    _snwprintf(m.text, 384,
               L"ローカル超軽量日本語LLM起動完了 (%ls / 1.74M INT8)！\n"
               L"[Enter] で文字入力、[F3] で例文を送信して会話ごっこできるのだ。",
               st->models[st->modelIdx].name);
  } else {
    _snwprintf(m.text, 384,
               L"モデルが見つかりません: %ls\n"
               L"apps\\models\\zundamon-v4-base_q8.bin と tok.bin を配置してください。",
               st->loadErr[0] ? st->loadErr : L"(missing)");
  }
  m.text[383] = L'\0';
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
    wcsncpy(m.text, text, 383);
    m.text[383] = L'\0';
  }
  return &m;
}

static void startChatPrompt(ChatState * st, const wchar_t * promptW) {
  if (!st || !promptW || !promptW[0]) return;
  if (!st->engine.isLoaded()) {
    setToast(st, L"LLMモデルが未ロードです");
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

  if (!st->engine.beginPrompt(promptW, kTemps[st->tempIdx], 64)) {
    ai->generating = false;
    wcsncpy(ai->text, L"(プロンプト変換エラー)", 383);
    return;
  }

  st->generating = true;
  st->genStartTick = GetTickCount();
  st->firstTokTick = 0;
  st->lastGenTokens = 0;
  st->window.setTimerInterval(15);
  LiliumSettings::suiteLog(L"Chat", L"Begin prompt len=%d temp=%.1f",
                           st->engine.promptTokenCount(),
                           static_cast<double>(kTemps[st->tempIdx]));
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
      wcsncpy(ai.text, L"(中断しました)", 383);
      ai.text[383] = L'\0';
    }
  }
}

static int wrapBubbleText(LiliumUI::Canvas & c, const wchar_t * src, int maxPx,
                          wchar_t outLines[kMaxWrappedPerMsg][96]) {
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
    if (curLen > 0 && (curW + cw > maxPx || curLen >= 90)) {
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

static void renderEmotionFace(LiliumUI::Canvas & c, int x, int y, int emoIdx, bool thinking, int tick) {
  // Crisp 56x56 pixel-art mascot portrait matching Brain's Misaki/Teal aesthetic
  c.drawBox(x, y, 56, 56, 0xEFFAF4, LiliumUI::kColTealDark);
  c.drawThickFrame(x, y, 56, 56, 2, LiliumUI::kColTealDark);

  // Cute twin pod / bean ears on top
  c.fillRect(x + 17, y + 6, 8, 8, 0x62C978);
  c.fillRect(x + 31, y + 6, 8, 8, 0x62C978);
  // Head outline & soft green/cream face
  c.drawBox(x + 12, y + 13, 32, 33, 0xFFFDF8, LiliumUI::kColCharcoal);
  c.fillRect(x + 12, y + 13, 32, 7, 0x8DE09D);

  if (thinking) {
    int activeDot = tick % 3;
    for (int d = 0; d < 3; ++d) {
      c.fillRect(x + 18 + d * 8, y + 29, 5, 5,
                 (d == activeDot) ? LiliumUI::kColTealDark : LiliumUI::kColBorderStrong);
    }
    c.fillRect(x + 24, y + 39, 8, 2, LiliumUI::kColMuted);
    return;
  }

  // Cheeks
  if (emoIdx == LiliumLLM::EmoHappy || emoIdx == LiliumLLM::EmoSurprised) {
    c.fillRect(x + 15, y + 34, 4, 3, 0xFF9E9E);
    c.fillRect(x + 37, y + 34, 4, 3, 0xFF9E9E);
  }

  switch (emoIdx) {
    case LiliumLLM::EmoHappy:
      // ^ ^ eyes & smile
      c.fillRect(x + 18, y + 26, 6, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 17, y + 28, 2, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 23, y + 28, 2, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 32, y + 26, 6, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 31, y + 28, 2, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 37, y + 28, 2, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 23, y + 36, 10, 4, 0xE86A6A);
      break;
    case LiliumLLM::EmoSmug:
      // Half-lidded smug eyes + smirk
      c.fillRect(x + 17, y + 25, 7, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 20, y + 27, 3, 3, LiliumUI::kColCharcoal);
      c.fillRect(x + 32, y + 25, 7, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 33, y + 27, 3, 3, LiliumUI::kColCharcoal);
      c.fillRect(x + 24, y + 37, 9, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 32, y + 35, 3, 2, LiliumUI::kColCharcoal);
      break;
    case LiliumLLM::EmoSurprised:
      // Wide eyes + O mouth
      c.drawBox(x + 17, y + 24, 6, 6, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
      c.fillRect(x + 19, y + 26, 2, 2, LiliumUI::kColCharcoal);
      c.drawBox(x + 33, y + 24, 6, 6, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
      c.fillRect(x + 35, y + 26, 2, 2, LiliumUI::kColCharcoal);
      c.drawBox(x + 25, y + 35, 6, 6, 0xE86A6A, LiliumUI::kColCharcoal);
      break;
    case LiliumLLM::EmoSad:
    case LiliumLLM::EmoTroubled:
      // Droopy eyes + wavy/sad mouth
      c.fillRect(x + 17, y + 24, 5, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 19, y + 27, 3, 4, LiliumUI::kColCharcoal);
      c.fillRect(x + 34, y + 24, 5, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 34, y + 27, 3, 4, LiliumUI::kColCharcoal);
      c.fillRect(x + 23, y + 38, 10, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 22, y + 40, 2, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 32, y + 40, 2, 2, LiliumUI::kColCharcoal);
      break;
    case LiliumLLM::EmoAngry:
      // Angled brows + triangle mouth
      c.fillRect(x + 17, y + 23, 6, 2, LiliumUI::kColDangerText);
      c.fillRect(x + 21, y + 25, 3, 2, LiliumUI::kColDangerText);
      c.fillRect(x + 33, y + 23, 6, 2, LiliumUI::kColDangerText);
      c.fillRect(x + 32, y + 25, 3, 2, LiliumUI::kColDangerText);
      c.fillRect(x + 19, y + 28, 4, 4, LiliumUI::kColCharcoal);
      c.fillRect(x + 33, y + 28, 4, 4, LiliumUI::kColCharcoal);
      c.fillRect(x + 23, y + 37, 10, 3, LiliumUI::kColDangerFill);
      break;
    case LiliumLLM::EmoSleepy:
      // - - eyes + small yawn
      c.fillRect(x + 17, y + 28, 6, 2, LiliumUI::kColMuted);
      c.fillRect(x + 33, y + 28, 6, 2, LiliumUI::kColMuted);
      c.drawBox(x + 26, y + 36, 4, 5, 0xE86A6A, LiliumUI::kColCharcoal);
      break;
    default:
      // Normal friendly face
      c.fillRect(x + 19, y + 26, 4, 5, LiliumUI::kColCharcoal);
      c.fillRect(x + 33, y + 26, 4, 5, LiliumUI::kColCharcoal);
      c.fillRect(x + 24, y + 37, 8, 2, LiliumUI::kColCharcoal);
      break;
  }
}

static void renderChatArea(LiliumUI::Canvas & c, ChatState * st) {
  const int ax = 14, ay = kChatViewTop, aw = kChatAreaW, ah = kChatViewBottom - kChatViewTop;
  c.drawBox(ax, ay, aw, ah, LiliumUI::kColWhite, LiliumUI::kColBorder);

  struct RenderedBubble {
    bool isUser;
    int lineCount;
    wchar_t lines[kMaxWrappedPerMsg][96];
    int boxW;
    int boxH;
    int emotionIdx;
    int tokCount;
    uint32_t genMs;
    bool generating;
  };
  RenderedBubble * bubbles = static_cast<RenderedBubble *>(calloc(kMaxMsgs, sizeof(RenderedBubble)));
  if (!bubbles) return;

  const int bubbleMaxTextW = 416;
  int totalH = 10;
  for (int i = 0; i < st->msgCount; ++i) {
    const ChatMsg & m = st->msgs[i];
    RenderedBubble & b = bubbles[i];
    b.isUser = m.isUser;
    b.emotionIdx = m.emotionIdx;
    b.tokCount = m.tokCount;
    b.genMs = m.genMs;
    b.generating = m.generating;
    const wchar_t * txt = (m.text[0]) ? m.text : (m.generating ? L"考えているのだ..." : L" ");
    b.lineCount = wrapBubbleText(c, txt, bubbleMaxTextW, b.lines);

    int maxLineW = 0;
    for (int l = 0; l < b.lineCount; ++l) {
      int lw = c.measureWidth(b.lines[l], 2);
      if (lw > maxLineW) maxLineW = lw;
    }
    int minW = b.isUser ? 64 : 168;
    b.boxW = maxLineW + 24;
    if (b.boxW < minW) b.boxW = minW;
    if (b.boxW > aw - 28) b.boxW = aw - 28;
    int footerH = (!b.isUser) ? 20 : 0;
    b.boxH = 14 + b.lineCount * 20 + footerH;
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
    const RenderedBubble & b = bubbles[i];
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
      int ly = by + 7 + l * 20;
      if (ly < ay + 4 || ly + 18 > ay + ah - 4) continue;
      c.drawText(bx + 12, ly, b.boxW - 24, 18,
                 b.isUser ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 b.lines[l], 2, LiliumUI::Align::Left, true);
    }

    if (!b.isUser) {
      int fy = by + 9 + b.lineCount * 20;
      if (fy >= ay + 4 && fy + 16 <= ay + ah - 4) {
        wchar_t tag[48];
        if (b.generating) {
          _snwprintf(tag, 48, L"[生成中... %d tok]", b.tokCount);
        } else if (b.tokCount > 0) {
          _snwprintf(tag, 48, L"[%ls] %dtok", LiliumLLM::emotionLabel(b.emotionIdx), b.tokCount);
        } else {
          _snwprintf(tag, 48, L"[%ls]", LiliumLLM::emotionLabel(b.emotionIdx));
        }
        tag[47] = L'\0';
        c.drawText(bx + 12, fy, b.boxW - 24, 16, LiliumUI::kColTealDark, tag, 1, LiliumUI::Align::Right, true);
      }
    }
  }
  free(bubbles);
}

static void renderBottomControls(LiliumUI::Canvas & c, ChatState * st) {
  const int ix = 14, iy = 398, iw = kChatAreaW, ih = 32;
  c.drawBox(ix, iy, iw, ih, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawText(ix + 12, iy, iw - 24, ih, LiliumUI::kColTealDeep,
             L"[Enter] 日本語で話しかける (Lilium IME)   [↑↓] スクロール",
             2, LiliumUI::Align::Left, true);

  wchar_t tempBtn[20];
  _snwprintf(tempBtn, 20, L"[F2]温度:%ls", kTempLabels[st->tempIdx]);
  tempBtn[19] = L'\0';
  const wchar_t * btns[5] = {
    st->generating ? L"[F1] 停止" : L"[F1] 再生成",
    tempBtn,
    L"[F3] 例文送信",
    L"[F4] 消去",
    L"[F5] モデル"
  };
  for (int b = 0; b < 5; ++b) {
    int bx = 14 + b * 123;
    int bw = (b == 4) ? (iw - 4 * 123) : 119;
    int by = 436;
    int bh = 32;
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
    }
    c.drawBox(bx, by, bw, bh, bg, bd);
    c.drawText(bx + 2, by, bw - 4, bh, fg, btns[b], 1, LiliumUI::Align::Center, true);
  }
}

static void renderChatSidebar(LiliumUI::Canvas & c, ChatState * st) {
  // Custom sidebar layout integrating emotion portrait + telemetry
  c.fillRect(LiliumUI::kSidebarX, 0, LiliumUI::kSidebarW, LiliumUI::kScreenH, LiliumUI::kColSidebar);
  c.fillRect(LiliumUI::kSidebarX, 0, 1, LiliumUI::kScreenH, LiliumUI::kColBorder);

  LiliumUI::renderSidebarStatus(c, activeStatusText(st));

  const int sx = LiliumUI::kSidebarX + 6;
  const int sw = LiliumUI::kSidebarW - 12;
  c.drawBox(sx, 74, sw, 352, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
  c.drawBox(sx, 74, sw, 24, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
  c.drawText(sx, 74, sw, 24, LiliumUI::kColTealDeep, L"ローカルLLM", 2, LiliumUI::Align::Center, true);

  renderEmotionFace(c, sx + (sw - 56) / 2, 106, st->activeEmotion, st->generating, st->animTick);

  wchar_t emoBuf[24];
  _snwprintf(emoBuf, 24, L"【%ls】", LiliumLLM::emotionLabel(st->activeEmotion));
  emoBuf[23] = L'\0';
  c.drawText(sx + 4, 168, sw - 8, 22, LiliumUI::kColTealDark, emoBuf, 2, LiliumUI::Align::Center, true);

  c.fillRect(sx + 8, 196, sw - 16, 1, LiliumUI::kColBorder);

  int totMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totMB, availMB, usedPct);
  wchar_t l1[36], l2[36], l3[36], l4[36], l5[36];
  if (st->engine.isLoaded()) {
    _snwprintf(l1, 36, L"規模: INT8 %.1fMB", static_cast<double>(st->engine.info().fileBytes) / (1024.0 * 1024.0));
  } else {
    wcsncpy(l1, L"モデル: 未読込", 35);
  }
  _snwprintf(l2, 36, L"速度: %.1f tok/s", st->lastTokPerSec);
  _snwprintf(l3, 36, L"生成: %d tok", st->lastGenTokens);
  _snwprintf(l4, 36, L"温度: %ls", kTempLabels[st->tempIdx]);
  _snwprintf(l5, 36, L"空RAM: %d/%dMB", availMB, totMB);
  l1[35] = l2[35] = l3[35] = l4[35] = l5[35] = L'\0';

  c.drawText(sx + 6, 206, sw - 12, 20, LiliumUI::kColText, l1, 1, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 230, sw - 12, 20, LiliumUI::kColTealDeep, l2, 1, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 254, sw - 12, 20, LiliumUI::kColText, l3, 1, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 278, sw - 12, 20, LiliumUI::kColText, l4, 1, LiliumUI::Align::Left, true);
  c.drawText(sx + 6, 302, sw - 12, 20, LiliumUI::kColMuted, l5, 1, LiliumUI::Align::Left, true);

  c.fillRect(sx + 8, 330, sw - 16, 1, LiliumUI::kColBorder);
  c.drawWrappedText(sx + 6, 338, sw - 12, 420, LiliumUI::kColMuted,
                    L"1.7M超軽量実験モデル。会話の練習用ジョークアプリです。",
                    1, 4);

  c.drawBox(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
            LiliumUI::kColBtnBg, LiliumUI::kColBtnBorder);
  c.drawText(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
             LiliumUI::kColText, L"戻 る", 2, LiliumUI::Align::Center, true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (st->modalOpen && st->inputModal.isOpen() &&
      !c.fullRedraw && st->inputModal.hasDrawnChrome()) {
    st->inputModal.render(c, true);
    return;
  }

  if (c.fullRedraw) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
    LiliumUI::renderTopHeader(c, L"Lilium Chat", activeStatusText(st));
    renderChatArea(c, st);
    renderBottomControls(c, st);
    renderChatSidebar(c, st);
  } else {
    LiliumUI::renderTopHeader(c, L"Lilium Chat", activeStatusText(st));
    renderChatArea(c, st);
    renderBottomControls(c, st);
    renderChatSidebar(c, st);
  }

  if (st->modalOpen) {
    st->inputModal.render(c, true);
  }
}

static void onKeyDown(WPARAM vkCode, bool isShift, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (st->modalOpen) {
    bool confirmed = false, closed = false;
    st->inputModal.handleKey(vkCode, isShift, confirmed, closed);
    if (confirmed && st->inputModal.text()[0]) {
      startChatPrompt(st, st->inputModal.text());
    }
    if (closed) st->modalOpen = false;
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_HOME || vkCode == VK_F10) {
    stopGeneration(st);
    st->window.requestQuit();
    return;
  }

  if (vkCode == VK_ESCAPE) {
    if (st->generating) {
      stopGeneration(st);
      st->window.requestRepaint(true);
    } else {
      st->window.requestQuit();
    }
    return;
  }

  if (vkCode == VK_RETURN || vkCode == VK_SPACE) {
    if (!st->generating) {
      st->modalOpen = true;
      st->inputModal.open(L"話しかけるメッセージ入力", L"日本語 (Lilium IME) で入力して決定:", L"", 96);
      st->window.requestRepaint(true);
    }
    return;
  }

  if (vkCode == VK_F1) {
    if (st->generating) {
      stopGeneration(st);
    } else if (st->lastPromptText[0]) {
      startChatPrompt(st, st->lastPromptText);
    } else {
      const wchar_t * p = kPresetPrompts[st->presetIdx];
      st->presetIdx = (st->presetIdx + 1) % 6;
      startChatPrompt(st, p);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_F2) {
    st->tempIdx = (st->tempIdx + 1) % 4;
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_F3) {
    const wchar_t * p = kPresetPrompts[st->presetIdx];
    st->presetIdx = (st->presetIdx + 1) % 6;
    startChatPrompt(st, p);
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_F4) {
    stopGeneration(st);
    pushWelcomeMessage(st);
    setToast(st, L"会話履歴を消去しました");
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_F5) {
    if (!st->generating && st->modelCount > 1) {
      st->modelIdx = (st->modelIdx + 1) % st->modelCount;
      loadActiveModel(st);
      pushWelcomeMessage(st);
      setToast(st, st->models[st->modelIdx].name);
      st->window.requestRepaint(true);
    } else if (st->modelCount <= 1) {
      setToast(st, L"切替可能な他モデルがありません");
      st->window.requestRepaint(false);
    }
    return;
  }

  if (vkCode == VK_UP) {
    if (st->scrollLinesFromBottom < st->maxScrollLines) ++st->scrollLinesFromBottom;
    st->window.requestRepaint(false);
    return;
  }
  if (vkCode == VK_DOWN) {
    if (st->scrollLinesFromBottom > 0) --st->scrollLinesFromBottom;
    st->window.requestRepaint(false);
    return;
  }
  if (vkCode == VK_PRIOR) {
    st->scrollLinesFromBottom += 6;
    if (st->scrollLinesFromBottom > st->maxScrollLines) st->scrollLinesFromBottom = st->maxScrollLines;
    st->window.requestRepaint(false);
    return;
  }
  if (vkCode == VK_NEXT) {
    st->scrollLinesFromBottom -= 6;
    if (st->scrollLinesFromBottom < 0) st->scrollLinesFromBottom = 0;
    st->window.requestRepaint(false);
    return;
  }
}

static void onClick(int x, int y, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);

  if (st->modalOpen) {
    bool confirmed = false, closed = false;
    st->inputModal.handleClick(x, y, confirmed, closed);
    if (confirmed && st->inputModal.text()[0]) {
      startChatPrompt(st, st->inputModal.text());
    }
    if (closed) st->modalOpen = false;
    st->window.requestRepaint(true);
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    onKeyDown(VK_F10, false, userData);
    return;
  }

  // Prompt bar
  if (x >= 14 && x < 14 + kChatAreaW && y >= 398 && y <= 430) {
    onKeyDown(VK_RETURN, false, userData);
    return;
  }

  // Bottom 5 action buttons
  if (y >= 436 && y <= 468 && x >= 14 && x < 14 + kChatAreaW) {
    int b = (x - 14) / 123;
    static constexpr WPARAM kBtnVks[5] = { VK_F1, VK_F2, VK_F3, VK_F4, VK_F5 };
    if (b >= 0 && b < 5) onKeyDown(kBtnVks[b], false, userData);
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);
  if (st->modalOpen) return;

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

  // Execute prefill fast within a 35ms slice; emit 1 generated text token per timer tick
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
        if (curLen + addLen < 383) {
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
      if (!ai.text[0]) wcsncpy(ai.text, L"...", 383);
    }
  } else if (gotTextTok) {
    DWORD genMs = (st->firstTokTick > 0) ? (GetTickCount() - st->firstTokTick) : 0;
    int genToks = st->engine.generatedTokenCount();
    st->lastGenTokens = genToks;
    if (genMs >= 50 && genToks > 0) {
      st->lastTokPerSec = static_cast<double>(genToks) * 1000.0 / static_cast<double>(genMs);
    }
  }

  onRender(c, st);
}

static bool onCloseRequest(void * userData) {
  ChatState * st = static_cast<ChatState *>(userData);
  stopGeneration(st);
  return true;
}

static int runSelfTest() {
  ChatState * st = static_cast<ChatState *>(calloc(1, sizeof(ChatState)));
  if (!st) return 2;
  new (st) ChatState();
  scanModels(st);
  if (!loadActiveModel(st)) {
    fprintf(stderr, "Load failed: modelCount=%d\n", st->modelCount);
    st->~ChatState();
    free(st);
    return 1;
  }

  const wchar_t * prompt = L"\x304a\x306f\x3088\x3046\x3001\x305a\x3093\x3060\x3082\x3093\xff01";
  if (!st->engine.beginPrompt(prompt, 0.0f, 48)) {
    st->~ChatState();
    free(st);
    return 1;
  }

  wchar_t reply[512] = L"";
  int emo = LiliumLLM::EmoNormal;
  bool done = false;
  DWORD t0 = GetTickCount();
  while (!done) {
    wchar_t delta[32] = L"";
    bool isGen = false;
    int e = -1;
    st->engine.stepToken(delta, 32, isGen, e, done);
    if (delta[0] && wcslen(reply) + wcslen(delta) < 511) wcscat(reply, delta);
    if (e >= 0) emo = e;
  }
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

} // namespace

int main(int argc, char * argv[]) {
  if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
    return runSelfTest();
  }

  void * raw = calloc(1, sizeof(ChatState));
  if (!raw) return 1;
  ChatState * st = new (raw) ChatState();
  st->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

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
                        L"Lilium Chat (Local INT8 LLM)",
                        cb, st, st->manageTaskBar)) {
    rc = st->window.runLoop();
  }

  stopGeneration(st);
  st->~ChatState();
  free(raw);
  return rc;
}
