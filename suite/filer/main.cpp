#include "../common/lilium_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace {

constexpr int kMaxEntries = 256;
constexpr int kVisibleRows = 9;
constexpr int kViewerLinesMax = 1024;
constexpr int kViewerVisibleLines = 15;
constexpr DWORD kMaxTextLoadBytes = 64 * 1024;
constexpr int kHexBytesPerRow = 12;
constexpr int kHexRowsPerPage = 15;

struct FileEntry {
  wchar_t name[MAX_PATH];
  bool isDir;
  bool isParent;
  bool isDriveRoot;
  uint64_t sizeBytes;
};

enum class ModalMode {
  None = 0,
  DeleteConfirm,
  ProtectedWarn,
  MessageBox
};

struct FilerState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;

  wchar_t curDir[MAX_PATH] = L"";
  bool atDriveList = false; // Host PC drive list view; WinCE uses "\" root directly
  FileEntry entries[kMaxEntries] = {};
  int entryCount = 0;
  int sel = 0;
  int prevSel = 0;
  int scroll = 0;
  int prevScroll = 0;

  // Clipboard copy mark
  wchar_t copySrcPath[MAX_PATH] = L"";
  wchar_t copySrcName[128] = L"";

  // Built-in Viewer state
  bool viewing = false;
  bool viewHexMode = false;
  wchar_t viewPath[MAX_PATH] = L"";
  wchar_t viewName[128] = L"";
  uint64_t viewFileSize = 0;
  bool viewTruncated = false;

  wchar_t * textBuf = nullptr;
  const wchar_t * linePtrs[kViewerLinesMax] = {};
  int lineLens[kViewerLinesMax] = {};
  int lineCount = 0;
  int viewLineScroll = 0;
  uint64_t hexOffset = 0;

  ModalMode modal = ModalMode::None;
  int modalFocus = 1;
  wchar_t msgTitle[64] = L"";
  wchar_t msgLine1[96] = L"";
  wchar_t msgLine2[96] = L"";
};

static bool containsNoCase(const wchar_t * hay, const wchar_t * needle) {
  if (!hay || !needle || !needle[0]) return false;
  size_t hLen = wcslen(hay), nLen = wcslen(needle);
  if (nLen > hLen) return false;
  for (size_t i = 0; i + nLen <= hLen; ++i) {
    if (_wcsnicmp(hay + i, needle, nLen) == 0) return true;
  }
  return false;
}

static void formatSizeStr(const FileEntry & e, wchar_t * out, size_t maxLen) {
  if (e.isParent) {
    wcsncpy(out, L"親へ", maxLen - 1);
  } else if (e.isDriveRoot) {
    wcsncpy(out, L"DRIVE", maxLen - 1);
  } else if (e.isDir) {
    wcsncpy(out, L"<DIR>", maxLen - 1);
  } else if (e.sizeBytes < 1024ULL) {
    _snwprintf(out, maxLen, L"%lu B", static_cast<unsigned long>(e.sizeBytes));
  } else if (e.sizeBytes < 1024ULL * 1024ULL) {
    _snwprintf(out, maxLen, L"%lu KB", static_cast<unsigned long>((e.sizeBytes + 512ULL) / 1024ULL));
  } else {
    DWORD mb10 = static_cast<DWORD>((e.sizeBytes * 10ULL) / (1024ULL * 1024ULL));
    _snwprintf(out, maxLen, L"%lu.%lu MB", static_cast<unsigned long>(mb10 / 10),
               static_cast<unsigned long>(mb10 % 10));
  }
  out[maxLen - 1] = L'\0';
}

static void adjustListScroll(int sel, int count, int & scroll) {
  if (sel < scroll) scroll = sel;
  if (sel >= scroll + kVisibleRows) scroll = sel - kVisibleRows + 1;
  int maxScroll = (count > kVisibleRows) ? (count - kVisibleRows) : 0;
  if (scroll > maxScroll) scroll = maxScroll;
  if (scroll < 0) scroll = 0;
}

static void buildFullEntryPath(const FilerState * st, const FileEntry & e, wchar_t * out, size_t maxLen) {
  if (e.isDriveRoot) {
    wcsncpy(out, e.name, maxLen - 1);
    out[maxLen - 1] = L'\0';
    return;
  }
  size_t dirLen = wcslen(st->curDir);
  if (dirLen == 0 || (dirLen == 1 && st->curDir[0] == L'\\')) {
    _snwprintf(out, maxLen, L"\\%ls", e.name);
  } else if (st->curDir[dirLen - 1] == L'\\') {
    _snwprintf(out, maxLen, L"%ls%ls", st->curDir, e.name);
  } else {
    _snwprintf(out, maxLen, L"%ls\\%ls", st->curDir, e.name);
  }
  out[maxLen - 1] = L'\0';
}

static void sortEntries(FileEntry * arr, int startIdx, int count) {
  for (int i = startIdx; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      bool swapIt = false;
      if (arr[j].isDir != arr[i].isDir) {
        swapIt = arr[j].isDir;
      } else if (_wcsicmp(arr[j].name, arr[i].name) < 0) {
        swapIt = true;
      }
      if (swapIt) {
        FileEntry tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
      }
    }
  }
}

