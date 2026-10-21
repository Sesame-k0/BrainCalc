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

#include <ion/rtc.h>

static Ion::RTC::Mode sMode = Ion::RTC::Mode::HSE;

void Ion::RTC::setMode(Ion::RTC::Mode mode) {
  sMode = mode;
}

Ion::RTC::Mode Ion::RTC::mode() {
  return sMode;
}

void Ion::RTC::setDateTime(Ion::RTC::DateTime /*dateTime*/) {
  // No console output on Windows CE (FOR-AGENTS.md)
}

Ion::RTC::DateTime Ion::RTC::dateTime() {
  SYSTEMTIME st;
  GetLocalTime(&st);

  return DateTime {
    static_cast<int>(st.wSecond),
    static_cast<int>(st.wMinute),
    static_cast<int>(st.wHour),
    static_cast<int>(st.wDay),
    static_cast<int>(st.wMonth),
    static_cast<int>(st.wYear),
    st.wDayOfWeek != 0 ? static_cast<int>(st.wDayOfWeek) - 1 : 6,
  };
}

