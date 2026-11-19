#define LILIUM_WINCE_COMPAT_IMPL

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wince_compat.h"

static int utf8ToWidePath(const char * utf8, wchar_t * outW, int maxW) {
  if (!utf8 || !utf8[0] || !outW || maxW <= 0) return 0;
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, outW, maxW);
  if (len <= 0) return 0;
  for (int i = 0; outW[i]; ++i) {
    if (outW[i] == L'/') outW[i] = L'\\';
  }
  return len;
}

FILE * lilium_fopen(const char * utf8Path, const char * mode) {
  wchar_t wpath[MAX_PATH];
  wchar_t wmode[16];
  if (!utf8ToWidePath(utf8Path, wpath, MAX_PATH)) return NULL;
  if (!mode || !mode[0]) return NULL;
  if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16) <= 0) return NULL;
  return _wfopen(wpath, wmode);
}

int lilium_remove(const char * utf8Path) {
  wchar_t wpath[MAX_PATH];
  if (!utf8ToWidePath(utf8Path, wpath, MAX_PATH)) return -1;
  return DeleteFileW(wpath) ? 0 : -1;
}

int lilium_rename(const char * oldUtf8, const char * newUtf8) {
  wchar_t wold[MAX_PATH];
  wchar_t wnew[MAX_PATH];
  if (!utf8ToWidePath(oldUtf8, wold, MAX_PATH)) return -1;
  if (!utf8ToWidePath(newUtf8, wnew, MAX_PATH)) return -1;
  if (MoveFileW(wold, wnew)) return 0;
  DeleteFileW(wnew);
  return MoveFileW(wold, wnew) ? 0 : -1;
}

int lilium_mkdir(const char * utf8Path) {
  wchar_t wpath[MAX_PATH];
  if (!utf8ToWidePath(utf8Path, wpath, MAX_PATH)) return -1;
  size_t len = wcslen(wpath);
  while (len > 1 && wpath[len - 1] == L'\\') {
    if (len == 3 && wpath[1] == L':') break;
    wpath[--len] = L'\0';
  }
  if (CreateDirectoryW(wpath, NULL)) return 0;
  DWORD attr = GetFileAttributesW(wpath);
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    return 0;
  }
  return -1;
}

int lilium_file_exists(const char * utf8Path) {
  wchar_t wpath[MAX_PATH];
  if (!utf8ToWidePath(utf8Path, wpath, MAX_PATH)) return 0;
  return (GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
}

char * lilium_getenv(const char * name) {
  (void)name;
  return NULL;
}

int lilium_system(const char * cmd) {
  (void)cmd;
  return -1;
}