static void loadDirectory(FilerState * st, const wchar_t * targetDir) {
  st->entryCount = 0;
  st->sel = 0;
  st->prevSel = 0;
  st->scroll = 0;
  st->prevScroll = 0;

#if !defined(_WIN32_WCE) && !defined(__MINGW32CE__)
  if (!targetDir || !targetDir[0]) {
    st->atDriveList = true;
    wcsncpy(st->curDir, L"[ドライブ一覧]", MAX_PATH - 1);
    st->curDir[MAX_PATH - 1] = L'\0';
    DWORD mask = GetLogicalDrives();
    for (int d = 0; d < 26 && st->entryCount < kMaxEntries; ++d) {
      if (mask & (1UL << d)) {
        FileEntry & fe = st->entries[st->entryCount++];
        _snwprintf(fe.name, MAX_PATH, L"%c:\\", static_cast<wchar_t>(L'A' + d));
        fe.name[MAX_PATH - 1] = L'\0';
        fe.isDir = true;
        fe.isParent = false;
        fe.isDriveRoot = true;
        fe.sizeBytes = 0;
      }
    }
    return;
  }
#endif

  st->atDriveList = false;
  wcsncpy(st->curDir, (targetDir && targetDir[0]) ? targetDir : L"\\", MAX_PATH - 1);
  st->curDir[MAX_PATH - 1] = L'\0';

  // Trim trailing slash unless root ("\" or "X:\")
  size_t dLen = wcslen(st->curDir);
  while (dLen > 1 && st->curDir[dLen - 1] == L'\\' &&
         !(dLen == 3 && st->curDir[1] == L':')) {
    st->curDir[--dLen] = L'\0';
  }

  bool isRoot = (dLen == 1 && st->curDir[0] == L'\\');
#if !defined(_WIN32_WCE) && !defined(__MINGW32CE__)
  // On desktop Windows, allow ".." from drive root (e.g. "C:\") to Drive List
  bool addParent = true;
  (void)isRoot;
#else
  bool addParent = !isRoot;
#endif

  if (addParent && st->entryCount < kMaxEntries) {
    FileEntry & p = st->entries[st->entryCount++];
    wcscpy(p.name, L"..");
    p.isDir = true;
    p.isParent = true;
    p.isDriveRoot = false;
    p.sizeBytes = 0;
  }

  int sortStart = st->entryCount;
  wchar_t pattern[MAX_PATH];
  if (dLen == 1 && st->curDir[0] == L'\\') {
    wcscpy(pattern, L"\\*");
  } else if (st->curDir[dLen - 1] == L'\\') {
    _snwprintf(pattern, MAX_PATH, L"%ls*", st->curDir);
  } else {
    _snwprintf(pattern, MAX_PATH, L"%ls\\*", st->curDir);
  }
  pattern[MAX_PATH - 1] = L'\0';

  WIN32_FIND_DATAW ffd = {};
  HANDLE hFind = FindFirstFileW(pattern, &ffd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
      if (st->entryCount >= kMaxEntries) break;
      FileEntry & fe = st->entries[st->entryCount++];
      wcsncpy(fe.name, ffd.cFileName, MAX_PATH - 1);
      fe.name[MAX_PATH - 1] = L'\0';
      fe.isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      fe.isParent = false;
      fe.isDriveRoot = false;
      fe.sizeBytes = (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32) |
                     static_cast<uint64_t>(ffd.nFileSizeLow);
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
  }

  sortEntries(st->entries, sortStart, st->entryCount);
}

static void navigateUp(FilerState * st) {
  if (st->atDriveList) return;
  size_t len = wcslen(st->curDir);
  if (len <= 1 && st->curDir[0] == L'\\') return;
#if !defined(_WIN32_WCE) && !defined(__MINGW32CE__)
  if (len == 3 && st->curDir[1] == L':' && st->curDir[2] == L'\\') {
    loadDirectory(st, L"");
    return;
  }
#endif
  wchar_t parent[MAX_PATH];
  wcsncpy(parent, st->curDir, MAX_PATH - 1);
  parent[MAX_PATH - 1] = L'\0';
  wchar_t * slash = wcsrchr(parent, L'\\');
  if (!slash || slash == parent) {
    loadDirectory(st, L"\\");
    return;
  }
  if (slash == parent + 2 && parent[1] == L':') {
    *(slash + 1) = L'\0'; // keep "C:\"
  } else {
    *slash = L'\0';
  }
  loadDirectory(st, parent);
}

static void freeViewerBuffer(FilerState * st) {
  if (st->textBuf) {
    free(st->textBuf);
    st->textBuf = nullptr;
  }
  st->lineCount = 0;
}

static void parseTextLines(FilerState * st) {
  st->lineCount = 0;
  if (!st->textBuf) return;

  const wchar_t * p = st->textBuf;
  while (*p && st->lineCount < kViewerLinesMax) {
    const wchar_t * lineStart = p;
    int col = 0;
    while (*p && *p != L'\n' && *p != L'\r') {
      int chUnits = (*p < 0x80) ? 1 : 2;
      if (col + chUnits > 72 && p > lineStart) {
        break;
      }
      col += chUnits;
      ++p;
    }
    st->linePtrs[st->lineCount] = lineStart;
    st->lineLens[st->lineCount] = static_cast<int>(p - lineStart);
    ++st->lineCount;

    if (*p == L'\r') ++p;
    if (*p == L'\n') ++p;
  }
}

