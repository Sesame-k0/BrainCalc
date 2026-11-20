#include "../common/lilium_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace {

static constexpr int kMaxCols = 200;
static constexpr int kMaxRows = 120;
static constexpr int kRuleCount = 6;
static constexpr int kPresetCount = 6;
static constexpr int kThemeCount = 3;
static constexpr int kNumAnts = 4;
static constexpr int kAntStepsPerFrame = 25;
static constexpr int kHashHistory = 8;

struct RuleSpec {
  const wchar_t * shortName;
  const wchar_t * fullName;
  const wchar_t * formula;
  const wchar_t * descJa;
};

static constexpr RuleSpec kRules[kRuleCount] = {
  { L"Life",    L"Conway Life",   L"B3/S23",     L"古典的ライフゲーム。\n安定したグライダーと振動子が誕生。" },
  { L"High",    L"HighLife",      L"B36/S23",    L"B36拡張ルール。\n自己複製子(レプリケーター)が出現!" },
  { L"Seeds",   L"Seeds",         L"B2/S",       L"生存条件なし。\n毎世代すべて消滅し爆発的宇宙船を量産。" },
  { L"D&N",     L"Day & Night",   L"B3678/S34678", L"明暗対称ルール。\n溶岩や迷路状の有機的模様へ成長。" },
  { L"Brain",   L"Brian's Brain", L"3状態 CA",   L"白→青(不応期)→黒。\n無数の高速宇宙船群が画面を飛び交う!" },
  { L"Ant",     L"Langton's Ant", L"4匹のアリ",   L"白右折/色左折。\n混沌の末に斜めのハイウェイを構築!" }
};

static constexpr const wchar_t * kPresetNames[kPresetCount] = {
  L"ランダム",
  L"対称万華鏡",
  L"グライダー銃",
  L"パルサー&船",
  L"メトセラ",
  L"レプリケーター"
};

struct ThemePalette {
  const wchar_t * name;
  uint32_t bg;
  uint32_t newborn;
  uint32_t live;
  uint32_t mature;
  uint32_t dying;   // Brian's Brain state 2 or Ant trail 2..4
  uint32_t antTrail2;
  uint32_t antTrail3;
  uint32_t antHead;
  uint32_t cursor;
};

static constexpr ThemePalette kThemes[kThemeCount] = {
  { L"Lilium",  0x141B20, 0xA8ECE2, 0x4FBFAF, 0x2A9486, 0x1B4A44, 0x2F7E74, 0x62C9BA, 0xFFF275, 0xFFFFFF },
  { L"Cyber",   0x0A0E14, 0x9EF5FF, 0x38E8FF, 0xFFB84D, 0xFF4D8D, 0xB84DFF, 0x38E8FF, 0xFFFFFF, 0xFFEE58 },
  { L"LCD",     0xF5F7FA, 0x167468, 0x1E2832, 0x3A4652, 0x9AA6B2, 0x5E9E94, 0x3A6E8C, 0xE04434, 0x4FBFAF }
};

static constexpr UINT kSpeedMs[5] = { 240, 140, 80, 45, 25 };

struct AntState {
  int x;
  int y;
  int dir; // 0=N, 1=E, 2=S, 3=W
};

struct LifeState {
  LiliumUI::AppWindow window;
  uint8_t grid[kMaxCols * kMaxRows] = {};
  uint8_t nextGrid[kMaxCols * kMaxRows] = {};
  uint8_t age[kMaxCols * kMaxRows] = {};
  AntState ants[kNumAnts] = {};

  int ruleIndex = 0;      // 0..5
  int presetIndex = 1;    // start with Kaleidoscope/Symmetric for immediate visual punch
  int themeIndex = 0;     // 0..2
  int cellSize = 4;       // 4 or 8
  int speedLevel = 3;     // 1..5 (index = speedLevel - 1)
  bool running = true;
  bool fullscreen = false;
  bool autoEvolve = true;
  bool gridDirtyOnly = false;

  int cols = 160;
  int rows = 99;
  int vpX = 0;
  int vpY = 42;
  int vpW = 640;
  int vpH = 398;
  int boardOffX = 0;
  int boardOffY = 43;

