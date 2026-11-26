#ifndef LILIUM_SUITE_COMMON_LILIUM_SETTINGS_H
#define LILIUM_SUITE_COMMON_LILIUM_SETTINGS_H

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
#include <stddef.h>
#include <stdint.h>

namespace LiliumSettings {

static constexpr const wchar_t * kLiliumVersion = L"v2.7.0";
static constexpr int kMaxApps = 64;

struct Config {
  bool logEnabled = false;       // [general] log_enabled (0/1, default 0)
  bool restoreHwOnExit = true;   // [general] restore_hw_on_exit (0/1, default 1)
  bool directCalcBoot = false;   // [general] direct_calc_boot (0/1, default 0)
  int brightnessLevel = 0;       // [hardware] brightness_level (0=Brain標準, 1..5)
  bool backlightDimOff = false;  // [hardware] backlight_dim_off (0/1, default 0)
  int powerOffSec = -1;          // [hardware] power_off_sec (-1=Brain標準, 0=無効, 60,180,300,600,1800)
  int lidMode = 0;               // [hardware] lid_mode (0=通常:スリープ, 1=維持, 2=画面消灯のみ)
  bool imeResident = true;       // [apps] ime_resident (0/1, default 1)
  int calcDefaultMode = 0;       // [apps] calc_default_mode (0=12x, 1=abc, 2=ABC)
  bool clock24h = true;          // [apps] clock_24h (0/1, default 1)
  int lifeDefaultRule = 0;       // [apps] life_default_rule (0..5)
  int lifeDefaultTheme = 0;      // [apps] life_default_theme (0..2)
  int editorTabSpaces = 2;       // [apps] editor_tab_spaces (2 or 4)
  bool doomLowDetail = false;    // [apps] doom_low_detail (0/1, default 0)
  wchar_t hiddenApps[kMaxApps][96] = {};
  int hiddenCount = 0;
};

struct DiscoveredApp {
  wchar_t name[48];
  wchar_t exePath[MAX_PATH];
  wchar_t workDir[MAX_PATH];
  wchar_t relDesc[96];
  wchar_t orderKey[96];
  wchar_t iconFallbackExe[MAX_PATH];
  int builtInKind; // 0=Calc, 1=TaskMgr, 2=Clock, 3=Filer, 4=DOOM, 5=Life, 6=TimeTable, 7=Editor, 8=Setting, 9=Recorder, 10=Chat, -1=External
  bool isLiliumSuite;
  bool isFolderApp;
};

struct HwStatus {
  bool isWinCE;
  bool sharpLibLoaded;
  int sysBoxBrightness; // 1..5, or 0 if unavailable
  int battPowerOffSec;  // -1 if unreadable
  int disablePowerOff;  // -1 if unavailable, 0 or 1
  int mrSensorState;    // -1 if unavailable, 0=open, 1=closed
  int bklPowerState;    // -1 if unavailable, 0..4 = D0..D4
  bool backlightBlankedByLid;
};

// Path helpers (always rooted at <LiliumRoot>)
void getSettingsPath(wchar_t * outPath, size_t maxLen);
void getOrderFilePath(wchar_t * outPath, size_t maxLen);
void getSuiteLogPath(wchar_t * outPath, size_t maxLen);

// Settings lifecycle & hidden-app protection
void setDefaults(Config & cfg);
bool load(Config & cfg);
bool save(const Config & cfg);

bool isProtectedOrderKey(const wchar_t * orderKey);
bool isAppHidden(const Config & cfg, const wchar_t * orderKey);
bool setAppHidden(Config & cfg, const wchar_t * orderKey, bool hidden);

// Global suite logger (<LiliumRoot>\lilium_suite.log)
void syncSuiteLogFlag(bool enabled);
bool isSuiteLogEnabled();
void suiteLog(const wchar_t * appTag, const wchar_t * fmt, ...);

// Shared app discovery & launcher_order.cfg management
int scanAllLauncherApps(DiscoveredApp * outArr, int maxCount,
                        bool includeHidden, const Config * filterCfg);
bool saveOrderKeys(const DiscoveredApp * apps, int count);
bool saveVisibleOrderPreservingHidden(const DiscoveredApp * visibleApps, int visibleCount);
void resetOrderFile();

// Clean-room SHARP Brain hardware control (PW-SB4 / WinCE APIs)
void captureInitialHardwareBaseline();
void queryHwStatus(HwStatus & out);
void applyHardwareSettings(const Config & cfg);
void tickHardwareKeepAlive(const Config & cfg);
void restoreHardwareDefaultsOnExit(const Config & cfg);

} // namespace LiliumSettings

#endif // LILIUM_SUITE_COMMON_LILIUM_SETTINGS_H