static void openFileInViewer(FilerState * st, const FileEntry & e) {
  wchar_t fullPath[MAX_PATH];
  buildFullEntryPath(st, e, fullPath, MAX_PATH);

  HANDLE hFile = CreateFileW(fullPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    wcsncpy(st->msgTitle, L"【 読み込みエラー 】", 63);
    wcsncpy(st->msgLine1, L"ファイルを開けませんでした", 95);
    wcsncpy(st->msgLine2, e.name, 95);
    st->modal = ModalMode::MessageBox;
    st->modalFocus = 0;
    st->window.requestRepaint(true);
    return;
  }

  freeViewerBuffer(st);
  DWORD szHigh = 0;
  DWORD szLow = GetFileSize(hFile, &szHigh);
  uint64_t totalSz = (szLow == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
                     ? 0
                     : ((static_cast<uint64_t>(szHigh) << 32) | static_cast<uint64_t>(szLow));

  DWORD toRead = (totalSz > kMaxTextLoadBytes) ? kMaxTextLoadBytes : static_cast<DWORD>(totalSz);
  uint8_t * raw = static_cast<uint8_t *>(malloc(toRead + 4));
  DWORD actualRead = 0;
  if (raw && toRead > 0) {
    ReadFile(hFile, raw, toRead, &actualRead, NULL);
  }
  CloseHandle(hFile);

  wcsncpy(st->viewPath, fullPath, MAX_PATH - 1);
  st->viewPath[MAX_PATH - 1] = L'\0';
  wcsncpy(st->viewName, e.name, 127);
  st->viewName[127] = L'\0';
  st->viewFileSize = totalSz;
  st->viewTruncated = (totalSz > kMaxTextLoadBytes);
  st->viewLineScroll = 0;
  st->hexOffset = 0;

  bool looksBinary = false;
  bool isUtf16Le = (actualRead >= 2 && raw && raw[0] == 0xFF && raw[1] == 0xFE);
  if (!isUtf16Le && raw) {
    DWORD checkN = (actualRead > 1024) ? 1024 : actualRead;
    for (DWORD i = 0; i < checkN; ++i) {
      uint8_t b = raw[i];
      if (b == 0 || (b < 0x09) || (b > 0x0D && b < 0x20 && b != 0x1B)) {
        looksBinary = true;
        break;
      }
    }
  }

  if (raw) {
    raw[actualRead] = 0;
    raw[actualRead + 1] = 0;
    if (isUtf16Le) {
      DWORD wChars = (actualRead - 2) / 2;
      st->textBuf = static_cast<wchar_t *>(malloc((wChars + 1) * sizeof(wchar_t)));
      if (st->textBuf) {
        memcpy(st->textBuf, raw + 2, wChars * sizeof(wchar_t));
        st->textBuf[wChars] = L'\0';
      }
    } else {
      int needed = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char *>(raw),
                                       static_cast<int>(actualRead), NULL, 0);
      if (needed <= 0 && actualRead > 0) {
        needed = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char *>(raw),
                                     static_cast<int>(actualRead), NULL, 0);
        if (needed > 0) {
          st->textBuf = static_cast<wchar_t *>(malloc((needed + 1) * sizeof(wchar_t)));
          if (st->textBuf) {
            MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char *>(raw),
                                static_cast<int>(actualRead), st->textBuf, needed);
            st->textBuf[needed] = L'\0';
          }
        }
      } else if (needed > 0) {
        st->textBuf = static_cast<wchar_t *>(malloc((needed + 1) * sizeof(wchar_t)));
        if (st->textBuf) {
          MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char *>(raw),
                              static_cast<int>(actualRead), st->textBuf, needed);
          st->textBuf[needed] = L'\0';
        }
      }
    }
    free(raw);
  }

  parseTextLines(st);
  st->viewHexMode = looksBinary;
  st->viewing = true;
  st->window.requestRepaint(true);
}

static bool isProtectedFile(const wchar_t * fullPath, const wchar_t * name) {
  if (containsNoCase(fullPath, L"\\Windows")) return true;
  if (_wcsicmp(name, L"AppMain.exe") == 0 ||
      _wcsicmp(name, L"AppMain.cfg") == 0 ||
      _wcsicmp(name, L"AppMain_.exe") == 0 ||
      _wcsicmp(name, L"index.din") == 0) {
    return true;
  }
  return false;
}

