#ifndef LILIUM_SUITE_INSTALLER_CORE_H
#define LILIUM_SUITE_INSTALLER_CORE_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "../common/lilium_ui.h"
#include "../common/lilium_settings.h"

namespace LiliumInstallerCore {

static constexpr const wchar_t * kInstallerVersion = LiliumSettings::kLiliumVersion;
static constexpr int kCatalogCount = 14;
static constexpr int kMaxFilesPerApp = 4;
static constexpr int kMaxDrives = 16;
static constexpr int kMaxInstallTasks = 160;

enum class BrainGen {
  Unknown = 0,
  Gen4Supported,     // PW-SH4..SH7, SB4..SB7, SJ4..SJ5, SS6..SS7, H7700..H9100 etc. (exeopener needed)
  Gen3Supported,     // PW-SH1..SH3, SB1..SB3, SJ1..SJ3, SA1..SA3, SR1, HC4..HC6 (exeopener optional)
  Gen1or2Blocked,    // Pre-PW-SH1 (480x320 HVGA, unsupported)
  Gen5Blocked        // PW-H1/H2/S1/S2/B1/B2/J1/J2/A1/A2/ES8/ES9 (2021+ non-WinCE, unsupported)
};

enum class InstallDetectStatus {
  NotInstalled = 0,
  OlderVersion,      // Existing version < kInstallerVersion -> update prompt
  SameOrNewer        // Existing version >= kInstallerVersion -> already installed prompt
};

enum class DryRunScenario {
  None = 0,
  FreshInstall,      // Simulate clean SD card install
  UpdateFromOlder,   // Simulate updating v2.5.0 -> v2.7.0
  AlreadyInstalled   // Simulate inspecting already-installed v2.7.0
};

enum class LoaderMode {
  None = 0,
  UserExeOpener,     // Copy user-supplied exeopener.exe -> AppMain.exe
  ReuseExisting,     // Keep existing AppMain.exe on target
  DirectNoOpener,    // Gen 3 only: copy AppMain_.exe -> AppMain.exe
  VirtualDryRun      // Dry-run virtual loader
};

struct AppCatalogItem {
  const wchar_t * id;
  const wchar_t * title;
  const wchar_t * shortName;
  const wchar_t * standaloneFolder;
  const wchar_t * descLine1;
  const wchar_t * descLine2;
  int iconKind;       // 99=Launcher, 0..10 matching Launcher, 11=Chat10M, 12=IME
  bool required;
  bool defaultOn;
  bool recommendedOn;
  bool minimalOn;
  int fileCount;
  const wchar_t * relFiles[kMaxFilesPerApp];
};

static constexpr AppCatalogItem kAppCatalog[kCatalogCount] = {
  { L"launcher", L"Lilium Launcher", L"ランチャー", L"Lilium",
    L"4x3画面ランチャー本体(推奨)", L"連携機能・日本語IME自動管理",
    99, false, true, true, true, 3,
    { L"AppMain_.exe", L"font.ttf", L"misaki_gothic.ttf", nullptr } },
  { L"calc", L"Lilium Calc", L"関数電卓", L"LiliumCalc",
    L"グラフ関数電卓+経理電卓", L"Epsilon/Upsilon + CashCalc",
    0, false, true, true, true, 1,
    { L"apps\\LiliumCalc.exe", nullptr, nullptr, nullptr } },
  { L"editor", L"Lilium Editor", L"メモ帳", L"LiliumEditor",
    L"Markdown対応テキストメモ", L"日本語IME連携テキスト編集",
    7, false, true, true, false, 1,
    { L"apps\\LiliumEditor.exe", nullptr, nullptr, nullptr } },
  { L"timetable", L"Lilium TimeTable", L"時間割", L"LiliumTimeTable",
    L"高校/大学時間割管理アプリ", L"コマ配色・時刻連動対応",
    6, false, true, true, false, 1,
    { L"apps\\LiliumTimeTable.exe", nullptr, nullptr, nullptr } },
  { L"filer", L"Lilium Filer", L"ファイラ", L"LiliumFiler",
    L"システム保護付きファイル管理", L"HEX/テキスト表示・改名対応",
    3, false, true, true, false, 1,
    { L"apps\\LiliumFiler.exe", nullptr, nullptr, nullptr } },
  { L"taskmgr", L"Lilium TaskMgr", L"タスク管理", L"LiliumTaskMgr",
    L"プロセス・RAM監視/辞書終了", L"内蔵辞書メモリの安全解放",
    1, false, true, true, false, 1,
    { L"apps\\LiliumTaskMgr.exe", nullptr, nullptr, nullptr } },
  { L"clock", L"Lilium Clock", L"時計/タイマ", L"LiliumClock",
    L"デジタル時計・ストップウォッチ", L"タイマー付・完全消音動作",
    2, false, true, true, false, 1,
    { L"apps\\LiliumClock.exe", nullptr, nullptr, nullptr } },
  { L"life", L"Lilium Life", L"ライフゲーム", L"LiliumLife",
    L"Conway / Brian / Langton", L"セルオートマトン&画面保護",
    5, false, true, true, false, 1,
    { L"apps\\LiliumLife.exe", nullptr, nullptr, nullptr } },
  { L"setting", L"Lilium Setting", L"統合設定", L"LiliumSetting",
    L"輝度・電源・蓋・並替・ログ", L"lilium_settings.cfg 一括管理",
    8, false, true, true, true, 1,
    { L"apps\\LiliumSetting.exe", nullptr, nullptr, nullptr } },
  { L"recorder", L"Lilium Recorder", L"録音機", L"LiliumRecorder",
    L"内蔵マイク PCM WAV録音", L"波形表示・倍速・A-Bリピート",
    9, false, true, true, false, 1,
    { L"apps\\LiliumRecorder.exe", nullptr, nullptr, nullptr } },
  { L"chat17m", L"Lilium Chat 1.7M", L"AIチャット小", L"LiliumChat",
    L"ずんだもん 1.7M ローカルLLM", L"軽量高速・1往復会話モデル",
    10, false, true, true, false, 3,
    { L"apps\\LiliumChat.exe", L"apps\\models\\zundamon-v4-base_q8.bin", L"apps\\models\\tok.bin", nullptr } },
  { L"chat10m", L"Akebono +10M", L"AI追加10M", L"LiliumChat",
    L"Akebono 10M 連続会話モデル", L"容量約10.5MB(Chat本体連動)",
    11, false, true, false, false, 2,
    { L"apps\\models\\akebono-10m-chat_q8.bin", L"apps\\models\\akebono-10m-chat_q8.tok", nullptr, nullptr } },
  { L"doom", L"Lilium DOOM", L"DOOM", L"LiliumDOOM",
    L"サイレント版 Freedoom Ep.1", L"音声なし・軽量ZWAD版(約4.6MB)",
    4, false, true, false, false, 2,
    { L"apps\\doom.exe", L"apps\\freedoom1.zwad", nullptr, nullptr } },
  { L"ime", L"Lilium IME", L"日本語IME", L"LiliumIME",
    L"常駐ローマ字漢字変換デーモン", L"Mozc由来辞書(約920KB・推奨)",
    12, false, true, true, true, 2,
    { L"apps\\LiliumIME.exe", L"apps\\lilium_ime.dic", nullptr, nullptr } }
};

struct TargetInspectInfo {
  bool dirExists = false;
  bool hasAppMain = false;
  bool hasAppMainUnderscore = false;
  bool hasAppMainCfg = false;
  bool hasManifest = false;
  InstallDetectStatus status = InstallDetectStatus::NotInstalled;
  wchar_t installedVersion[32] = L"";
  int installedAppCount = 0;
  uint32_t installedAppMask = 0;
};

struct DriveEntry {
  wchar_t rootPath[MAX_PATH] = L"";     // e.g. L"E:\\" or chosen folder path
  wchar_t destLiliumDir[MAX_PATH] = L""; // e.g. L"E:\\アプリ\\Lilium"
  wchar_t label[64] = L"";
  wchar_t fsName[32] = L"";
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  bool isRemovable = false;
  bool isCustomFolder = false;
  bool isReady = false;
  TargetInspectInfo existing = {};
};

enum class CopyTaskKind {
  MkDir = 0,
  CopyFile,
  ReuseLoader,
  VirtualLoader,
  WriteCfg,
  WriteIndexDin,
  WriteManifest
};

struct InstallTask {
  CopyTaskKind kind = CopyTaskKind::CopyFile;
  wchar_t srcPath[MAX_PATH] = L"";
  wchar_t dstPath[MAX_PATH] = L"";
  wchar_t displayName[96] = L"";
  uint64_t sizeBytes = 0;
};

inline bool fileExistsW(const wchar_t * path) {
  if (!path || !path[0]) return false;
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline bool dirExistsW(const wchar_t * path) {
  if (!path || !path[0]) return false;
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline uint64_t queryFileSizeW(const wchar_t * path) {
  WIN32_FILE_ATTRIBUTE_DATA fad = {};
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return 0;
  if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
  return (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | static_cast<uint64_t>(fad.nFileSizeLow);
}

inline void trimTrailingSlashW(wchar_t * path) {
  if (!path) return;
  size_t n = wcslen(path);
  while (n > 1 && (path[n - 1] == L'\\' || path[n - 1] == L'/')) {
    if (n == 3 && path[1] == L':') break;
    path[--n] = L'\0';
  }
}

inline void formatBytesHuman(uint64_t bytes, wchar_t * out, size_t maxLen) {
  if (!out || maxLen == 0) return;
  if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    _snwprintf(out, maxLen, L"%.1f GB", gb);
  } else if (bytes >= 1024ULL * 1024ULL) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    _snwprintf(out, maxLen, L"%.1f MB", mb);
  } else if (bytes >= 1024ULL) {
    uint64_t kb = (bytes + 1023ULL) / 1024ULL;
    _snwprintf(out, maxLen, L"%llu KB", static_cast<unsigned long long>(kb));
  } else {
    _snwprintf(out, maxLen, L"%llu B", static_cast<unsigned long long>(bytes));
  }
  out[maxLen - 1] = L'\0';
}

inline bool isDryRunCode(const wchar_t * rawInput) {
  if (!rawInput) return false;
  wchar_t buf[64] = L"";
  int len = 0;
  for (int i = 0; rawInput[i] && len < 60; ++i) {
    wchar_t ch = rawInput[i];
    if (iswspace(ch)) continue;
    buf[len++] = static_cast<wchar_t>(towupper(ch));
  }
  buf[len] = L'\0';
  return wcscmp(buf, L"LILIUM-TEST") == 0 ||
         wcscmp(buf, L"DRYRUN") == 0 ||
         wcscmp(buf, L"TESTMODE") == 0;
}

inline void normalizeModelToken(const wchar_t * rawInput, wchar_t * out, size_t maxLen) {
  if (!out || maxLen == 0) return;
  out[0] = L'\0';
  if (!rawInput) return;
  wchar_t tmp[64] = L"";
  int j = 0;
  for (int i = 0; rawInput[i] && j < 60; ++i) {
    wchar_t ch = rawInput[i];
    if (iswspace(ch) || ch == L'-' || ch == L'_') continue;
    tmp[j++] = static_cast<wchar_t>(towupper(ch));
  }
  tmp[j] = L'\0';
  const wchar_t * p = tmp;
  if (p[0] == L'P' && p[1] == L'W') {
    p += 2;
  }
  wcsncpy(out, p, maxLen - 1);
  out[maxLen - 1] = L'\0';
}

inline bool startsWithW(const wchar_t * s, const wchar_t * prefix) {
  size_t plen = wcslen(prefix);
  return wcsncmp(s, prefix, plen) == 0;
}

inline BrainGen classifyBrainModel(const wchar_t * rawInput) {
  wchar_t tok[64] = L"";
  normalizeModelToken(rawInput, tok, 64);
  if (!tok[0]) return BrainGen::Unknown;

  // Gen 4 (2017-2020, 800x480, WinCE 6.0, needs exeopener on non-SB5+ models)
  static constexpr const wchar_t * kGen4ExactOrPrefix[] = {
    L"SH4", L"SH5", L"SH6", L"SH7",
    L"SB4", L"SB5", L"SB6", L"SB7",
    L"SJ4", L"SJ5", L"SS6", L"SS7",
    L"SA4", L"SA5", L"AA1", L"AA2",
    L"AJ1", L"AJ2", L"SR2", L"SR3",
    L"H7700", L"H7800", L"H8000", L"H8100", L"H9100"
  };
  for (size_t i = 0; i < sizeof(kGen4ExactOrPrefix) / sizeof(kGen4ExactOrPrefix[0]); ++i) {
    if (startsWithW(tok, kGen4ExactOrPrefix[i])) return BrainGen::Gen4Supported;
  }

  // Gen 3 (2014-2016, 800x480, WinCE 6.0, no protect)
  static constexpr const wchar_t * kGen3Prefix[] = {
    L"SH1", L"SH2", L"SH3",
    L"SB1", L"SB2", L"SB3",
    L"SJ1", L"SJ2", L"SJ3",
    L"SA1", L"SA2", L"SA3",
    L"SR1", L"HC4", L"HC5", L"HC6"
  };
  for (size_t i = 0; i < sizeof(kGen3Prefix) / sizeof(kGen3Prefix[0]); ++i) {
    if (startsWithW(tok, kGen3Prefix[i])) return BrainGen::Gen3Supported;
  }

  // Gen 1 & Gen 2 (2008-2013, 480x320 HVGA, pre-PW-SH1, blocked)
  static constexpr const wchar_t * kGen12Prefix[] = {
    L"AC8", L"AC9", L"TC9", L"GC5", L"GC6",
    L"G40", L"G42", L"G50", L"G51", L"G52", L"G53",
    L"GX3", L"GX5", L"A70", L"A72", L"A73", L"A74",
    L"A90", L"A91", L"A92", L"A93",
    L"HC1", L"HC2", L"HC3"
  };
  for (size_t i = 0; i < sizeof(kGen12Prefix) / sizeof(kGen12Prefix[0]); ++i) {
    if (startsWithW(tok, kGen12Prefix[i])) return BrainGen::Gen1or2Blocked;
  }

  // Gen 5 (2021+, non-WinCE, blocked)
  static constexpr const wchar_t * kGen5Exact[] = {
    L"H1", L"H2", L"S1", L"S2", L"B1", L"B2", L"J1", L"J2", L"A1", L"A2"
  };
  for (size_t i = 0; i < sizeof(kGen5Exact) / sizeof(kGen5Exact[0]); ++i) {
    if (wcscmp(tok, kGen5Exact[i]) == 0) return BrainGen::Gen5Blocked;
  }
  if (startsWithW(tok, L"ES8") || startsWithW(tok, L"ES9")) return BrainGen::Gen5Blocked;

  return BrainGen::Unknown;
}

inline bool parseSemVer(const wchar_t * ver, int & maj, int & min, int & pat) {
  maj = min = pat = 0;
  if (!ver || !ver[0]) return false;
  const wchar_t * p = ver;
  while (*p && (*p == L'v' || *p == L'V' || iswspace(*p))) ++p;
  int a = 0, b = 0, c = 0;
  if (swscanf(p, L"%d.%d.%d", &a, &b, &c) < 2) return false;
  maj = a;
  min = b;
  pat = c;
  return true;
}

inline int compareSemVer(const wchar_t * a, const wchar_t * b) {
  int ma = 0, na = 0, pa = 0;
  int mb = 0, nb = 0, pb = 0;
  bool okA = parseSemVer(a, ma, na, pa);
  bool okB = parseSemVer(b, mb, nb, pb);
  if (!okA && !okB) return 0;
  if (!okA) return -1;
  if (!okB) return 1;
  if (ma != mb) return (ma < mb) ? -1 : 1;
  if (na != nb) return (na < nb) ? -1 : 1;
  if (pa != pb) return (pa < pb) ? -1 : 1;
  return 0;
}

inline void computeAppsRootDir(const wchar_t * basePath, wchar_t * outAppsDir, size_t maxLen) {
  if (!outAppsDir || maxLen == 0) return;
  outAppsDir[0] = L'\0';
  if (!basePath || !basePath[0]) return;

  wchar_t clean[MAX_PATH] = L"";
  wcsncpy(clean, basePath, MAX_PATH - 1);
  clean[MAX_PATH - 1] = L'\0';
  trimTrailingSlashW(clean);

  size_t len = wcslen(clean);
  if (len == 2 && clean[1] == L':') {
    _snwprintf(outAppsDir, maxLen, L"%ls\\アプリ", clean);
    outAppsDir[maxLen - 1] = L'\0';
    return;
  }
  if (len == 3 && clean[1] == L':' && clean[2] == L'\\') {
    _snwprintf(outAppsDir, maxLen, L"%c:\\アプリ", clean[0]);
    outAppsDir[maxLen - 1] = L'\0';
    return;
  }

  const wchar_t * slash = wcsrchr(clean, L'\\');
  const wchar_t * fslash = wcsrchr(clean, L'/');
  if (fslash && (!slash || fslash > slash)) slash = fslash;
  const wchar_t * leaf = slash ? (slash + 1) : clean;

  if (_wcsicmp(leaf, L"アプリ") == 0) {
    wcsncpy(outAppsDir, clean, maxLen - 1);
  } else {
    _snwprintf(outAppsDir, maxLen, L"%ls\\アプリ", clean);
  }
  outAppsDir[maxLen - 1] = L'\0';
}

inline void computeDestLiliumDir(const wchar_t * basePath, wchar_t * outDir, size_t maxLen) {
  if (!outDir || maxLen == 0) return;
  outDir[0] = L'\0';
  wchar_t appsRoot[MAX_PATH] = L"";
  computeAppsRootDir(basePath, appsRoot, MAX_PATH);
  if (appsRoot[0]) {
    _snwprintf(outDir, maxLen, L"%ls\\Lilium", appsRoot);
    outDir[maxLen - 1] = L'\0';
  }
}

inline void extractAppsRootFromDestDir(const wchar_t * destLiliumDir, wchar_t * outAppsDir, size_t maxLen) {
  if (!outAppsDir || maxLen == 0) return;
  outAppsDir[0] = L'\0';
  if (!destLiliumDir || !destLiliumDir[0]) return;
  wcsncpy(outAppsDir, destLiliumDir, maxLen - 1);
  outAppsDir[maxLen - 1] = L'\0';
  trimTrailingSlashW(outAppsDir);
  wchar_t * lastSl = wcsrchr(outAppsDir, L'\\');
  if (lastSl && _wcsicmp(lastSl + 1, L"Lilium") == 0) {
    *lastSl = L'\0';
    return;
  }
  computeAppsRootDir(destLiliumDir, outAppsDir, maxLen);
}

inline bool scanVersionFromExe(const wchar_t * exePath, wchar_t * outVer, size_t maxLen) {
  if (!outVer || maxLen == 0) return false;
  outVer[0] = L'\0';
  uint64_t fsz = queryFileSizeW(exePath);
  if (fsz < 256 || fsz > 2ULL * 1024ULL * 1024ULL) return false;

  HANDLE h = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;

  size_t sz = static_cast<size_t>(fsz);
  uint8_t * buf = static_cast<uint8_t *>(malloc(sz));
  bool found = false;
  if (buf) {
    DWORD rd = 0;
    if (ReadFile(h, buf, static_cast<DWORD>(sz), &rd, NULL) && rd == sz) {
      // Search UTF-16LE literal matching L"v<d>.<d>.<d>" (e.g. L"v2.7.0")
      for (size_t i = 0; i + 12 <= sz; i += 2) {
        if (buf[i] == 'v' && buf[i + 1] == 0 &&
            buf[i + 2] >= '0' && buf[i + 2] <= '9' && buf[i + 3] == 0 &&
            buf[i + 4] == '.' && buf[i + 5] == 0 &&
            buf[i + 6] >= '0' && buf[i + 6] <= '9' && buf[i + 7] == 0 &&
            buf[i + 8] == '.' && buf[i + 9] == 0 &&
            buf[i + 10] >= '0' && buf[i + 10] <= '9' && buf[i + 11] == 0) {
          // Make sure next wchar isn't also a digit/garbage (or parse cleanly)
          wchar_t cand[16] = {
            L'v',
            static_cast<wchar_t>(buf[i + 2]),
            L'.',
            static_cast<wchar_t>(buf[i + 6]),
            L'.',
            static_cast<wchar_t>(buf[i + 10]),
            L'\0'
          };
          // Heuristic: LiliumSetting versions are >= v2.0.0; pick first valid v2+ match if multiple
          if (cand[1] >= L'2' || !found) {
            wcsncpy(outVer, cand, maxLen - 1);
            outVer[maxLen - 1] = L'\0';
            found = true;
            if (cand[1] >= L'2') break;
          }
        }
      }
    }
    free(buf);
  }
  CloseHandle(h);
  return found;
}

inline bool readVersionFromManifest(const wchar_t * manifestPath, wchar_t * outVer, size_t maxLen) {
  if (!outVer || maxLen == 0) return false;
  outVer[0] = L'\0';
  FILE * fp = _wfopen(manifestPath, L"rb");
  if (!fp) return false;
  char line[256];
  bool found = false;
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "version=", 8) == 0) {
      const char * p = line + 8;
      size_t k = 0;
      while (p[k] && p[k] != '\r' && p[k] != '\n' && k + 1 < maxLen) {
        outVer[k] = static_cast<wchar_t>(static_cast<unsigned char>(p[k]));
        ++k;
      }
      outVer[k] = L'\0';
      found = (k > 0);
      break;
    }
  }
  fclose(fp);
  return found;
}

