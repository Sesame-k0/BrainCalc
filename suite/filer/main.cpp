#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"

#include <new>
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
constexpr int kToolCount = 8;

enum ToolAction {
  ToolRoot = 0,
  ToolLilium = 1,
  ToolCopy = 2,
  ToolPaste = 3,
  ToolNewFolder = 4,
  ToolDelete = 5,
  ToolRefresh = 6,
  ToolRename = 7
};

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
  wchar_t selfLiliumRoot[MAX_PATH] = L"";
  int entryCount = 0;
  int sel = 0;
  int prevSel = 0;
  int scroll = 0;
  int prevScroll = 0;

  // Icon-only top toolbar focus state (1-7 / Q-U focus, Esc/BS/Down returns to list)
  bool toolbarFocused = false;
  int toolbarSel = 0;

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

  LiliumIME::TextInputModal inputModal;
};

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

static bool pathMatchesPrefixDir(const wchar_t * path, const wchar_t * dirPrefix) {
  if (!path || !dirPrefix || !dirPrefix[0]) return false;
  size_t pLen = wcslen(dirPrefix);
  if (_wcsnicmp(path, dirPrefix, pLen) != 0) return false;
  wchar_t nextCh = path[pLen];
  return nextCh == L'\0' || nextCh == L'\\' || nextCh == L'/';
}

static bool isProtectedSystemDir(const wchar_t * dirPath) {
  if (!dirPath || !dirPath[0]) return true;
  if (wcscmp(dirPath, L"\\") == 0) return true;

  static constexpr const wchar_t * kCeProtectedDirs[] = {
    L"\\Windows",
    L"\\NAND",
    L"\\NAND1",
    L"\\NAND2",
    L"\\NAND4",
    L"\\NAND5",
    L"\\NAND6",
    L"\\NAND8",
    L"\\Program Files",
    L"\\Application Data",
    L"\\Profiles"
  };
  for (int i = 0; i < static_cast<int>(sizeof(kCeProtectedDirs) / sizeof(kCeProtectedDirs[0])); ++i) {
    if (pathMatchesPrefixDir(dirPath, kCeProtectedDirs[i])) return true;
  }

  // Host Windows system directories (X:\Windows, X:\Program Files, etc.)
  if (((dirPath[0] >= L'A' && dirPath[0] <= L'Z') || (dirPath[0] >= L'a' && dirPath[0] <= L'z')) &&
      dirPath[1] == L':') {
    const wchar_t * sub = dirPath + 2;
    static constexpr const wchar_t * kHostProtectedDirs[] = {
      L"\\Windows",
      L"\\Program Files",
      L"\\Program Files (x86)",
      L"\\ProgramData",
      L"\\System Volume Information",
      L"\\$Recycle.Bin"
    };
    for (int i = 0; i < static_cast<int>(sizeof(kHostProtectedDirs) / sizeof(kHostProtectedDirs[0])); ++i) {
      if (pathMatchesPrefixDir(sub, kHostProtectedDirs[i])) return true;
    }
  }
  return false;
}

static void trimTrailingSlashCopy(const wchar_t * src, wchar_t * dst, size_t maxLen) {
  if (!dst || maxLen == 0) return;
  dst[0] = L'\0';
  if (!src || !src[0]) return;
  wcsncpy(dst, src, maxLen - 1);
  dst[maxLen - 1] = L'\0';
  size_t len = wcslen(dst);
  while (len > 1 && (dst[len - 1] == L'\\' || dst[len - 1] == L'/')) {
    if (len == 3 && dst[1] == L':') break; // Preserve Windows drive root like C:\.
    dst[--len] = L'\0';
  }
}

static bool isEntryProtectedForDelete(const FilerState * st, const wchar_t * fullPath, const wchar_t * name) {
  if (!st || st->atDriveList) return true;
  wchar_t trimmedFull[MAX_PATH];
  trimTrailingSlashCopy(fullPath, trimmedFull, MAX_PATH);
  if (isProtectedSystemDir(st->curDir) || isProtectedSystemDir(trimmedFull)) return true;

  const bool isCoreFileName =
      (_wcsicmp(name, L"AppMain.exe") == 0 ||
       _wcsicmp(name, L"AppMain.cfg") == 0 ||
       _wcsicmp(name, L"AppMain_.exe") == 0 ||
       _wcsicmp(name, L"index.din") == 0 ||
       _wcsicmp(name, L"LiliumFiler.exe") == 0 ||
       _wcsicmp(name, L"LiliumLauncher.exe") == 0);

  if (st->selfLiliumRoot[0] != L'\0') {
    // Protect the running Lilium folder itself and all its ancestor directories (e.g., \NAND3\アプリ).
    if (pathMatchesPrefixDir(st->selfLiliumRoot, trimmedFull)) return true;

    // Inside the running Lilium folder, protect core binaries/config/apps dir;
    // sibling app folders outside selfLiliumRoot (e.g., \NAND3\アプリ\ceOpener) can be deleted freely.
    if (pathMatchesPrefixDir(trimmedFull, st->selfLiliumRoot)) {
      if (isCoreFileName || _wcsicmp(name, L"apps") == 0) return true;
    }
    return false;
  }

  return isCoreFileName;
}

