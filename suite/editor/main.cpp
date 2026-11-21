#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"

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
#ifndef VK_OEM_3
#define VK_OEM_3 0xC0
#endif
#ifndef VK_OEM_4
#define VK_OEM_4 0xDB
#endif
#ifndef VK_OEM_6
#define VK_OEM_6 0xDD
#endif
#ifndef VK_OEM_7
#define VK_OEM_7 0xDE
#endif

namespace {

static constexpr int kMaxFiles = 64;
static constexpr int kMaxTextChars = 32768; // 64 KB UTF-16 buffer (heap allocated)
static constexpr int kMaxViewerLines = 1024;
static constexpr int kMaxEditVisLines = 2048;

enum class Screen {
  List = 0,
  Viewer = 1,
  Edit = 2
};

enum class ModalKind {
  None = 0,
  TextInput = 1, // New or Rename via LiliumIME::TextInputModal
  DeleteConfirm = 2
};

enum class MdLineKind : uint8_t {
  Blank = 0,
  Paragraph,
  H1,
  H2,
  H3,
  Bullet,
  Numbered,
  CheckUnchecked,
  CheckChecked,
  Quote,
  Code,
  Rule
};

struct NoteEntry {
  wchar_t name[64];
  wchar_t snippet[64];
  DWORD sizeBytes;
};

struct ViewerLine {
  MdLineKind kind;
  int indentPx;
  wchar_t prefix[12];
  wchar_t text[128];
};

struct VisEditRow {
  int startOffset;
  int endOffset; // exclusive (excluding trailing \n)
  bool hardEnd;
};

struct EditorState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;
  wchar_t notesDir[MAX_PATH] = L"";

  Screen screen = Screen::List;
  ModalKind modal = ModalKind::None;
  bool modalForRename = false;
  int deleteFocus = 1; // 0=Delete, 1=Cancel

  NoteEntry files[kMaxFiles] = {};
  int fileCount = 0;
  int listSel = 0;
  int listScroll = 0;

  // Currently opened document
  wchar_t activeName[64] = L"";
  wchar_t * textBuf = nullptr; // [kMaxTextChars]
  int textLen = 0;
  int cursorPos = 0;
  bool dirty = false;
  DWORD savedToastTick = 0;

  // Viewer parsed visual lines
  ViewerLine * viewLines = nullptr; // [kMaxViewerLines]
  int viewLineCount = 0;
  int viewScroll = 0;

  // Editor wrapped visual lines & IME
  VisEditRow * editRows = nullptr; // [kMaxEditVisLines]
  int editRowCount = 0;
  int editScrollRow = 0;
  int inputMode = 0; // 0=あ(IME), 1=abc, 2=ABC, 3=12x記号/MD

  LiliumIME::RomajiEngine romaji;
  bool converting = false;
  wchar_t cands[LiliumIME::kMaxCands][LiliumIME::kMaxCandChars] = {};
  int candCount = 0;
  int candSel = 0;
  int candPageStart = 0;
  int segLen = 0;
  LiliumIME::ImeClient ime;

  LiliumIME::TextInputModal inputModal;
};

static bool hasNoteSuffix(const wchar_t * name) {
  size_t len = wcslen(name);
  if (len >= 3 && _wcsicmp(name + len - 3, L".md") == 0) return true;
  if (len >= 4 && _wcsicmp(name + len - 4, L".txt") == 0) return true;
  return false;
}

static void stripNoteSuffix(const wchar_t * name, wchar_t * out, size_t maxLen) {
  wcsncpy(out, name, maxLen - 1);
  out[maxLen - 1] = L'\0';
  size_t l = wcslen(out);
  if (l >= 3 && _wcsicmp(out + l - 3, L".md") == 0) out[l - 3] = L'\0';
  else if (l >= 4 && _wcsicmp(out + l - 4, L".txt") == 0) out[l - 4] = L'\0';
}

static void ensureDefaultWelcomeNote(const EditorState * st) {
  wchar_t welPath[MAX_PATH] = L"";
  LiliumUI::joinPath(st->notesDir, L"ようこそ.md", welPath, MAX_PATH);
  if (GetFileAttributesW(welPath) != INVALID_FILE_ATTRIBUTES) return;

  static const char * kWelcomeUtf8 =
      "# Lilium Editor へようこそ\n\n"
      "Markdown記法に対応した軽量メモ・ビューワです。\n"
      "文字入力には常駐 **Lilium IME** (Mozc辞書圧縮搭載) を利用できます。\n\n"
      "## 基本操作\n"
      "- [Enter]: メモを閲覧 (ビューワ表示)\n"
      "- [F2]: 閲覧画面 ⇔ 編集画面を切替\n"
      "- [F1]: 新規メモ作成 / 編集中は即保存\n"
      "- [F3]: 編集中に保存してMarkdownプレビューへ\n\n"
      "## 入力モード ([F2] で切替)\n"
      "1. `あ (IME)`: ローマ字 → `[Space]` で漢字変換 / `[F7]` カタカナ\n"
      "2. `abc` / `ABC`: 英字入力\n"
      "3. `12x記号`: 数字と `# - * / ` > [ ]` 等のMarkdown記号\n\n"
      "> ヒント: Brain右側パネルに現在のキー配列が表示されます。\n\n"
      "- [x] Lilium IME 常駐対応\n"
      "- [ ] [F2] でこのメモを編集してみる\n\n"
      "```markdown\n"
      "# 見出し1\n"
      "- 箇条書きリスト\n"
      "```\n";

  FILE * fp = _wfopen(welPath, L"wb");
  if (fp) {
    fwrite(kWelcomeUtf8, 1, strlen(kWelcomeUtf8), fp);
    fclose(fp);
  }
}

static void loadSnippetForFile(const wchar_t * fullPath, wchar_t * outSnippet, size_t maxLen) {
  outSnippet[0] = L'\0';
  FILE * fp = _wfopen(fullPath, L"rb");
  if (!fp) return;
  char buf[256] = {};
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = '\0';

  const char * p = buf;
  if (n >= 3 && static_cast<uint8_t>(buf[0]) == 0xEF &&
      static_cast<uint8_t>(buf[1]) == 0xBB && static_cast<uint8_t>(buf[2]) == 0xBF) {
    p += 3;
  }
  while (*p == '#' || *p == ' ' || *p == '\r' || *p == '\n') ++p;
  char line[128] = {};
  int k = 0;
  while (p[k] && p[k] != '\r' && p[k] != '\n' && k < 120) {
    line[k] = p[k];
    ++k;
  }
  line[k] = '\0';
  MultiByteToWideChar(CP_UTF8, 0, line, -1, outSnippet, static_cast<int>(maxLen) - 1);
  outSnippet[maxLen - 1] = L'\0';
}

static void scanNoteFiles(EditorState * st) {
  if (!st) return;
  CreateDirectoryW(st->notesDir, NULL);
  st->fileCount = 0;

  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(st->notesDir, L"*", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pat, &fd);
  bool anyFiles = false;
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
      if (hasNoteSuffix(fd.cFileName)) {
        anyFiles = true;
        if (st->fileCount < kMaxFiles) {
          NoteEntry & ne = st->files[st->fileCount++];
          wcsncpy(ne.name, fd.cFileName, 63);
          ne.name[63] = L'\0';
          ne.sizeBytes = fd.nFileSizeLow;
          wchar_t full[MAX_PATH] = L"";
          LiliumUI::joinPath(st->notesDir, fd.cFileName, full, MAX_PATH);
          loadSnippetForFile(full, ne.snippet, 64);
        }
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }

  if (!anyFiles) {
    ensureDefaultWelcomeNote(st);
    NoteEntry & ne = st->files[st->fileCount++];
    wcscpy(ne.name, L"ようこそ.md");
    wchar_t full[MAX_PATH] = L"";
    LiliumUI::joinPath(st->notesDir, ne.name, full, MAX_PATH);
    WIN32_FIND_DATAW wfd = {};
    HANDLE hw = FindFirstFileW(full, &wfd);
    if (hw != INVALID_HANDLE_VALUE) {
      ne.sizeBytes = wfd.nFileSizeLow;
      FindClose(hw);
    }
    loadSnippetForFile(full, ne.snippet, 64);
  }

  // Sort alphabetically
  for (int i = 1; i < st->fileCount; ++i) {
    NoteEntry tmp = st->files[i];
    int j = i - 1;
    while (j >= 0 && _wcsicmp(st->files[j].name, tmp.name) > 0) {
      st->files[j + 1] = st->files[j];
      --j;
    }
    st->files[j + 1] = tmp;
  }

  if (st->listSel < 0) st->listSel = 0;
  if (st->listSel >= st->fileCount && st->fileCount > 0) st->listSel = st->fileCount - 1;
  if (st->listScroll > st->listSel) st->listScroll = st->listSel;
}