  int cursorX = 80;
  int cursorY = 49;
  uint32_t gen = 0;
  int liveCount = 0;
  uint32_t rng = 0x20261120u;

  uint64_t hashHist[kHashHistory] = {};
  int stagnantTicks = 0;
  int lastMin = -1;
  int lastBat = -1;
};

static uint32_t xorshift32(LifeState * st) {
  uint32_t x = st->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  st->rng = x ? x : 0x12345679u;
  return st->rng;
}

static void computeBoardMetrics(LifeState * st) {
  if (st->fullscreen) {
    st->vpX = 0;
    st->vpY = 0;
    st->vpW = LiliumUI::kScreenW;
    st->vpH = LiliumUI::kScreenH;
  } else {
    st->vpX = 0;
    st->vpY = LiliumUI::kHeaderH;
    st->vpW = LiliumUI::kMainW;
    st->vpH = 440 - LiliumUI::kHeaderH; // 398
  }
  st->cols = st->vpW / st->cellSize;
  st->rows = st->vpH / st->cellSize;
  if (st->cols > kMaxCols) st->cols = kMaxCols;
  if (st->rows > kMaxRows) st->rows = kMaxRows;
  st->boardOffX = st->vpX + (st->vpW - st->cols * st->cellSize) / 2;
  st->boardOffY = st->vpY + (st->vpH - st->rows * st->cellSize) / 2;
  if (st->cursorX >= st->cols) st->cursorX = st->cols / 2;
  if (st->cursorY >= st->rows) st->cursorY = st->rows / 2;
}

static void setCell(LifeState * st, int x, int y, uint8_t val) {
  int cx = ((x % st->cols) + st->cols) % st->cols;
  int cy = ((y % st->rows) + st->rows) % st->rows;
  st->grid[cy * st->cols + cx] = val;
  st->age[cy * st->cols + cx] = val ? 1 : 0;
}

static void resetAnts(LifeState * st) {
  int cx = st->cols / 2;
  int cy = st->rows / 2;
  int off = (st->cols > 40) ? 10 : 5;
  st->ants[0] = { (cx - off + st->cols) % st->cols, (cy - off + st->rows) % st->rows, 0 };
  st->ants[1] = { (cx + off) % st->cols, (cy - off + st->rows) % st->rows, 1 };
  st->ants[2] = { (cx + off) % st->cols, (cy + off) % st->rows, 2 };
  st->ants[3] = { (cx - off + st->cols) % st->cols, (cy + off) % st->rows, 3 };
}

static uint64_t computeBoardHash(const LifeState * st) {
  uint64_t h = 14695981039346656037ULL;
  int n = st->cols * st->rows;
  for (int i = 0; i < n; ++i) {
    h ^= st->grid[i];
    h *= 1099511628211ULL;
  }
  return h;
}

static void updateLiveCount(LifeState * st) {
  int n = st->cols * st->rows;
  int cnt = 0;
  for (int i = 0; i < n; ++i) {
    if (st->grid[i] == 1) ++cnt;
  }
  st->liveCount = cnt;
}