inline TargetInspectInfo inspectTargetInstall(const wchar_t * destLiliumDir) {
  TargetInspectInfo info = {};
  if (!destLiliumDir || !destLiliumDir[0]) return info;
  info.dirExists = dirExistsW(destLiliumDir);
  if (!info.dirExists) {
    info.status = InstallDetectStatus::NotInstalled;
    return info;
  }

  wchar_t p[MAX_PATH] = L"";
  LiliumUI::joinPath(destLiliumDir, L"AppMain.exe", p, MAX_PATH);
  info.hasAppMain = fileExistsW(p);
  LiliumUI::joinPath(destLiliumDir, L"AppMain_.exe", p, MAX_PATH);
  info.hasAppMainUnderscore = fileExistsW(p);
  LiliumUI::joinPath(destLiliumDir, L"AppMain.cfg", p, MAX_PATH);
  info.hasAppMainCfg = fileExistsW(p);

  LiliumUI::joinPath(destLiliumDir, L"lilium_manifest.ini", p, MAX_PATH);
  if (readVersionFromManifest(p, info.installedVersion, 32)) {
    info.hasManifest = true;
  }

  for (int i = 0; i < kCatalogCount; ++i) {
    if (kAppCatalog[i].fileCount > 0 && kAppCatalog[i].relFiles[0]) {
      LiliumUI::joinPath(destLiliumDir, kAppCatalog[i].relFiles[0], p, MAX_PATH);
      if (fileExistsW(p)) {
        info.installedAppMask |= (1U << i);
        info.installedAppCount++;
      }
    }
  }

  if (!info.hasManifest) {
    wchar_t settingExe[MAX_PATH] = L"";
    LiliumUI::joinPath(destLiliumDir, L"apps\\LiliumSetting.exe", settingExe, MAX_PATH);
    if (fileExistsW(settingExe)) {
      scanVersionFromExe(settingExe, info.installedVersion, 32);
    }
  }

  bool anyCore = info.hasManifest || info.hasAppMainUnderscore || (info.installedAppCount > 0);
  if (!anyCore) {
    info.status = InstallDetectStatus::NotInstalled;
    return info;
  }

  if (!info.installedVersion[0]) {
    wcscpy(info.installedVersion, L"v1.0.0");
  }

  int cmp = compareSemVer(info.installedVersion, kInstallerVersion);
  info.status = (cmp >= 0) ? InstallDetectStatus::SameOrNewer : InstallDetectStatus::OlderVersion;
  return info;
}