static bool loadNoteContent(EditorState * st, const wchar_t * fileName) {
  if (!st || !st->textBuf || !fileName || !fileName[0]) return false;
  wchar_t full[MAX_PATH] = L"";
  LiliumUI::joinPath(st->notesDir, fileName, full, MAX_PATH);
  FILE * fp = _wfopen(full, L"rb");
  st->textBuf[0] = L'\0';
  st->textLen = 0;
  st->cursorPos = 0;
  st->dirty = false;
  wcsncpy(st->activeName, fileName, 63);
  st->activeName[63] = L'\0';

  if (!fp) return false;
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (sz < 0) sz = 0;
  if (sz > 65536) sz = 65536;

  char * utf8 = static_cast<char *>(malloc(static_cast<size_t>(sz) + 4));
  if (!utf8) { fclose(fp); return false; }
  size_t n = fread(utf8, 1, static_cast<size_t>(sz), fp);
  fclose(fp);
  utf8[n] = '\0';

  const char * src = utf8;
  if (n >= 3 && static_cast<uint8_t>(utf8[0]) == 0xEF &&
      static_cast<uint8_t>(utf8[1]) == 0xBB && static_cast<uint8_t>(utf8[2]) == 0xBF) {
    src += 3;
  }

  int wLen = MultiByteToWideChar(CP_UTF8, 0, src, -1, st->textBuf, kMaxTextChars - 1);
  free(utf8);
  if (wLen <= 0) {
    st->textBuf[0] = L'\0';
    st->textLen = 0;
    return true;
  }

  // Strip '\r' for clean uniform '\n' newlines
  int dst = 0;
  for (int i = 0; st->textBuf[i] != L'\0' && dst + 1 < kMaxTextChars; ++i) {
    if (st->textBuf[i] != L'\r') st->textBuf[dst++] = st->textBuf[i];
  }
  st->textBuf[dst] = L'\0';
  st->textLen = dst;
  st->cursorPos = dst;
  return true;
}

static bool saveNoteContent(EditorState * st) {
  if (!st || !st->textBuf || !st->activeName[0]) return false;
  CreateDirectoryW(st->notesDir, NULL);
  wchar_t full[MAX_PATH] = L"";
  LiliumUI::joinPath(st->notesDir, st->activeName, full, MAX_PATH);

  int need = WideCharToMultiByte(CP_UTF8, 0, st->textBuf, st->textLen, NULL, 0, NULL, NULL);
  if (need < 0) return false;
  char * utf8 = static_cast<char *>(malloc(static_cast<size_t>(need) + 4));
  if (!utf8) return false;
  WideCharToMultiByte(CP_UTF8, 0, st->textBuf, st->textLen, utf8, need, NULL, NULL);

  FILE * fp = _wfopen(full, L"wb");
  bool ok = false;
  if (fp) {
    fwrite(utf8, 1, static_cast<size_t>(need), fp);
    fclose(fp);
    ok = true;
  }
  free(utf8);
  if (ok) {
    st->dirty = false;
    st->savedToastTick = GetTickCount();
  }
  return ok;
}

// =============================================================================
// Markdown Parser -> ViewerLines & Wrapped Edit Rows
// =============================================================================

static void addViewerWrapped(EditorState * st, MdLineKind kind, int indentPx,
                             const wchar_t * prefix, const wchar_t * rawLine, int maxWidthPx) {
  if (!rawLine[0]) {
    if (st->viewLineCount < kMaxViewerLines) {
      ViewerLine & vl = st->viewLines[st->viewLineCount++];
      vl.kind = kind;
      vl.indentPx = indentPx;
      wcsncpy(vl.prefix, prefix ? prefix : L"", 11);
      vl.prefix[11] = L'\0';
      vl.text[0] = L'\0';
    }
    return;
  }

  int pos = 0;
  int total = static_cast<int>(wcslen(rawLine));
  bool firstLine = true;
  while (pos < total && st->viewLineCount < kMaxViewerLines) {
    int avail = maxWidthPx - indentPx;
    if (avail < 120) avail = 120;
    int len = 0;
    wchar_t tmp[128] = L"";
    while (pos + len < total && len < 120) {
      tmp[len] = rawLine[pos + len];
      tmp[len + 1] = L'\0';
      if (::MisakiFont::measureTextWidth(tmp, len + 1, 2) > avail && len > 0) break;
      ++len;
    }
    if (len == 0) len = 1;
    ViewerLine & vl = st->viewLines[st->viewLineCount++];
    vl.kind = kind;
    vl.indentPx = indentPx;
    wcsncpy(vl.prefix, (firstLine && prefix) ? prefix : L"", 11);
    vl.prefix[11] = L'\0';
    wcsncpy(vl.text, rawLine + pos, static_cast<size_t>(len));
    vl.text[len] = L'\0';
    pos += len;
    firstLine = false;
  }
}

static void stripInlineMarkdownMarkers(const wchar_t * src, wchar_t * dst, size_t maxLen) {
  size_t di = 0;
  for (size_t i = 0; src[i] != L'\0' && di + 1 < maxLen;) {
    if (src[i] == L'*' && src[i + 1] == L'*') {
      i += 2;
      continue;
    }
    dst[di++] = src[i++];
  }
  dst[di] = L'\0';
}