static void executeDeleteSelected(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry & e = st->entries[st->sel];
  if (e.isParent || e.isDriveRoot) return;

  wchar_t fullPath[MAX_PATH];
  buildFullEntryPath(st, e, fullPath, MAX_PATH);
  BOOL ok = e.isDir ? RemoveDirectoryW(fullPath) : DeleteFileW(fullPath);

  if (ok) {
    int oldSel = st->sel;
    wchar_t curCopy[MAX_PATH];
    wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
    curCopy[MAX_PATH - 1] = L'\0';
    loadDirectory(st, curCopy);
    if (oldSel < st->entryCount) st->sel = oldSel;
    else if (st->entryCount > 0) st->sel = st->entryCount - 1;
    adjustListScroll(st->sel, st->entryCount, st->scroll);
    wcsncpy(st->msgTitle, L"【 削除完了 】", 63);
    wcsncpy(st->msgLine1, e.name, 95);
    wcsncpy(st->msgLine2, L"削除しました。", 95);
  } else {
    wcsncpy(st->msgTitle, L"【 削除失敗 】", 63);
    wcsncpy(st->msgLine1, e.name, 95);
    wcsncpy(st->msgLine2, e.isDir ? L"空でないフォルダは削除できません。" : L"使用中または保護されています。", 95);
  }
  st->msgTitle[63] = L'\0';
  st->msgLine1[95] = L'\0';
  st->msgLine2[95] = L'\0';
  st->modal = ModalMode::MessageBox;
  st->modalFocus = 0;
  st->window.requestRepaint(true);
}

static void executePasteCopy(FilerState * st) {
  if (st->atDriveList || !st->copySrcPath[0]) return;
  wchar_t dstPath[MAX_PATH];
  FileEntry dummy = {};
  wcsncpy(dummy.name, st->copySrcName, MAX_PATH - 1);
  buildFullEntryPath(st, dummy, dstPath, MAX_PATH);

  if (_wcsicmp(dstPath, st->copySrcPath) == 0) {
    // Append _copy suffix to allow in-same-folder duplication
    _snwprintf(dstPath, MAX_PATH, L"%ls\\copy_%ls", st->curDir, st->copySrcName);
    dstPath[MAX_PATH - 1] = L'\0';
  }

  BOOL ok = CopyFileW(st->copySrcPath, dstPath, FALSE);
  if (ok) {
    wchar_t curCopy[MAX_PATH];
    wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
    curCopy[MAX_PATH - 1] = L'\0';
    loadDirectory(st, curCopy);
    wcsncpy(st->msgTitle, L"【 コピー完了 】", 63);
    wcsncpy(st->msgLine1, st->copySrcName, 95);
    wcsncpy(st->msgLine2, L"現在のフォルダへコピーしました。", 95);
  } else {
    wcsncpy(st->msgTitle, L"【 コピー失敗 】", 63);
    wcsncpy(st->msgLine1, st->copySrcName, 95);
    wcsncpy(st->msgLine2, L"書き込み先や空き容量をご確認ください。", 95);
  }
  st->msgTitle[63] = L'\0';
  st->msgLine1[95] = L'\0';
  st->msgLine2[95] = L'\0';
  st->modal = ModalMode::MessageBox;
  st->modalFocus = 0;
  st->window.requestRepaint(true);
}

static void renderEntryRow(LiliumUI::Canvas & c, const FilerState * st, int r) {
  const int listX = 12, listY = 114, listW = LiliumUI::kMainW - 24, rowH = 34;
  int idx = st->scroll + r;
  int ry = listY + 4 + r * rowH;
  if (idx >= st->entryCount) {
    c.fillRect(listX + 4, ry, listW - 8, rowH - 2, LiliumUI::kColWhite);
    return;
  }

  const FileEntry & e = st->entries[idx];
  bool sel = (idx == st->sel);
  uint32_t rowBg = sel ? LiliumUI::kColTealSoft : ((r % 2 == 1) ? LiliumUI::kColRowAlt : LiliumUI::kColWhite);
  uint32_t rowBd = sel ? LiliumUI::kColTeal : rowBg;
  c.drawBox(listX + 4, ry, listW - 8, rowH - 2, rowBg, rowBd);

  if (e.isDir) {
    c.drawBox(listX + 10, ry + 5, 48, rowH - 12,
              e.isParent ? 0xEEF2F6 : LiliumUI::kColTealHeader,
              e.isParent ? LiliumUI::kColBorder : LiliumUI::kColTealBorder);
    c.drawText(listX + 10, ry + 5, 48, rowH - 12,
               e.isParent ? LiliumUI::kColMuted : LiliumUI::kColTealDeep,
               e.isParent ? L"UP" : L"DIR", 2, LiliumUI::Align::Center, true);
  } else {
    c.drawBox(listX + 10, ry + 5, 48, rowH - 12, 0xF4F6FA, LiliumUI::kColBtnBorder);
    c.drawText(listX + 10, ry + 5, 48, rowH - 12, LiliumUI::kColMuted, L"FILE", 2, LiliumUI::Align::Center, true);
  }

  c.drawText(listX + 66, ry, 420, rowH - 2, LiliumUI::kColText, e.name, 2, LiliumUI::Align::Left, true);

  wchar_t szStr[24];
  formatSizeStr(e, szStr, 24);
  c.drawText(listX + listW - 116, ry, 102, rowH - 2,
             e.isDir ? LiliumUI::kColTealDeep : LiliumUI::kColMuted,
             szStr, 2, LiliumUI::Align::Right, true);
}

