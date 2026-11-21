#include "../common/lilium_ui.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace {

static constexpr int kDays = 7;
static constexpr int kMaxPeriods = 8;
static constexpr int kMaxCourses = 32;
static constexpr int kMaxFiles = 12;
static constexpr int kColorCount = 10;
static constexpr int kPresetCount = 44;
static constexpr int kRoomCount = 12;
static constexpr int kNewNameCount = 14;
static constexpr int kTmplCount = 4;

static constexpr const wchar_t * kDayShort[kDays] = {
  L"日", L"月", L"火", L"水", L"木", L"金", L"土"
};

static constexpr const wchar_t * kDayFull[kDays] = {
  L"日曜", L"月曜", L"火曜", L"水曜", L"木曜", L"金曜", L"土曜"
};

struct ColorTheme {
  const wchar_t * name;
  uint32_t bg;
  uint32_t accent;
  uint32_t text;
};

static constexpr ColorTheme kColors[kColorCount] = {
  { L"ティール",   0xE2F5F2, 0x269A8B, 0x104E46 },
  { L"ブルー",     0xE6F0FC, 0x3578D4, 0x163A70 },
  { L"ローズ",     0xFCE8EC, 0xD44864, 0x7A1C30 },
  { L"アンバー",   0xFDF1DE, 0xCC8218, 0x6E4206 },
  { L"グリーン",   0xE6F6EA, 0x2E9B56, 0x13522A },
  { L"バイオレット", 0xF0EAFB, 0x7A54C8, 0x3E2378 },
  { L"シアン",     0xE4F5F8, 0x1B95B0, 0x0E5262 },
  { L"コーラル",   0xFDECE5, 0xD65E32, 0x752B10 },
  { L"グレー",     0xECEFF3, 0x62707E, 0x2A323A },
  { L"インディゴ", 0xE8EAF8, 0x4C5CC5, 0x202A6E }
};

static constexpr const wchar_t * kSubjectPresets[kPresetCount] = {
  L"数学I/A", L"数学II/B", L"数学III/C", L"微分積分", L"線形代数",
  L"英語", L"英文読解", L"英語表現", L"英会話",
  L"現代文", L"古典・古文", L"漢文", L"日本語表現",
  L"物理", L"化学", L"生物", L"地学", L"理科基礎",
  L"世界史", L"日本史", L"地理", L"現代社会", L"政治経済", L"倫理",
  L"情報", L"アルゴリズム", L"データ構造", L"OS/計算機", L"プログラミング", L"ネットワーク",
  L"音楽", L"美術", L"保健体育", L"家庭科", L"総合探究", L"LHR",
  L"ゼミ/演習", L"卒業研究", L"実験", L"教養講義", L"法学", L"経済学", L"統計学", L"自習/空き"
};

static constexpr const wchar_t * kRoomPresets[kRoomCount] = {
  L"HR教室", L"講義室A", L"講義室B", L"大講義室",
  L"理科室", L"物理室", L"化学室", L"PC室",
  L"体育館", L"音楽室", L"図書館", L"実習室"
};

static constexpr const wchar_t * kNewFileNames[kNewNameCount] = {
  L"前期時間割", L"後期時間割", L"高校時間割", L"大学時間割",
  L"1学期", L"2学期", L"3学期", L"夏期講習",
  L"テスト期間", L"予備時間割", L"時間割A", L"時間割B", L"時間割C", L"標準時間割"
};

enum BreakKind {
  BreakNone = 0,
  BreakShort = 1,
  BreakLunch = 2
};

struct PeriodSlot {
  int startMin; // minutes from 00:00
  int endMin;
  int breakKind; // 0=なし, 1=小休み, 2=昼休み
};

struct Course {
  bool used;
  int colorIdx;
  wchar_t name[32];
  wchar_t room[20];
};

struct TimetableData {
  bool dayVisible[kDays];
  int dayColorIdx[kDays];
  int periodCount;
  PeriodSlot periods[kMaxPeriods];
  Course courses[kMaxCourses]; // courseId = index + 1
  int grid[kDays][kMaxPeriods]; // 0 = empty, 1..kMaxCourses
};

enum class ScreenMode {
  Grid = 0,
  CellEdit,
  PairPicker,
  Settings,
  DeleteConfirm,
  InfoModal
};

struct TimeTableState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;

  wchar_t storeDir[MAX_PATH] = L"";
  wchar_t fileNames[kMaxFiles][64] = {}; // e.g. L"標準時間割.ttb"
  wchar_t tabNames[kMaxFiles][64] = {};  // e.g. L"標準時間割"
  int fileCount = 0;
  int activeFileIdx = 0;
  bool showTabBar = false;

  TimetableData data = {};

  int curDay = 1;    // 0..6 (Mon default)
  int curPeriod = 0; // 0..periodCount-1

  ScreenMode mode = ScreenMode::Grid;

  // Cell editor state
  int editRow = 0;         // 0=Subject, 1=Color, 2=Room, 3=LinkCourse, 4=Scope, 5=Buttons
  int editBtnFocus = 0;    // 0=Save, 1=Clear, 2=Cancel
  int editPresetIdx = 0;   // -1 if custom text preserved
  wchar_t editCustomName[32] = L"";
  int editColorIdx = 0;
  int editRoomIdx = 0;     // -1 if custom room preserved
  wchar_t editCustomRoom[20] = L"";
  int editLinkCourseId = 0; // 0=new/standalone, >0=pair with existing courseId
  bool editSyncAll = true;  // true=edit shared course, false=split into own course

  // Quick 対開講 Picker state
  int pairCourseIds[kMaxCourses + 1] = {}; // 0 indicates unlink
  int pairListCount = 0;
  int pairSel = 0;

  // Settings state
  int setTab = 0;          // 0=Days, 1=Periods, 2=Files/Tabs
  int setRow = 0;
  int setScroll = 0;
  int setPeriodCol = 0;    // 0=startMin, 1=endMin, 2=breakKind
  int setTmplIdx = 0;      // 0..kTmplCount-1
  int newNameIdx = 0;      // 0..kNewNameCount-1
  int newTemplateKind = 0; // 0=HS6, 1=Univ5, 2=CopyCurrent

  // Confirm / Info modals
  int modalFocus = 1;
  ScreenMode returnModeAfterModal = ScreenMode::Grid;
  wchar_t modalTitle[64] = L"";
  wchar_t modalLine1[96] = L"";
  wchar_t modalLine2[96] = L"";

  int lastMin = -1;
  int lastBat = -1;
};

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void formatTimeHM(int mins, wchar_t * out, size_t maxLen) {
  mins = clampInt(mins, 0, 23 * 60 + 59);
  _snwprintf(out, maxLen, L"%02d:%02d", mins / 60, mins % 60);
  out[maxLen - 1] = L'\0';
}

static void applyPeriodTemplate(TimetableData & d, int tmplIdx) {
  if (tmplIdx == 0) {
    // 高校 50分 (6限, 4限後に昼休み)
    d.periodCount = 6;
    static constexpr int starts[6] = { 8 * 60 + 50, 9 * 60 + 50, 10 * 60 + 50, 11 * 60 + 50, 13 * 60 + 25, 14 * 60 + 25 };
    for (int i = 0; i < 6; ++i) {
      d.periods[i].startMin = starts[i];
      d.periods[i].endMin = starts[i] + 50;
      d.periods[i].breakKind = (i == 3) ? BreakLunch : BreakNone;
    }
  } else if (tmplIdx == 1) {
    // 高校 50分 (7限, 4限後に昼休み)
    d.periodCount = 7;
    static constexpr int starts[7] = { 8 * 60 + 50, 9 * 60 + 50, 10 * 60 + 50, 11 * 60 + 50, 13 * 60 + 25, 14 * 60 + 25, 15 * 60 + 25 };
    for (int i = 0; i < 7; ++i) {
      d.periods[i].startMin = starts[i];
      d.periods[i].endMin = starts[i] + 50;
      d.periods[i].breakKind = (i == 3) ? BreakLunch : BreakNone;
    }
  } else if (tmplIdx == 2) {
    // 大学 90分 (5限, 2限後に昼休み)
    d.periodCount = 5;
    static constexpr int starts[5] = { 9 * 60 + 0, 10 * 60 + 40, 13 * 60 + 0, 14 * 60 + 40, 16 * 60 + 20 };
    for (int i = 0; i < 5; ++i) {
      d.periods[i].startMin = starts[i];
      d.periods[i].endMin = starts[i] + 90;
      d.periods[i].breakKind = (i == 1) ? BreakLunch : BreakNone;
    }
  } else {
    // 大学 90分 (6限, 2限後に昼休み)
    d.periodCount = 6;
    static constexpr int starts[6] = { 9 * 60 + 0, 10 * 60 + 40, 13 * 60 + 0, 14 * 60 + 40, 16 * 60 + 20, 18 * 60 + 0 };
    for (int i = 0; i < 6; ++i) {
      d.periods[i].startMin = starts[i];
      d.periods[i].endMin = starts[i] + 90;
      d.periods[i].breakKind = (i == 1) ? BreakLunch : BreakNone;
    }
  }
  for (int i = d.periodCount; i < kMaxPeriods; ++i) {
    d.periods[i].startMin = 16 * 60 + (i - 5) * 60;
    d.periods[i].endMin = d.periods[i].startMin + 50;
    d.periods[i].breakKind = BreakNone;
  }
}

static void initBlankTimetable(TimetableData & d, int tmplIdx) {
  memset(&d, 0, sizeof(d));
  for (int i = 0; i < kDays; ++i) {
    d.dayVisible[i] = (i >= 1 && i <= 5);
  }
  d.dayColorIdx[0] = 2; // Sun Rose
  d.dayColorIdx[1] = 0; // Mon Teal
  d.dayColorIdx[2] = 1; // Tue Blue
  d.dayColorIdx[3] = 4; // Wed Green
  d.dayColorIdx[4] = 3; // Thu Amber
  d.dayColorIdx[5] = 5; // Fri Violet
  d.dayColorIdx[6] = 6; // Sat Cyan
  applyPeriodTemplate(d, tmplIdx);
}

static int allocCourse(TimetableData & d, const wchar_t * name, int colorIdx, const wchar_t * room) {
  for (int i = 0; i < kMaxCourses; ++i) {
    if (!d.courses[i].used) {
      d.courses[i].used = true;
      d.courses[i].colorIdx = clampInt(colorIdx, 0, kColorCount - 1);
      wcsncpy(d.courses[i].name, name ? name : L"", 31);
      d.courses[i].name[31] = L'\0';
      wcsncpy(d.courses[i].room, room ? room : L"", 19);
      d.courses[i].room[19] = L'\0';
      return i + 1;
    }
  }
  return 0;
}