static bool deleteDirectoryRecursive(const FilerState * st, const wchar_t * dirPath, int depth) {
  if (depth > 16 || !dirPath || !dirPath[0]) return false;
  wchar_t trimmed[MAX_PATH];
  trimTrailingSlashCopy(dirPath, trimmed, MAX_PATH);
  if (isProtectedSystemDir(trimmed)) return false;
  if (st && st->selfLiliumRoot[0] != L'\0' && pathMatchesPrefixDir(st->selfLiliumRoot, trimmed)) {
    return false;
  }

  wchar_t pattern[MAX_PATH];
  LiliumUI::joinPath(trimmed, L"*", pattern, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pattern, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
      wchar_t child[MAX_PATH];
      LiliumUI::joinPath(trimmed, fd.cFileName, child, MAX_PATH);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (!deleteDirectoryRecursive(st, child, depth + 1)) {
          FindClose(hFind);
          return false;
        }
      } else {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
          SetFileAttributesW(child, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
        }
        if (!DeleteFileW(child)) {
          FindClose(hFind);
          return false;
        }
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }

  DWORD attrs = GetFileAttributesW(trimmed);
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
    SetFileAttributesW(trimmed, attrs & ~FILE_ATTRIBUTE_READONLY);
  }
  return RemoveDirectoryW(trimmed) != 0;
}

static void showFilerModal(FilerState * st, ModalMode mode,
                           const wchar_t * title, const wchar_t * l1, const wchar_t * l2) {
  wcsncpy(st->msgTitle, title, 63);
  st->msgTitle[63] = L'\0';
  wcsncpy(st->msgLine1, l1, 95);
  st->msgLine1[95] = L'\0';
  wcsncpy(st->msgLine2, l2, 95);
  st->msgLine2[95] = L'\0';
  st->modal = mode;
  st->modalFocus = 0;
  st->window.requestRepaint(true);
}

static void copySelectedEntry(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry & e = st->entries[st->sel];
  if (e.isParent || e.isDriveRoot || e.isDir) {
    showFilerModal(st, ModalMode::MessageBox,
                   L"【 コピー指定不可 】",
                   L"フォルダ等はコピー指定できません。",
                   L"コピー対象のファイルを選択してから実行してください。");
    return;
  }
  buildFullEntryPath(st, e, st->copySrcPath, MAX_PATH);
  wcsncpy(st->copySrcName, e.name, 127);
  st->copySrcName[127] = L'\0';
  st->window.requestRepaint(true);
}

static void executeDeleteSelected(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry e = st->entries[st->sel];
  if (e.isParent || e.isDriveRoot) return;

  wchar_t fullPath[MAX_PATH];
  buildFullEntryPath(st, e, fullPath, MAX_PATH);
  if (isEntryProtectedForDelete(st, fullPath, e.name)) {
    showFilerModal(st, ModalMode::ProtectedWarn,
                   L"【 削除保護領域 】",
                   e.name,
                   L"OS・NANDシステム領域や必須ファイルは削除できません。");
    return;
  }

  BOOL ok = e.isDir ? (deleteDirectoryRecursive(st, fullPath, 0) ? TRUE : FALSE) : DeleteFileW(fullPath);
  if (ok) {
    int oldSel = st->sel;
    wchar_t curCopy[MAX_PATH];
    wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
    curCopy[MAX_PATH - 1] = L'\0';
    loadDirectory(st, curCopy);
    if (oldSel < st->entryCount) st->sel = oldSel;
    else if (st->entryCount > 0) st->sel = st->entryCount - 1;
    adjustListScroll(st->sel, st->entryCount, st->scroll);
    showFilerModal(st, ModalMode::MessageBox, L"【 削除完了 】", e.name, L"削除しました。");
  } else {
    showFilerModal(st, ModalMode::MessageBox,
                   L"【 削除失敗 】",
                   e.name,
                   e.isDir ? L"使用中または削除保護された項目を含みます。" : L"使用中または保護されています。");
  }
}

static void requestDeleteSelected(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry & e = st->entries[st->sel];
  if (e.isParent || e.isDriveRoot) return;
  wchar_t fullPath[MAX_PATH];
  buildFullEntryPath(st, e, fullPath, MAX_PATH);
  if (isEntryProtectedForDelete(st, fullPath, e.name)) {
    showFilerModal(st, ModalMode::ProtectedWarn,
                   L"【 削除保護領域 】",
                   e.name,
                   L"OS・NANDシステム領域や必須ファイルは削除できません。");
  } else {
    st->modal = ModalMode::DeleteConfirm;
    st->modalFocus = 1; // Cancel default
    st->window.requestRepaint(true);
  }
}