inline bool validateWinCeArmExe(const wchar_t * path, wchar_t * outReason, size_t maxReason) {
  auto setErr = [&](const wchar_t * msg) {
    if (outReason && maxReason > 0) {
      wcsncpy(outReason, msg, maxReason - 1);
      outReason[maxReason - 1] = L'\0';
    }
  };

  uint64_t fsz = queryFileSizeW(path);
  if (fsz < 4096 || fsz > 2ULL * 1024ULL * 1024ULL) {
    setErr(L"ファイルサイズが妥当なローダー範囲外です(4KB..2MB)");
    return false;
  }

  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    setErr(L"ファイルを開けません");
    return false;
  }

  uint8_t dos[64] = {};
  DWORD rd = 0;
  if (!ReadFile(h, dos, sizeof(dos), &rd, NULL) || rd != sizeof(dos)) {
    CloseHandle(h);
    setErr(L"DOSヘッダを読めません");
    return false;
  }
  if (dos[0] != 'M' || dos[1] != 'Z') {
    CloseHandle(h);
    setErr(L"実行ファイル(MZヘッダ)ではありません");
    return false;
  }

  uint32_t peOff = static_cast<uint32_t>(dos[0x3C]) |
                   (static_cast<uint32_t>(dos[0x3D]) << 8) |
                   (static_cast<uint32_t>(dos[0x3E]) << 16) |
                   (static_cast<uint32_t>(dos[0x3F]) << 24);
  if (peOff < 64 || static_cast<uint64_t>(peOff) + 96 > fsz) {
    CloseHandle(h);
    setErr(L"PEヘッダ位置が不正です");
    return false;
  }

  SetFilePointer(h, static_cast<LONG>(peOff), NULL, FILE_BEGIN);
  uint8_t pe[96] = {};
  if (!ReadFile(h, pe, sizeof(pe), &rd, NULL) || rd != sizeof(pe)) {
    CloseHandle(h);
    setErr(L"PEヘッダを読めません");
    return false;
  }
  CloseHandle(h);

  if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) {
    setErr(L"PEシグネチャが不正です");
    return false;
  }

  uint16_t machine = static_cast<uint16_t>(pe[4]) | (static_cast<uint16_t>(pe[5]) << 8);
  if (machine != 0x01C0 && machine != 0x01C2 && machine != 0x01C4) {
    setErr(L"ARM用EXEではありません (PC用EXE等)");
    return false;
  }

  uint16_t optMagic = static_cast<uint16_t>(pe[24]) | (static_cast<uint16_t>(pe[25]) << 8);
  if (optMagic != 0x010B) {
    setErr(L"PE32形式ではありません");
    return false;
  }

  // Subsystem at OptionalHeader + 68 -> pe + 24 + 68 = pe + 92
  uint16_t subsystem = static_cast<uint16_t>(pe[92]) | (static_cast<uint16_t>(pe[93]) << 8);
  if (subsystem != 9) {
    setErr(L"Windows CEサブシステム(9)用EXEではありません");
    return false;
  }

  setErr(L"確認済: Windows CE ARM 実行ファイル");
  return true;
}