static void gcUnusedCourses(TimetableData & d) {
  bool referenced[kMaxCourses + 1] = {};
  for (int day = 0; day < kDays; ++day) {
    for (int p = 0; p < kMaxPeriods; ++p) {
      int cid = d.grid[day][p];
      if (cid >= 1 && cid <= kMaxCourses) {
        referenced[cid] = true;
      } else if (cid != 0) {
        d.grid[day][p] = 0;
      }
    }
  }
  for (int i = 0; i < kMaxCourses; ++i) {
    if (!referenced[i + 1]) {
      memset(&d.courses[i], 0, sizeof(Course));
    }
  }
}

static int countSlotsForCourse(const TimetableData & d, int courseId) {
  if (courseId <= 0 || courseId > kMaxCourses) return 0;
  int cnt = 0;
  for (int day = 0; day < kDays; ++day) {
    if (!d.dayVisible[day]) continue;
    for (int p = 0; p < d.periodCount; ++p) {
      if (d.grid[day][p] == courseId) ++cnt;
    }
  }
  return cnt;
}

static void initSampleTimetable(TimetableData & d) {
  initBlankTimetable(d, 0);
  // Seed sample classes with two explicit 対開講 pairs so pairing is visible immediately
  int cMath = allocCourse(d, L"数学II/B", 1, L"HR教室");
  int cEng  = allocCourse(d, L"英語",     2, L"講義室A");
  int cJpn  = allocCourse(d, L"現代文",   3, L"HR教室");
  int cPhy  = allocCourse(d, L"物理",     0, L"物理室");
  int cInfo = allocCourse(d, L"情報",     6, L"PC室");
  int cChe  = allocCourse(d, L"化学",     4, L"化学室");
  int cPe   = allocCourse(d, L"保健体育", 7, L"体育館");
  int cHis  = allocCourse(d, L"世界史",   5, L"講義室B");

  // 対開講: 数学II/B -> 月1 & 水3 & 金2
  d.grid[1][0] = cMath;
  d.grid[3][2] = cMath;
  d.grid[5][1] = cMath;
  // 対開講: 英語 -> 火1 & 木2
  d.grid[2][0] = cEng;
  d.grid[4][1] = cEng;

  d.grid[1][1] = cJpn;
  d.grid[1][2] = cPhy;
  d.grid[1][4] = cInfo;

  d.grid[2][1] = cChe;
  d.grid[2][3] = cHis;
  d.grid[2][4] = cPe;

  d.grid[3][0] = cJpn;
  d.grid[3][1] = cPhy;
  d.grid[3][4] = cInfo;

  d.grid[4][0] = cHis;
  d.grid[4][2] = cChe;
  d.grid[4][4] = cJpn;

  d.grid[5][0] = cPhy;
  d.grid[5][3] = cPe;
  d.grid[5][4] = cHis;
}

static void wideToUtf8(const wchar_t * src, char * dst, int maxBytes) {
  if (!dst || maxBytes <= 0) return;
  dst[0] = '\0';
  if (!src || !src[0]) return;
  WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, maxBytes, NULL, NULL);
}

static void utf8ToWide(const char * src, wchar_t * dst, int maxChars) {
  if (!dst || maxChars <= 0) return;
  dst[0] = L'\0';
  if (!src || !src[0]) return;
  MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, maxChars);
  dst[maxChars - 1] = L'\0';
}

static void trimLine(char * s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

static void sanitizeTextField(wchar_t * s) {
  if (!s) return;
  for (int i = 0; s[i]; ++i) {
    if (s[i] == L'|' || s[i] == L'\r' || s[i] == L'\n') s[i] = L' ';
  }
}

static bool saveTimetableFile(const TimeTableState * st, int fileIdx, const TimetableData & d) {
  if (!st || fileIdx < 0 || fileIdx >= st->fileCount) return false;
  wchar_t full[MAX_PATH] = L"";
  LiliumUI::joinPath(st->storeDir, st->fileNames[fileIdx], full, MAX_PATH);
  FILE * fp = _wfopen(full, L"wb");
  if (!fp) return false;

  fprintf(fp, "# LiliumTimeTable v1 (UTF-8 Portable Format)\n");
  fprintf(fp, "days=%d,%d,%d,%d,%d,%d,%d\n",
          d.dayVisible[0] ? 1 : 0, d.dayVisible[1] ? 1 : 0, d.dayVisible[2] ? 1 : 0,
          d.dayVisible[3] ? 1 : 0, d.dayVisible[4] ? 1 : 0, d.dayVisible[5] ? 1 : 0,
          d.dayVisible[6] ? 1 : 0);
  fprintf(fp, "day_colors=%d,%d,%d,%d,%d,%d,%d\n",
          d.dayColorIdx[0], d.dayColorIdx[1], d.dayColorIdx[2], d.dayColorIdx[3],
          d.dayColorIdx[4], d.dayColorIdx[5], d.dayColorIdx[6]);
  fprintf(fp, "period_count=%d\n", d.periodCount);

  for (int p = 0; p < kMaxPeriods; ++p) {
    fprintf(fp, "period=%d,%d,%d,%d\n",
            p, d.periods[p].startMin, d.periods[p].endMin, d.periods[p].breakKind);
  }
  for (int i = 0; i < kMaxCourses; ++i) {
    if (!d.courses[i].used) continue;
    char nm[128] = "";
    char rm[80] = "";
    wideToUtf8(d.courses[i].name, nm, sizeof(nm));
    wideToUtf8(d.courses[i].room, rm, sizeof(rm));
    fprintf(fp, "course=%d|%d|%s|%s\n", i + 1, d.courses[i].colorIdx, nm, rm);
  }
  for (int day = 0; day < kDays; ++day) {
    for (int p = 0; p < kMaxPeriods; ++p) {
      if (d.grid[day][p] > 0) {
        fprintf(fp, "slot=%d,%d,%d\n", day, p, d.grid[day][p]);
      }
    }
  }
  fclose(fp);
  return true;
}

static bool loadTimetableFile(const wchar_t * fullPath, TimetableData & d) {
  FILE * fp = _wfopen(fullPath, L"rb");
  if (!fp) return false;

  initBlankTimetable(d, 0);
  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    trimLine(line);
    if (!line[0] || line[0] == '#') continue;

    if (strncmp(line, "days=", 5) == 0) {
      int v[7] = {0, 1, 1, 1, 1, 1, 0};
      if (sscanf(line + 5, "%d,%d,%d,%d,%d,%d,%d",
                 &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7) {
        int any = 0;
        for (int i = 0; i < 7; ++i) {
          d.dayVisible[i] = (v[i] != 0);
          if (d.dayVisible[i]) ++any;
        }
        if (any == 0) d.dayVisible[1] = true;
      }
    } else if (strncmp(line, "day_colors=", 11) == 0) {
      int c[7] = {2, 0, 1, 4, 3, 5, 6};
      if (sscanf(line + 11, "%d,%d,%d,%d,%d,%d,%d",
                 &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6]) == 7) {
        for (int i = 0; i < 7; ++i) d.dayColorIdx[i] = clampInt(c[i], 0, kColorCount - 1);
      }
    } else if (strncmp(line, "period_count=", 13) == 0) {
      d.periodCount = clampInt(atoi(line + 13), 1, kMaxPeriods);
    } else if (strncmp(line, "period=", 7) == 0) {
      int idx = 0, sm = 0, em = 0, br = 0;
      if (sscanf(line + 7, "%d,%d,%d,%d", &idx, &sm, &em, &br) == 4 &&
          idx >= 0 && idx < kMaxPeriods) {
        d.periods[idx].startMin = clampInt(sm, 0, 23 * 60 + 50);
        d.periods[idx].endMin = clampInt(em, d.periods[idx].startMin + 5, 23 * 60 + 59);
        d.periods[idx].breakKind = clampInt(br, 0, 2);
      }
    } else if (strncmp(line, "course=", 7) == 0) {
      char * p1 = strchr(line + 7, '|');
      char * p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
      char * p3 = p2 ? strchr(p2 + 1, '|') : nullptr;
      if (p1 && p2 && p3) {
        *p1 = '\0';
        *p2 = '\0';
        *p3 = '\0';
        int cid = atoi(line + 7);
        int col = atoi(p1 + 1);
        if (cid >= 1 && cid <= kMaxCourses) {
          Course & cr = d.courses[cid - 1];
          cr.used = true;
          cr.colorIdx = clampInt(col, 0, kColorCount - 1);
          utf8ToWide(p2 + 1, cr.name, 32);
          utf8ToWide(p3 + 1, cr.room, 20);
        }
      }
    } else if (strncmp(line, "slot=", 5) == 0) {
      int day = 0, per = 0, cid = 0;
      if (sscanf(line + 5, "%d,%d,%d", &day, &per, &cid) == 3 &&
          day >= 0 && day < kDays && per >= 0 && per < kMaxPeriods &&
          cid >= 1 && cid <= kMaxCourses) {
        d.grid[day][per] = cid;
      }
    }
  }
  fclose(fp);
  gcUnusedCourses(d);
  return true;
}

static void saveUiConfig(const TimeTableState * st) {
  if (!st || !st->storeDir[0]) return;
  wchar_t cfgPath[MAX_PATH] = L"";
  LiliumUI::joinPath(st->storeDir, L"_ui.cfg", cfgPath, MAX_PATH);
  FILE * fp = _wfopen(cfgPath, L"wb");
  if (!fp) return;
  fprintf(fp, "show_tab_bar=%d\n", st->showTabBar ? 1 : 0);
  if (st->activeFileIdx >= 0 && st->activeFileIdx < st->fileCount) {
    char nm[192] = "";
    wideToUtf8(st->fileNames[st->activeFileIdx], nm, sizeof(nm));
    fprintf(fp, "active_file=%s\n", nm);
  }
  fclose(fp);
}