static void executeCreateFolder(FilerState * st) {
  if (st->atDriveList || isProtectedSystemDir(st->curDir)) {
    showFilerModal(st, ModalMode::ProtectedWarn,
                   L"【 書き込み保護領域 】",
                   L"システム保護領域には新規作成できません。",
                   L"\\Storage Card や \\NAND3 等で実行してください。");
    return;
  }

  wchar_t newName[64] = L"";
  wchar_t candPath[MAX_PATH] = L"";
  bool foundFree = false;
  for (int i = 1; i <= 99; ++i) {
    if (i == 1) wcscpy(newName, L"NewFolder");
    else _snwprintf(newName, 64, L"NewFolder_%d", i);
    newName[63] = L'\0';

    FileEntry dummy = {};
    wcsncpy(dummy.name, newName, MAX_PATH - 1);
    buildFullEntryPath(st, dummy, candPath, MAX_PATH);
    if (GetFileAttributesW(candPath) == INVALID_FILE_ATTRIBUTES) {
      foundFree = true;
      break;
    }
  }

  if (!foundFree || !CreateDirectoryW(candPath, NULL)) {
    showFilerModal(st, ModalMode::MessageBox,
                   L"【 フォルダ作成失敗 】",
                   newName,
                   L"書き込み権限または空き容量をご確認ください。");
    return;
  }

  wchar_t curCopy[MAX_PATH];
  wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
  curCopy[MAX_PATH - 1] = L'\0';
  loadDirectory(st, curCopy);
  for (int i = 0; i < st->entryCount; ++i) {
    if (_wcsicmp(st->entries[i].name, newName) == 0) {
      st->sel = i;
      adjustListScroll(st->sel, st->entryCount, st->scroll);
      break;
    }
  }
  showFilerModal(st, ModalMode::MessageBox, L"【 フォルダ作成完了 】", newName, L"新しいフォルダを作成しました。");
}

static bool hasInvalidFileNameChars(const wchar_t * name) {
  if (!name || !name[0]) return true;
  if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) return true;
  for (int i = 0; name[i]; ++i) {
    wchar_t ch = name[i];
    if (ch < 0x20 || ch == L'\\' || ch == L'/' || ch == L':' ||
        ch == L'*' || ch == L'?' || ch == L'\"' || ch == L'<' || ch == L'>' || ch == L'|') {
      return true;
    }
  }
  return false;
}

static void requestRenameSelected(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry & e = st->entries[st->sel];
  if (e.isParent || e.isDriveRoot) return;

  wchar_t fullPath[MAX_PATH];
  buildFullEntryPath(st, e, fullPath, MAX_PATH);
  if (isEntryProtectedForDelete(st, fullPath, e.name)) {
    showFilerModal(st, ModalMode::ProtectedWarn,
                   L"【 名前変更保護領域 】",
                   e.name,
                   L"OS・NANDシステム領域や必須ファイルは変更できません。");
    return;
  }

  st->inputModal.open(L"【 名前変更 (Lilium IME) 】",
                      L"新しい名前を入力してください ([F2]で入力モード切替)",
                      e.name, 96);
  st->window.requestRepaint(true);
}