static void seedPreset(LifeState * st) {
  computeBoardMetrics(st);
  memset(st->grid, 0, sizeof(st->grid));
  memset(st->nextGrid, 0, sizeof(st->nextGrid));
  memset(st->age, 0, sizeof(st->age));
  memset(st->hashHist, 0, sizeof(st->hashHist));
  st->gen = 0;
  st->stagnantTicks = 0;
  st->rng ^= GetTickCount() | 1u;

  int cx = st->cols / 2;
  int cy = st->rows / 2;
  int p = st->presetIndex;

  if (st->ruleIndex == 5) {
    // Langton's Ant: optionally place initial seed motif + 4 symmetric ants
    if (p != 0) {
      int r = 6;
      for (int dy = -r; dy <= r; ++dy) {
        if (abs(dy) % 2 == 0) setCell(st, cx, cy + dy, 1);
      }
      for (int dx = -r; dx <= r; ++dx) {
        if (abs(dx) % 2 == 0) setCell(st, cx + dx, cy, 2);
      }
    }
    resetAnts(st);
    updateLiveCount(st);
    return;
  }

  if (p == 0) {
    // Random seed tuned to rule
    int marginX = st->cols / 8;
    int marginY = st->rows / 8;
    uint32_t pct = (st->ruleIndex == 2 || st->ruleIndex == 4) ? 16u :
                   (st->ruleIndex == 3) ? 45u : 30u;
    if (st->ruleIndex == 2 || st->ruleIndex == 4) {
      marginX = st->cols / 3;
      marginY = st->rows / 3;
    }
    for (int y = marginY; y < st->rows - marginY; ++y) {
      for (int x = marginX; x < st->cols - marginX; ++x) {
        if ((xorshift32(st) % 100u) < pct) {
          setCell(st, x, y, 1);
        }
      }
    }
  } else if (p == 1) {
    // 4-way symmetric kaleidoscope
    int halfW = st->cols / 5;
    int halfH = st->rows / 5;
    uint32_t pct = (st->ruleIndex == 2 || st->ruleIndex == 4) ? 22u : 42u;
    for (int dy = 0; dy < halfH; ++dy) {
      for (int dx = 0; dx < halfW; ++dx) {
        if ((xorshift32(st) % 100u) < pct) {
          setCell(st, cx + dx, cy + dy, 1);
          setCell(st, cx - 1 - dx, cy + dy, 1);
          setCell(st, cx + dx, cy - 1 - dy, 1);
          setCell(st, cx - 1 - dx, cy - 1 - dy, 1);
        }
      }
    }
  } else if (p == 2) {
    // Classic Gosper Glider Gun (36x9)
    static const int kGun[][2] = {
      {24,0},{22,1},{24,1},{12,2},{13,2},{20,2},{21,2},{34,2},{35,2},
      {11,3},{15,3},{20,3},{21,3},{34,3},{35,3},{0,4},{1,4},{10,4},
      {16,4},{20,4},{21,4},{0,5},{1,5},{10,5},{14,5},{16,5},{17,5},
      {22,5},{24,5},{10,6},{16,6},{24,6},{11,7},{15,7},{12,8},{13,8}
    };
    int gx = cx - 18;
    int gy = cy - 16;
    if (gy < 4) gy = 4;
    if (gx < 4) gx = 4;
    for (size_t i = 0; i < sizeof(kGun) / sizeof(kGun[0]); ++i) {
      setCell(st, gx + kGun[i][0], gy + kGun[i][1], 1);
    }
    // Add symmetric twin gun inverted cleanly when enough vertical room
    if (st->rows >= 64) {
      for (size_t i = 0; i < sizeof(kGun) / sizeof(kGun[0]); ++i) {
        setCell(st, st->cols - 6 - kGun[i][0], st->rows - 6 - kGun[i][1], 1);
      }
    }
  } else if (p == 3) {
    // Period-3 Pulsar + 4 LWSS spaceships
    static const int kPulsarOct[12][2] = {
      {2,1},{3,1},{4,1},{1,2},{1,3},{1,4},{6,2},{6,3},{6,4},{2,6},{3,6},{4,6}
    };
    for (int i = 0; i < 12; ++i) {
      int dx = kPulsarOct[i][0], dy = kPulsarOct[i][1];
      setCell(st, cx + dx, cy + dy, 1);
      setCell(st, cx - dx, cy + dy, 1);
      setCell(st, cx + dx, cy - dy, 1);
      setCell(st, cx - dx, cy - dy, 1);
    }
    static const int kLwss[9][2] = {
      {0,0},{3,0},{4,1},{0,2},{4,2},{1,3},{2,3},{3,3},{4,3}
    };
    for (int i = 0; i < 9; ++i) {
      setCell(st, 10 + kLwss[i][0], 10 + kLwss[i][1], 1);
      setCell(st, st->cols - 15 - kLwss[i][0], st->rows - 14 + kLwss[i][1], 1);
    }
  } else if (p == 4) {
    // Methuselahs: Acorn + R-Pentomino
    static const int kAcorn[7][2] = { {1,0},{3,1},{0,2},{1,2},{4,2},{5,2},{6,2} };
    static const int kRPent[5][2] = { {1,0},{2,0},{0,1},{1,1},{1,2} };
    for (int i = 0; i < 7; ++i) setCell(st, cx - 14 + kAcorn[i][0], cy + kAcorn[i][1], 1);
    for (int i = 0; i < 5; ++i) setCell(st, cx + 12 + kRPent[i][0], cy - 2 + kRPent[i][1], 1);
  } else {
    // HighLife Replicator (diagonal 12 cells) + Brian/Seeds burst
    static const int kRep[12][2] = {
      {2,0},{3,0},{4,0},{1,1},{4,1},{0,2},{4,2},{0,3},{3,3},{0,4},{1,4},{2,4}
    };
    for (int i = 0; i < 12; ++i) setCell(st, cx - 2 + kRep[i][0], cy - 2 + kRep[i][1], 1);
  }

  updateLiveCount(st);
}