inline void generateAppMainCfgBytes(int cfgMode, uint8_t out[8]) {
  if (cfgMode == 1) {
    // Explicit WVGA dimensions derived from LiliumUI logical resolution
    const uint16_t w = static_cast<uint16_t>(LiliumUI::kScreenW);
    const uint16_t h = static_cast<uint16_t>(LiliumUI::kScreenH);
    out[0] = 0x01; out[1] = 0x00; out[2] = 0x00; out[3] = 0x00;
    out[4] = static_cast<uint8_t>(w & 0xFF);
    out[5] = static_cast<uint8_t>((w >> 8) & 0xFF);
    out[6] = static_cast<uint8_t>(h & 0xFF);
    out[7] = static_cast<uint8_t>((h >> 8) & 0xFF);
  } else if (cfgMode == 2) {
    // Experimental ASCII 8-byte tag
    static constexpr uint8_t kTag[8] = { 'l', 'i', 'l', 'i', 'u', 'm', 'p', 'j' };
    memcpy(out, kTag, 8);
  } else {
    // Default / Recommended: Clean-room Brain Wiki public WVGA spec
    out[0] = 0x01; out[1] = 0x00; out[2] = 0x00; out[3] = 0x00;
    out[4] = 0xFF; out[5] = 0xFF; out[6] = 0xFF; out[7] = 0xFF;
  }
}