static void renderViewerPanel(LiliumUI::Canvas & c, const FilerState * st) {
  // Path & mode strip (y=48..78)
  c.drawBox(12, 48, 428, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(20, 48, 412, 28, LiliumUI::kColText, st->viewName, 2, LiliumUI::Align::Left, true);

  c.drawBox(446, 48, 182, 28, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
  c.drawText(446, 48, 182, 28, LiliumUI::kColTealDeep,
             st->viewHexMode ? L"[F2] HEXダンプ" : L"[F2] テキスト",
             2, LiliumUI::Align::Center, true);

  // Content area (y=84..432)
  c.drawBox(12, 84, LiliumUI::kMainW - 24, 348, LiliumUI::kColWhite, LiliumUI::kColBorder);

  if (!st->viewHexMode) {
    if (st->lineCount == 0) {
      c.drawText(24, 220, LiliumUI::kMainW - 48, 40, LiliumUI::kColLightMuted,
                 L"(空のファイル、またはテキスト未検出)", 2, LiliumUI::Align::Center, true);
    } else {
      for (int i = 0; i < kViewerVisibleLines; ++i) {
        int li = st->viewLineScroll + i;
        if (li >= st->lineCount) break;
        wchar_t lineBuf[128];
        int copyLen = st->lineLens[li];
        if (copyLen > 127) copyLen = 127;
        wcsncpy(lineBuf, st->linePtrs[li], copyLen);
        lineBuf[copyLen] = L'\0';
        // Replace tabs with 2 spaces visually
        for (int k = 0; k < copyLen; ++k) {
          if (lineBuf[k] == L'\t') lineBuf[k] = L' ';
        }
        c.drawText(20, 92 + i * 22, LiliumUI::kMainW - 40, 20, LiliumUI::kColText,
                   lineBuf, 2, LiliumUI::Align::Left, true);
      }
    }
  } else {
    // Low-RAM streaming 168 bytes/page HEX dump
    HANDLE hFile = CreateFileW(st->viewPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    uint8_t pageBuf[kHexBytesPerRow * kHexRowsPerPage] = {};
    DWORD got = 0;
    if (hFile != INVALID_HANDLE_VALUE) {
      LONG hi = static_cast<LONG>(st->hexOffset >> 32);
      LONG lo = static_cast<LONG>(st->hexOffset & 0xFFFFFFFFULL);
      SetFilePointer(hFile, lo, &hi, FILE_BEGIN);
      ReadFile(hFile, pageBuf, sizeof(pageBuf), &got, NULL);
      CloseHandle(hFile);
    }

    if (got == 0) {
      c.drawText(24, 220, LiliumUI::kMainW - 48, 40, LiliumUI::kColLightMuted,
                 L"(データなし)", 2, LiliumUI::Align::Center, true);
    } else {
      for (int r = 0; r < kHexRowsPerPage; ++r) {
        DWORD rowOff = static_cast<DWORD>(r * kHexBytesPerRow);
        if (rowOff >= got) break;
        wchar_t hexRow[128];
        int pos = _snwprintf(hexRow, 128, L"%06lX:", static_cast<unsigned long>(st->hexOffset + rowOff));
        for (int b = 0; b < kHexBytesPerRow; ++b) {
          if (rowOff + b < got) {
            pos += _snwprintf(hexRow + pos, 128 - pos, L" %02X", pageBuf[rowOff + b]);
          } else {
            pos += _snwprintf(hexRow + pos, 128 - pos, L"   ");
          }
        }
        pos += _snwprintf(hexRow + pos, 128 - pos, L"  ");
        for (int b = 0; b < kHexBytesPerRow && rowOff + b < got; ++b) {
          uint8_t ch = pageBuf[rowOff + b];
          hexRow[pos++] = (ch >= 0x20 && ch <= 0x7E) ? static_cast<wchar_t>(ch) : L'.';
        }
        hexRow[pos] = L'\0';
        c.drawText(20, 92 + r * 22, LiliumUI::kMainW - 40, 20, LiliumUI::kColText,
                   hexRow, 2, LiliumUI::Align::Left, true);
      }
    }
  }
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);

  if (c.fullRedraw || st->modal != ModalMode::None || st->viewing) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

    wchar_t badge[48];
    if (st->viewing) {
      if (st->viewHexMode) {
        _snwprintf(badge, 48, L"HEX 0x%lX", static_cast<unsigned long>(st->hexOffset));
      } else {
        _snwprintf(badge, 48, L"行 %d/%d",
                   (st->lineCount > 0) ? (st->viewLineScroll + 1) : 0, st->lineCount);
      }
    } else {
      _snwprintf(badge, 48, L"%d 項目", st->entryCount);
    }
    badge[47] = L'\0';
    LiliumUI::renderTopHeader(c, L"LILIUM FILER", badge);

    if (st->viewing) {
      renderViewerPanel(c, st);
    } else {
      // Path bar (y=48)
      c.drawBox(12, 48, LiliumUI::kMainW - 24, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
      c.drawText(20, 48, LiliumUI::kMainW - 40, 28, LiliumUI::kColText,
                 st->curDir, 2, LiliumUI::Align::Left, true);

      // Quick action strip (y=82)
      c.drawBox(12, 82, 134, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
      c.drawText(12, 82, 134, 28, LiliumUI::kColTextSub, L"[F1]ルート", 2, LiliumUI::Align::Center, true);

      c.drawBox(152, 82, 142, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
      c.drawText(152, 82, 142, 28, LiliumUI::kColTextSub, L"[F2]Lilium", 2, LiliumUI::Align::Center, true);

      c.drawBox(300, 82, 160, 28, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
      c.drawText(300, 82, 160, 28, LiliumUI::kColTealDeep, L"[F8]コピー指定", 2, LiliumUI::Align::Center, true);

      bool hasClip = (st->copySrcPath[0] != L'\0' && !st->atDriveList);
      c.drawBox(466, 82, 162, 28,
                hasClip ? LiliumUI::kColWarnBg : 0xF0F3F7,
                hasClip ? LiliumUI::kColWarnBorder : LiliumUI::kColBorder);
      c.drawText(466, 82, 162, 28,
                 hasClip ? LiliumUI::kColWarnText : LiliumUI::kColLightMuted,
                 L"[F9]貼付", 2, LiliumUI::Align::Center, true);

      const int listX = 12, listY = 114, listW = LiliumUI::kMainW - 24, rowH = 34;
      c.drawBox(listX, listY, listW, kVisibleRows * rowH + 8, LiliumUI::kColWhite, LiliumUI::kColBorder);
      for (int r = 0; r < kVisibleRows; ++r) {
        renderEntryRow(c, st, r);
      }
    }

    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    c.drawText(14, 440, LiliumUI::kMainW - 28, 40, LiliumUI::kColTextSub,
               st->viewing
                 ? L"[↑↓/PgUpDn]スクロール [F2]テキスト/HEX [Esc]一覧へ戻る"
                 : L"[↑↓]選択 [Ent]開く [BS]上へ [F5]削除 [F8/F9]複写 [Esc]戻る",
               2, LiliumUI::Align::Left, true);
  } else {
    if (st->prevScroll != st->scroll) {
      for (int r = 0; r < kVisibleRows; ++r) renderEntryRow(c, st, r);
    } else if (st->prevSel != st->sel) {
      int rOld = st->prevSel - st->scroll;
      int rNew = st->sel - st->scroll;
      if (rOld >= 0 && rOld < kVisibleRows) renderEntryRow(c, st, rOld);
      if (rNew >= 0 && rNew < kVisibleRows) renderEntryRow(c, st, rNew);
    }
  }

  st->prevSel = st->sel;
  st->prevScroll = st->scroll;

  const wchar_t * cardTitle = L"ファイル詳細";
  wchar_t cardBody[512] = L"項目なし";
  if (st->viewing) {
    cardTitle = L"ビューア情報";
    _snwprintf(cardBody, 512,
               L"【ファイル名】\n%s\n【サイズ】\n%lu バイト\n【モード】\n%s%s",
               st->viewName,
               static_cast<unsigned long>(st->viewFileSize),
               st->viewHexMode ? L"HEXダンプ(省メモリ)" : L"テキスト(UTF-8/16)",
               st->viewTruncated ? L"\n(先頭64KBまで)" : L"");
    cardBody[511] = L'\0';
  } else if (st->sel >= 0 && st->sel < st->entryCount) {
    const FileEntry & e = st->entries[st->sel];
    cardTitle = e.isParent ? L"親フォルダ" : e.name;
    wchar_t szBuf[24];
    formatSizeStr(e, szBuf, 24);
    _snwprintf(cardBody, 512,
               L"【種別】\n%s\n【サイズ】\n%s\n【コピー待機】\n%s",
               e.isDir ? L"フォルダ" : L"ファイル",
               szBuf,
               st->copySrcName[0] ? st->copySrcName : L"(なし)");
    cardBody[511] = L'\0';
  }

  wchar_t statusLine[48];
  _snwprintf(statusLine, 48, st->viewing ? L"閲覧中" : L"%d 項目", st->entryCount);
  statusLine[47] = L'\0';

  LiliumUI::renderSidebar(c, statusLine, cardTitle, cardBody,
                          st->viewing ? L"[F2]:HEX切替" : L"[F8]:複写 [F9]:貼付",
                          st->viewing ? L"[Esc]:一覧に戻る" : L"[F5]:削除 [F4]:更新",
                          st->viewing ? L"一覧へ" : L"戻　る", false);

  if (st->modal == ModalMode::DeleteConfirm) {
    static constexpr LiliumUI::ModalButton kDelBtns[2] = {
      { L"削除する", true },
      { L"キャンセル", false }
    };
    const wchar_t * nm = (st->sel >= 0 && st->sel < st->entryCount) ? st->entries[st->sel].name : L"";
    LiliumUI::renderConfirmModal(c, L"【 削除の確認 】",
                                 nm,
                                 L"この項目を削除しますか？ (元に戻せません)",
                                 kDelBtns, 2, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:決定   [Esc]:中止");
  } else if (st->modal == ModalMode::ProtectedWarn || st->modal == ModalMode::MessageBox) {
    static constexpr LiliumUI::ModalButton kOkBtns[1] = { { L"OK", false } };
    LiliumUI::renderConfirmModal(c, st->msgTitle, st->msgLine1, st->msgLine2,
                                 kOkBtns, 1, 0, L"[Enter / Esc]: 閉じる");
  }
}

static void activateSelectedEntry(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry e = st->entries[st->sel];
  if (e.isParent) {
    navigateUp(st);
    st->window.requestRepaint(true);
    return;
  }
  if (e.isDir) {
    wchar_t full[MAX_PATH];
    buildFullEntryPath(st, e, full, MAX_PATH);
    loadDirectory(st, full);
    st->window.requestRepaint(true);
    return;
  }
  openFileInViewer(st, e);
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);

  if (st->modal == ModalMode::ProtectedWarn || st->modal == ModalMode::MessageBox) {
    if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->modal = ModalMode::None;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->modal == ModalMode::DeleteConfirm) {
    switch (vkCode) {
      case VK_LEFT:
      case VK_RIGHT:
      case VK_UP:
      case VK_DOWN:
        st->modalFocus = (st->modalFocus == 0) ? 1 : 0;
        st->window.requestRepaint(true);
        break;
      case VK_RETURN:
      case VK_F3:
        if (st->modalFocus == 0) executeDeleteSelected(st);
        else { st->modal = ModalMode::None; st->window.requestRepaint(true); }
        break;
      case VK_ESCAPE:
      case VK_BACK:
        st->modal = ModalMode::None;
        st->window.requestRepaint(true);
        break;
      default:
        break;
    }
    return;
  }

  if (st->viewing) {
    switch (vkCode) {
      case VK_UP:
        if (st->viewHexMode) {
          if (st->hexOffset >= kHexBytesPerRow) st->hexOffset -= kHexBytesPerRow;
        } else if (st->viewLineScroll > 0) {
          --st->viewLineScroll;
        }
        st->window.requestRepaint(true);
        break;
      case VK_DOWN:
        if (st->viewHexMode) {
          if (st->hexOffset + kHexBytesPerRow < st->viewFileSize) st->hexOffset += kHexBytesPerRow;
        } else if (st->viewLineScroll + kViewerVisibleLines < st->lineCount) {
          ++st->viewLineScroll;
        }
        st->window.requestRepaint(true);
        break;
      case VK_PRIOR:
        if (st->viewHexMode) {
          uint64_t step = kHexBytesPerRow * kHexRowsPerPage;
          st->hexOffset = (st->hexOffset >= step) ? (st->hexOffset - step) : 0;
        } else {
          st->viewLineScroll -= kViewerVisibleLines;
          if (st->viewLineScroll < 0) st->viewLineScroll = 0;
        }
        st->window.requestRepaint(true);
        break;
      case VK_NEXT:
        if (st->viewHexMode) {
          uint64_t step = kHexBytesPerRow * kHexRowsPerPage;
          if (st->hexOffset + step < st->viewFileSize) st->hexOffset += step;
        } else {
          st->viewLineScroll += kViewerVisibleLines;
          int maxSc = (st->lineCount > kViewerVisibleLines) ? (st->lineCount - kViewerVisibleLines) : 0;
          if (st->viewLineScroll > maxSc) st->viewLineScroll = maxSc;
        }
        st->window.requestRepaint(true);
        break;
      case VK_F2:
        st->viewHexMode = !st->viewHexMode;
        st->window.requestRepaint(true);
        break;
      case VK_ESCAPE:
      case VK_BACK:
        st->viewing = false;
        freeViewerBuffer(st);
        st->window.requestRepaint(true);
        break;
      case VK_HOME:
      case VK_F10:
        freeViewerBuffer(st);
        st->window.requestQuit();
        break;
      default:
        break;
    }
    return;
  }

  switch (vkCode) {
    case VK_UP:
      if (st->entryCount > 0) {
        st->prevSel = st->sel;
        st->prevScroll = st->scroll;
        st->sel = (st->sel - 1 + st->entryCount) % st->entryCount;
        adjustListScroll(st->sel, st->entryCount, st->scroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_DOWN:
      if (st->entryCount > 0) {
        st->prevSel = st->sel;
        st->prevScroll = st->scroll;
        st->sel = (st->sel + 1) % st->entryCount;
        adjustListScroll(st->sel, st->entryCount, st->scroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_PRIOR:
      if (st->entryCount > 0) {
        st->prevSel = st->sel;
        st->prevScroll = st->scroll;
        st->sel -= kVisibleRows;
        if (st->sel < 0) st->sel = 0;
        adjustListScroll(st->sel, st->entryCount, st->scroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_NEXT:
      if (st->entryCount > 0) {
        st->prevSel = st->sel;
        st->prevScroll = st->scroll;
        st->sel += kVisibleRows;
        if (st->sel >= st->entryCount) st->sel = st->entryCount - 1;
        adjustListScroll(st->sel, st->entryCount, st->scroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_RETURN:
    case VK_F3:
      activateSelectedEntry(st);
      break;
    case VK_BACK:
    case VK_LEFT:
      navigateUp(st);
      st->window.requestRepaint(true);
      break;
    case VK_F1:
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
      loadDirectory(st, L"\\");
#else
      loadDirectory(st, L"");
#endif
      st->window.requestRepaint(true);
      break;
    case VK_F2: {
      wchar_t rootDir[MAX_PATH] = L"";
      LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
      loadDirectory(st, rootDir);
      st->window.requestRepaint(true);
      break;
    }
    case VK_F4: {
      wchar_t curCopy[MAX_PATH];
      wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
      curCopy[MAX_PATH - 1] = L'\0';
      loadDirectory(st, st->atDriveList ? L"" : curCopy);
      st->window.requestRepaint(true);
      break;
    }
    case VK_F8:
    case 'C':
      if (st->sel >= 0 && st->sel < st->entryCount && !st->entries[st->sel].isDir) {
        buildFullEntryPath(st, st->entries[st->sel], st->copySrcPath, MAX_PATH);
        wcsncpy(st->copySrcName, st->entries[st->sel].name, 127);
        st->copySrcName[127] = L'\0';
        st->window.requestRepaint(true);
      }
      break;
    case VK_F9:
      executePasteCopy(st);
      break;
    case VK_F5:
    case VK_DELETE:
      if (st->sel >= 0 && st->sel < st->entryCount) {
        const FileEntry & e = st->entries[st->sel];
        if (e.isParent || e.isDriveRoot) break;
        wchar_t fullPath[MAX_PATH];
        buildFullEntryPath(st, e, fullPath, MAX_PATH);
        if (isProtectedFile(fullPath, e.name)) {
          wcsncpy(st->msgTitle, L"【 保護対象ファイル 】", 63);
          wcsncpy(st->msgLine1, e.name, 95);
          wcsncpy(st->msgLine2, L"OS/Lilium必須ファイルは削除保護されています。", 95);
          st->modal = ModalMode::ProtectedWarn;
        } else {
          st->modal = ModalMode::DeleteConfirm;
          st->modalFocus = 1; // Cancel default
        }
        st->window.requestRepaint(true);
      }
      break;
    case VK_ESCAPE:
    case VK_HOME:
    case VK_F10:
      st->window.requestQuit();
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);

  if (st->modal == ModalMode::ProtectedWarn || st->modal == ModalMode::MessageBox) {
    st->modal = ModalMode::None;
    st->window.requestRepaint(true);
    return;
  }

  if (st->modal == ModalMode::DeleteConfirm) {
    const int dlgX = (LiliumUI::kScreenW - 500) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int totalW = 2 * 160 + 22;
    const int bx0 = dlgX + (500 - totalW) / 2;
    const int bx1 = bx0 + 160 + 22;
    if (y >= by && y < by + 42) {
      if (x >= bx0 && x < bx0 + 160) executeDeleteSelected(st);
      else if (x >= bx1 && x < bx1 + 160) {
        st->modal = ModalMode::None;
        st->window.requestRepaint(true);
      }
    }
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    if (st->viewing) {
      st->viewing = false;
      freeViewerBuffer(st);
      st->window.requestRepaint(true);
    } else {
      st->window.requestQuit();
    }
    return;
  }

  if (st->viewing) {
    if (y >= 48 && y < 76 && x >= 446 && x < 628) {
      st->viewHexMode = !st->viewHexMode;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (y >= 82 && y < 110) {
    if (x >= 12 && x < 146) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
      loadDirectory(st, L"\\");
#else
      loadDirectory(st, L"");
#endif
      st->window.requestRepaint(true);
      return;
    }
    if (x >= 152 && x < 294) {
      wchar_t rootDir[MAX_PATH] = L"";
      LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
      loadDirectory(st, rootDir);
      st->window.requestRepaint(true);
      return;
    }
    if (x >= 300 && x < 460) {
      if (st->sel >= 0 && st->sel < st->entryCount && !st->entries[st->sel].isDir) {
        buildFullEntryPath(st, st->entries[st->sel], st->copySrcPath, MAX_PATH);
        wcsncpy(st->copySrcName, st->entries[st->sel].name, 127);
        st->copySrcName[127] = L'\0';
        st->window.requestRepaint(true);
      }
      return;
    }
    if (x >= 466 && x < 628) {
      executePasteCopy(st);
      return;
    }
  }

  if (x >= 16 && x < LiliumUI::kMainW - 16 && y >= 118 && y < 118 + kVisibleRows * 34) {
    int r = (y - 118) / 34;
    int idx = st->scroll + r;
    if (idx < st->entryCount) {
      if (st->sel == idx) {
        activateSelectedEntry(st);
      } else {
        st->prevSel = st->sel;
        st->prevScroll = st->scroll;
        st->sel = idx;
        st->window.requestRepaint(false);
      }
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);
  wchar_t statusLine[48];
  _snwprintf(statusLine, 48, st->viewing ? L"閲覧中" : L"%d 項目", st->entryCount);
  statusLine[47] = L'\0';
  LiliumUI::renderSidebarStatus(c, statusLine);
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  FilerState state;
  state.manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  loadDirectory(&state, rootDir);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;

  if (!state.window.create(L"LiliumFilerWinClass",
                           L"Lilium Filer (SHARP Brain 800x480)",
                           cb, &state, state.manageTaskBar)) {
    freeViewerBuffer(&state);
    return 1;
  }

  int rc = state.window.runLoop();
  freeViewerBuffer(&state);
  return rc;
}
