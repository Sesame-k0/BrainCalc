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

#include <ion/battery.h>

namespace Ion {
namespace Battery {

namespace {

struct CachedBatteryState {
  DWORD lastQueryTick;
  BYTE lifePercent;
  bool isCharging;
  float voltage;
  bool initialized;
};

static CachedBatteryState sCache = { 0, 100, false, 4.0f, false };

static void updateBatteryCache() {
  DWORD now = GetTickCount();
  if (sCache.initialized && (now - sCache.lastQueryTick) < 2000) {
    return;
  }
  sCache.lastQueryTick = now;
  sCache.initialized = true;

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  SYSTEM_POWER_STATUS_EX2 status = {};
  DWORD copied = GetSystemPowerStatusEx2(&status, sizeof(status), FALSE);
  if (copied == 0) {
    copied = GetSystemPowerStatusEx2(&status, sizeof(status), TRUE);
  }
  if (copied > 0) {
    sCache.lifePercent = status.BatteryLifePercent;
    sCache.isCharging = ((status.BatteryFlag & BATTERY_FLAG_CHARGING) != 0) ||
                        (status.ACLineStatus == AC_LINE_ONLINE);
    if (status.BatteryVoltage > 0 && status.BatteryVoltage < 20000) {
      sCache.voltage = static_cast<float>(status.BatteryVoltage) / 1000.0f;
    } else {
      BYTE pct = (status.BatteryLifePercent <= 100) ? status.BatteryLifePercent : 100;
      sCache.voltage = 3.5f + (static_cast<float>(pct) / 100.0f) * 0.7f;
    }
    return;
  }
#else
  SYSTEM_POWER_STATUS status = {};
  if (GetSystemPowerStatus(&status)) {
    sCache.lifePercent = status.BatteryLifePercent;
    sCache.isCharging = ((status.BatteryFlag & 8) != 0);
    BYTE pct = (status.BatteryLifePercent <= 100) ? status.BatteryLifePercent : 100;
    sCache.voltage = 3.5f + (static_cast<float>(pct) / 100.0f) * 0.7f;
    return;
  }
#endif

  sCache.lifePercent = 100;
  sCache.isCharging = false;
  sCache.voltage = 4.0f;
}

} // namespace

bool isCharging() {
  updateBatteryCache();
  return sCache.isCharging;
}

Charge level() {
  updateBatteryCache();
  BYTE pct = sCache.lifePercent;
  if (pct > 100) {
    // 255 (BATTERY_PERCENTAGE_UNKNOWN) 等は FULL 扱い
    return Charge::FULL;
  }
  if (pct >= 65) {
    return Charge::FULL;
  }
  if (pct >= 25) {
    return Charge::SOMEWHERE_INBETWEEN;
  }
  // Charge::EMPTY を返すと自動シャットダウン処理が走るため LOW を返す
  return Charge::LOW;
}

float voltage() {
  updateBatteryCache();
  return sCache.voltage;
}

}
}