inline bool locatePayloadDir(wchar_t * outDir, size_t maxLen) {
  if (!outDir || maxLen == 0) return false;
  outDir[0] = L'\0';
  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  if (!exeDir[0]) return false;

  static constexpr const wchar_t * kRelCandidates[] = {
    L"payload",
    L"Lilium",
    L"..\\simulator\\brain\\Lilium",
    L"..\\output\\release\\simulator\\brain\\Lilium",
    L"..\\..\\output\\release\\simulator\\brain\\Lilium",
    L"..\\..\\..\\output\\release\\simulator\\brain\\Lilium"
  };

  for (size_t i = 0; i < sizeof(kRelCandidates) / sizeof(kRelCandidates[0]); ++i) {
    wchar_t cand[MAX_PATH] = L"";
    LiliumUI::joinPath(exeDir, kRelCandidates[i], cand, MAX_PATH);
    wchar_t full[MAX_PATH] = L"";
    DWORD n = GetFullPathNameW(cand, MAX_PATH, full, NULL);
    if (n > 0 && n < MAX_PATH) {
      trimTrailingSlashW(full);
      wchar_t probe[MAX_PATH] = L"";
      LiliumUI::joinPath(full, L"AppMain_.exe", probe, MAX_PATH);
      if (fileExistsW(probe)) {
        wcsncpy(outDir, full, maxLen - 1);
        outDir[maxLen - 1] = L'\0';
        return true;
      }
    }
  }
  return false;
}

inline int scanDrives(DriveEntry * outDrives, int maxDrives) {
  if (!outDrives || maxDrives <= 0) return 0;
  int count = 0;

  wchar_t winDir[MAX_PATH] = L"";
  wchar_t sysDriveLetter = L'C';
  if (GetWindowsDirectoryW(winDir, MAX_PATH) > 0 && winDir[1] == L':') {
    sysDriveLetter = static_cast<wchar_t>(towupper(winDir[0]));
  }

  wchar_t raw[512] = L"";
  DWORD len = GetLogicalDriveStringsW(511, raw);
  if (len == 0 || len >= 511) return 0;

  // Pass 1: DRIVE_REMOVABLE (skip A:/B: floppy), Pass 2: non-system DRIVE_FIXED
  for (int pass = 0; pass < 2; ++pass) {
    const wchar_t * p = raw;
    while (*p && count < maxDrives) {
      UINT dtype = GetDriveTypeW(p);
      wchar_t dl = static_cast<wchar_t>(towupper(p[0]));
      bool want = false;
      if (pass == 0) {
        want = (dtype == DRIVE_REMOVABLE) && (dl != L'A' && dl != L'B');
      } else {
        want = (dtype == DRIVE_FIXED) && (dl != sysDriveLetter);
      }

      if (want) {
        ULARGE_INTEGER avail = {}, total = {}, freeTot = {};
        if (GetDiskFreeSpaceExW(p, &avail, &total, &freeTot) && total.QuadPart > 0) {
          DriveEntry & d = outDrives[count];
          d = DriveEntry{};
          wcsncpy(d.rootPath, p, MAX_PATH - 1);
          d.rootPath[MAX_PATH - 1] = L'\0';
          computeDestLiliumDir(p, d.destLiliumDir, MAX_PATH);

          wchar_t vol[64] = L"";
          wchar_t fs[32] = L"";
          GetVolumeInformationW(p, vol, 64, NULL, NULL, NULL, fs, 32);
          if (vol[0]) {
            wcsncpy(d.label, vol, 63);
          } else {
            wcscpy(d.label, (pass == 0) ? L"リムーバブル" : L"ローカルドライブ");
          }
          wcsncpy(d.fsName, fs[0] ? fs : L"FAT32", 31);
          d.totalBytes = total.QuadPart;
          d.freeBytes = avail.QuadPart;
          d.isRemovable = (pass == 0);
          d.isCustomFolder = false;
          d.isReady = true;
          d.existing = inspectTargetInstall(d.destLiliumDir);
          ++count;
        }
      }
      p += wcslen(p) + 1;
    }
  }
  return count;
}