static void executeRenameSelected(FilerState * st, const wchar_t * rawNewName) {
  if (!rawNewName || st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry e = st->entries[st->sel];
  if (e.isParent || e.isDriveRoot) return;

  wchar_t newName[MAX_PATH] = L"";
  wcsncpy(newName, rawNewName, MAX_PATH - 1);
  newName[MAX_PATH - 1] = L'\0';
  int start = 0;
  while (newName[start] == L' ' || newName[start] == L'\t') ++start;
  if (start > 0) memmove(newName, newName + start, (wcslen(newName + start) + 1) * sizeof(wchar_t));
  size_t len = wcslen(newName);
  while (len > 0 && (newName[len - 1] == L' ' || newName[len - 1] == L'\t')) newName[--len] = L'\0';

  if (hasInvalidFileNameChars(newName)) {
    showFilerModal(st, ModalMode::MessageBox,
                   L"【 名前変更エラー 】",
                   newName[0] ? newName : L"(空欄)",
                   L"使用できない文字が含まれているか空欄です。");
    return;
  }
  if (wcscmp(newName, e.name) == 0) {
    st->window.requestRepaint(true);
    return;
  }

  wchar_t oldPath[MAX_PATH], newPath[MAX_PATH];
  buildFullEntryPath(st, e, oldPath, MAX_PATH);
  if (isEntryProtectedForDelete(st, oldPath, e.name)) {
    showFilerModal(st, ModalMode::ProtectedWarn,
                   L"【 名前変更保護領域 】",
                   e.name,
                   L"OS・NANDシステム領域や必須ファイルは変更できません。");
    return;
  }

  FileEntry dummy = {};
  wcsncpy(dummy.name, newName, MAX_PATH - 1);
  buildFullEntryPath(st, dummy, newPath, MAX_PATH);

  if (MoveFileW(oldPath, newPath)) {
    wchar_t curCopy[MAX_PATH];
    wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
    curCopy[MAX_PATH - 1] = L'\0';
    loadDirectory(st, curCopy);
    for (int i = 0; i < st->entryCount; ++i) {
      if (_wcsicmp(st->entries[i].name, newName) == 0) {
        st->sel = i;
        adjustListScroll(st->sel, st->entryCount, st->scroll);
        break;
      }
    }
    showFilerModal(st, ModalMode::MessageBox, L"【 名前変更完了 】", newName, L"名前を変更しました。");
  } else {
    showFilerModal(st, ModalMode::MessageBox,
                   L"【 名前変更失敗 】",
                   newName,
                   L"同名の項目があるか使用中の可能性があります。");
  }
}

static void executePasteCopy(FilerState * st) {
  if (st->atDriveList || isProtectedSystemDir(st->curDir)) {
    showFilerModal(st, ModalMode::ProtectedWarn,
                   L"【 書き込み保護領域 】",
                   L"システム保護領域へは貼り付けできません。",
                   L"コピー元としての読み出しのみ許可されています。");
    return;
  }
  if (!st->copySrcPath[0]) {
    showFilerModal(st, ModalMode::MessageBox,
                   L"【 貼り付け待機なし 】",
                   L"コピー元のファイルが選択されていません。",
                   L"[3] アイコンでコピー元を指定してください。");
    return;
  }
  wchar_t dstPath[MAX_PATH];
  FileEntry dummy = {};
  wcsncpy(dummy.name, st->copySrcName, MAX_PATH - 1);
  buildFullEntryPath(st, dummy, dstPath, MAX_PATH);

  if (_wcsicmp(dstPath, st->copySrcPath) == 0) {
    _snwprintf(dstPath, MAX_PATH, L"%ls\\copy_%ls", st->curDir, st->copySrcName);
    dstPath[MAX_PATH - 1] = L'\0';
  }

  BOOL ok = CopyFileW(st->copySrcPath, dstPath, FALSE);
  if (ok) {
    wchar_t curCopy[MAX_PATH];
    wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
    curCopy[MAX_PATH - 1] = L'\0';
    loadDirectory(st, curCopy);
    showFilerModal(st, ModalMode::MessageBox, L"【 コピー完了 】", st->copySrcName, L"現在のフォルダへコピーしました。");
  } else {
    showFilerModal(st, ModalMode::MessageBox, L"【 コピー失敗 】", st->copySrcName, L"書き込み先や空き容量をご確認ください。");
  }
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

static void drawToolbarIconGlyph(LiliumUI::Canvas & c, int idx, int bx, int by, int bw, int bh,
                                 bool focused, bool locked) {
  int cx = bx + bw / 2;
  int cy = by + bh / 2;
  uint32_t ink = locked ? LiliumUI::kColLightMuted : LiliumUI::kColCharcoal;
  uint32_t accent = locked ? LiliumUI::kColLightMuted : LiliumUI::kColTealDark;

  switch (idx) {
    case ToolRoot: {
      c.drawBox(cx - 11, cy - 6, 22, 12, LiliumUI::kColWhite, ink);
      c.fillRect(cx - 8, cy - 2, 16, 2, ink);
      c.fillRect(cx + 4, cy + 2, 4, 2, focused ? LiliumUI::kColTealDark : LiliumUI::kColTeal);
      break;
    }
    case ToolLilium: {
      for (int row = 0; row < 6; ++row) {
        c.fillRect(cx - row * 2 - 1, cy - 9 + row, row * 4 + 3, 1, accent);
      }
      c.drawBox(cx - 8, cy - 3, 16, 11, LiliumUI::kColWhite, accent);
      c.fillRect(cx - 2, cy + 2, 5, 6, accent);
      break;
    }
    case ToolCopy: {
      c.drawBox(cx - 9, cy - 8, 12, 13, 0xE6EEF2, ink);
      c.drawBox(cx - 3, cy - 4, 12, 13, LiliumUI::kColWhite, ink);
      c.fillRect(cx, cy, 6, 2, accent);
      c.fillRect(cx, cy + 4, 6, 2, ink);
      break;
    }
    case ToolPaste: {
      c.drawBox(cx - 8, cy - 6, 16, 15, LiliumUI::kColWhite, ink);
      c.fillRect(cx - 4, cy - 9, 8, 4, accent);
      c.fillRect(cx - 4, cy - 1, 8, 2, ink);
      c.fillRect(cx - 4, cy + 3, 6, 2, ink);
      break;
    }
    case ToolNewFolder: {
      uint32_t bodyBg = locked ? 0xE8ECEF : LiliumUI::kColTealSoft;
      c.fillRect(cx - 10, cy - 7, 8, 3, ink);
      c.drawBox(cx - 10, cy - 5, 20, 13, bodyBg, ink);
      c.fillRect(cx - 4, cy + 1, 8, 2, accent);
      c.fillRect(cx - 1, cy - 2, 2, 8, accent);
      break;
    }
    case ToolDelete: {
      uint32_t dCol = locked ? LiliumUI::kColLightMuted : LiliumUI::kColDangerFill;
      uint32_t dBg = locked ? 0xEAECEF : LiliumUI::kColWhite;
      c.fillRect(cx - 3, cy - 9, 6, 2, dCol);
      c.fillRect(cx - 8, cy - 7, 16, 2, dCol);
      c.drawBox(cx - 6, cy - 4, 12, 12, dBg, dCol);
      c.fillRect(cx - 3, cy - 1, 2, 7, dCol);
      c.fillRect(cx - 1, cy - 1, 2, 7, dCol);
      c.fillRect(cx + 1, cy - 1, 2, 7, dCol);
      break;
    }
    case ToolRefresh: {
      c.fillRect(cx - 7, cy - 5, 11, 2, accent);
      c.fillRect(cx + 3, cy - 7, 2, 6, accent);
      c.fillRect(cx + 5, cy - 6, 2, 4, accent);
      c.fillRect(cx + 7, cy - 5, 2, 2, accent);
      c.fillRect(cx - 4, cy + 3, 11, 2, accent);
      c.fillRect(cx - 5, cy + 1, 2, 6, accent);
      c.fillRect(cx - 7, cy + 2, 2, 4, accent);
      c.fillRect(cx - 9, cy + 3, 2, 2, accent);
      break;
    }
    case ToolRename:
    default: {
      // Pencil / Rename glyph
      for (int p = 0; p < 8; ++p) {
        c.fillRect(cx - 5 + p, cy + 3 - p, 4, 3, accent);
      }
      c.fillRect(cx + 3, cy - 6, 4, 4, ink);
      c.fillRect(cx - 7, cy + 5, 3, 2, ink);
      c.fillRect(cx - 8, cy + 8, 16, 2, ink);
      break;
    }
  }

  if (locked) {
    // Compact lock indicator at top-right of icon button
    c.drawBox(bx + bw - 9, by + 3, 6, 6, LiliumUI::kColDangerBg, LiliumUI::kColDangerFill);
  }
}

static bool isToolActionLocked(const FilerState * st, int idx) {
  if (!st) return true;
  bool dirProt = st->atDriveList || isProtectedSystemDir(st->curDir);
  if (idx == ToolPaste || idx == ToolNewFolder) return dirProt;
  if (idx == ToolDelete || idx == ToolRename) {
    if (st->sel < 0 || st->sel >= st->entryCount) return true;
    const FileEntry & e = st->entries[st->sel];
    if (e.isParent || e.isDriveRoot) return true;
    wchar_t full[MAX_PATH];
    buildFullEntryPath(st, e, full, MAX_PATH);
    return isEntryProtectedForDelete(st, full, e.name);
  }
  return false;
}

static void renderIconToolbar(LiliumUI::Canvas & c, const FilerState * st) {
  for (int i = 0; i < kToolCount; ++i) {
    int bx = 12 + i * 50;
    int by = 80;
    int bw = 45;
    int bh = 30;
    bool focused = st->toolbarFocused && (st->toolbarSel == i);
    bool locked = isToolActionLocked(st, i);

    uint32_t bg = LiliumUI::kColWhite;
    uint32_t bd = LiliumUI::kColBorder;
    if (locked && !focused) {
      bg = 0xF0F3F7;
    }
    if (focused) {
      if (i == ToolDelete && !locked) {
        bg = LiliumUI::kColDangerBg;
        bd = LiliumUI::kColDangerFill;
      } else if (locked) {
        bg = LiliumUI::kColWarnBg;
        bd = LiliumUI::kColWarnBorder;
      } else {
        bg = LiliumUI::kColTealSoft;
        bd = LiliumUI::kColTealDark;
      }
    }
    c.drawBox(bx, by, bw, bh, bg, bd);
    if (focused) {
      c.drawThickFrame(bx, by, bw, bh, 2, bd);
      c.fillRect(bx, by + bh - 2, bw, 2, bd);
    }
    drawToolbarIconGlyph(c, i, bx, by, bw, bh, focused, locked);
  }

  // Status pill on right of toolbar (x=414..628)
  bool dirProt = st->atDriveList || isProtectedSystemDir(st->curDir);
  if (dirProt) {
    c.drawBox(414, 80, 214, 30, LiliumUI::kColWarnBg, LiliumUI::kColWarnBorder);
    c.drawText(418, 80, 206, 30, LiliumUI::kColWarnText,
               L"保護領域 (複写元のみ可)", 2, LiliumUI::Align::Center, true);
  } else if (st->copySrcName[0]) {
    wchar_t cpBuf[96];
    _snwprintf(cpBuf, 96, L"複写:%ls", st->copySrcName);
    cpBuf[95] = L'\0';
    c.drawBox(414, 80, 214, 30, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
    c.drawText(418, 80, 206, 30, LiliumUI::kColTealDeep, cpBuf, 2, LiliumUI::Align::Center, true);
  } else {
    c.drawBox(414, 80, 214, 30, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawText(418, 80, 206, 30, LiliumUI::kColMuted,
               L"[1-8]機能アイコン選択", 2, LiliumUI::Align::Center, true);
  }
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);

  if (st->inputModal.isOpen() && !c.fullRedraw && st->inputModal.hasDrawnChrome()) {
    st->inputModal.render(c, true);
    return;
  }

  if (c.fullRedraw || st->modal != ModalMode::None || st->viewing || st->inputModal.isOpen()) {
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
      bool dirProt = st->atDriveList || isProtectedSystemDir(st->curDir);
      // Path bar (y=48)
      c.drawBox(12, 48, LiliumUI::kMainW - 24, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
      c.drawText(20, 48, dirProt ? 530 : (LiliumUI::kMainW - 40), 28, LiliumUI::kColText,
                 st->curDir, 2, LiliumUI::Align::Left, true);
      if (dirProt) {
        c.drawBox(556, 51, 66, 22, LiliumUI::kColDangerBg, LiliumUI::kColDangerBorder);
        c.drawText(556, 51, 66, 22, LiliumUI::kColDangerText, L"保護", 2, LiliumUI::Align::Center, true);
      }

      // Pure icon-only quick toolbar (y=80..110)
      renderIconToolbar(c, st);

      const int listX = 12, listY = 114, listW = LiliumUI::kMainW - 24, rowH = 34;
      c.drawBox(listX, listY, listW, kVisibleRows * rowH + 8, LiliumUI::kColWhite,
                st->toolbarFocused ? LiliumUI::kColBorder : LiliumUI::kColBorderStrong);
      for (int r = 0; r < kVisibleRows; ++r) {
        renderEntryRow(c, st, r);
      }
    }

    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    c.drawText(14, 440, LiliumUI::kMainW - 28, 40, LiliumUI::kColTextSub,
               st->viewing
                 ? L"[↑↓/PgUpDn]スクロール [F2]テキスト/HEX [Esc]一覧へ戻る"
                 : (st->toolbarFocused
                      ? L"[←→/1-8]機能選択 [Ent]実行 [戻る/↓]一覧へ戻る [HOME]終了"
                      : L"[↑↓]選択 [Ent]開く [BS]上 [1-8]機能 [F6]名前変更 [戻る]終了"),
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
  const wchar_t * statusLine = L"一覧中";
  const wchar_t * hint1 = L"[1-8]機能選択";
  const wchar_t * hint2 = L"[Ent]開[BS]上";
  const wchar_t * exitLbl = L"戻　る";

  wchar_t statusBuf[48];
  if (st->viewing) {
    statusLine = L"閲覧中";
    cardTitle = L"ビューア情報";
    _snwprintf(cardBody, 512,
               L"【ファイル名】\n%s\n【サイズ】\n%lu バイト\n【モード】\n%s%s",
               st->viewName,
               static_cast<unsigned long>(st->viewFileSize),
               st->viewHexMode ? L"HEXダンプ(省メモリ)" : L"テキスト(UTF-8/16)",
               st->viewTruncated ? L"\n(先頭64KBまで)" : L"");
    cardBody[511] = L'\0';
    hint1 = L"[F2]HEX切替";
    hint2 = L"[Esc]一覧へ";
    exitLbl = L"一覧へ";
  } else if (st->toolbarFocused) {
    statusLine = L"機能アイコン";
    bool locked = isToolActionLocked(st, st->toolbarSel);
    switch (st->toolbarSel) {
      case ToolRoot:
        cardTitle = L"[1] ルートへ移動";
        wcscpy(cardBody, L"【機能説明】\n記憶領域の最上位ルート\n(\\/ドライブ一覧)へ\n移動します。\n【操作】\n[Ent]移動 [戻る]一覧");
        break;
      case ToolLilium:
        cardTitle = L"[2] Liliumへ移動";
        wcscpy(cardBody, L"【機能説明】\nLiliumランチャーの\n格納フォルダへ素早く\n移動します。\n【操作】\n[Ent]移動 [戻る]一覧");
        break;
      case ToolCopy:
        cardTitle = L"[3] コピー指定";
        _snwprintf(cardBody, 512,
                   L"【機能説明】\n選択中ファイルをコピー\n元として記憶します。\n保護領域からの読み出\nしも可能です。\n【待機】%ls",
                   st->copySrcName[0] ? st->copySrcName : L"(なし)");
        cardBody[511] = L'\0';
        break;
      case ToolPaste:
        cardTitle = L"[4] 貼り付け";
        _snwprintf(cardBody, 512,
                   L"【機能説明】\nコピー指定ファイルを\n現在のフォルダへ複写\nします。\n【状態】\n%ls",
                   locked ? L"※保護領域のため貼付不可" : (st->copySrcName[0] ? st->copySrcName : L"コピー指定待機なし"));
        cardBody[511] = L'\0';
        break;
      case ToolNewFolder:
        cardTitle = L"[5] 新規フォルダ";
        wcscpy(cardBody, locked
          ? L"【機能説明】\n新しいフォルダを作成\nします。\n【状態】\n※保護領域のため作成不可\n(\\NAND3/SDは作成可)"
          : L"【機能説明】\n現在のフォルダ内に\nNewFolder を新規作成\nします。\n【操作】\n[Ent]作成 [戻る]一覧");
        break;
      case ToolDelete:
        cardTitle = L"[6] 項目を削除";
        wcscpy(cardBody, locked
          ? L"【機能説明】\n選択中のファイル/空\nフォルダを削除します。\n【状態】\n※システム保護対象は削除不可"
          : L"【機能説明】\n選択中のファイル/空\nフォルダを削除します。\n確認画面が出ます。\n【操作】\n[Ent]削除 [戻る]一覧");
        break;
      case ToolRefresh:
        cardTitle = L"[7] 最新に更新";
        wcscpy(cardBody, L"【機能説明】\n現在のフォルダ一覧を\n再読み込みします。\n【操作】\n[Ent]更新 [戻る]一覧");
        break;
      case ToolRename:
      default:
        cardTitle = L"[8] 名前変更(IME)";
        wcscpy(cardBody, locked
          ? L"【機能説明】\n選択中ファイル/フォルダ\nの名前を変更します。\n【状態】\n※システム保護対象は変更不可"
          : L"【機能説明】\n選択中項目の名前を\nLilium IME(日本語可)\nで変更します。\n【操作】\n[Ent]変更 [戻る]一覧");
        break;
    }
    hint1 = L"[Ent]機能実行";
    hint2 = L"[戻る/↓]一覧へ";
    exitLbl = L"一覧へ";
  } else {
    _snwprintf(statusBuf, 48, L"%d 項目", st->entryCount);
    statusBuf[47] = L'\0';
    statusLine = statusBuf;
    if (st->sel >= 0 && st->sel < st->entryCount) {
      const FileEntry & e = st->entries[st->sel];
      cardTitle = e.isParent ? L"親フォルダ" : e.name;
      wchar_t szBuf[24];
      formatSizeStr(e, szBuf, 24);
      bool dirProt = st->atDriveList || isProtectedSystemDir(st->curDir);
      _snwprintf(cardBody, 512,
                 L"【種別 / 保護】\n%s / %s\n【サイズ】\n%s\n【コピー待機】\n%s",
                 e.isDir ? L"フォルダ" : L"ファイル",
                 dirProt ? L"読取保護" : L"書込可",
                 szBuf,
                 st->copySrcName[0] ? st->copySrcName : L"(なし)");
      cardBody[511] = L'\0';
    }
  }

  LiliumUI::renderSidebar(c, statusLine, cardTitle, cardBody,
                          hint1, hint2, exitLbl, false);

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

  if (st->inputModal.isOpen()) {
    st->inputModal.render(c, true);
  }
}

static void executeToolAction(FilerState * st, int idx) {
  st->toolbarFocused = false;
  switch (idx) {
    case ToolRoot:
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
      loadDirectory(st, L"\\");
#else
      loadDirectory(st, L"");
#endif
      st->window.requestRepaint(true);
      break;
    case ToolLilium: {
      wchar_t rootDir[MAX_PATH] = L"";
      LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
      loadDirectory(st, rootDir);
      st->window.requestRepaint(true);
      break;
    }
    case ToolCopy:
      copySelectedEntry(st);
      break;
    case ToolPaste:
      executePasteCopy(st);
      break;
    case ToolNewFolder:
      executeCreateFolder(st);
      break;
    case ToolDelete:
      requestDeleteSelected(st);
      break;
    case ToolRefresh: {
      wchar_t curCopy[MAX_PATH];
      wcsncpy(curCopy, st->curDir, MAX_PATH - 1);
      curCopy[MAX_PATH - 1] = L'\0';
      loadDirectory(st, st->atDriveList ? L"" : curCopy);
      st->window.requestRepaint(true);
      break;
    }
    case ToolRename:
      requestRenameSelected(st);
      break;
    default:
      st->window.requestRepaint(true);
      break;
  }
}

static int toolIndexFromKey(WPARAM vkCode) {
  if (vkCode >= '1' && vkCode <= '8') return static_cast<int>(vkCode - '1');
  if (vkCode >= VK_NUMPAD1 && vkCode <= VK_NUMPAD8) return static_cast<int>(vkCode - VK_NUMPAD1);
  // SHARP Brain top-row number keys (when unshifted)
  switch (vkCode) {
    case 'Q': return 0;
    case 'W': return 1;
    case 'E': return 2;
    case 'R': return 3;
    case 'T': return 4;
    case 'Y': return 5;
    case 'U': return 6;
    case 'I': return 7;
    default: return -1;
  }
}

static void activateSelectedEntry(FilerState * st) {
  if (st->sel < 0 || st->sel >= st->entryCount) return;
  const FileEntry e = st->entries[st->sel];
  st->toolbarFocused = false;
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

static void onKeyDown(WPARAM vkCode, bool isShift, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);

  if (st->inputModal.isOpen()) {
    bool confirmed = false, closed = false;
    st->inputModal.handleKey(vkCode, isShift, confirmed, closed);
    if (confirmed) {
      executeRenameSelected(st, st->inputModal.text());
      st->window.requestRepaint(true);
    } else {
      st->window.requestRepaint(closed);
    }
    return;
  }

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

  int tKey = toolIndexFromKey(vkCode);
  if (tKey >= 0) {
    st->toolbarFocused = true;
    st->toolbarSel = tKey;
    st->window.requestRepaint(true);
    return;
  }

  if (st->toolbarFocused) {
    switch (vkCode) {
      case VK_LEFT:
        st->toolbarSel = (st->toolbarSel + kToolCount - 1) % kToolCount;
        st->window.requestRepaint(true);
        break;
      case VK_RIGHT:
        st->toolbarSel = (st->toolbarSel + 1) % kToolCount;
        st->window.requestRepaint(true);
        break;
      case VK_RETURN:
      case VK_F3:
        executeToolAction(st, st->toolbarSel);
        break;
      case VK_ESCAPE:
      case VK_BACK:
      case VK_DOWN:
        st->toolbarFocused = false;
        st->window.requestRepaint(true);
        break;
      case VK_HOME:
      case VK_F10:
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
      executeToolAction(st, ToolRoot);
      break;
    case VK_F2:
      executeToolAction(st, ToolLilium);
      break;
    case VK_F4:
      executeToolAction(st, ToolRefresh);
      break;
    case VK_F8:
    case 'C':
      executeToolAction(st, ToolCopy);
      break;
    case VK_F9:
      executeToolAction(st, ToolPaste);
      break;
    case VK_F5:
    case VK_DELETE:
      executeToolAction(st, ToolDelete);
      break;
    case VK_F6:
      executeToolAction(st, ToolRename);
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

  if (st->inputModal.isOpen()) {
    bool confirmed = false, closed = false;
    st->inputModal.handleClick(x, y, confirmed, closed);
    if (confirmed) {
      executeRenameSelected(st, st->inputModal.text());
      st->window.requestRepaint(true);
    } else {
      st->window.requestRepaint(closed);
    }
    return;
  }

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
    } else if (st->toolbarFocused) {
      st->toolbarFocused = false;
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

  if (y >= 80 && y < 110) {
    for (int i = 0; i < kToolCount; ++i) {
      int bx = 12 + i * 50;
      if (x >= bx && x < bx + 45) {
        if (st->toolbarFocused && st->toolbarSel == i) {
          executeToolAction(st, i);
        } else {
          st->toolbarFocused = true;
          st->toolbarSel = i;
          st->window.requestRepaint(true);
        }
        return;
      }
    }
  }

  if (x >= 16 && x < LiliumUI::kMainW - 16 && y >= 118 && y < 118 + kVisibleRows * 34) {
    st->toolbarFocused = false;
    int r = (y - 118) / 34;
    int idx = st->scroll + r;
    if (idx < st->entryCount) {
      if (st->sel == idx) {
        activateSelectedEntry(st);
      } else {
        st->prevSel = st->sel;
        st->prevScroll = st->scroll;
        st->sel = idx;
        st->window.requestRepaint(true);
      }
    } else {
      st->window.requestRepaint(true);
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  FilerState * st = static_cast<FilerState *>(userData);
  wchar_t statusLine[48];
  if (st->viewing) {
    wcscpy(statusLine, L"閲覧中");
  } else if (st->toolbarFocused) {
    wcscpy(statusLine, L"機能アイコン");
  } else {
    _snwprintf(statusLine, 48, L"%d 項目", st->entryCount);
    statusLine[47] = L'\0';
  }
  LiliumUI::renderSidebarStatus(c, statusLine);
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * rawMem = calloc(1, sizeof(FilerState));
  if (!rawMem) return 1;
  FilerState * state = new (rawMem) FilerState();
  state->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  trimTrailingSlashCopy(rootDir, state->selfLiliumRoot, MAX_PATH);
  loadDirectory(state, rootDir);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;

  int rc = 1;
  if (state->window.create(L"LiliumFilerWinClass",
                           L"Lilium Filer (SHARP Brain 800x480)",
                           cb, state, state->manageTaskBar)) {
    rc = state->window.runLoop();
  }

  freeViewerBuffer(state);
  state->~FilerState();
  free(rawMem);
  return rc;
}