static void loadUiConfig(TimeTableState * st, wchar_t * outActiveFile, size_t maxChars) {
  outActiveFile[0] = L'\0';
  st->showTabBar = false;
  wchar_t cfgPath[MAX_PATH] = L"";
  LiliumUI::joinPath(st->storeDir, L"_ui.cfg", cfgPath, MAX_PATH);
  FILE * fp = _wfopen(cfgPath, L"rb");
  if (!fp) return;
  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    trimLine(line);
    if (strncmp(line, "show_tab_bar=", 13) == 0) {
      st->showTabBar = (atoi(line + 13) != 0);
    } else if (strncmp(line, "active_file=", 12) == 0) {
      utf8ToWide(line + 12, outActiveFile, static_cast<int>(maxChars));
    }
  }
  fclose(fp);
}

static void scanTimetableFiles(TimeTableState * st, const wchar_t * preferredFile) {
  st->fileCount = 0;
  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(st->storeDir, L"*.ttb", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pat, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (st->fileCount >= kMaxFiles) break;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      int idx = st->fileCount++;
      wcsncpy(st->fileNames[idx], fd.cFileName, 63);
      st->fileNames[idx][63] = L'\0';
      wcsncpy(st->tabNames[idx], fd.cFileName, 63);
      st->tabNames[idx][63] = L'\0';
      size_t n = wcslen(st->tabNames[idx]);
      if (n >= 4 && _wcsicmp(st->tabNames[idx] + n - 4, L".ttb") == 0) {
        st->tabNames[idx][n - 4] = L'\0';
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }

  // Sort alphabetically by tab name
  for (int i = 0; i < st->fileCount; ++i) {
    for (int j = i + 1; j < st->fileCount; ++j) {
      if (_wcsicmp(st->fileNames[j], st->fileNames[i]) < 0) {
        wchar_t tmpA[64], tmpB[64];
        wcscpy(tmpA, st->fileNames[i]);
        wcscpy(tmpB, st->tabNames[i]);
        wcscpy(st->fileNames[i], st->fileNames[j]);
        wcscpy(st->tabNames[i], st->tabNames[j]);
        wcscpy(st->fileNames[j], tmpA);
        wcscpy(st->tabNames[j], tmpB);
      }
    }
  }

  if (st->fileCount == 0) {
    wcscpy(st->fileNames[0], L"標準時間割.ttb");
    wcscpy(st->tabNames[0], L"標準時間割");
    st->fileCount = 1;
    st->activeFileIdx = 0;
    initSampleTimetable(st->data);
    saveTimetableFile(st, 0, st->data);
    return;
  }

  st->activeFileIdx = 0;
  if (preferredFile && preferredFile[0]) {
    for (int i = 0; i < st->fileCount; ++i) {
      if (_wcsicmp(st->fileNames[i], preferredFile) == 0) {
        st->activeFileIdx = i;
        break;
      }
    }
  }
}

static void ensureCursorVisible(TimeTableState * st) {
  if (!st->data.dayVisible[st->curDay]) {
    for (int i = 0; i < kDays; ++i) {
      if (st->data.dayVisible[i]) {
        st->curDay = i;
        break;
      }
    }
  }
  st->curPeriod = clampInt(st->curPeriod, 0, st->data.periodCount - 1);
}

static void switchToTimetableFile(TimeTableState * st, int newIdx) {
  if (st->fileCount <= 0) return;
  saveTimetableFile(st, st->activeFileIdx, st->data);
  st->activeFileIdx = ((newIdx % st->fileCount) + st->fileCount) % st->fileCount;
  wchar_t full[MAX_PATH] = L"";
  LiliumUI::joinPath(st->storeDir, st->fileNames[st->activeFileIdx], full, MAX_PATH);
  if (!loadTimetableFile(full, st->data)) {
    initBlankTimetable(st->data, 0);
  }
  ensureCursorVisible(st);
  saveUiConfig(st);
}

static void moveCursorDay(TimeTableState * st, int delta) {
  int d = st->curDay;
  for (int step = 0; step < kDays; ++step) {
    d = (d + delta + kDays) % kDays;
    if (st->data.dayVisible[d]) {
      st->curDay = d;
      return;
    }
  }
}

static int findPresetIndex(const wchar_t * name) {
  if (!name) return -1;
  for (int i = 0; i < kPresetCount; ++i) {
    if (wcscmp(kSubjectPresets[i], name) == 0) return i;
  }
  return -1;
}

static int findRoomIndex(const wchar_t * room) {
  if (!room) return -1;
  for (int i = 0; i < kRoomCount; ++i) {
    if (wcscmp(kRoomPresets[i], room) == 0) return i;
  }
  return -1;
}

static void formatPairedSlotsSummary(const TimetableData & d, int courseId, wchar_t * out, size_t maxLen) {
  out[0] = L'\0';
  if (courseId <= 0 || courseId > kMaxCourses) {
    wcsncpy(out, L"なし", maxLen - 1);
    out[maxLen - 1] = L'\0';
    return;
  }
  int cnt = 0;
  for (int day = 0; day < kDays; ++day) {
    if (!d.dayVisible[day]) continue;
    for (int p = 0; p < d.periodCount; ++p) {
      if (d.grid[day][p] == courseId) {
        wchar_t piece[16];
        _snwprintf(piece, 16, L"%ls%ls%d", (cnt > 0) ? L"," : L"", kDayShort[day], p + 1);
        piece[15] = L'\0';
        size_t cur = wcslen(out);
        if (cur + wcslen(piece) + 1 < maxLen) {
          wcscat(out, piece);
        }
        ++cnt;
      }
    }
  }
  if (cnt <= 1) {
    wcsncpy(out, L"単独開講", maxLen - 1);
    out[maxLen - 1] = L'\0';
  }
}

struct GridLayout {
  int gridX;
  int gridY0;
  int timeColW;
  int dayHdrH;
  int brkH;
  int rowH;
  int colW;
  int visDayCount;
  int visDays[kDays];
  int periodY[kMaxPeriods];
  int breakAfterCount;
  int breakY[kMaxPeriods];
  int breakPeriodIdx[kMaxPeriods];
};

static GridLayout computeGridLayout(const TimeTableState * st) {
  GridLayout g = {};
  g.gridX = 6;
  g.gridY0 = st->showTabBar ? 71 : 46;
  g.timeColW = 54;
  g.dayHdrH = 26;
  g.brkH = 16;

  g.visDayCount = 0;
  for (int d = 0; d < kDays; ++d) {
    if (st->data.dayVisible[d]) {
      g.visDays[g.visDayCount++] = d;
    }
  }
  if (g.visDayCount == 0) {
    g.visDays[g.visDayCount++] = 1;
  }

  g.colW = (628 - g.timeColW) / g.visDayCount;

  int brkCount = 0;
  for (int p = 0; p < st->data.periodCount - 1; ++p) {
    if (st->data.periods[p].breakKind != BreakNone) ++brkCount;
  }

  int availH = 436 - g.gridY0 - g.dayHdrH - brkCount * g.brkH;
  g.rowH = availH / st->data.periodCount;
  if (g.rowH < 24) g.rowH = 24;

  int curY = g.gridY0 + g.dayHdrH;
  g.breakAfterCount = 0;
  for (int p = 0; p < st->data.periodCount; ++p) {
    g.periodY[p] = curY;
    curY += g.rowH;
    if (p < st->data.periodCount - 1 && st->data.periods[p].breakKind != BreakNone) {
      g.breakPeriodIdx[g.breakAfterCount] = p;
      g.breakY[g.breakAfterCount] = curY;
      ++g.breakAfterCount;
      curY += g.brkH;
    }
  }
  return g;
}

static int computeTabWidth(const wchar_t * label) {
  if (!label || !label[0]) return 66;
  int tw = ::MisakiFont::measureTextWidth(label, static_cast<int>(wcslen(label)), 2) + 18;
  if (tw < 66) tw = 66;
  if (tw > 140) tw = 140;
  return tw;
}

static void renderTabBar(LiliumUI::Canvas & c, const TimeTableState * st) {
  if (!st->showTabBar) return;
  c.fillRect(0, 42, LiliumUI::kMainW, 27, 0xE4E9EF);
  c.fillRect(0, 68, LiliumUI::kMainW, 1, LiliumUI::kColBorder);

  int x = 6;
  const int maxTabRight = 600;
  for (int i = 0; i < st->fileCount; ++i) {
    int tw = computeTabWidth(st->tabNames[i]);
    if (x + tw > maxTabRight) break;
    bool act = (i == st->activeFileIdx);
    c.drawBox(x, 44, tw, 24,
              act ? LiliumUI::kColWhite : 0xD6DEE6,
              act ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    if (act) {
      c.fillRect(x, 44, tw, 2, LiliumUI::kColTeal);
    }
    c.drawText(x + 4, 44, tw - 8, 24,
               act ? LiliumUI::kColTealDeep : LiliumUI::kColTextSub,
               st->tabNames[i], 2, LiliumUI::Align::Center, true);
    x += tw + 4;
  }

  // [+] quick button to file/tab manager in Settings
  c.drawBox(604, 44, 30, 24, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
  c.drawText(604, 44, 30, 24, LiliumUI::kColTealDeep, L"+", 2, LiliumUI::Align::Center, true);
}

static void renderGridMode(LiliumUI::Canvas & c, const TimeTableState * st) {
  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
  LiliumUI::renderTopHeader(c, L"LILIUM TIMETABLE", st->tabNames[st->activeFileIdx]);
  renderTabBar(c, st);

  GridLayout g = computeGridLayout(st);
  int gridTotalW = g.timeColW + g.visDayCount * g.colW;
  int focusedCourseId = st->data.grid[st->curDay][st->curPeriod];
  bool focusedHasPair = (countSlotsForCourse(st->data, focusedCourseId) >= 2);

  // Corner header cell
  c.drawBox(g.gridX, g.gridY0, g.timeColW, g.dayHdrH, LiliumUI::kColCharcoal, LiliumUI::kColBorderStrong);
  c.drawText(g.gridX, g.gridY0, g.timeColW, g.dayHdrH, LiliumUI::kColTealSoft,
             L"限", 2, LiliumUI::Align::Center, true);

  // Day headers (with per-day colors!)
  for (int vi = 0; vi < g.visDayCount; ++vi) {
    int dayIdx = g.visDays[vi];
    int dx = g.gridX + g.timeColW + vi * g.colW;
    const ColorTheme & dth = kColors[clampInt(st->data.dayColorIdx[dayIdx], 0, kColorCount - 1)];
    bool isCurCol = (dayIdx == st->curDay);
    c.drawBox(dx, g.gridY0, g.colW, g.dayHdrH,
              isCurCol ? dth.accent : dth.bg,
              isCurCol ? dth.text : dth.accent);
    c.drawText(dx, g.gridY0, g.colW, g.dayHdrH,
               isCurCol ? LiliumUI::kColWhite : dth.text,
               kDayShort[dayIdx], 2, LiliumUI::Align::Center, true);
  }

  // Period rows & Cells
  for (int p = 0; p < st->data.periodCount; ++p) {
    int ry = g.periodY[p];
    bool isCurRow = (p == st->curPeriod);

    // Period & time cell on left
    c.drawBox(g.gridX, ry, g.timeColW, g.rowH,
              isCurRow ? LiliumUI::kColTealSoft : LiliumUI::kColWhite,
              LiliumUI::kColBorder);
    wchar_t pBuf[16], tBuf[16];
    _snwprintf(pBuf, 16, L"%d限", p + 1);
    pBuf[15] = L'\0';
    formatTimeHM(st->data.periods[p].startMin, tBuf, 16);
    if (g.rowH >= 38) {
      c.drawText(g.gridX + 2, ry + 3, g.timeColW - 4, 16,
                 isCurRow ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 pBuf, 2, LiliumUI::Align::Center, true);
      c.drawText(g.gridX + 2, ry + g.rowH - 13, g.timeColW - 4, 10,
                 LiliumUI::kColMuted, tBuf, 1, LiliumUI::Align::Center, true);
    } else {
      c.drawText(g.gridX + 2, ry, g.timeColW - 4, g.rowH,
                 isCurRow ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 pBuf, 2, LiliumUI::Align::Center, true);
    }

    for (int vi = 0; vi < g.visDayCount; ++vi) {
      int dayIdx = g.visDays[vi];
      int cx = g.gridX + g.timeColW + vi * g.colW;
      int cid = st->data.grid[dayIdx][p];
      bool isFocused = (dayIdx == st->curDay && p == st->curPeriod);
      bool isPartner = (!isFocused && focusedHasPair && cid > 0 && cid == focusedCourseId);

      if (cid >= 1 && cid <= kMaxCourses && st->data.courses[cid - 1].used) {
        const Course & cr = st->data.courses[cid - 1];
        const ColorTheme & cth = kColors[clampInt(cr.colorIdx, 0, kColorCount - 1)];
        bool isPaired = (countSlotsForCourse(st->data, cid) >= 2);

        c.drawBox(cx, ry, g.colW, g.rowH, cth.bg, LiliumUI::kColBorder);
        c.fillRect(cx + 1, ry + 1, 4, g.rowH - 2, cth.accent);

        if (isFocused) {
          c.drawThickFrame(cx, ry, g.colW, g.rowH, 2, LiliumUI::kColCharcoal);
        } else if (isPartner) {
          c.drawThickFrame(cx, ry, g.colW, g.rowH, 2, cth.accent);
        }

        int textW = g.colW - 10;
        if (g.rowH >= 48) {
          c.drawText(cx + 7, ry + 5, isPaired ? (textW - 14) : textW, 18,
                     cth.text, cr.name, 2, LiliumUI::Align::Left, true);
          if (cr.room[0]) {
            c.drawText(cx + 7, ry + g.rowH - 15, textW, 12,
                       LiliumUI::kColMuted, cr.room, 1, LiliumUI::Align::Left, true);
          }
        } else if (g.rowH >= 36) {
          c.drawText(cx + 7, ry + 3, isPaired ? (textW - 14) : textW, 16,
                     cth.text, cr.name, 2, LiliumUI::Align::Left, true);
          if (cr.room[0] && g.rowH >= 40) {
            c.drawText(cx + 7, ry + g.rowH - 12, textW, 10,
                       LiliumUI::kColMuted, cr.room, 1, LiliumUI::Align::Left, true);
          }
        } else {
          c.drawText(cx + 7, ry, isPaired ? (textW - 14) : textW, g.rowH,
                     cth.text, cr.name, 1, LiliumUI::Align::Left, true);
        }

        if (isPaired) {
          c.drawBox(cx + g.colW - 15, ry + 3, 12, 12,
                    isPartner ? cth.text : cth.accent, cth.accent);
          c.drawText(cx + g.colW - 15, ry + 3, 12, 12,
                     LiliumUI::kColWhite, L"対", 1, LiliumUI::Align::Center, true);
        }
      } else {
        uint32_t emptyBg = isFocused ? LiliumUI::kColTealSoft : LiliumUI::kColWhite;
        c.drawBox(cx, ry, g.colW, g.rowH, emptyBg, LiliumUI::kColBorder);
        if (isFocused) {
          c.drawThickFrame(cx, ry, g.colW, g.rowH, 2, LiliumUI::kColTealDark);
        }
      }
    }
  }

  // Break strips
  for (int b = 0; b < g.breakAfterCount; ++b) {
    int pIdx = g.breakPeriodIdx[b];
    int by = g.breakY[b];
    int kind = st->data.periods[pIdx].breakKind;
    wchar_t t0[16], t1[16], bLbl[64];
    formatTimeHM(st->data.periods[pIdx].endMin, t0, 16);
    formatTimeHM(st->data.periods[pIdx + 1].startMin, t1, 16);
    _snwprintf(bLbl, 64, L"%ls (%ls - %ls)",
               (kind == BreakLunch) ? L"昼休み" : L"小休み", t0, t1);
    bLbl[63] = L'\0';
    uint32_t bBg = (kind == BreakLunch) ? LiliumUI::kColWarnBg : 0xEAEFF4;
    uint32_t bBd = (kind == BreakLunch) ? LiliumUI::kColWarnBorder : LiliumUI::kColBorder;
    uint32_t bTx = (kind == BreakLunch) ? LiliumUI::kColWarnText : LiliumUI::kColMuted;
    c.drawBox(g.gridX, by, gridTotalW, g.brkH, bBg, bBd);
    c.drawText(g.gridX, by, gridTotalW, g.brkH, bTx, bLbl, 1, LiliumUI::Align::Center, true);
  }

  // Bottom bar
  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  c.drawText(10, 440, LiliumUI::kMainW - 20, 40, LiliumUI::kColTextSub,
             L"[Ent]編集 [F1]設定 [F2]タブ [F4]対開講 [F5]削除 [F8]バー",
             2, LiliumUI::Align::Left, true);

  // Sidebar details
  wchar_t statusStr[32], cardTitle[32], cardBody[512];
  _snwprintf(statusStr, 32, L"%d日 %d限", g.visDayCount, st->data.periodCount);
  statusStr[31] = L'\0';
  _snwprintf(cardTitle, 32, L"%ls %d限", kDayFull[st->curDay], st->curPeriod + 1);
  cardTitle[31] = L'\0';

  wchar_t ts0[16], ts1[16];
  formatTimeHM(st->data.periods[st->curPeriod].startMin, ts0, 16);
  formatTimeHM(st->data.periods[st->curPeriod].endMin, ts1, 16);

  if (focusedCourseId >= 1 && focusedCourseId <= kMaxCourses && st->data.courses[focusedCourseId - 1].used) {
    const Course & cr = st->data.courses[focusedCourseId - 1];
    wchar_t pairBuf[64];
    formatPairedSlotsSummary(st->data, focusedCourseId, pairBuf, 64);
    _snwprintf(cardBody, 512,
               L"【時間】\n%ls-%ls\n【科目】\n%ls\n【教室】\n%ls\n【対開講】\n%ls\n【色】%ls",
               ts0, ts1,
               cr.name,
               cr.room[0] ? cr.room : L"(未設定)",
               pairBuf,
               kColors[clampInt(cr.colorIdx, 0, kColorCount - 1)].name);
  } else {
    _snwprintf(cardBody, 512,
               L"【時間】\n%ls-%ls\n【科目】\n(空きコマ)\n【操作案内】\n[Ent]: 授業登録\n[F4]: 対開講連動",
               ts0, ts1);
  }
  cardBody[511] = L'\0';

  LiliumUI::renderSidebar(c, statusStr, cardTitle, cardBody,
                          L"[Ent]編集[F4]対", L"[F1]設定[F8]タブ",
                          L"戻　る", false);
}

static void openCellEditor(TimeTableState * st) {
  int cid = st->data.grid[st->curDay][st->curPeriod];
  st->editRow = 0;
  st->editBtnFocus = 0;
  if (cid >= 1 && cid <= kMaxCourses && st->data.courses[cid - 1].used) {
    const Course & cr = st->data.courses[cid - 1];
    st->editPresetIdx = findPresetIndex(cr.name);
    wcsncpy(st->editCustomName, cr.name, 31);
    st->editCustomName[31] = L'\0';
    st->editColorIdx = clampInt(cr.colorIdx, 0, kColorCount - 1);
    st->editRoomIdx = findRoomIndex(cr.room);
    wcsncpy(st->editCustomRoom, cr.room, 19);
    st->editCustomRoom[19] = L'\0';
    st->editLinkCourseId = cid;
    st->editSyncAll = true;
  } else {
    st->editPresetIdx = 0;
    wcsncpy(st->editCustomName, kSubjectPresets[0], 31);
    st->editCustomName[31] = L'\0';
    st->editColorIdx = st->data.dayColorIdx[st->curDay];
    st->editRoomIdx = 0;
    wcsncpy(st->editCustomRoom, kRoomPresets[0], 19);
    st->editCustomRoom[19] = L'\0';
    st->editLinkCourseId = 0;
    st->editSyncAll = true;
  }
  st->mode = ScreenMode::CellEdit;
}

static void saveCellEditor(TimeTableState * st) {
  const wchar_t * finalName = (st->editPresetIdx >= 0) ? kSubjectPresets[st->editPresetIdx] : st->editCustomName;
  const wchar_t * finalRoom = (st->editRoomIdx >= 0) ? kRoomPresets[st->editRoomIdx] : st->editCustomRoom;

  int origCid = st->data.grid[st->curDay][st->curPeriod];
  bool origPaired = (countSlotsForCourse(st->data, origCid) >= 2);

  if (st->editLinkCourseId > 0 && st->editLinkCourseId != origCid &&
      st->editLinkCourseId <= kMaxCourses && st->data.courses[st->editLinkCourseId - 1].used) {
    // Linking focused slot onto an existing course (対開講)
    st->data.grid[st->curDay][st->curPeriod] = st->editLinkCourseId;
  } else if (origCid > 0 && st->editLinkCourseId == origCid &&
             (!origPaired || st->editSyncAll)) {
    // Update existing course in-place (synced across all 対開講 cells)
    Course & cr = st->data.courses[origCid - 1];
    wcsncpy(cr.name, finalName, 31);
    cr.name[31] = L'\0';
    sanitizeTextField(cr.name);
    cr.colorIdx = clampInt(st->editColorIdx, 0, kColorCount - 1);
    wcsncpy(cr.room, finalRoom, 19);
    cr.room[19] = L'\0';
    sanitizeTextField(cr.room);
  } else {
    // Allocate a standalone course for this cell
    int newId = allocCourse(st->data, finalName, st->editColorIdx, finalRoom);
    if (newId > 0) {
      sanitizeTextField(st->data.courses[newId - 1].name);
      sanitizeTextField(st->data.courses[newId - 1].room);
      st->data.grid[st->curDay][st->curPeriod] = newId;
    }
  }

  gcUnusedCourses(st->data);
  saveTimetableFile(st, st->activeFileIdx, st->data);
  st->mode = ScreenMode::Grid;
}

static int collectExistingCourseIds(const TimeTableState * st, int * outIds, int maxOut) {
  int cnt = 0;
  for (int i = 0; i < kMaxCourses && cnt < maxOut; ++i) {
    if (st->data.courses[i].used && countSlotsForCourse(st->data, i + 1) > 0) {
      outIds[cnt++] = i + 1;
    }
  }
  return cnt;
}

static void renderCellEditorModal(LiliumUI::Canvas & c, const TimeTableState * st) {
  c.darkenScreen();
  const int mx = 26, my = 46, mw = 588, mh = 388;
  c.drawBox(mx, my, mw, mh, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawThickFrame(mx, my, mw, mh, 2, LiliumUI::kColTealDark);
  c.fillRect(mx, my, mw, 36, LiliumUI::kColTeal);

  wchar_t title[64];
  _snwprintf(title, 64, L"【 授業編集 : %ls %d限 】", kDayFull[st->curDay], st->curPeriod + 1);
  title[63] = L'\0';
  c.drawText(mx + 14, my, mw - 28, 36, LiliumUI::kColWhite, title, 2, LiliumUI::Align::Left, true);

  const wchar_t * subjName = (st->editPresetIdx >= 0) ? kSubjectPresets[st->editPresetIdx] : st->editCustomName;
  const wchar_t * roomName = (st->editRoomIdx >= 0) ? kRoomPresets[st->editRoomIdx] : st->editCustomRoom;
  const ColorTheme & cth = kColors[clampInt(st->editColorIdx, 0, kColorCount - 1)];

  wchar_t linkStr[64] = L"単独 / 現在値";
  if (st->editLinkCourseId > 0 && st->editLinkCourseId <= kMaxCourses &&
      st->data.courses[st->editLinkCourseId - 1].used) {
    wchar_t slotsBuf[32];
    formatPairedSlotsSummary(st->data, st->editLinkCourseId, slotsBuf, 32);
    _snwprintf(linkStr, 64, L"%ls (%ls)", st->data.courses[st->editLinkCourseId - 1].name, slotsBuf);
    linkStr[63] = L'\0';
  }

  int origCid = st->data.grid[st->curDay][st->curPeriod];
  bool origPaired = (countSlotsForCourse(st->data, origCid) >= 2);
  const wchar_t * scopeStr = origPaired
    ? (st->editSyncAll ? L"全対開講コマを同期" : L"このコマのみ分離")
    : L"(単独コマ)";

  const wchar_t * labels[5] = { L"科目名", L"授業カラー", L"教室・場所", L"対開講リンク", L"編集範囲" };
  for (int r = 0; r < 5; ++r) {
    int ry = my + 50 + r * 48;
    bool act = (st->editRow == r);
    c.drawBox(mx + 18, ry, mw - 36, 42,
              act ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
              act ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(mx + 30, ry, 134, 42,
               act ? LiliumUI::kColTealDeep : LiliumUI::kColTextSub,
               labels[r], 2, LiliumUI::Align::Left, true);

    if (r == 1) {
      c.drawBox(mx + 174, ry + 8, 52, 26, cth.bg, cth.accent);
      c.fillRect(mx + 175, ry + 9, 8, 24, cth.accent);
      c.drawText(mx + 236, ry, 310, 42, LiliumUI::kColText, cth.name, 2, LiliumUI::Align::Left, true);
    } else {
      const wchar_t * val = (r == 0) ? subjName : (r == 2) ? roomName : (r == 3) ? linkStr : scopeStr;
      c.drawText(mx + 174, ry, mw - 220, 42,
                 (r == 3 && st->editLinkCourseId > 0) ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 val, 2, LiliumUI::Align::Left, true);
    }
    if (act) {
      c.drawText(mx + mw - 56, ry, 32, 42, LiliumUI::kColTealDark, L"<>", 2, LiliumUI::Align::Center, true);
    }
  }

  // Buttons at bottom
  static constexpr const wchar_t * kBtns[3] = { L"保　存", L"空きにする", L"キャンセル" };
  int by = my + 306;
  for (int b = 0; b < 3; ++b) {
    int bx = mx + 28 + b * 182;
    bool foc = (st->editRow == 5 && st->editBtnFocus == b);
    uint32_t bg = (b == 0) ? (foc ? LiliumUI::kColTealDark : LiliumUI::kColTeal)
                : (b == 1) ? (foc ? LiliumUI::kColDangerFill : LiliumUI::kColDangerBg)
                : (foc ? LiliumUI::kColBorderStrong : LiliumUI::kColBtnBg);
    uint32_t bd = foc ? LiliumUI::kColCharcoal : ((b == 1) ? LiliumUI::kColDangerBorder : LiliumUI::kColBorder);
    uint32_t tx = (b == 0 || (b == 1 && foc)) ? LiliumUI::kColWhite
                : (b == 1) ? LiliumUI::kColDangerText : LiliumUI::kColText;
    c.drawBox(bx, by, 168, 42, bg, bd);
    if (foc) c.drawThickFrame(bx, by, 168, 42, 2, LiliumUI::kColCharcoal);
    c.drawText(bx, by, 168, 42, tx, kBtns[b], 2, LiliumUI::Align::Center, true);
  }

  c.drawText(mx + 18, my + 356, mw - 36, 24, LiliumUI::kColMuted,
             L"[↑↓]項目 [←→]変更 [PgUp/Dn]科目ジャンル [Ent]実行", 2, LiliumUI::Align::Center, true);

  LiliumUI::renderSidebar(c, L"授業編集", L"セル設定",
                          L"【操作方法】\n[↑↓]: 項目移動\n[←→]: 値変更\n[PgUpDn]: 大幅移動\n[Ent]: 保存/決定\n[Esc]: 中止",
                          L"[Ent]決定/保存", L"[Esc]戻る", L"戻　る", false);
}

static void openPairPicker(TimeTableState * st) {
  int ids[kMaxCourses] = {};
  int n = collectExistingCourseIds(st, ids, kMaxCourses);
  st->pairCourseIds[0] = 0; // Unlink/standalone option
  int k = 1;
  int curCid = st->data.grid[st->curDay][st->curPeriod];
  st->pairSel = 0;
  for (int i = 0; i < n; ++i) {
    st->pairCourseIds[k] = ids[i];
    if (ids[i] == curCid) st->pairSel = k;
    ++k;
  }
  st->pairListCount = k;
  st->mode = ScreenMode::PairPicker;
}

static void renderPairPickerModal(LiliumUI::Canvas & c, const TimeTableState * st) {
  c.darkenScreen();
  const int mx = 44, my = 54, mw = 552, mh = 368;
  c.drawBox(mx, my, mw, mh, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawThickFrame(mx, my, mw, mh, 2, LiliumUI::kColTealDark);
  c.fillRect(mx, my, mw, 36, LiliumUI::kColTeal);

  wchar_t hdr[64];
  _snwprintf(hdr, 64, L"【 対開講リンク先を選択 (%ls%d) 】", kDayShort[st->curDay], st->curPeriod + 1);
  hdr[63] = L'\0';
  c.drawText(mx + 14, my, mw - 28, 36, LiliumUI::kColWhite, hdr, 2, LiliumUI::Align::Left, true);

  const int visRows = 8;
  int start = 0;
  if (st->pairSel >= visRows) start = st->pairSel - visRows + 1;

  for (int r = 0; r < visRows; ++r) {
    int idx = start + r;
    int ry = my + 48 + r * 35;
    if (idx >= st->pairListCount) break;
    bool sel = (idx == st->pairSel);
    int cid = st->pairCourseIds[idx];
    c.drawBox(mx + 16, ry, mw - 32, 32,
              sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
              sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);

    if (cid == 0) {
      c.drawText(mx + 28, ry, mw - 56, 32, LiliumUI::kColText,
                 L"リンク解除 (空き / 単独へ)", 2, LiliumUI::Align::Left, true);
    } else {
      const Course & cr = st->data.courses[cid - 1];
      const ColorTheme & cth = kColors[clampInt(cr.colorIdx, 0, kColorCount - 1)];
      c.fillRect(mx + 22, ry + 6, 12, 20, cth.accent);
      wchar_t slots[48], line[96];
      formatPairedSlotsSummary(st->data, cid, slots, 48);
      _snwprintf(line, 96, L"%ls  [%ls]", cr.name, slots);
      line[95] = L'\0';
      c.drawText(mx + 42, ry, mw - 70, 32, LiliumUI::kColText, line, 2, LiliumUI::Align::Left, true);
    }
  }

  c.drawText(mx + 16, my + 334, mw - 32, 26, LiliumUI::kColMuted,
             L"[↑↓]授業選択   [Enter]このコマへ対開講リンク   [Esc]戻る",
             2, LiliumUI::Align::Center, true);

  LiliumUI::renderSidebar(c, L"対開講選択", L"対開講リンク",
                          L"既存授業を選ぶと\n同じ色・同じ授業\nとして複数コマに\n連動配置されます。",
                          L"[Enter]リンク実行", L"[Esc]戻る", L"戻　る", false);
}

static void renderSettingsScreen(LiliumUI::Canvas & c, const TimeTableState * st) {
  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
  LiliumUI::renderTopHeader(c, L"TIMETABLE 設定", st->tabNames[st->activeFileIdx]);

  // Sub-tabs (y=48..76)
  static constexpr const wchar_t * kTabs[3] = { L"1:曜日・色", L"2:時限・休憩", L"3:時間割・タブ" };
  for (int t = 0; t < 3; ++t) {
    int tx = 12 + t * 206;
    bool act = (st->setTab == t);
    c.drawBox(tx, 48, 200, 28,
              act ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              act ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(tx, 48, 200, 28,
               act ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
               kTabs[t], 2, LiliumUI::Align::Center, true);
  }

  c.drawBox(12, 82, LiliumUI::kMainW - 24, 350, LiliumUI::kColWhite, LiliumUI::kColBorder);

  if (st->setTab == 0) {
    // 7 Days: visibility + per-day color
    for (int d = 0; d < kDays; ++d) {
      int ry = 92 + d * 46;
      bool sel = (st->setRow == d);
      const ColorTheme & dth = kColors[clampInt(st->data.dayColorIdx[d], 0, kColorCount - 1)];
      c.drawBox(22, ry, 596, 40,
                sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
      c.drawBox(32, ry + 6, 74, 28, dth.bg, dth.accent);
      c.drawText(32, ry + 6, 74, 28, dth.text, kDayFull[d], 2, LiliumUI::Align::Center, true);

      bool vis = st->data.dayVisible[d];
      c.drawBox(122, ry + 6, 110, 28,
                vis ? LiliumUI::kColTealHeader : 0xECEFF3,
                vis ? LiliumUI::kColTealBorder : LiliumUI::kColBorder);
      c.drawText(122, ry + 6, 110, 28,
                 vis ? LiliumUI::kColTealDeep : LiliumUI::kColMuted,
                 vis ? L"表示する" : L"非表示", 2, LiliumUI::Align::Center, true);

      c.fillRect(250, ry + 8, 24, 24, dth.accent);
      c.drawText(284, ry, 280, 40, LiliumUI::kColText, dth.name, 2, LiliumUI::Align::Left, true);
    }
  } else if (st->setTab == 1) {
    static constexpr const wchar_t * kTmpls[kTmplCount] = {
      L"高校50分 (6限)", L"高校50分 (7限)", L"大学90分 (5限)", L"大学90分 (6限)"
    };
    const int visRows = 8;
    int totalRows = 2 + st->data.periodCount;
    for (int r = 0; r < visRows; ++r) {
      int idx = st->setScroll + r;
      if (idx >= totalRows) break;
      int ry = 92 + r * 41;
      bool sel = (st->setRow == idx);
      c.drawBox(22, ry, 596, 37,
                sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);

      if (idx == 0) {
        c.drawText(32, ry, 180, 37, LiliumUI::kColTextSub, L"時限数 (何限まで)", 2, LiliumUI::Align::Left, true);
        wchar_t nBuf[24];
        _snwprintf(nBuf, 24, L"%d 限まで", st->data.periodCount);
        nBuf[23] = L'\0';
        c.drawText(240, ry, 340, 37, LiliumUI::kColTealDeep, nBuf, 2, LiliumUI::Align::Left, true);
      } else if (idx == 1) {
        c.drawText(32, ry, 180, 37, LiliumUI::kColTextSub, L"時間割テンプレート", 2, LiliumUI::Align::Left, true);
        c.drawText(240, ry, 360, 37, LiliumUI::kColText, kTmpls[st->setTmplIdx], 2, LiliumUI::Align::Left, true);
      } else {
        int p = idx - 2;
        wchar_t pLbl[16], sBuf[16], eBuf[16];
        _snwprintf(pLbl, 16, L"%d限", p + 1);
        pLbl[15] = L'\0';
        formatTimeHM(st->data.periods[p].startMin, sBuf, 16);
        formatTimeHM(st->data.periods[p].endMin, eBuf, 16);
        const wchar_t * brStr = (st->data.periods[p].breakKind == BreakLunch) ? L"昼休み"
                              : (st->data.periods[p].breakKind == BreakShort) ? L"小休み" : L"休み無";

        c.drawText(32, ry, 68, 37, LiliumUI::kColTealDeep, pLbl, 2, LiliumUI::Align::Left, true);

        c.drawBox(112, ry + 5, 114, 27,
                  (sel && st->setPeriodCol == 0) ? LiliumUI::kColTeal : LiliumUI::kColWhite,
                  LiliumUI::kColBorder);
        c.drawText(112, ry + 5, 114, 27,
                   (sel && st->setPeriodCol == 0) ? LiliumUI::kColWhite : LiliumUI::kColText,
                   sBuf, 2, LiliumUI::Align::Center, true);

        c.drawText(230, ry, 24, 37, LiliumUI::kColMuted, L"-", 2, LiliumUI::Align::Center, true);

        c.drawBox(258, ry + 5, 114, 27,
                  (sel && st->setPeriodCol == 1) ? LiliumUI::kColTeal : LiliumUI::kColWhite,
                  LiliumUI::kColBorder);
        c.drawText(258, ry + 5, 114, 27,
                   (sel && st->setPeriodCol == 1) ? LiliumUI::kColWhite : LiliumUI::kColText,
                   eBuf, 2, LiliumUI::Align::Center, true);

        c.drawBox(392, ry + 5, 200, 27,
                  (sel && st->setPeriodCol == 2) ? LiliumUI::kColWarnBg : LiliumUI::kColWhite,
                  (sel && st->setPeriodCol == 2) ? LiliumUI::kColWarnBorder : LiliumUI::kColBorder);
        c.drawText(392, ry + 5, 200, 27,
                   (st->data.periods[p].breakKind != BreakNone) ? LiliumUI::kColWarnText : LiliumUI::kColMuted,
                   brStr, 2, LiliumUI::Align::Center, true);
      }
    }
  } else {
    // Tab 2: Timetable Files & Tab Bar Settings
    static constexpr const wchar_t * kNewTmpl[3] = {
      L"高校6限(空)", L"大学5限(空)", L"現在の構成を複製"
    };
    const wchar_t * labels[6] = {
      L"タブバー表示",
      L"表示する時間割",
      L"新規ファイル名",
      L"新規のひな形",
      L"▶ 新規時間割を作成",
      L"▶ 表示中の時間割を削除"
    };
    for (int r = 0; r < 6; ++r) {
      int ry = 94 + r * 54;
      bool sel = (st->setRow == r);
      bool isDel = (r == 5);
      c.drawBox(22, ry, 596, 46,
                sel ? (isDel ? LiliumUI::kColDangerBg : LiliumUI::kColTealSoft) : LiliumUI::kColRowAlt,
                sel ? (isDel ? LiliumUI::kColDangerFill : LiliumUI::kColTealDark) : LiliumUI::kColBorder);
      c.drawText(34, ry, 230, 46,
                 isDel ? LiliumUI::kColDangerText : LiliumUI::kColTextSub,
                 labels[r], 2, LiliumUI::Align::Left, true);

      wchar_t valBuf[96] = L"";
      if (r == 0) {
        wcscpy(valBuf, st->showTabBar ? L"ON (上部タブで切替)" : L"OFF (設定から切替)");
      } else if (r == 1) {
        _snwprintf(valBuf, 96, L"%ls (%d/%d)", st->tabNames[st->activeFileIdx], st->activeFileIdx + 1, st->fileCount);
      } else if (r == 2) {
        _snwprintf(valBuf, 96, L"%ls.ttb", kNewFileNames[st->newNameIdx]);
      } else if (r == 3) {
        wcsncpy(valBuf, kNewTmpl[st->newTemplateKind], 95);
      } else if (r == 4) {
        wcscpy(valBuf, L"[Enter] で新規作成&タブON");
      } else {
        wcscpy(valBuf, (st->fileCount > 1) ? L"[Enter] で削除" : L"(残り1件のため不可)");
      }
      valBuf[95] = L'\0';
      c.drawText(270, ry, 336, 46,
                 (r == 5 && st->fileCount <= 1) ? LiliumUI::kColLightMuted : LiliumUI::kColText,
                 valBuf, 2, LiliumUI::Align::Left, true);
    }
  }

  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  const wchar_t * foot = (st->setTab == 0) ? L"[↑↓]曜日 [Spc/Ent]表示切替 [←→]曜日色 [F2]タブ [Esc]戻る"
                       : (st->setTab == 1) ? L"[↑↓]行 [Ent]項目/適用 [←→]時刻・限・休憩 [F2]タブ [Esc]戻る"
                                           : L"[↑↓]項目 [←→]切替 [Ent]決定・新規作成 [F2]タブ [Esc]戻る";
  c.drawText(10, 440, LiliumUI::kMainW - 20, 40, LiliumUI::kColTextSub, foot, 2, LiliumUI::Align::Left, true);

  wchar_t sbBody[384];
  _snwprintf(sbBody, 384,
             L"【保存先】\ntimetables\\\n【ファイル数】\n%d 件 (.ttb)\n【タブ表示】\n%ls\n別端末へコピー可",
             st->fileCount, st->showTabBar ? L"ON" : L"OFF");
  sbBody[383] = L'\0';
  LiliumUI::renderSidebar(c, L"設定画面", kTabs[st->setTab], sbBody,
                          L"[F2/1-3]タブ", L"[Esc]時間割へ", L"時間割へ", false);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  TimeTableState * st = static_cast<TimeTableState *>(userData);
  if (st->mode == ScreenMode::Settings) {
    renderSettingsScreen(c, st);
  } else {
    renderGridMode(c, st);
    if (st->mode == ScreenMode::CellEdit) {
      renderCellEditorModal(c, st);
    } else if (st->mode == ScreenMode::PairPicker) {
      renderPairPickerModal(c, st);
    } else if (st->mode == ScreenMode::DeleteConfirm) {
      static constexpr LiliumUI::ModalButton kDelBtns[2] = {
        { L"削除する", true }, { L"キャンセル", false }
      };
      LiliumUI::renderConfirmModal(c, L"【 時間割削除 】",
                                   st->fileNames[st->activeFileIdx],
                                   L"この時間割ファイルを削除しますか？",
                                   kDelBtns, 2, st->modalFocus,
                                   L"[←→]選択 [Enter]実行 [Esc]中止");
    } else if (st->mode == ScreenMode::InfoModal) {
      static constexpr LiliumUI::ModalButton kOkBtn[1] = { { L"OK", false } };
      LiliumUI::renderConfirmModal(c, st->modalTitle, st->modalLine1, st->modalLine2,
                                   kOkBtn, 1, 0, L"[Enter / Esc] 閉じる");
    }
  }
}

static void createNewTimetableFile(TimeTableState * st) {
  if (st->fileCount >= kMaxFiles) {
    wcscpy(st->modalTitle, L"【 上限に達しました 】");
    wcscpy(st->modalLine1, L"最大12個まで作成可能です。");
    wcscpy(st->modalLine2, L"不要な時間割を削除してから作成してください。");
    st->returnModeAfterModal = ScreenMode::Settings;
    st->mode = ScreenMode::InfoModal;
    return;
  }

  wchar_t baseName[48];
  wcsncpy(baseName, kNewFileNames[st->newNameIdx], 47);
  baseName[47] = L'\0';

  wchar_t fname[64] = L"";
  wchar_t full[MAX_PATH] = L"";
  for (int suffix = 1; suffix <= 20; ++suffix) {
    if (suffix == 1) _snwprintf(fname, 64, L"%ls.ttb", baseName);
    else _snwprintf(fname, 64, L"%ls_%d.ttb", baseName, suffix);
    fname[63] = L'\0';
    LiliumUI::joinPath(st->storeDir, fname, full, MAX_PATH);
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) break;
  }

  TimetableData * newData = static_cast<TimetableData *>(calloc(1, sizeof(TimetableData)));
  if (!newData) return;
  if (st->newTemplateKind == 2) {
    *newData = st->data;
  } else {
    initBlankTimetable(*newData, (st->newTemplateKind == 1) ? 2 : 0);
  }

  // Save current active timetable, write new file on disk, then switch without overwriting newData
  saveTimetableFile(st, st->activeFileIdx, st->data);
  if (st->fileCount < kMaxFiles) {
    int idx = st->fileCount;
    wcscpy(st->fileNames[idx], fname);
    ++st->fileCount;
    saveTimetableFile(st, idx, *newData);
  }
  st->data = *newData;
  free(newData);

  st->showTabBar = true;
  scanTimetableFiles(st, fname);
  ensureCursorVisible(st);
  saveUiConfig(st);

  wcscpy(st->modalTitle, L"【 新規時間割を作成 】");
  wcsncpy(st->modalLine1, st->fileNames[st->activeFileIdx], 95);
  st->modalLine1[95] = L'\0';
  wcscpy(st->modalLine2, L"上部タブバーをONにし切り替えました。");
  st->returnModeAfterModal = ScreenMode::Settings;
  st->mode = ScreenMode::InfoModal;
}

static void deleteCurrentTimetableFile(TimeTableState * st) {
  if (st->fileCount <= 1) return;
  wchar_t full[MAX_PATH] = L"";
  LiliumUI::joinPath(st->storeDir, st->fileNames[st->activeFileIdx], full, MAX_PATH);
  DeleteFileW(full);
  scanTimetableFiles(st, L"");
  switchToTimetableFile(st, 0);
  st->mode = ScreenMode::Settings;
}

static void handleSettingsKey(TimeTableState * st, WPARAM vkCode) {
  if (vkCode == VK_F2) {
    st->setTab = (st->setTab + 1) % 3;
    st->setRow = 0;
    st->setScroll = 0;
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == '1' || vkCode == 'Q') { st->setTab = 0; st->setRow = 0; st->setScroll = 0; st->window.requestRepaint(true); return; }
  if (vkCode == '2' || vkCode == 'W') { st->setTab = 1; st->setRow = 0; st->setScroll = 0; st->window.requestRepaint(true); return; }
  if (vkCode == '3' || vkCode == 'E') { st->setTab = 2; st->setRow = 0; st->setScroll = 0; st->window.requestRepaint(true); return; }

  if (vkCode == VK_ESCAPE || vkCode == VK_BACK || vkCode == VK_F1) {
    saveTimetableFile(st, st->activeFileIdx, st->data);
    saveUiConfig(st);
    ensureCursorVisible(st);
    st->mode = ScreenMode::Grid;
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_HOME || vkCode == VK_F10) {
    saveTimetableFile(st, st->activeFileIdx, st->data);
    saveUiConfig(st);
    st->window.requestQuit();
    return;
  }

  if (st->setTab == 0) {
    if (vkCode == VK_UP) st->setRow = (st->setRow + kDays - 1) % kDays;
    else if (vkCode == VK_DOWN) st->setRow = (st->setRow + 1) % kDays;
    else if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_SPACE) {
      int visCnt = 0;
      for (int i = 0; i < kDays; ++i) if (st->data.dayVisible[i]) ++visCnt;
      if (!st->data.dayVisible[st->setRow] || visCnt > 1) {
        st->data.dayVisible[st->setRow] = !st->data.dayVisible[st->setRow];
        saveTimetableFile(st, st->activeFileIdx, st->data);
      }
    } else if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
      int delta = (vkCode == VK_RIGHT) ? 1 : (kColorCount - 1);
      st->data.dayColorIdx[st->setRow] = (st->data.dayColorIdx[st->setRow] + delta) % kColorCount;
      saveTimetableFile(st, st->activeFileIdx, st->data);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (st->setTab == 1) {
    int totalRows = 2 + st->data.periodCount;
    if (vkCode == VK_UP) st->setRow = (st->setRow + totalRows - 1) % totalRows;
    else if (vkCode == VK_DOWN) st->setRow = (st->setRow + 1) % totalRows;
    else if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
      int dir = (vkCode == VK_RIGHT) ? 1 : -1;
      if (st->setRow == 0) {
        st->data.periodCount = clampInt(st->data.periodCount + dir, 1, kMaxPeriods);
        saveTimetableFile(st, st->activeFileIdx, st->data);
      } else if (st->setRow == 1) {
        st->setTmplIdx = (st->setTmplIdx + kTmplCount + dir) % kTmplCount;
      } else {
        int p = st->setRow - 2;
        if (st->setPeriodCol == 0) {
          st->data.periods[p].startMin = clampInt(st->data.periods[p].startMin + dir * 5, 6 * 60, 22 * 60);
          if (st->data.periods[p].endMin <= st->data.periods[p].startMin) {
            st->data.periods[p].endMin = clampInt(st->data.periods[p].startMin + 45, 6 * 60 + 5, 23 * 60 + 50);
          }
        } else if (st->setPeriodCol == 1) {
          st->data.periods[p].endMin = clampInt(st->data.periods[p].endMin + dir * 5,
                                                st->data.periods[p].startMin + 5, 23 * 60 + 50);
        } else {
          st->data.periods[p].breakKind = (st->data.periods[p].breakKind + 3 + dir) % 3;
        }
        saveTimetableFile(st, st->activeFileIdx, st->data);
      }
    } else if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_SPACE) {
      if (st->setRow == 1) {
        applyPeriodTemplate(st->data, st->setTmplIdx);
        saveTimetableFile(st, st->activeFileIdx, st->data);
      } else if (st->setRow >= 2) {
        st->setPeriodCol = (st->setPeriodCol + 1) % 3;
      }
    }
    if (st->setRow >= st->setScroll + 8) st->setScroll = st->setRow - 7;
    if (st->setRow < st->setScroll) st->setScroll = st->setRow;
    st->window.requestRepaint(true);
    return;
  }

  // Tab 2: Files & Tab Bar
  if (vkCode == VK_UP) st->setRow = (st->setRow + 5) % 6;
  else if (vkCode == VK_DOWN) st->setRow = (st->setRow + 1) % 6;
  else if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
    int dir = (vkCode == VK_RIGHT) ? 1 : -1;
    if (st->setRow == 0) {
      st->showTabBar = !st->showTabBar;
      saveUiConfig(st);
    } else if (st->setRow == 1 && st->fileCount > 1) {
      switchToTimetableFile(st, st->activeFileIdx + dir);
    } else if (st->setRow == 2) {
      st->newNameIdx = (st->newNameIdx + kNewNameCount + dir) % kNewNameCount;
    } else if (st->setRow == 3) {
      st->newTemplateKind = (st->newTemplateKind + 3 + dir) % 3;
    }
  } else if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_SPACE) {
    if (st->setRow == 0) {
      st->showTabBar = !st->showTabBar;
      saveUiConfig(st);
    } else if (st->setRow == 1 && st->fileCount > 1) {
      switchToTimetableFile(st, st->activeFileIdx + 1);
    } else if (st->setRow == 4) {
      createNewTimetableFile(st);
    } else if (st->setRow == 5 && st->fileCount > 1) {
      st->modalFocus = 1;
      st->mode = ScreenMode::DeleteConfirm;
    }
  }
  st->window.requestRepaint(true);
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  TimeTableState * st = static_cast<TimeTableState *>(userData);

  if (st->mode == ScreenMode::InfoModal) {
    if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->mode = st->returnModeAfterModal;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->mode == ScreenMode::DeleteConfirm) {
    if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
      st->modalFocus = (st->modalFocus == 0) ? 1 : 0;
      st->window.requestRepaint(true);
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      if (st->modalFocus == 0) deleteCurrentTimetableFile(st);
      else st->mode = ScreenMode::Settings;
      st->window.requestRepaint(true);
    } else if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->mode = ScreenMode::Settings;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->mode == ScreenMode::Settings) {
    handleSettingsKey(st, vkCode);
    return;
  }

  if (st->mode == ScreenMode::PairPicker) {
    if (vkCode == VK_UP && st->pairListCount > 0) {
      st->pairSel = (st->pairSel + st->pairListCount - 1) % st->pairListCount;
      st->window.requestRepaint(true);
    } else if (vkCode == VK_DOWN && st->pairListCount > 0) {
      st->pairSel = (st->pairSel + 1) % st->pairListCount;
      st->window.requestRepaint(true);
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      if (st->pairSel >= 0 && st->pairSel < st->pairListCount) {
        st->data.grid[st->curDay][st->curPeriod] = st->pairCourseIds[st->pairSel];
        gcUnusedCourses(st->data);
        saveTimetableFile(st, st->activeFileIdx, st->data);
      }
      st->mode = ScreenMode::Grid;
      st->window.requestRepaint(true);
    } else if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->mode = ScreenMode::Grid;
      st->window.requestRepaint(true);
    } else if (vkCode == VK_HOME || vkCode == VK_F10) {
      st->window.requestQuit();
    }
    return;
  }

  if (st->mode == ScreenMode::CellEdit) {
    int ids[kMaxCourses] = {};
    int idCount = collectExistingCourseIds(st, ids, kMaxCourses);

    if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->mode = ScreenMode::Grid;
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_UP) st->editRow = (st->editRow + 5) % 6;
    else if (vkCode == VK_DOWN) st->editRow = (st->editRow + 1) % 6;
    else if (vkCode == VK_PRIOR || vkCode == VK_NEXT) {
      if (st->editRow == 0) {
        int step = (vkCode == VK_NEXT) ? 6 : -6;
        if (st->editPresetIdx < 0) st->editPresetIdx = 0;
        else st->editPresetIdx = (st->editPresetIdx + kPresetCount + step) % kPresetCount;
      }
    } else if (vkCode == VK_LEFT || vkCode == VK_RIGHT) {
      int dir = (vkCode == VK_RIGHT) ? 1 : -1;
      if (st->editRow == 0) {
        if (st->editPresetIdx < 0) st->editPresetIdx = 0;
        else st->editPresetIdx = (st->editPresetIdx + kPresetCount + dir) % kPresetCount;
      } else if (st->editRow == 1) {
        st->editColorIdx = (st->editColorIdx + kColorCount + dir) % kColorCount;
      } else if (st->editRow == 2) {
        if (st->editRoomIdx < 0) st->editRoomIdx = 0;
        else st->editRoomIdx = (st->editRoomIdx + kRoomCount + dir) % kRoomCount;
      } else if (st->editRow == 3 && idCount > 0) {
        int curPos = 0;
        for (int i = 0; i < idCount; ++i) {
          if (ids[i] == st->editLinkCourseId) { curPos = i + 1; break; }
        }
        curPos = (curPos + (idCount + 1) + dir) % (idCount + 1);
        st->editLinkCourseId = (curPos == 0) ? 0 : ids[curPos - 1];
      } else if (st->editRow == 4) {
        st->editSyncAll = !st->editSyncAll;
      } else if (st->editRow == 5) {
        st->editBtnFocus = (st->editBtnFocus + 3 + dir) % 3;
      }
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      if (st->editRow < 5 || st->editBtnFocus == 0) {
        saveCellEditor(st);
      } else if (st->editBtnFocus == 1) {
        st->data.grid[st->curDay][st->curPeriod] = 0;
        gcUnusedCourses(st->data);
        saveTimetableFile(st, st->activeFileIdx, st->data);
        st->mode = ScreenMode::Grid;
      } else {
        st->mode = ScreenMode::Grid;
      }
    }
    st->window.requestRepaint(true);
    return;
  }

  // Grid mode
  switch (vkCode) {
    case VK_LEFT:
      moveCursorDay(st, -1);
      st->window.requestRepaint(true);
      break;
    case VK_RIGHT:
      moveCursorDay(st, 1);
      st->window.requestRepaint(true);
      break;
    case VK_UP:
      st->curPeriod = (st->curPeriod + st->data.periodCount - 1) % st->data.periodCount;
      st->window.requestRepaint(true);
      break;
    case VK_DOWN:
      st->curPeriod = (st->curPeriod + 1) % st->data.periodCount;
      st->window.requestRepaint(true);
      break;
    case VK_RETURN:
    case VK_F3:
      openCellEditor(st);
      st->window.requestRepaint(true);
      break;
    case VK_F1:
      st->setTab = 0;
      st->setRow = 0;
      st->setScroll = 0;
      st->mode = ScreenMode::Settings;
      st->window.requestRepaint(true);
      break;
    case VK_F2:
    case VK_NEXT:
      if (st->fileCount > 1) {
        switchToTimetableFile(st, st->activeFileIdx + 1);
        st->window.requestRepaint(true);
      }
      break;
    case VK_PRIOR:
      if (st->fileCount > 1) {
        switchToTimetableFile(st, st->activeFileIdx - 1);
        st->window.requestRepaint(true);
      }
      break;
    case VK_F4:
      openPairPicker(st);
      st->window.requestRepaint(true);
      break;
    case VK_F5:
    case VK_DELETE:
    case VK_BACK:
      if (st->data.grid[st->curDay][st->curPeriod] != 0) {
        st->data.grid[st->curDay][st->curPeriod] = 0;
        gcUnusedCourses(st->data);
        saveTimetableFile(st, st->activeFileIdx, st->data);
        st->window.requestRepaint(true);
      }
      break;
    case VK_F8:
      st->showTabBar = !st->showTabBar;
      saveUiConfig(st);
      st->window.requestRepaint(true);
      break;
    case VK_ESCAPE:
    case VK_HOME:
    case VK_F10:
      saveTimetableFile(st, st->activeFileIdx, st->data);
      saveUiConfig(st);
      st->window.requestQuit();
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  TimeTableState * st = static_cast<TimeTableState *>(userData);

  if (st->mode == ScreenMode::InfoModal) {
    st->mode = st->returnModeAfterModal;
    st->window.requestRepaint(true);
    return;
  }

  if (st->mode == ScreenMode::DeleteConfirm) {
    const int dlgX = (LiliumUI::kScreenW - 500) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int bx0 = dlgX + (500 - (2 * 160 + 22)) / 2;
    const int bx1 = bx0 + 182;
    if (y >= by && y < by + 42) {
      if (x >= bx0 && x < bx0 + 160) deleteCurrentTimetableFile(st);
      else if (x >= bx1 && x < bx1 + 160) st->mode = ScreenMode::Settings;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    if (st->mode == ScreenMode::Grid) {
      saveTimetableFile(st, st->activeFileIdx, st->data);
      saveUiConfig(st);
      st->window.requestQuit();
    } else {
      saveTimetableFile(st, st->activeFileIdx, st->data);
      saveUiConfig(st);
      ensureCursorVisible(st);
      st->mode = ScreenMode::Grid;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->mode == ScreenMode::Settings) {
    if (y >= 48 && y < 76) {
      for (int t = 0; t < 3; ++t) {
        int tx = 12 + t * 206;
        if (x >= tx && x < tx + 200) {
          st->setTab = t;
          st->setRow = 0;
          st->setScroll = 0;
          st->window.requestRepaint(true);
          return;
        }
      }
    }
    if (st->setTab == 0 && y >= 92 && y < 92 + kDays * 46) {
      int d = (y - 92) / 46;
      st->setRow = d;
      if (x >= 120 && x < 236) {
        handleSettingsKey(st, VK_SPACE);
      } else if (x >= 244 && x < 610) {
        handleSettingsKey(st, VK_RIGHT);
      } else {
        st->window.requestRepaint(true);
      }
      return;
    }
    return;
  }

  if (st->mode == ScreenMode::CellEdit) {
    const int mx = 26, my = 46;
    for (int r = 0; r < 5; ++r) {
      int ry = my + 50 + r * 48;
      if (x >= mx + 18 && x < mx + 570 && y >= ry && y < ry + 42) {
        st->editRow = r;
        onKeyDown(VK_RIGHT, false, userData);
        return;
      }
    }
    int by = my + 306;
    if (y >= by && y < by + 42) {
      for (int b = 0; b < 3; ++b) {
        int bx = mx + 28 + b * 182;
        if (x >= bx && x < bx + 168) {
          st->editRow = 5;
          st->editBtnFocus = b;
          onKeyDown(VK_RETURN, false, userData);
          return;
        }
      }
    }
    return;
  }

  if (st->mode != ScreenMode::Grid) return;

  // Tab bar clicks
  if (st->showTabBar && y >= 42 && y <= 68) {
    if (x >= 604 && x <= 634) {
      st->setTab = 2;
      st->setRow = 4;
      st->mode = ScreenMode::Settings;
      st->window.requestRepaint(true);
      return;
    }
    int tx = 6;
    const int maxTabRight = 600;
    for (int i = 0; i < st->fileCount; ++i) {
      int tw = computeTabWidth(st->tabNames[i]);
      if (tx + tw > maxTabRight) break;
      if (x >= tx && x < tx + tw) {
        switchToTimetableFile(st, i);
        st->window.requestRepaint(true);
        return;
      }
      tx += tw + 4;
    }
  }

  // Grid cell clicks
  GridLayout g = computeGridLayout(st);
  for (int p = 0; p < st->data.periodCount; ++p) {
    if (y >= g.periodY[p] && y < g.periodY[p] + g.rowH) {
      for (int vi = 0; vi < g.visDayCount; ++vi) {
        int cx = g.gridX + g.timeColW + vi * g.colW;
        if (x >= cx && x < cx + g.colW) {
          int dayIdx = g.visDays[vi];
          if (st->curDay == dayIdx && st->curPeriod == p) {
            openCellEditor(st);
          } else {
            st->curDay = dayIdx;
            st->curPeriod = p;
          }
          st->window.requestRepaint(true);
          return;
        }
      }
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  TimeTableState * st = static_cast<TimeTableState *>(userData);
  if (st->mode != ScreenMode::Grid) return;
  SYSTEMTIME tm;
  GetLocalTime(&tm);
  int bat = 100;
  bool chg = false;
  LiliumUI::queryBattery(bat, chg);
  if (tm.wMinute != st->lastMin || bat != st->lastBat) {
    st->lastMin = tm.wMinute;
    st->lastBat = bat;
    GridLayout g = computeGridLayout(st);
    wchar_t statusStr[32];
    _snwprintf(statusStr, 32, L"%d日 %d限", g.visDayCount, st->data.periodCount);
    statusStr[31] = L'\0';
    LiliumUI::renderSidebarStatus(c, statusStr);
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * rawMem = calloc(1, sizeof(TimeTableState));
  if (!rawMem) return 1;
  TimeTableState * state = new (rawMem) TimeTableState();
  state->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  LiliumUI::joinPath(exeDir, L"timetables", state->storeDir, MAX_PATH);
  CreateDirectoryW(state->storeDir, NULL);

  wchar_t prefFile[64] = L"";
  loadUiConfig(state, prefFile, 64);
  scanTimetableFiles(state, prefFile);

  wchar_t activeFull[MAX_PATH] = L"";
  LiliumUI::joinPath(state->storeDir, state->fileNames[state->activeFileIdx], activeFull, MAX_PATH);
  if (!loadTimetableFile(activeFull, state->data)) {
    initSampleTimetable(state->data);
    saveTimetableFile(state, state->activeFileIdx, state->data);
  }
  ensureCursorVisible(state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;

  int rc = 1;
  if (state->window.create(L"LiliumTimeTableWinClass",
                           L"Lilium TimeTable (SHARP Brain 800x480)",
                           cb, state, state->manageTaskBar)) {
    rc = state->window.runLoop();
  }

  saveTimetableFile(state, state->activeFileIdx, state->data);
  saveUiConfig(state);
  state->~TimeTableState();
  free(rawMem);
  return rc;
}