inline uint64_t computeCatalogItemBytes(const wchar_t * payloadDir, int idx, bool payloadOk) {
  if (idx < 0 || idx >= kCatalogCount) return 0;
  static constexpr uint64_t kFallbackSizes[kCatalogCount] = {
    290000ULL,   // Launcher + Misaki fonts
    9760000ULL,  // Calc
    196000ULL,   // Editor
    216000ULL,   // TimeTable
    198000ULL,   // Filer
    194000ULL,   // TaskMgr
    181000ULL,   // Clock
    252000ULL,   // Life
    190000ULL,   // Setting
    191000ULL,   // Recorder
    1100000ULL,  // Chat 1.7M
    11000000ULL, // Chat +10M
    5400000ULL,  // DOOM
    1100000ULL   // IME
  };
  if (!payloadOk || !payloadDir || !payloadDir[0]) return kFallbackSizes[idx];
  uint64_t sum = 0;
  for (int f = 0; f < kAppCatalog[idx].fileCount; ++f) {
    if (!kAppCatalog[idx].relFiles[f]) continue;
    wchar_t full[MAX_PATH] = L"";
    LiliumUI::joinPath(payloadDir, kAppCatalog[idx].relFiles[f], full, MAX_PATH);
    sum += queryFileSizeW(full);
  }
  return (sum > 0) ? sum : kFallbackSizes[idx];
}