static void resizePreservingBoard(LifeState * st, int oldCols, int oldRows) {
  uint8_t temp[kMaxCols * kMaxRows];
  uint8_t tempAge[kMaxCols * kMaxRows];
  memcpy(temp, st->grid, sizeof(temp));
  memcpy(tempAge, st->age, sizeof(tempAge));

  computeBoardMetrics(st);
  memset(st->grid, 0, sizeof(st->grid));
  memset(st->age, 0, sizeof(st->age));

  int dxOff = (st->cols - oldCols) / 2;
  int dyOff = (st->rows - oldRows) / 2;
  for (int y = 0; y < oldRows; ++y) {
    int ny = y + dyOff;
    if (ny < 0 || ny >= st->rows) continue;
    for (int x = 0; x < oldCols; ++x) {
      int nx = x + dxOff;
      if (nx < 0 || nx >= st->cols) continue;
      st->grid[ny * st->cols + nx] = temp[y * oldCols + x];
      st->age[ny * st->cols + nx] = tempAge[y * oldCols + x];
    }
  }
  for (int a = 0; a < kNumAnts; ++a) {
    st->ants[a].x = ((st->ants[a].x + dxOff) % st->cols + st->cols) % st->cols;
    st->ants[a].y = ((st->ants[a].y + dyOff) % st->rows + st->rows) % st->rows;
  }
  updateLiveCount(st);
}