static void rebuildViewerLines(EditorState * st) {
  if (!st || !st->viewLines || !st->textBuf) return;
  st->viewLineCount = 0;
  bool inCodeBlock = false;
  const int maxW = LiliumUI::kMainW - 56;

  int i = 0;
  while (i <= st->textLen && st->viewLineCount < kMaxViewerLines) {
    int lineStart = i;
    while (i < st->textLen && st->textBuf[i] != L'\n') ++i;
    int lineLen = i - lineStart;
    if (i < st->textLen && st->textBuf[i] == L'\n') ++i;

    wchar_t line[512] = L"";
    int cpy = (lineLen < 511) ? lineLen : 511;
    wcsncpy(line, st->textBuf + lineStart, static_cast<size_t>(cpy));
    line[cpy] = L'\0';

    if (wcsncmp(line, L"```", 3) == 0) {
      inCodeBlock = !inCodeBlock;
      continue;
    }
    if (inCodeBlock) {
      addViewerWrapped(st, MdLineKind::Code, 14, L"", line, maxW);
      continue;
    }
    if (line[0] == L'\0') {
      addViewerWrapped(st, MdLineKind::Blank, 0, L"", L"", maxW);
      continue;
    }
    if (wcscmp(line, L"---") == 0 || wcscmp(line, L"***") == 0) {
      addViewerWrapped(st, MdLineKind::Rule, 0, L"", L"", maxW);
      continue;
    }

    wchar_t clean[512] = L"";
    if (wcsncmp(line, L"### ", 4) == 0) {
      stripInlineMarkdownMarkers(line + 4, clean, 512);
      addViewerWrapped(st, MdLineKind::H3, 0, L"◆", clean, maxW);
    } else if (wcsncmp(line, L"## ", 3) == 0) {
      stripInlineMarkdownMarkers(line + 3, clean, 512);
      addViewerWrapped(st, MdLineKind::H2, 8, L"", clean, maxW);
    } else if (wcsncmp(line, L"# ", 2) == 0) {
      stripInlineMarkdownMarkers(line + 2, clean, 512);
      addViewerWrapped(st, MdLineKind::H1, 0, L"", clean, maxW);
    } else if (wcsncmp(line, L"- [x] ", 6) == 0 || wcsncmp(line, L"- [X] ", 6) == 0) {
      stripInlineMarkdownMarkers(line + 6, clean, 512);
      addViewerWrapped(st, MdLineKind::CheckChecked, 26, L"", clean, maxW);
    } else if (wcsncmp(line, L"- [ ] ", 6) == 0) {
      stripInlineMarkdownMarkers(line + 6, clean, 512);
      addViewerWrapped(st, MdLineKind::CheckUnchecked, 26, L"", clean, maxW);
    } else if (wcsncmp(line, L"- ", 2) == 0 || wcsncmp(line, L"* ", 2) == 0) {
      stripInlineMarkdownMarkers(line + 2, clean, 512);
      addViewerWrapped(st, MdLineKind::Bullet, 20, L"・", clean, maxW);
    } else if (wcsncmp(line, L"> ", 2) == 0) {
      stripInlineMarkdownMarkers(line + 2, clean, 512);
      addViewerWrapped(st, MdLineKind::Quote, 16, L"", clean, maxW);
    } else if (line[0] >= L'0' && line[0] <= L'9' && line[1] == L'.' && line[2] == L' ') {
      wchar_t pfx[8] = { line[0], L'.', L' ', L'\0' };
      stripInlineMarkdownMarkers(line + 3, clean, 512);
      addViewerWrapped(st, MdLineKind::Numbered, 28, pfx, clean, maxW);
    } else {
      stripInlineMarkdownMarkers(line, clean, 512);
      addViewerWrapped(st, MdLineKind::Paragraph, 0, L"", clean, maxW);
    }

    if (i > st->textLen) break;
  }
  if (st->viewScroll >= st->viewLineCount) st->viewScroll = 0;
}

static void rebuildEditWrapRows(EditorState * st, int maxWidthPx) {
  if (!st || !st->editRows || !st->textBuf) return;
  st->editRowCount = 0;
  int i = 0;
  while (i <= st->textLen && st->editRowCount < kMaxEditVisLines) {
    int lineStart = i;
    while (i < st->textLen && st->textBuf[i] != L'\n') ++i;
    int hardEnd = i;
    if (lineStart == hardEnd) {
      VisEditRow & r = st->editRows[st->editRowCount++];
      r.startOffset = lineStart;
      r.endOffset = lineStart;
      r.hardEnd = true;
    } else {
      int pos = lineStart;
      while (pos < hardEnd && st->editRowCount < kMaxEditVisLines) {
        int len = 0;
        wchar_t tmp[128] = L"";
        while (pos + len < hardEnd && len < 120) {
          tmp[len] = st->textBuf[pos + len];
          tmp[len + 1] = L'\0';
          if (::MisakiFont::measureTextWidth(tmp, len + 1, 2) > maxWidthPx && len > 0) break;
          ++len;
        }
        if (len == 0) len = 1;
        VisEditRow & r = st->editRows[st->editRowCount++];
        r.startOffset = pos;
        r.endOffset = pos + len;
        r.hardEnd = (pos + len == hardEnd);
        pos += len;
      }
    }
    if (i < st->textLen && st->textBuf[i] == L'\n') {
      ++i;
      if (i == st->textLen && st->editRowCount < kMaxEditVisLines) {
        // Trailing newline adds an empty final row
        VisEditRow & r = st->editRows[st->editRowCount++];
        r.startOffset = i;
        r.endOffset = i;
        r.hardEnd = true;
        break;
      }
    } else {
      break;
    }
  }
  if (st->editRowCount == 0) {
    st->editRows[0] = { 0, 0, true };
    st->editRowCount = 1;
  }
}

static int findEditRowForCursor(const EditorState * st) {
  for (int r = 0; r < st->editRowCount; ++r) {
    const VisEditRow & row = st->editRows[r];
    if (st->cursorPos >= row.startOffset &&
        (st->cursorPos < row.endOffset || (st->cursorPos == row.endOffset && row.hardEnd))) {
      return r;
    }
  }
  return (st->editRowCount > 0) ? (st->editRowCount - 1) : 0;
}

static void ensureEditorCursorVisible(EditorState * st, int visRowsPerScreen) {
  rebuildEditWrapRows(st, LiliumUI::kMainW - 44);
  int cr = findEditRowForCursor(st);
  if (cr < st->editScrollRow) st->editScrollRow = cr;
  if (cr >= st->editScrollRow + visRowsPerScreen) st->editScrollRow = cr - visRowsPerScreen + 1;
  if (st->editScrollRow < 0) st->editScrollRow = 0;
}

// =============================================================================
// Editor Text Mutation & IME Helpers
// =============================================================================

static void insertEditorText(EditorState * st, const wchar_t * str) {
  if (!st || !st->textBuf || !str || !str[0]) return;
  int addLen = static_cast<int>(wcslen(str));
  int room = (kMaxTextChars - 1) - st->textLen;
  if (room <= 0) return;
  if (addLen > room) addLen = room;

  memmove(st->textBuf + st->cursorPos + addLen,
          st->textBuf + st->cursorPos,
          static_cast<size_t>(st->textLen - st->cursorPos + 1) * sizeof(wchar_t));
  memcpy(st->textBuf + st->cursorPos, str, static_cast<size_t>(addLen) * sizeof(wchar_t));
  st->textLen += addLen;
  st->cursorPos += addLen;
  st->dirty = true;
}

static void backspaceEditor(EditorState * st) {
  if (!st || !st->textBuf || st->cursorPos <= 0) return;
  memmove(st->textBuf + st->cursorPos - 1,
          st->textBuf + st->cursorPos,
          static_cast<size_t>(st->textLen - st->cursorPos + 1) * sizeof(wchar_t));
  --st->textLen;
  --st->cursorPos;
  st->dirty = true;
}

static void deleteEditor(EditorState * st) {
  if (!st || !st->textBuf || st->cursorPos >= st->textLen) return;
  memmove(st->textBuf + st->cursorPos,
          st->textBuf + st->cursorPos + 1,
          static_cast<size_t>(st->textLen - st->cursorPos) * sizeof(wchar_t));
  --st->textLen;
  st->dirty = true;
}