inline int buildInstallTasks(const wchar_t * payloadDir,
                             const wchar_t * destLiliumDir,
                             const bool selected[kCatalogCount],
                             LoaderMode loaderMode,
                             const wchar_t * exeopenerPath,
                             bool dryRun,
                             InstallTask * outTasks,
                             int maxTasks,
                             uint64_t & outTotalBytes) {
  outTotalBytes = 0;
  if (!destLiliumDir || !destLiliumDir[0] || !outTasks || maxTasks <= 0) return 0;
  int count = 0;

  auto pushTask = [&](CopyTaskKind k, const wchar_t * src, const wchar_t * dst, const wchar_t * disp, uint64_t sz) {
    if (count >= maxTasks) return;
    InstallTask & t = outTasks[count++];
    t.kind = k;
    if (src) {
      wcsncpy(t.srcPath, src, MAX_PATH - 1);
      t.srcPath[MAX_PATH - 1] = L'\0';
    } else {
      t.srcPath[0] = L'\0';
    }
    if (dst) {
      wcsncpy(t.dstPath, dst, MAX_PATH - 1);
      t.dstPath[MAX_PATH - 1] = L'\0';
    } else {
      t.dstPath[0] = L'\0';
    }
    wcsncpy(t.displayName, disp ? disp : L"", 95);
    t.displayName[95] = L'\0';
    t.sizeBytes = sz;
    outTotalBytes += sz;
  };

  const bool standaloneMode = !selected[0];

  if (!standaloneMode) {
    // Launcher mode: integrated <Base>\アプリ\Lilium\ bundle
    wchar_t appFolder[MAX_PATH] = L"";
    extractAppsRootFromDestDir(destLiliumDir, appFolder, MAX_PATH);

    if (appFolder[0]) {
      pushTask(CopyTaskKind::MkDir, nullptr, appFolder, L"アプリフォルダ確認", 0);
    }
    pushTask(CopyTaskKind::MkDir, nullptr, destLiliumDir, L"Liliumフォルダ作成", 0);

    wchar_t appsSub[MAX_PATH] = L"";
    LiliumUI::joinPath(destLiliumDir, L"apps", appsSub, MAX_PATH);
    pushTask(CopyTaskKind::MkDir, nullptr, appsSub, L"appsフォルダ作成", 0);

    if (selected[10] || selected[11]) {
      wchar_t modelsSub[MAX_PATH] = L"";
      LiliumUI::joinPath(destLiliumDir, L"apps\\models", modelsSub, MAX_PATH);
      pushTask(CopyTaskKind::MkDir, nullptr, modelsSub, L"modelsフォルダ作成", 0);
    }

    wchar_t scriptsSub[MAX_PATH] = L"";
    LiliumUI::joinPath(destLiliumDir, L"scripts", scriptsSub, MAX_PATH);
    pushTask(CopyTaskKind::MkDir, nullptr, scriptsSub, L"scriptsフォルダ確認", 0);

    // Loader (AppMain.exe)
    wchar_t dstAppMain[MAX_PATH] = L"";
    LiliumUI::joinPath(destLiliumDir, L"AppMain.exe", dstAppMain, MAX_PATH);
    if (loaderMode == LoaderMode::UserExeOpener && exeopenerPath && exeopenerPath[0]) {
      uint64_t lsz = queryFileSizeW(exeopenerPath);
      if (lsz == 0) lsz = 192512ULL;
      pushTask(CopyTaskKind::CopyFile, exeopenerPath, dstAppMain, L"AppMain.exe (exeopener)", lsz);
    } else if (loaderMode == LoaderMode::ReuseExisting) {
      pushTask(CopyTaskKind::ReuseLoader, nullptr, dstAppMain, L"AppMain.exe (既存を流用)", 0);
    } else if (loaderMode == LoaderMode::DirectNoOpener) {
      wchar_t srcLauncher[MAX_PATH] = L"";
      LiliumUI::joinPath(payloadDir, L"AppMain_.exe", srcLauncher, MAX_PATH);
      uint64_t lsz = queryFileSizeW(srcLauncher);
      if (lsz == 0) lsz = 188416ULL;
      pushTask(CopyTaskKind::CopyFile, srcLauncher, dstAppMain, L"AppMain.exe (第3世代直接)", lsz);
    } else {
      pushTask(CopyTaskKind::VirtualLoader, nullptr, dstAppMain, L"AppMain.exe (試運転仮想)", dryRun ? 192512ULL : 0);
    }

    // Selected catalog files
    for (int i = 0; i < kCatalogCount; ++i) {
      if (!selected[i]) continue;
      for (int f = 0; f < kAppCatalog[i].fileCount; ++f) {
        const wchar_t * rel = kAppCatalog[i].relFiles[f];
        if (!rel) continue;
        wchar_t src[MAX_PATH] = L"";
        wchar_t dst[MAX_PATH] = L"";
        LiliumUI::joinPath(payloadDir, rel, src, MAX_PATH);
        LiliumUI::joinPath(destLiliumDir, rel, dst, MAX_PATH);
        uint64_t fsz = queryFileSizeW(src);
        if (fsz == 0 && dryRun) fsz = 200000ULL;
        if (fsz > 0 || dryRun) {
          pushTask(CopyTaskKind::CopyFile, src, dst, rel, fsz);
        }
      }
    }

    wchar_t dstCfg[MAX_PATH] = L"";
    wchar_t dstDin[MAX_PATH] = L"";
    wchar_t dstMan[MAX_PATH] = L"";
    LiliumUI::joinPath(destLiliumDir, L"AppMain.cfg", dstCfg, MAX_PATH);
    LiliumUI::joinPath(destLiliumDir, L"index.din", dstDin, MAX_PATH);
    LiliumUI::joinPath(destLiliumDir, L"lilium_manifest.ini", dstMan, MAX_PATH);
    pushTask(CopyTaskKind::WriteCfg, nullptr, dstCfg, L"AppMain.cfg (8B生成)", 8);
    pushTask(CopyTaskKind::WriteIndexDin, nullptr, dstDin, L"index.din 生成", 0);
    pushTask(CopyTaskKind::WriteManifest, nullptr, dstMan, L"lilium_manifest.ini 生成", 256);
    return count;
  }

  // Standalone (No-Launcher) mode: each selected app goes into <Base>\アプリ\<AppName>
  wchar_t appsRoot[MAX_PATH] = L"";
  extractAppsRootFromDestDir(destLiliumDir, appsRoot, MAX_PATH);
  if (appsRoot[0]) {
    pushTask(CopyTaskKind::MkDir, nullptr, appsRoot, L"アプリフォルダ確認", 0);
  }

  wchar_t fallbackExistingLoader[MAX_PATH] = L"";
  LiliumUI::joinPath(destLiliumDir, L"AppMain.exe", fallbackExistingLoader, MAX_PATH);

  for (int i = 1; i < kCatalogCount; ++i) {
    if (i == 11) continue; // chat10m shares LiliumChat folder and is handled inside i == 10
    if (!selected[i]) continue;
    if (kAppCatalog[i].fileCount <= 0 || !kAppCatalog[i].relFiles[0]) continue;

    const wchar_t * folder = kAppCatalog[i].standaloneFolder;
    wchar_t appDir[MAX_PATH] = L"";
    LiliumUI::joinPath(appsRoot, folder, appDir, MAX_PATH);
    wchar_t mkDisp[96] = L"";
    _snwprintf(mkDisp, 95, L"%ls フォルダ", folder);
    pushTask(CopyTaskKind::MkDir, nullptr, appDir, mkDisp, 0);

    wchar_t srcMain[MAX_PATH] = L"";
    LiliumUI::joinPath(payloadDir, kAppCatalog[i].relFiles[0], srcMain, MAX_PATH);
    uint64_t mainSz = queryFileSizeW(srcMain);
    if (mainSz == 0 && dryRun) mainSz = 200000ULL;

    wchar_t dstMainLoader[MAX_PATH] = L"";
    wchar_t dstMainBody[MAX_PATH] = L"";
    LiliumUI::joinPath(appDir, L"AppMain.exe", dstMainLoader, MAX_PATH);
    LiliumUI::joinPath(appDir, L"AppMain_.exe", dstMainBody, MAX_PATH);

    wchar_t dispA[96] = L"";
    wchar_t dispB[96] = L"";
    _snwprintf(dispA, 95, L"%ls\\AppMain.exe", folder);
    _snwprintf(dispB, 95, L"%ls\\AppMain_.exe", folder);

    if (loaderMode == LoaderMode::UserExeOpener && exeopenerPath && exeopenerPath[0]) {
      uint64_t lsz = queryFileSizeW(exeopenerPath);
      if (lsz == 0) lsz = 192512ULL;
      pushTask(CopyTaskKind::CopyFile, exeopenerPath, dstMainLoader, dispA, lsz);
      if (mainSz > 0 || dryRun) pushTask(CopyTaskKind::CopyFile, srcMain, dstMainBody, dispB, mainSz);
    } else if (loaderMode == LoaderMode::DirectNoOpener) {
      if (mainSz > 0 || dryRun) pushTask(CopyTaskKind::CopyFile, srcMain, dstMainLoader, dispA, mainSz);
    } else if (loaderMode == LoaderMode::ReuseExisting) {
      if (fileExistsW(dstMainLoader)) {
        pushTask(CopyTaskKind::ReuseLoader, nullptr, dstMainLoader, dispA, 0);
      } else if (fileExistsW(fallbackExistingLoader)) {
        uint64_t lsz = queryFileSizeW(fallbackExistingLoader);
        pushTask(CopyTaskKind::CopyFile, fallbackExistingLoader, dstMainLoader, dispA, lsz);
      } else if (dryRun) {
        pushTask(CopyTaskKind::VirtualLoader, nullptr, dstMainLoader, dispA, 192512ULL);
      } else {
        pushTask(CopyTaskKind::ReuseLoader, nullptr, dstMainLoader, dispA, 0);
      }
      if (mainSz > 0 || dryRun) pushTask(CopyTaskKind::CopyFile, srcMain, dstMainBody, dispB, mainSz);
    } else {
      pushTask(CopyTaskKind::VirtualLoader, nullptr, dstMainLoader, dispA, dryRun ? 192512ULL : 0);
      if (mainSz > 0 || dryRun) pushTask(CopyTaskKind::CopyFile, srcMain, dstMainBody, dispB, mainSz);
    }

    auto copyAsset = [&](const wchar_t * srcRel, const wchar_t * dstRel) {
      wchar_t srcA[MAX_PATH] = L"";
      wchar_t dstA[MAX_PATH] = L"";
      LiliumUI::joinPath(payloadDir, srcRel, srcA, MAX_PATH);
      LiliumUI::joinPath(appDir, dstRel, dstA, MAX_PATH);
      uint64_t asz = queryFileSizeW(srcA);
      if (asz == 0 && dryRun) asz = 200000ULL;
      if (asz > 0 || dryRun) {
        wchar_t dName[96] = L"";
        _snwprintf(dName, 95, L"%ls\\%ls", folder, dstRel);
        pushTask(CopyTaskKind::CopyFile, srcA, dstA, dName, asz);
      }
    };

    if (i == 1) {
      // Calc fonts
      copyAsset(L"font.ttf", L"font.ttf");
      copyAsset(L"misaki_gothic.ttf", L"misaki_gothic.ttf");
    } else if (i == 10) {
      // Chat models subfolder
      wchar_t mDir[MAX_PATH] = L"";
      LiliumUI::joinPath(appDir, L"models", mDir, MAX_PATH);
      pushTask(CopyTaskKind::MkDir, nullptr, mDir, L"LiliumChat\\models", 0);
      copyAsset(L"apps\\models\\zundamon-v4-base_q8.bin", L"models\\zundamon-v4-base_q8.bin");
      copyAsset(L"apps\\models\\tok.bin", L"models\\tok.bin");
      if (selected[11]) {
        copyAsset(L"apps\\models\\akebono-10m-chat_q8.bin", L"models\\akebono-10m-chat_q8.bin");
        copyAsset(L"apps\\models\\akebono-10m-chat_q8.tok", L"models\\akebono-10m-chat_q8.tok");
      }
    } else if (i == 12) {
      // DOOM IWAD
      copyAsset(L"apps\\freedoom1.zwad", L"freedoom1.zwad");
    } else if (i == 13) {
      // IME dictionary
      copyAsset(L"apps\\lilium_ime.dic", L"lilium_ime.dic");
    }

    wchar_t dstCfg[MAX_PATH] = L"";
    wchar_t dstDin[MAX_PATH] = L"";
    wchar_t dstMan[MAX_PATH] = L"";
    LiliumUI::joinPath(appDir, L"AppMain.cfg", dstCfg, MAX_PATH);
    LiliumUI::joinPath(appDir, L"index.din", dstDin, MAX_PATH);
    LiliumUI::joinPath(appDir, L"lilium_manifest.ini", dstMan, MAX_PATH);
    wchar_t cfgD[96] = L"", dinD[96] = L"", manD[96] = L"";
    _snwprintf(cfgD, 95, L"%ls\\AppMain.cfg", folder);
    _snwprintf(dinD, 95, L"%ls\\index.din", folder);
    _snwprintf(manD, 95, L"%ls\\lilium_manifest.ini", folder);
    pushTask(CopyTaskKind::WriteCfg, nullptr, dstCfg, cfgD, 8);
    pushTask(CopyTaskKind::WriteIndexDin, nullptr, dstDin, dinD, 0);
    pushTask(CopyTaskKind::WriteManifest, nullptr, dstMan, manD, 256);
  }

  return count;
}