static void stepSimulation(LifeState * st) {
  const int W = st->cols;
  const int H = st->rows;

  if (st->ruleIndex == 5) {
    // Langton's Ant (4 ants)
    static constexpr int kDx[4] = { 0, 1, 0, -1 };
    static constexpr int kDy[4] = { -1, 0, 1, 0 };
    for (int step = 0; step < kAntStepsPerFrame; ++step) {
      for (int a = 0; a < kNumAnts; ++a) {
        AntState & ant = st->ants[a];
        int idx = ant.y * W + ant.x;
        uint8_t cell = st->grid[idx];
        if (cell == 0) {
          ant.dir = (ant.dir + 1) & 3;
          st->grid[idx] = static_cast<uint8_t>(1 + (a % 3));
        } else {
          ant.dir = (ant.dir + 3) & 3;
          st->grid[idx] = 0;
        }
        ant.x = (ant.x + kDx[ant.dir] + W) % W;
        ant.y = (ant.y + kDy[ant.dir] + H) % H;
      }
      ++st->gen;
    }
    int n = W * H;
    int cnt = 0;
    for (int i = 0; i < n; ++i) {
      if (st->grid[i] != 0) ++cnt;
    }
    st->liveCount = cnt;

    if (st->autoEvolve && st->gen >= 4000) {
      st->ruleIndex = 0;
      st->presetIndex = (st->presetIndex + 1) % kPresetCount;
      seedPreset(st);
    }
    return;
  }

  int nextLive = 0;
  for (int y = 0; y < H; ++y) {
    int ym1 = (y == 0) ? (H - 1) : (y - 1);
    int yp1 = (y + 1 == H) ? 0 : (y + 1);
    const uint8_t * rPrev = st->grid + ym1 * W;
    const uint8_t * rCur  = st->grid + y * W;
    const uint8_t * rNext = st->grid + yp1 * W;
    uint8_t * dst = st->nextGrid + y * W;
    uint8_t * ageRow = st->age + y * W;

    for (int x = 0; x < W; ++x) {
      int xm1 = (x == 0) ? (W - 1) : (x - 1);
      int xp1 = (x + 1 == W) ? 0 : (x + 1);
      uint8_t s = rCur[x];

      int n = (rPrev[xm1] == 1) + (rPrev[x] == 1) + (rPrev[xp1] == 1) +
              (rCur[xm1]  == 1) +                   (rCur[xp1]  == 1) +
              (rNext[xm1] == 1) + (rNext[x] == 1) + (rNext[xp1] == 1);

      uint8_t out = 0;
      switch (st->ruleIndex) {
        case 0: // Conway B3/S23
          out = s ? ((n == 2 || n == 3) ? 1 : 0) : ((n == 3) ? 1 : 0);
          break;
        case 1: // HighLife B36/S23
          out = s ? ((n == 2 || n == 3) ? 1 : 0) : ((n == 3 || n == 6) ? 1 : 0);
          break;
        case 2: // Seeds B2/S
          out = (!s && n == 2) ? 1 : 0;
          break;
        case 3: // Day & Night B3678/S34678
          out = s ? ((n == 3 || n == 4 || n >= 6) ? 1 : 0) : ((n == 3 || n >= 6) ? 1 : 0);
          break;
        case 4: // Brian's Brain: 0->1 if 2 live neighbors, 1->2, 2->0
          if (s == 1) out = 2;
          else if (s == 2) out = 0;
          else out = (n == 2) ? 1 : 0;
          break;
        default:
          break;
      }

      dst[x] = out;
      if (out == 1) {
        ++nextLive;
        if (ageRow[x] < 255) ageRow[x]++;
      } else {
        ageRow[x] = 0;
      }
    }
  }

  memcpy(st->grid, st->nextGrid, static_cast<size_t>(W * H));
  ++st->gen;
  st->liveCount = nextLive;

  // Auto-Evolve stagnation detector
  if (st->autoEvolve) {
    uint64_t h = computeBoardHash(st);
    bool seen = false;
    for (int i = 0; i < kHashHistory; ++i) {
      if (st->hashHist[i] == h) {
        seen = true;
        break;
      }
    }
    st->hashHist[st->gen % kHashHistory] = h;
    if (seen || nextLive == 0) {
      ++st->stagnantTicks;
    } else {
      st->stagnantTicks = 0;
    }
    uint32_t maxGen = (st->ruleIndex == 2 || st->ruleIndex == 4) ? 280u : 520u;
    if (nextLive == 0 || st->stagnantTicks >= 24 || st->gen >= maxGen) {
      st->ruleIndex = (st->ruleIndex + 1) % kRuleCount;
      if (st->ruleIndex == 0) {
        st->presetIndex = (st->presetIndex + 1) % kPresetCount;
      }
      seedPreset(st);
    }
  }
}

static void renderBoard(LiliumUI::Canvas & c, const LifeState * st) {
  const ThemePalette & th = kThemes[st->themeIndex];
  c.fillRect(st->vpX, st->vpY, st->vpW, st->vpH, th.bg);

  const int cs = st->cellSize;
  const int drawSize = (cs >= 4) ? (cs - 1) : cs;

  for (int y = 0; y < st->rows; ++y) {
    int py0 = st->boardOffY + y * cs;
    const uint8_t * gRow = st->grid + y * st->cols;
    const uint8_t * aRow = st->age + y * st->cols;

    for (int x = 0; x < st->cols; ++x) {
      uint8_t s = gRow[x];
      if (s == 0) continue;

      uint32_t col = th.live;
      if (st->ruleIndex == 5) {
        col = (s == 1) ? th.live : (s == 2) ? th.antTrail2 : th.antTrail3;
      } else if (s == 2) {
        col = th.dying;
      } else {
        uint8_t ag = aRow[x];
        if (ag <= 2) col = th.newborn;
        else if (ag >= 18) col = th.mature;
        else col = th.live;
      }

      int px0 = st->boardOffX + x * cs;
      for (int dy = 0; dy < drawSize; ++dy) {
        uint32_t * dst = c.pixels + (py0 + dy) * c.width + px0;
        for (int dx = 0; dx < drawSize; ++dx) {
          dst[dx] = col;
        }
      }
    }
  }

  // Highlight Langton's Ants cleanly
  if (st->ruleIndex == 5) {
    for (int a = 0; a < kNumAnts; ++a) {
      int px0 = st->boardOffX + st->ants[a].x * cs;
      int py0 = st->boardOffY + st->ants[a].y * cs;
      c.fillRect(px0, py0, cs, cs, th.antHead);
    }
  }

  // Interactive edit cursor outline when paused
  if (!st->running) {
    int cxPx = st->boardOffX + st->cursorX * cs;
    int cyPx = st->boardOffY + st->cursorY * cs;
    c.drawThickFrame(cxPx - 1, cyPx - 1, cs + 2, cs + 2, 1, th.cursor);
  }

  c.addDirty(st->vpX, st->vpY, st->vpW, st->vpH);
}