static void startEditorConversion(EditorState * st) {
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

static void commitEditorCandidate(EditorState * st) {
  if (!st->converting || st->candCount <= 0) return;
  const wchar_t * chosen = st->cands[st->candSel];
  int slen = (st->segLen > 0 && st->segLen <= st->romaji.kanaLen) ? st->segLen : st->romaji.kanaLen;

  wchar_t segR[LiliumIME::kMaxReadingChars] = L"";
  wcsncpy(segR, st->romaji.kana, static_cast<size_t>(slen));
  segR[slen] = L'\0';
  st->ime.learnChoice(segR, chosen);
  insertEditorText(st, chosen);

  int rem = st->romaji.kanaLen - slen;
  if (rem > 0) {
    memmove(st->romaji.kana, st->romaji.kana + slen, static_cast<size_t>(rem + 1) * sizeof(wchar_t));
    st->romaji.kanaLen = rem;
    st->converting = false;
    st->candCount = 0;
    startEditorConversion(st);
  } else {
    st->romaji.reset();
    st->converting = false;
    st->candCount = 0;
    st->candSel = 0;
    st->candPageStart = 0;
    st->segLen = 0;
  }
}

static void commitEditorRawPreedit(EditorState * st) {
  st->romaji.flushPendingN();
  wchar_t disp[LiliumIME::kMaxReadingChars] = L"";
  st->romaji.getPreeditDisplay(disp, LiliumIME::kMaxReadingChars);
  if (disp[0]) insertEditorText(st, disp);
  st->romaji.reset();
  st->converting = false;
  st->candCount = 0;
}

static void commitEditorKatakana(EditorState * st) {
  st->romaji.flushPendingN();
  if (st->romaji.kanaLen > 0) {
    wchar_t kata[LiliumIME::kMaxReadingChars] = L"";
    LiliumIME::RomajiEngine::toKatakana(st->romaji.kana, kata, LiliumIME::kMaxReadingChars);
    insertEditorText(st, kata);
  }
  st->romaji.reset();
  st->converting = false;
  st->candCount = 0;
}

static wchar_t mapEditor12xMarkdown(WPARAM vkCode) {
  if (vkCode >= '0' && vkCode <= '9') return static_cast<wchar_t>(vkCode);
  switch (vkCode) {
    // Digits Q..P -> 1..0
    case 'Q': return L'1'; case 'W': return L'2'; case 'E': return L'3';
    case 'R': return L'4'; case 'T': return L'5'; case 'Y': return L'6';
    case 'U': return L'7'; case 'I': return L'8'; case 'O': return L'9';
    case 'P': return L'0';
    // Core Markdown + basic arithmetic symbols (A..G / H..L / Z..B / N..M..ー)
    case 'A': return L'#'; case 'S': return L'-'; case 'D': return L'*';
    case 'F': return L'/'; case 'G': return L'=';
    case 'H': return L'('; case 'J': return L')'; case 'K': return L'['; case 'L': return L']';
    case 'Z': return L'`'; case 'X': return L'>'; case 'C': return L'_'; case 'V': return L':'; case 'B': return L'!';
    case 'N': return L','; case 'M': return L'.';
    case VK_OEM_MINUS: return L'?';
    case VK_OEM_PERIOD: return L'.';
    case VK_OEM_COMMA: return L',';
    case VK_OEM_2: return L'/';
    case VK_OEM_4: return L'[';
    case VK_OEM_6: return L']';
    case VK_OEM_1: return L':';
    case VK_OEM_3: return L'`';
    default: return L'\0';
  }
}

// =============================================================================
// Rendering: List / Viewer / Edit (with LiliumCalc-style Keycap Right Sidebar)
// =============================================================================

static void renderListScreen(LiliumUI::Canvas & c, const EditorState * st) {
  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
  wchar_t badge[32];
  _snwprintf(badge, 32, L"%d件", st->fileCount);
  LiliumUI::renderTopHeader(c, L"LILIUM EDITOR (メモ一覧)", badge);

  const int listX = 12, listY = 52, listW = LiliumUI::kMainW - 24, rowH = 58, visRows = 6;
  c.drawBox(listX, listY, listW, visRows * rowH + 8, LiliumUI::kColWhite, LiliumUI::kColBorder);

  for (int r = 0; r < visRows; ++r) {
    int idx = st->listScroll + r;
    int ry = listY + 4 + r * rowH;
    if (idx >= st->fileCount) break;
    const NoteEntry & ne = st->files[idx];
    bool sel = (idx == st->listSel);
    c.drawBox(listX + 6, ry, listW - 12, rowH - 4,
              sel ? LiliumUI::kColTealSoft : ((r % 2 == 1) ? LiliumUI::kColRowAlt : LiliumUI::kColWhite),
              sel ? LiliumUI::kColTeal : LiliumUI::kColBorder);
    if (sel) c.fillRect(listX + 6, ry, 5, rowH - 4, LiliumUI::kColTealDark);

    c.drawText(listX + 18, ry + 5, 380, 24,
               sel ? LiliumUI::kColTealDeep : LiliumUI::kColText, ne.name, 2, LiliumUI::Align::Left, true);

    wchar_t szStr[24];
    _snwprintf(szStr, 24, L"%lu B", static_cast<unsigned long>(ne.sizeBytes));
    c.drawText(listX + listW - 120, ry + 5, 96, 24, LiliumUI::kColMuted, szStr, 2, LiliumUI::Align::Right, true);

    c.drawText(listX + 18, ry + 30, listW - 40, 20, LiliumUI::kColMuted,
               ne.snippet[0] ? ne.snippet : L"(空のメモ)", 2, LiliumUI::Align::Left, true);
  }

  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  c.drawText(12, 440, LiliumUI::kMainW - 24, 40, LiliumUI::kColTextSub,
             L"[Ent]閲覧  [F1]新規(IME)  [F4]改名(IME)  [F5]削除  [Esc]終了",
             2, LiliumUI::Align::Left, true);

  LiliumUI::renderSidebar(c, L"メモ一覧", L"Markdownメモ",
                          L"【使い方】\n[↑↓]: 選択\n[Ent]: 閲覧へ\n閲覧画面で[F2]を押すと編集画面へ移動します。\n[F1]: 新規作成\n[F4]: 名前変更",
                          L"[Ent]閲覧 [F1]新規", L"[F4]改名 [F5]削除", L"戻　る", false);
}

static void renderViewerScreen(LiliumUI::Canvas & c, const EditorState * st) {
  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
  LiliumUI::renderTopHeader(c, st->activeName, L"MD閲覧");

  // Top-right Edit CTA button in viewer header
  c.drawBox(498, 7, 130, 28, LiliumUI::kColTealSoft, LiliumUI::kColWhite);
  c.drawText(498, 7, 130, 28, LiliumUI::kColTealDeep, L"[F2] 編集する", 2, LiliumUI::Align::Center, true);

  const int docX = 12, docY = 50, docW = LiliumUI::kMainW - 24, docH = 384;
  c.drawBox(docX, docY, docW, docH, LiliumUI::kColWhite, LiliumUI::kColBorder);

  const int lineH = 22;
  const int visLines = (docH - 12) / lineH; // 16 lines
  for (int r = 0; r < visLines; ++r) {
    int idx = st->viewScroll + r;
    if (idx >= st->viewLineCount) break;
    const ViewerLine & vl = st->viewLines[idx];
    int ry = docY + 8 + r * lineH;
    int tx = docX + 14 + vl.indentPx;

    switch (vl.kind) {
      case MdLineKind::H1:
        c.fillRect(docX + 10, ry - 1, docW - 28, lineH - 2, LiliumUI::kColTealHeader);
        c.fillRect(docX + 10, ry + lineH - 3, docW - 28, 2, LiliumUI::kColTeal);
        c.drawText(tx, ry, docW - 36, lineH - 2, LiliumUI::kColTealDeep, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      case MdLineKind::H2:
        c.fillRect(docX + 12, ry + 2, 4, lineH - 6, LiliumUI::kColTeal);
        c.drawText(docX + 22, ry, docW - 36, lineH, LiliumUI::kColTealDark, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      case MdLineKind::H3:
        c.drawText(docX + 14, ry, 20, lineH, LiliumUI::kColTeal, vl.prefix, 2, LiliumUI::Align::Left, true);
        c.drawText(docX + 34, ry, docW - 50, lineH, LiliumUI::kColCharcoal, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      case MdLineKind::CheckUnchecked:
      case MdLineKind::CheckChecked: {
        bool chk = (vl.kind == MdLineKind::CheckChecked);
        c.drawBox(docX + 16, ry + 4, 14, 14,
                  chk ? LiliumUI::kColTeal : LiliumUI::kColWhite, LiliumUI::kColTealDark);
        if (chk) c.fillRect(docX + 19, ry + 7, 8, 8, LiliumUI::kColWhite);
        c.drawText(tx, ry, docW - 50, lineH,
                   chk ? LiliumUI::kColMuted : LiliumUI::kColText, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      }
      case MdLineKind::Bullet:
      case MdLineKind::Numbered:
        if (vl.prefix[0]) {
          c.drawText(docX + 14, ry, 28, lineH, LiliumUI::kColTealDark, vl.prefix, 2, LiliumUI::Align::Left, true);
        }
        c.drawText(tx, ry, docW - 44, lineH, LiliumUI::kColText, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      case MdLineKind::Quote:
        c.fillRect(docX + 14, ry, docW - 32, lineH, 0xF3F7FA);
        c.fillRect(docX + 14, ry, 4, lineH, LiliumUI::kColTealBorder);
        c.drawText(tx + 8, ry, docW - 50, lineH, LiliumUI::kColMuted, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      case MdLineKind::Code:
        c.fillRect(docX + 14, ry, docW - 32, lineH, 0xEDF1F5);
        c.drawText(tx + 6, ry, docW - 48, lineH, LiliumUI::kColCharcoal, vl.text, 2, LiliumUI::Align::Left, true);
        break;
      case MdLineKind::Rule:
        c.fillRect(docX + 16, ry + lineH / 2, docW - 36, 2, LiliumUI::kColBorderStrong);
        break;
      case MdLineKind::Blank:
        break;
      default:
        c.drawText(tx, ry, docW - 36, lineH, LiliumUI::kColText, vl.text, 2, LiliumUI::Align::Left, true);
        break;
    }
  }

  // Right edge scrollbar inside doc box
  if (st->viewLineCount > visLines) {
    int trackH = docH - 16;
    int barH = (trackH * visLines) / st->viewLineCount;
    if (barH < 24) barH = 24;
    int barY = docY + 8 + ((trackH - barH) * st->viewScroll) / (st->viewLineCount - visLines);
    c.fillRect(docX + docW - 8, docY + 8, 4, trackH, LiliumUI::kColSidebar);
    c.fillRect(docX + docW - 8, barY, 4, barH, LiliumUI::kColTeal);
  }

  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  c.drawText(12, 440, LiliumUI::kMainW - 24, 40, LiliumUI::kColTextSub,
             L"[↑↓]スクロール  [PgUp/Dn]頁  [F2]編集画面へ  [Esc]一覧へ戻る",
             2, LiliumUI::Align::Left, true);

  LiliumUI::renderSidebar(c, L"MD閲覧", L"ビューワ操作",
                          L"【閲覧ガイド】\n見出し(#), リスト(-), チェック([ ]), 引用(>), コードを自動整形表示します。\n\n[F2]: 編集画面へ\n[↑↓]: スクロール\n[Esc]: 一覧へ",
                          L"[F2] 編集画面へ", L"[↑↓] スクロール", L"一覧へ", false);
}

static void renderEditorSidebarKeyGrid(LiliumUI::Canvas & c, int mode) {
  // Mirrors display.cpp's iconic Brain right-sidebar keyboard cheat-sheet!
  const int sx = LiliumUI::kSidebarX + 4; // 644
  const int sw = LiliumUI::kSidebarW - 8; // 152

  c.fillRect(LiliumUI::kSidebarX, 0, LiliumUI::kSidebarW, LiliumUI::kScreenH, LiliumUI::kColSidebar);
  c.fillRect(LiliumUI::kSidebarX, 0, 2, LiliumUI::kScreenH, LiliumUI::kColBorder);

  const wchar_t * modeTitle = L"モード: あ";
  uint32_t modeCol = LiliumUI::kColTealDeep;
  const wchar_t * modeHint = L"F2:切替 Spc:変換";
  if (mode == 1) {
    modeTitle = L"モード: abc";
    modeCol = 0x914E18;
    modeHint = L"F2:切替 Ent:改行";
  } else if (mode == 2) {
    modeTitle = L"モード: ABC";
    modeCol = 0xA03050;
    modeHint = L"F2:切替 Ent:改行";
  } else if (mode == 3) {
    modeTitle = L"モード: 12x記号";
    modeCol = LiliumUI::kColTealDark;
    modeHint = L"F2:切替 MD記号可";
  }

  // Top mode box (y=6..60)
  c.drawBox(sx, 6, sw, 54, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
  c.drawText(sx + 4, 8, sw - 8, 22, modeCol, modeTitle, 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 4, 33, sw - 8, 20, LiliumUI::kColTextSub, modeHint, 2, LiliumUI::Align::Left, true);

  // Function keys summary (y=66..126)
  c.drawBox(sx, 66, sw, 58, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(sx + 4, 70, sw - 8, 17, LiliumUI::kColText, L"F1:保存 F3:閲覧", 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 4, 88, sw - 8, 17, LiliumUI::kColTextSub, L"F7:カナ Spc:変換", 2, LiliumUI::Align::Left, true);
  c.drawText(sx + 4, 106, sw - 8, 17, LiliumUI::kColMuted, L"PgUp:改行 Ent:確定", 1, LiliumUI::Align::Left, true);

  struct KeySpec { const wchar_t * k; const wchar_t * sym; const wchar_t * lo; const wchar_t * up; };
  static constexpr KeySpec kLeft[3][5] = {
    { {L"Q",L"1",L"q",L"Q"}, {L"W",L"2",L"w",L"W"}, {L"E",L"3",L"e",L"E"}, {L"R",L"4",L"r",L"R"}, {L"T",L"5",L"t",L"T"} },
    { {L"A",L"#",L"a",L"A"}, {L"S",L"-",L"s",L"S"}, {L"D",L"*",L"d",L"D"}, {L"F",L"/",L"f",L"F"}, {L"G",L"=",L"g",L"G"} },
    { {L"Z",L"`",L"z",L"Z"}, {L"X",L">",L"x",L"X"}, {L"C",L"_",L"c",L"C"}, {L"V",L":",L"v",L"V"}, {L"B",L"!",L"b",L"B"} }
  };
  static constexpr KeySpec kRight[3][5] = {
    { {L"Y",L"6",L"y",L"Y"}, {L"U",L"7",L"u",L"U"}, {L"I",L"8",L"i",L"I"}, {L"O",L"9",L"o",L"O"}, {L"P",L"0",L"p",L"P"} },
    { {L"H",L"(",L"h",L"H"}, {L"J",L")",L"j",L"J"}, {L"K",L"[",L"k",L"K"}, {L"L",L"]",L"l",L"L"}, {nullptr,nullptr,nullptr,nullptr} },
    { {L"N",L",",L"n",L"N"}, {L"M",L".",L"m",L"M"}, {L"ー",L"?",L"-",L"_"}, {nullptr,nullptr,nullptr,nullptr}, {nullptr,nullptr,nullptr,nullptr} }
  };

  auto drawGrid = [&](int topY, const wchar_t * title, const KeySpec grid[3][5]) {
    c.drawText(sx, topY, sw, 18, LiliumUI::kColTealDark, title, 2, LiliumUI::Align::Left, true);
    int gy = topY + 20;
    for (int r = 0; r < 3; ++r) {
      for (int col = 0; col < 5; ++col) {
        const KeySpec & ks = grid[r][col];
        if (!ks.k) continue;
        int kx = sx + col * 30;
        int ky = gy + r * 35;
        bool isDig = (mode == 3 && ks.sym[0] >= L'0' && ks.sym[0] <= L'9');
        uint32_t bg = isDig ? 0xEEF6FB : LiliumUI::kColWhite;
        uint32_t bd = isDig ? 0x8CB6D4 : LiliumUI::kColBorderStrong;
        c.drawBox(kx, ky, 29, 33, bg, bd);
        c.drawText(kx + 2, ky + 1, 25, 11, LiliumUI::kColLightMuted, ks.k, 1, LiliumUI::Align::Left, true);
        const wchar_t * lbl = (mode == 3) ? ks.sym : ((mode == 2) ? ks.up : ks.lo);
        uint32_t lcol = (mode == 3) ? (isDig ? 0x104A80 : LiliumUI::kColTealDeep) : modeCol;
        c.drawText(kx + 1, ky + 13, 27, 18, lcol, lbl, 2, LiliumUI::Align::Center, true);
      }
    }
  };

  drawGrid(130, L"■左手 (Q-T/A-G)", kLeft);
  drawGrid(260, L"■右手 (Y-P/H-ー)", kRight);

  // Bottom Exit button matching shared chrome
  c.drawBox(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY - 32, LiliumUI::kExitBtnW, 24,
            LiliumUI::kColTeal, LiliumUI::kColTealDark);
  c.drawText(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY - 32, LiliumUI::kExitBtnW, 24,
             LiliumUI::kColWhite, L"[F3]保存＆閲覧", 2, LiliumUI::Align::Center, true);

  c.drawBox(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
            LiliumUI::kColBtnBg, LiliumUI::kColBtnBorder);
  c.drawText(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
             LiliumUI::kColText, L"保存して戻る", 2, LiliumUI::Align::Center, true);
}

static void renderEditScreen(LiliumUI::Canvas & c, EditorState * st) {
  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

  wchar_t badge[32] = L"編集";
  DWORD now = GetTickCount();
  if (st->savedToastTick != 0 && (now - st->savedToastTick) < 2500) {
    wcscpy(badge, L"保存済!");
  } else if (st->dirty) {
    wcscpy(badge, L"編集中*");
  }
  LiliumUI::renderTopHeader(c, st->activeName, badge);

  const int edX = 12, edY = 48, edW = LiliumUI::kMainW - 24, edH = 338;
  const int lineH = 22;
  const int visRows = (edH - 12) / lineH; // 14 lines
  ensureEditorCursorVisible(st, visRows);

  c.drawBox(edX, edY, edW, edH, LiliumUI::kColWhite, LiliumUI::kColTealDark);

  int cursorVisRow = findEditRowForCursor(st);
  for (int r = 0; r < visRows; ++r) {
    int idx = st->editScrollRow + r;
    if (idx >= st->editRowCount) break;
    const VisEditRow & row = st->editRows[idx];
    int ry = edY + 6 + r * lineH;
    int tx = edX + 10;

    if (idx == cursorVisRow) {
      c.fillRect(edX + 2, ry, edW - 12, lineH, 0xF5FBFA);
      int leftLen = st->cursorPos - row.startOffset;
      int rightLen = row.endOffset - st->cursorPos;
      wchar_t leftTxt[128] = L"", rightTxt[128] = L"";
      if (leftLen > 0) {
        if (leftLen > 127) leftLen = 127;
        wcsncpy(leftTxt, st->textBuf + row.startOffset, static_cast<size_t>(leftLen));
        leftTxt[leftLen] = L'\0';
        int lw = c.measureWidth(leftTxt, 2);
        c.drawText(tx, ry, edW - 24, lineH, LiliumUI::kColText, leftTxt, 2, LiliumUI::Align::Left, true);
        tx += lw;
      }

      wchar_t pre[64] = L"";
      if (st->converting && st->candCount > 0) {
        wcsncpy(pre, st->cands[st->candSel], 63);
      } else {
        st->romaji.getPreeditDisplay(pre, 64);
      }
      if (pre[0]) {
        int pw = c.measureWidth(pre, 2);
        c.fillRect(tx, ry + 1, pw + 2, lineH - 2, st->converting ? LiliumUI::kColTealSoft : 0xFFF3D6);
        c.drawText(tx, ry, pw + 10, lineH, st->converting ? LiliumUI::kColTealDeep : 0x8A4E00,
                   pre, 2, LiliumUI::Align::Left, true);
        c.fillRect(tx, ry + lineH - 2, pw, 2, LiliumUI::kColTealDark);
        tx += pw + 2;
      } else {
        c.fillRect(tx, ry + 2, 2, lineH - 4, LiliumUI::kColTealDark);
        tx += 3;
      }

      if (rightLen > 0) {
        if (rightLen > 127) rightLen = 127;
        wcsncpy(rightTxt, st->textBuf + st->cursorPos, static_cast<size_t>(rightLen));
        rightTxt[rightLen] = L'\0';
        c.drawText(tx, ry, (edX + edW - 14) - tx, lineH, LiliumUI::kColText,
                   rightTxt, 2, LiliumUI::Align::Left, true);
      }
    } else {
      int len = row.endOffset - row.startOffset;
      if (len > 0) {
        if (len > 127) len = 127;
        wchar_t lineTxt[128] = L"";
        wcsncpy(lineTxt, st->textBuf + row.startOffset, static_cast<size_t>(len));
        lineTxt[len] = L'\0';
        c.drawText(tx, ry, edW - 24, lineH, LiliumUI::kColText, lineTxt, 2, LiliumUI::Align::Left, true);
      }
    }
  }

  // IME Candidate Bar OR Markdown Quick-Insert Bar (y=390..434)
  c.drawBox(12, 390, LiliumUI::kMainW - 24, 44, LiliumUI::kColRowAlt, LiliumUI::kColBorder);
  if (st->converting && st->candCount > 0) {
    int cx = 18;
    for (int i = 0; i < 8 && (st->candPageStart + i) < st->candCount; ++i) {
      int idx = st->candPageStart + i;
      bool sel = (idx == st->candSel);
      wchar_t pill[64];
      _snwprintf(pill, 64, L"%d.%ls", i + 1, st->cands[idx]);
      pill[63] = L'\0';
      int pw = c.measureWidth(pill, 2) + 14;
      if (cx + pw > LiliumUI::kMainW - 20) break;
      c.drawBox(cx, 396, pw, 32,
                sel ? LiliumUI::kColTeal : LiliumUI::kColWhite,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorderStrong);
      c.drawText(cx, 396, pw, 32,
                 sel ? LiliumUI::kColWhite : LiliumUI::kColText,
                 pill, 2, LiliumUI::Align::Center, true);
      cx += pw + 6;
    }
  } else {
    static constexpr const wchar_t * kQuickBtns[8] = {
      L"あ/A", L"# ", L"- ", L"* ", L"`", L"> ", L"- [ ] ", L"---"
    };
    for (int b = 0; b < 8; ++b) {
      int bx = 18 + b * 76;
      c.drawBox(bx, 396, 70, 32,
                b == 0 ? LiliumUI::kColTealSoft : LiliumUI::kColWhite,
                b == 0 ? LiliumUI::kColTealDark : LiliumUI::kColBorderStrong);
      c.drawText(bx, 396, 70, 32,
                 b == 0 ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 kQuickBtns[b], 2, LiliumUI::Align::Center, true);
    }
  }

  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  c.drawText(12, 440, LiliumUI::kMainW - 24, 40, LiliumUI::kColTextSub,
             L"[F2]入力切替 [Spc]漢字変換 [1-8/←→]候補 [F1]保存 [F3]プレビュー",
             2, LiliumUI::Align::Left, true);

  renderEditorSidebarKeyGrid(c, st->inputMode);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  EditorState * st = static_cast<EditorState *>(userData);
  if (st->screen == Screen::List) renderListScreen(c, st);
  else if (st->screen == Screen::Viewer) renderViewerScreen(c, st);
  else renderEditScreen(c, st);

  if (st->modal == ModalKind::TextInput) {
    st->inputModal.render(c, true);
  } else if (st->modal == ModalKind::DeleteConfirm && st->fileCount > 0) {
    static constexpr LiliumUI::ModalButton kDelBtns[2] = {
      { L"削除する", true }, { L"キャンセル", false }
    };
    LiliumUI::renderConfirmModal(c, L"【 メモ削除 】",
                                 st->files[st->listSel].name,
                                 L"選択中のメモを削除しますか？",
                                 kDelBtns, 2, st->deleteFocus,
                                 L"[←→]選択 [Enter]実行 [Esc]中止");
  }
}

static void handleModalConfirm(EditorState * st) {
  const wchar_t * rawName = st->inputModal.text();
  if (!rawName || !rawName[0]) return;

  wchar_t base[64] = L"";
  wcsncpy(base, rawName, 56);
  base[56] = L'\0';
  // Strip unsafe path separators
  for (int i = 0; base[i] != L'\0'; ++i) {
    if (base[i] == L'\\' || base[i] == L'/' || base[i] == L':') base[i] = L'_';
  }
  wchar_t fileName[64] = L"";
  if (hasNoteSuffix(base)) {
    wcsncpy(fileName, base, 63);
  } else {
    _snwprintf(fileName, 64, L"%ls.md", base);
  }
  fileName[63] = L'\0';

  if (st->modalForRename && st->fileCount > 0) {
    wchar_t oldPath[MAX_PATH] = L"", newPath[MAX_PATH] = L"";
    LiliumUI::joinPath(st->notesDir, st->files[st->listSel].name, oldPath, MAX_PATH);
    LiliumUI::joinPath(st->notesDir, fileName, newPath, MAX_PATH);
    if (_wcsicmp(oldPath, newPath) != 0) {
      MoveFileW(oldPath, newPath);
      scanNoteFiles(st);
      for (int i = 0; i < st->fileCount; ++i) {
        if (_wcsicmp(st->files[i].name, fileName) == 0) { st->listSel = i; break; }
      }
    }
  } else {
    // New note -> create and enter Edit mode right away!
    wcsncpy(st->activeName, fileName, 63);
    st->activeName[63] = L'\0';
    st->textBuf[0] = L'\0';
    st->textLen = 0;
    st->cursorPos = 0;
    st->dirty = false;
    st->romaji.reset();
    st->converting = false;
    st->inputMode = 0;
    saveNoteContent(st);
    scanNoteFiles(st);
    st->screen = Screen::Edit;
  }
}

static void onKeyDown(WPARAM vkCode, bool isShift, void * userData) {
  EditorState * st = static_cast<EditorState *>(userData);

  if (st->modal == ModalKind::TextInput) {
    bool confirmed = false, closed = false;
    st->inputModal.handleKey(vkCode, isShift, confirmed, closed);
    if (confirmed) handleModalConfirm(st);
    if (closed) st->modal = ModalKind::None;
    st->window.requestRepaint(true);
    return;
  }

  if (st->modal == ModalKind::DeleteConfirm) {
    if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
      st->deleteFocus = (st->deleteFocus == 0) ? 1 : 0;
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      if (st->deleteFocus == 0 && st->fileCount > 0) {
        wchar_t full[MAX_PATH] = L"";
        LiliumUI::joinPath(st->notesDir, st->files[st->listSel].name, full, MAX_PATH);
        DeleteFileW(full);
        scanNoteFiles(st);
      }
      st->modal = ModalKind::None;
    } else if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->modal = ModalKind::None;
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_HOME || vkCode == VK_F10) {
    if (st->screen == Screen::Edit) {
      if (st->converting) commitEditorCandidate(st);
      else if (st->romaji.hasContent()) commitEditorRawPreedit(st);
      saveNoteContent(st);
    }
    st->window.requestQuit();
    return;
  }

  if (st->screen == Screen::List) {
    if (vkCode == VK_UP && st->fileCount > 0) {
      st->listSel = (st->listSel + st->fileCount - 1) % st->fileCount;
      if (st->listSel < st->listScroll) st->listScroll = st->listSel;
      if (st->listSel >= st->listScroll + 6) st->listScroll = st->listSel - 5;
    } else if (vkCode == VK_DOWN && st->fileCount > 0) {
      st->listSel = (st->listSel + 1) % st->fileCount;
      if (st->listSel < st->listScroll) st->listScroll = st->listSel;
      if (st->listSel >= st->listScroll + 6) st->listScroll = st->listSel - 5;
    } else if ((vkCode == VK_RETURN || vkCode == VK_F3) && st->fileCount > 0) {
      loadNoteContent(st, st->files[st->listSel].name);
      rebuildViewerLines(st);
      st->viewScroll = 0;
      st->screen = Screen::Viewer;
    } else if (vkCode == VK_F1) {
      st->modalForRename = false;
      st->modal = ModalKind::TextInput;
      st->inputModal.open(L"新規メモ作成", L"ファイル名を入力 (.md 自動付与):", L"", 40);
    } else if (vkCode == VK_F4 && st->fileCount > 0) {
      wchar_t stem[64] = L"";
      stripNoteSuffix(st->files[st->listSel].name, stem, 64);
      st->modalForRename = true;
      st->modal = ModalKind::TextInput;
      st->inputModal.open(L"メモ名変更", L"新しい名前を入力:", stem, 40);
    } else if ((vkCode == VK_F5 || vkCode == VK_DELETE) && st->fileCount > 0) {
      st->deleteFocus = 1;
      st->modal = ModalKind::DeleteConfirm;
    } else if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->window.requestQuit();
      return;
    }
    st->window.requestRepaint(true);
    return;
  }

  if (st->screen == Screen::Viewer) {
    const int visLines = (384 - 12) / 22;
    int maxSc = (st->viewLineCount > visLines) ? (st->viewLineCount - visLines) : 0;
    if (vkCode == VK_UP) {
      if (st->viewScroll > 0) --st->viewScroll;
    } else if (vkCode == VK_DOWN) {
      if (st->viewScroll < maxSc) ++st->viewScroll;
    } else if (vkCode == VK_PRIOR) {
      st->viewScroll -= visLines;
      if (st->viewScroll < 0) st->viewScroll = 0;
    } else if (vkCode == VK_NEXT) {
      st->viewScroll += visLines;
      if (st->viewScroll > maxSc) st->viewScroll = maxSc;
    } else if (vkCode == VK_F2) {
      st->romaji.reset();
      st->converting = false;
      st->candCount = 0;
      st->editScrollRow = 0;
      st->screen = Screen::Edit;
    } else if (vkCode == VK_F1 || vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      scanNoteFiles(st);
      st->screen = Screen::List;
    }
    st->window.requestRepaint(true);
    return;
  }

  // Screen::Edit
  if (vkCode == VK_F2) {
    if (st->converting) commitEditorCandidate(st);
    else if (st->romaji.hasContent()) commitEditorRawPreedit(st);
    st->inputMode = (st->inputMode + 1) % 4;
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_F1) {
    if (st->converting) commitEditorCandidate(st);
    else if (st->romaji.hasContent()) commitEditorRawPreedit(st);
    saveNoteContent(st);
    scanNoteFiles(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_F3) {
    if (st->converting) commitEditorCandidate(st);
    else if (st->romaji.hasContent()) commitEditorRawPreedit(st);
    saveNoteContent(st);
    rebuildViewerLines(st);
    st->screen = Screen::Viewer;
    st->window.requestRepaint(true);
    return;
  }

  if (st->converting) {
    if (vkCode == VK_SPACE || vkCode == VK_RIGHT || vkCode == VK_DOWN) {
      st->candSel = (st->candSel + 1) % st->candCount;
      st->candPageStart = (st->candSel / 8) * 8;
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_LEFT || vkCode == VK_UP) {
      st->candSel = (st->candSel + st->candCount - 1) % st->candCount;
      st->candPageStart = (st->candSel / 8) * 8;
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode >= '1' && vkCode <= '8') {
      int pick = st->candPageStart + static_cast<int>(vkCode - '1');
      if (pick < st->candCount) {
        st->candSel = pick;
        commitEditorCandidate(st);
      }
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_F7) {
      commitEditorKatakana(st);
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_RETURN) {
      commitEditorCandidate(st);
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->converting = false;
      st->candCount = 0;
      st->window.requestRepaint(true);
      return;
    }
    commitEditorCandidate(st);
  }

  if (vkCode == VK_ESCAPE) {
    if (st->romaji.hasContent()) {
      st->romaji.reset();
    } else {
      saveNoteContent(st);
      rebuildViewerLines(st);
      st->screen = Screen::Viewer;
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_BACK) {
    if (!st->romaji.backspace()) backspaceEditor(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_DELETE) {
    if (st->romaji.hasContent()) st->romaji.reset();
    else deleteEditor(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_F7 && st->romaji.hasContent()) {
    commitEditorKatakana(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_RETURN) {
    if (st->romaji.hasContent()) commitEditorRawPreedit(st);
    else insertEditorText(st, L"\n");
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_PRIOR) { // PgUp convenient newline shortcut on Brain
    if (st->romaji.hasContent()) commitEditorRawPreedit(st);
    insertEditorText(st, L"\n");
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_SPACE) {
    if (st->inputMode == 0 && st->romaji.hasContent()) {
      startEditorConversion(st);
    } else {
      insertEditorText(st, L" ");
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_LEFT) {
    if (!st->romaji.hasContent() && st->cursorPos > 0) --st->cursorPos;
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_RIGHT) {
    if (!st->romaji.hasContent() && st->cursorPos < st->textLen) ++st->cursorPos;
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_UP) {
    if (st->romaji.hasContent()) return;
    rebuildEditWrapRows(st, LiliumUI::kMainW - 44);
    int r = findEditRowForCursor(st);
    if (r > 0) {
      int col = st->cursorPos - st->editRows[r].startOffset;
      int prevLen = st->editRows[r - 1].endOffset - st->editRows[r - 1].startOffset;
      st->cursorPos = st->editRows[r - 1].startOffset + (col < prevLen ? col : prevLen);
    }
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_DOWN) {
    if (st->romaji.hasContent()) return;
    rebuildEditWrapRows(st, LiliumUI::kMainW - 44);
    int r = findEditRowForCursor(st);
    if (r + 1 < st->editRowCount) {
      int col = st->cursorPos - st->editRows[r].startOffset;
      int nextLen = st->editRows[r + 1].endOffset - st->editRows[r + 1].startOffset;
      st->cursorPos = st->editRows[r + 1].startOffset + (col < nextLen ? col : nextLen);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (st->inputMode == 0) {
    if (vkCode >= 'A' && vkCode <= 'Z') {
      st->romaji.feedChar(static_cast<char>(vkCode - 'A' + 'a'));
    } else if (vkCode == VK_OEM_MINUS) {
      st->romaji.feedChar('-');
    } else if (vkCode == VK_OEM_PERIOD) {
      st->romaji.feedChar('.');
    } else if (vkCode == VK_OEM_COMMA) {
      st->romaji.feedChar(',');
    } else if (vkCode == VK_OEM_4) {
      st->romaji.feedChar('[');
    } else if (vkCode == VK_OEM_6) {
      st->romaji.feedChar(']');
    } else if (vkCode >= '0' && vkCode <= '9') {
      st->romaji.feedChar(static_cast<char>(vkCode));
    }
  } else if (st->inputMode == 1 || st->inputMode == 2) {
    if (vkCode >= 'A' && vkCode <= 'Z') {
      bool upper = (st->inputMode == 2) ^ isShift;
      wchar_t ch[2] = { static_cast<wchar_t>(upper ? vkCode : (vkCode - 'A' + 'a')), L'\0' };
      insertEditorText(st, ch);
    } else if (vkCode >= '0' && vkCode <= '9') {
      wchar_t ch[2] = { static_cast<wchar_t>(vkCode), L'\0' };
      insertEditorText(st, ch);
    } else if (vkCode == VK_OEM_MINUS) {
      insertEditorText(st, isShift ? L"_" : L"-");
    } else if (vkCode == VK_OEM_PERIOD) {
      insertEditorText(st, L".");
    } else if (vkCode == VK_OEM_COMMA) {
      insertEditorText(st, L",");
    } else if (vkCode == VK_OEM_2) {
      insertEditorText(st, isShift ? L"?" : L"/");
    }
  } else {
    wchar_t sym = mapEditor12xMarkdown(vkCode);
    if (sym) {
      wchar_t ch[2] = { sym, L'\0' };
      insertEditorText(st, ch);
    }
  }
  st->window.requestRepaint(true);
}

static void onClick(int x, int y, void * userData) {
  EditorState * st = static_cast<EditorState *>(userData);

  if (st->modal == ModalKind::TextInput) {
    bool confirmed = false, closed = false;
    st->inputModal.handleClick(x, y, confirmed, closed);
    if (confirmed) handleModalConfirm(st);
    if (closed) st->modal = ModalKind::None;
    st->window.requestRepaint(true);
    return;
  }

  if (st->modal == ModalKind::DeleteConfirm) {
    const int dlgX = (LiliumUI::kScreenW - 500) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int bx0 = dlgX + (500 - 342) / 2;
    const int bx1 = bx0 + 182;
    if (y >= by && y < by + 42) {
      if (x >= bx0 && x < bx0 + 160) onKeyDown(VK_RETURN, false, userData);
      else if (x >= bx1 && x < bx1 + 160) onKeyDown(VK_ESCAPE, false, userData);
    }
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    if (st->screen == Screen::Edit) onKeyDown(VK_ESCAPE, false, userData);
    else if (st->screen == Screen::Viewer) onKeyDown(VK_F1, false, userData);
    else st->window.requestQuit();
    return;
  }

  if (st->screen == Screen::Viewer) {
    if (x >= 498 && x < 628 && y >= 7 && y < 35) {
      onKeyDown(VK_F2, false, userData);
    }
    return;
  }

  if (st->screen == Screen::Edit) {
    if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
        y >= LiliumUI::kExitBtnY - 32 && y < LiliumUI::kExitBtnY - 8) {
      onKeyDown(VK_F3, false, userData);
      return;
    }
    if (y >= 396 && y < 428 && !st->converting) {
      static constexpr const wchar_t * kSnippets[8] = {
        nullptr, L"# ", L"- ", L"* ", L"`", L"> ", L"- [ ] ", L"---"
      };
      for (int b = 0; b < 8; ++b) {
        int bx = 18 + b * 76;
        if (x >= bx && x < bx + 70) {
          if (b == 0) onKeyDown(VK_F2, false, userData);
          else {
            if (st->romaji.hasContent()) commitEditorRawPreedit(st);
            insertEditorText(st, kSnippets[b]);
            st->window.requestRepaint(true);
          }
          return;
        }
      }
    }
    return;
  }

  // List click
  const int listX = 12, listY = 52, listW = LiliumUI::kMainW - 24, rowH = 58;
  for (int r = 0; r < 6; ++r) {
    int idx = st->listScroll + r;
    if (idx >= st->fileCount) break;
    int ry = listY + 4 + r * rowH;
    if (x >= listX + 6 && x < listX + listW - 6 && y >= ry && y < ry + rowH - 4) {
      if (st->listSel == idx) {
        onKeyDown(VK_RETURN, false, userData);
      } else {
        st->listSel = idx;
        st->window.requestRepaint(true);
      }
      return;
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  EditorState * st = static_cast<EditorState *>(userData);
  if (st->modal != ModalKind::None || st->screen == Screen::Edit) return;
  LiliumUI::renderSidebarStatus(c, (st->screen == Screen::Viewer) ? L"MD閲覧" : L"メモ一覧");
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * rawMem = calloc(1, sizeof(EditorState));
  if (!rawMem) return 1;
  EditorState * state = new (rawMem) EditorState();
  state->textBuf = static_cast<wchar_t *>(calloc(kMaxTextChars, sizeof(wchar_t)));
  state->viewLines = static_cast<ViewerLine *>(calloc(kMaxViewerLines, sizeof(ViewerLine)));
  state->editRows = static_cast<VisEditRow *>(calloc(kMaxEditVisLines, sizeof(VisEditRow)));
  if (!state->textBuf || !state->viewLines || !state->editRows) {
    free(state->editRows);
    free(state->viewLines);
    free(state->textBuf);
    state->~EditorState();
    free(rawMem);
    return 1;
  }

  state->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();
  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  LiliumUI::joinPath(exeDir, L"notes", state->notesDir, MAX_PATH);
  state->ime.connectOrLaunch(true);
  scanNoteFiles(state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;

  int rc = 1;
  if (state->window.create(L"LiliumEditorWinClass",
                           L"Lilium Editor (Markdown / Lilium IME)",
                           cb, state, state->manageTaskBar)) {
    rc = state->window.runLoop();
  }

  free(state->editRows);
  free(state->viewLines);
  free(state->textBuf);
  state->~EditorState();
  free(rawMem);
  return rc;
}