inline bool writeRawFileBytes(const wchar_t * path, const void * data, size_t len) {
  HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = (len == 0) ? TRUE : WriteFile(h, data, static_cast<DWORD>(len), &written, NULL);
  CloseHandle(h);
  return ok && written == static_cast<DWORD>(len);
}

inline bool executeOneInstallTask(const InstallTask & task,
                                  int cfgMode,
                                  const wchar_t * modelText,
                                  BrainGen gen,
                                  uint32_t selectedMask,
                                  bool dryRun,
                                  wchar_t * outLogLine,
                                  size_t maxLogLen) {
  if (outLogLine && maxLogLen > 0) outLogLine[0] = L'\0';

  if (dryRun) {
    // STRICT ZERO-WRITE GUARANTEE in Dry-Run mode
    if (outLogLine) {
      _snwprintf(outLogLine, maxLogLen, L"[試運転] %ls", task.displayName);
      outLogLine[maxLogLen - 1] = L'\0';
    }
    return true;
  }

  switch (task.kind) {
    case CopyTaskKind::MkDir: {
      BOOL ok = dirExistsW(task.dstPath) || CreateDirectoryW(task.dstPath, NULL);
      if (!ok) {
        if (outLogLine) _snwprintf(outLogLine, maxLogLen, L"[失敗] フォルダ作成: %ls", task.displayName);
        return false;
      }
      if (outLogLine) _snwprintf(outLogLine, maxLogLen, L"[作成] %ls", task.displayName);
      return true;
    }

    case CopyTaskKind::CopyFile: {
      if (!fileExistsW(task.srcPath)) {
        if (outLogLine) _snwprintf(outLogLine, maxLogLen, L"[欠損] 元ファイルなし: %ls", task.displayName);
        return false;
      }
      if (!CopyFileW(task.srcPath, task.dstPath, FALSE)) {
        if (outLogLine) _snwprintf(outLogLine, maxLogLen, L"[失敗] コピーエラー: %ls", task.displayName);
        return false;
      }
      if (outLogLine) _snwprintf(outLogLine, maxLogLen, L"[コピー] %ls", task.displayName);
      return true;
    }

    case CopyTaskKind::ReuseLoader:
      if (outLogLine) _snwprintf(outLogLine, maxLogLen, L"[維持] %ls", task.displayName);
      return fileExistsW(task.dstPath);

    case CopyTaskKind::VirtualLoader:
      return false; // Should never execute outside Dry-Run

    case CopyTaskKind::WriteCfg: {
      uint8_t cfgBytes[8] = {};
      generateAppMainCfgBytes(cfgMode, cfgBytes);
      bool ok = writeRawFileBytes(task.dstPath, cfgBytes, 8);
      if (outLogLine) _snwprintf(outLogLine, maxLogLen, ok ? L"[生成] %ls" : L"[失敗] AppMain.cfg", task.displayName);
      return ok;
    }

    case CopyTaskKind::WriteIndexDin: {
      bool ok = fileExistsW(task.dstPath) || writeRawFileBytes(task.dstPath, "", 0);
      if (outLogLine) _snwprintf(outLogLine, maxLogLen, ok ? L"[生成] index.din" : L"[失敗] index.din");
      return ok;
    }

    case CopyTaskKind::WriteManifest: {
      char buf[512] = "";
      char verUtf8[32] = "";
      WideCharToMultiByte(CP_UTF8, 0, kInstallerVersion, -1, verUtf8, sizeof(verUtf8), NULL, NULL);
      char modUtf8[64] = "";
      if (modelText && modelText[0]) {
        WideCharToMultiByte(CP_UTF8, 0, modelText, -1, modUtf8, sizeof(modUtf8), NULL, NULL);
      } else {
        strcpy(modUtf8, "generic");
      }
      const char * genStr = (gen == BrainGen::Gen3Supported) ? "gen3" :
                            (gen == BrainGen::Gen4Supported) ? "gen4" : "unknown";
      int len = snprintf(buf, sizeof(buf),
                         "signature=liliumpj\r\n"
                         "version=%s\r\n"
                         "generation=%s\r\n"
                         "model=%s\r\n"
                         "cfg_mode=%d\r\n"
                         "apps_mask=0x%04X\r\n",
                         verUtf8, genStr, modUtf8, cfgMode, static_cast<unsigned>(selectedMask));
      if (len < 0) len = 0;
      bool ok = writeRawFileBytes(task.dstPath, buf, static_cast<size_t>(len));
      if (outLogLine) _snwprintf(outLogLine, maxLogLen, ok ? L"[生成] lilium_manifest.ini" : L"[失敗] manifest");
      return ok;
    }
  }
  return false;
}

} // namespace LiliumInstallerCore

#endif // LILIUM_SUITE_INSTALLER_CORE_H