static void renderHeaderAndStatus(LiliumUI::Canvas & c, const LifeState * st) {
  if (st->fullscreen) return;

  wchar_t badge[48];
  _snwprintf(badge, 48, L"%s  G:%lu", kRules[st->ruleIndex].shortName,
             static_cast<unsigned long>(st->gen));
  badge[47] = L'\0';
  LiliumUI::renderTopHeader(c, L"LILIUM LIFE", badge);

  const RuleSpec & rs = kRules[st->ruleIndex];
  const ThemePalette & th = kThemes[st->themeIndex];
  wchar_t sideBody[384];
  _snwprintf(sideBody, 384,
             L"【ルール】\n%s\n【世代 / 生存】\n%lu / %d\n【速度 / セル】\nLv.%d / %dpx\n【自動切替】\n%s (%s)\n%s",
             rs.formula,
             static_cast<unsigned long>(st->gen), st->liveCount,
             st->speedLevel, st->cellSize,
             st->autoEvolve ? L"ON" : L"OFF",
             th.name,
             rs.descJa);
  sideBody[383] = L'\0';

  const wchar_t * statusStr = st->running ? L"再生中" : L"一時停止";
  LiliumUI::renderSidebar(c, statusStr, rs.fullName, sideBody,
                          L"[F1]種別[F2]配置", L"[F8]全画[F9]自動",
                          L"戻　る", false);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  LifeState * st = static_cast<LifeState *>(userData);

  if (st->fullscreen) {
    renderBoard(c, st);
    return;
  }

  if (c.fullRedraw || !st->gridDirtyOnly) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
    renderBoard(c, st);
    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    c.drawText(10, 440, LiliumUI::kMainW - 20, 40, LiliumUI::kColTextSub,
               L"[Spc]再生 [1-6/F1]種別 [F2]配置 [←→]速度 [F8]全画面 [F9]自動",
               2, LiliumUI::Align::Left, true);
    renderHeaderAndStatus(c, st);
  } else {
    renderBoard(c, st);
    renderHeaderAndStatus(c, st);
  }
  st->gridDirtyOnly = false;
}

static void applySpeedTimer(LifeState * st) {
  st->window.setTimerInterval(kSpeedMs[st->speedLevel - 1]);
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  LifeState * st = static_cast<LifeState *>(userData);

  if (st->fullscreen && (vkCode == VK_ESCAPE || vkCode == VK_F8)) {
    int oc = st->cols, orow = st->rows;
    st->fullscreen = false;
    resizePreservingBoard(st, oc, orow);
    st->window.requestRepaint(true);
    return;
  }

  switch (vkCode) {
    case VK_F8: {
      int oc = st->cols, orow = st->rows;
      st->fullscreen = !st->fullscreen;
      resizePreservingBoard(st, oc, orow);
      st->window.requestRepaint(true);
      break;
    }
    case VK_F1:
      st->ruleIndex = (st->ruleIndex + 1) % kRuleCount;
      seedPreset(st);
      st->window.requestRepaint(true);
      break;
    case '1': case '2': case '3': case '4': case '5': case '6':
      st->ruleIndex = static_cast<int>(vkCode - '1');
      seedPreset(st);
      st->window.requestRepaint(true);
      break;
    case VK_F2:
      st->presetIndex = (st->presetIndex + 1) % kPresetCount;
      seedPreset(st);
      st->window.requestRepaint(true);
      break;
    case VK_F4:
      seedPreset(st);
      st->window.requestRepaint(true);
      break;
    case VK_F9:
      st->autoEvolve = !st->autoEvolve;
      st->window.requestRepaint(true);
      break;
    case 'C':
      st->themeIndex = (st->themeIndex + 1) % kThemeCount;
      st->window.requestRepaint(true);
      break;
    case VK_PRIOR:
    case VK_NEXT:
    case 'K':
    case 'L': {
      int oc = st->cols, orow = st->rows;
      st->cellSize = (st->cellSize == 4) ? 8 : 4;
      resizePreservingBoard(st, oc, orow);
      st->window.requestRepaint(true);
      break;
    }
    case VK_SPACE:
      st->running = !st->running;
      st->window.requestRepaint(true);
      break;
    case VK_RETURN:
      if (!st->running) {
        int idx = st->cursorY * st->cols + st->cursorX;
        st->grid[idx] = st->grid[idx] ? 0 : 1;
        st->age[idx] = st->grid[idx];
        updateLiveCount(st);
        st->gridDirtyOnly = true;
        st->window.requestRepaint(false);
      } else {
        st->running = false;
        st->window.requestRepaint(true);
      }
      break;
    case VK_F3:
    case 'S':
      if (!st->running) {
        stepSimulation(st);
        st->gridDirtyOnly = true;
        st->window.requestRepaint(false);
      }
      break;
    case VK_LEFT:
      if (!st->running) {
        st->cursorX = (st->cursorX - 1 + st->cols) % st->cols;
        st->gridDirtyOnly = true;
        st->window.requestRepaint(false);
      } else if (st->speedLevel > 1) {
        --st->speedLevel;
        applySpeedTimer(st);
        st->window.requestRepaint(true);
      }
      break;
    case VK_RIGHT:
      if (!st->running) {
        st->cursorX = (st->cursorX + 1) % st->cols;
        st->gridDirtyOnly = true;
        st->window.requestRepaint(false);
      } else if (st->speedLevel < 5) {
        ++st->speedLevel;
        applySpeedTimer(st);
        st->window.requestRepaint(true);
      }
      break;
    case VK_UP:
      if (!st->running) {
        st->cursorY = (st->cursorY - 1 + st->rows) % st->rows;
        st->gridDirtyOnly = true;
        st->window.requestRepaint(false);
      }
      break;
    case VK_DOWN:
      if (!st->running) {
        st->cursorY = (st->cursorY + 1) % st->rows;
        st->gridDirtyOnly = true;
        st->window.requestRepaint(false);
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
  LifeState * st = static_cast<LifeState *>(userData);

  if (st->fullscreen) {
    int oc = st->cols, orow = st->rows;
    st->fullscreen = false;
    resizePreservingBoard(st, oc, orow);
    st->window.requestRepaint(true);
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    st->window.requestQuit();
    return;
  }

  if (x >= st->boardOffX && x < st->boardOffX + st->cols * st->cellSize &&
      y >= st->boardOffY && y < st->boardOffY + st->rows * st->cellSize) {
    int gx = (x - st->boardOffX) / st->cellSize;
    int gy = (y - st->boardOffY) / st->cellSize;
    st->cursorX = gx;
    st->cursorY = gy;
    // Stamp a 3x3 lively brush centered at click point
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        setCell(st, gx + dx, gy + dy, 1);
      }
    }
    updateLiveCount(st);
    st->gridDirtyOnly = true;
    st->window.requestRepaint(false);
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  LifeState * st = static_cast<LifeState *>(userData);

  if (st->running) {
    stepSimulation(st);
    renderBoard(c, st);
    if (!st->fullscreen && (st->gen % 4 == 0)) {
      renderHeaderAndStatus(c, st);
    }
    return;
  }

  if (!st->fullscreen) {
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    int batPct = 100;
    bool chg = false;
    LiliumUI::queryBattery(batPct, chg);
    if (tm.wMinute != st->lastMin || batPct != st->lastBat) {
      st->lastMin = tm.wMinute;
      st->lastBat = batPct;
      LiliumUI::renderSidebarStatus(c, L"一時停止");
    }
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  LifeState * state = new LifeState();
  bool manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  seedPreset(state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.timerIntervalMs = kSpeedMs[state->speedLevel - 1];
  cb.timerAlwaysFire = true;

  int rc = 1;
  if (state->window.create(L"LiliumLifeWinClass",
                           L"Lilium Life (SHARP Brain 800x480)",
                           cb, state, manageTaskBar)) {
    rc = state->window.runLoop();
  }

  delete state;
  return rc;
}
