#include "cashcalc_engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace CashCalc {

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static double clampDouble(double v, double lo, double hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static double pow10i(int n) {
  double r = 1.0;
  for (int i = 0; i < n; ++i) r *= 10.0;
  return r;
}

static void pushTapeLine(EngineState & st, const char * line) {
  if (!line || !line[0]) return;
  strlcpy(st.tapeLines[0], st.tapeLines[1], sizeof(st.tapeLines[0]));
  strlcpy(st.tapeLines[1], line, sizeof(st.tapeLines[1]));
}

static const char * opSym(uint8_t op) {
  switch (op) {
    case OpAdd: return "+";
    case OpSub: return "-";
    case OpMul: return "*";
    case OpDiv: return "/";
    default: return "";
  }
}

void initDefaults(EngineState & st) {
  memset(&st, 0, sizeof(st));
  st.mode = ModeCalc;
  st.taxRateStdTenths = 100; // 10.0%
  st.taxRateRedTenths = 80;  // 8.0%
  st.roundMode = RoundHalf;
  st.decMode = Dec0;
  st.gtAutoAccum = true;
  strlcpy(st.inputBuf, "0", sizeof(st.inputBuf));
  strlcpy(st.subDisplay, "Ready ([F1] Settings / [K/F9] GT)", sizeof(st.subDisplay));

  st.csCost = 800.0;
  st.csSell = 1000.0;
  st.csMargin = 20.0;
  strlcpy(st.csBuf, "0", sizeof(st.csBuf));

  strlcpy(st.invInputBuf, "0", sizeof(st.invInputBuf));

  st.startDate = { 2026, 4, 1 };
  st.endDate = { 2026, 11, 21 };
  st.finPrincipal = 1000000.0;
  st.finRateTenths = 25; // 2.5%
  st.finMonths = 24;
}

int decPlacesForMode(uint8_t decMode) {
  switch (decMode) {
    case Dec0: return 0;
    case Dec2: return 2;
    case Dec3: return 3;
    case Dec4: return 4;
    case DecAdd2: return 2;
    default: return 0;
  }
}

double applyRounding(double val, uint8_t roundMode, uint8_t decMode) {
  if (!isfinite(val)) return 0.0;
  if (roundMode == RoundFloat) return val;
  int dp = decPlacesForMode(decMode);
  double scale = pow10i(dp);
  double s = (val < 0.0) ? -1.0 : 1.0;
  double mag = fabs(val) * scale;
  double outMag = mag;
  if (roundMode == RoundCut) {
    outMag = floor(mag + 1e-9);
  } else if (roundMode == RoundUp) {
    outMag = ceil(mag - 1e-9);
  } else {
    outMag = floor(mag + 0.5 + 1e-9);
  }
  double res = (s * outMag) / scale;
  if (fabs(res) < 1e-12) res = 0.0;
  return res;
}

double parseAsciiDouble(const char * s) {
  if (!s) return 0.0;
  while (*s == ' ') ++s;
  bool neg = false;
  if (*s == '-') { neg = true; ++s; }
  else if (*s == '+') { ++s; }
  double intPart = 0.0;
  while (*s >= '0' && *s <= '9') {
    intPart = intPart * 10.0 + static_cast<double>(*s - '0');
    ++s;
  }
  double fracPart = 0.0;
  double div = 1.0;
  if (*s == '.') {
    ++s;
    while (*s >= '0' && *s <= '9') {
      div *= 10.0;
      fracPart += static_cast<double>(*s - '0') / div;
      ++s;
    }
  }
  double v = intPart + fracPart;
  return neg ? -v : v;
}

void formatCommasRaw(const char * rawNum, char * out, size_t maxLen) {
  if (!out || maxLen == 0) return;
  if (!rawNum || !rawNum[0]) {
    strlcpy(out, "0", maxLen);
    return;
  }
  bool neg = (rawNum[0] == '-');
  const char * p = neg ? (rawNum + 1) : rawNum;
  const char * dot = strchr(p, '.');
  size_t intLen = dot ? static_cast<size_t>(dot - p) : strlen(p);

  char buf[48] = "";
  size_t pos = 0;
  if (neg && pos + 1 < sizeof(buf)) buf[pos++] = '-';
  for (size_t i = 0; i < intLen && pos + 2 < sizeof(buf); ++i) {
    if (i > 0 && ((intLen - i) % 3) == 0) {
      buf[pos++] = ',';
    }
    buf[pos++] = p[i];
  }
  if (dot && dot[1] != '\0') {
    for (size_t k = 0; dot[k] != '\0' && pos + 1 < sizeof(buf); ++k) {
      buf[pos++] = dot[k];
    }
  }
  buf[pos] = '\0';
  strlcpy(out, buf, maxLen);
}

void formatFixedDec(double val, int decimals, char * out, size_t maxLen) {
  if (!out || maxLen == 0) return;
  if (!isfinite(val) || fabs(val) > kMaxDisplayAbs) {
    strlcpy(out, "ERR (OVERFLOW)", maxLen);
    return;
  }
  decimals = clampInt(decimals, 0, 4);
  double scale = pow10i(decimals);
  bool neg = (val < -1e-12);
  double magScaled = floor(fabs(val) * scale + 0.5 + 1e-9);
  if (magScaled < 1.0) neg = false;

  uint64_t totalInt = static_cast<uint64_t>(magScaled);
  uint64_t scaleInt = static_cast<uint64_t>(scale + 0.5);
  uint64_t intVal = totalInt / scaleInt;
  uint64_t fracVal = totalInt % scaleInt;

  char raw[48] = "";
  char intStr[28] = "";
  int ip = 0;
  if (intVal == 0) {
    intStr[ip++] = '0';
  } else {
    char rev[28];
    int rp = 0;
    uint64_t tmp = intVal;
    while (tmp > 0 && rp < 24) {
      rev[rp++] = static_cast<char>('0' + (tmp % 10));
      tmp /= 10;
    }
    while (rp > 0) intStr[ip++] = rev[--rp];
  }
  intStr[ip] = '\0';

  size_t pos = 0;
  if (neg) raw[pos++] = '-';
  for (int i = 0; intStr[i] != '\0' && pos + 1 < sizeof(raw); ++i) {
    raw[pos++] = intStr[i];
  }
  if (decimals > 0 && pos + static_cast<size_t>(decimals) + 2 < sizeof(raw)) {
    raw[pos++] = '.';
    uint64_t div = scaleInt / 10;
    for (int d = 0; d < decimals; ++d) {
      raw[pos++] = static_cast<char>('0' + ((fracVal / div) % 10));
      div /= 10;
    }
  }
  raw[pos] = '\0';
  formatCommasRaw(raw, out, maxLen);
}

void formatAccounting(double val, uint8_t roundMode, uint8_t decMode, bool forceDecimal,
                      char * out, size_t maxLen) {
  if (!out || maxLen == 0) return;
  if (!isfinite(val) || fabs(val) > kMaxDisplayAbs) {
    strlcpy(out, "ERR (OVERFLOW)", maxLen);
    return;
  }
  double v = applyRounding(val, roundMode, decMode);
  if (roundMode == RoundFloat && !forceDecimal) {
    char tmp[48];
    formatFixedDec(v, 4, tmp, sizeof(tmp));
    // Strip trailing decimal zeros in Float mode
    char * dot = strchr(tmp, '.');
    if (dot) {
      size_t n = strlen(tmp);
      while (n > 0 && tmp[n - 1] == '0') tmp[--n] = '\0';
      if (n > 0 && tmp[n - 1] == '.') tmp[--n] = '\0';
    }
    strlcpy(out, tmp, maxLen);
  } else {
    int dp = (roundMode == RoundFloat) ? 2 : decPlacesForMode(decMode);
    formatFixedDec(v, dp, out, maxLen);
  }
}

void formatTenthsPct(int tenths, char * out, size_t maxLen) {
  int whole = tenths / 10;
  int frac = tenths % 10;
  if (frac < 0) frac = -frac;
  snprintf(out, maxLen, "%d.%d", whole, frac);
  out[maxLen - 1] = '\0';
}

void formatYMD(const DateYMD & dt, char * out, size_t maxLen) {
  snprintf(out, maxLen, "%04d/%02d/%02d",
           static_cast<int>(dt.y), static_cast<int>(dt.m), static_cast<int>(dt.d));
  out[maxLen - 1] = '\0';
}

double taxRateStd(const EngineState & st) {
  return static_cast<double>(st.taxRateStdTenths) * 0.1;
}

double taxRateRed(const EngineState & st) {
  return static_cast<double>(st.taxRateRedTenths) * 0.1;
}

double activeTaxRate(const EngineState & st) {
  return st.useReducedTax ? taxRateRed(st) : taxRateStd(st);
}

const char * roundModeLabel(uint8_t r) {
  static constexpr const char * kLabels[4] = { "F", "CUT", "UP", "5/4" };
  return kLabels[r % 4];
}

const char * decModeLabel(uint8_t d) {
  static constexpr const char * kLabels[5] = { "0", "2", "3", "4", "ADD2" };
  return kLabels[d % 5];
}

static void syncInputBufFromValue(EngineState & st, double v) {
  char formatted[48];
  formatAccounting(v, RoundFloat, Dec0, false, formatted, sizeof(formatted));
  size_t pos = 0;
  for (size_t i = 0; formatted[i] != '\0' && pos + 1 < sizeof(st.inputBuf); ++i) {
    if (formatted[i] != ',') {
      st.inputBuf[pos++] = formatted[i];
    }
  }
  st.inputBuf[pos] = '\0';
  if (!st.inputBuf[0]) strlcpy(st.inputBuf, "0", sizeof(st.inputBuf));
}

static double commitStdInputValue(EngineState & st) {
  if (!st.entering) return st.curVal;
  double v = parseAsciiDouble(st.inputBuf);
  if (st.decMode == DecAdd2 && strchr(st.inputBuf, '.') == nullptr && fabs(v) > 0.5) {
    v /= 100.0;
  }
  st.curVal = v;
  return v;
}

static double evalBinaryOp(double a, uint8_t op, double b, bool & outErr) {
  outErr = false;
  switch (op) {
    case OpAdd: return a + b;
    case OpSub: return a - b;
    case OpMul: return a * b;
    case OpDiv:
      if (fabs(b) < 1e-12) { outErr = true; return 0.0; }
      return a / b;
    default:
      return b;
  }
}

void stdClearEntry(EngineState & st) {
  st.errorState = false;
  st.curVal = 0.0;
  strlcpy(st.inputBuf, "0", sizeof(st.inputBuf));
  st.entering = true;
  st.gtRecalledOnce = false;
  strlcpy(st.subDisplay, "C (Cleared Entry)", sizeof(st.subDisplay));
}

void stdAllClear(EngineState & st) {
  st.errorState = false;
  st.acc = 0.0;
  st.curVal = 0.0;
  st.pendingOp = OpNone;
  st.kLocked = false;
  st.kOp = OpNone;
  st.kVal = 0.0;
  st.entering = false;
  st.afterEquals = false;
  st.gtRecalledOnce = false;
  strlcpy(st.inputBuf, "0", sizeof(st.inputBuf));
  strlcpy(st.subDisplay, "AC (All Cleared)", sizeof(st.subDisplay));
}

static void appendDigitStr(char * buf, size_t bufSize, const char * digits) {
  size_t len = strlen(buf);
  const char * p = digits;
  while (*p) {
    if (*p == '.') {
      if (strchr(buf, '.') == nullptr && len + 1 < bufSize) {
        buf[len++] = '.';
        buf[len] = '\0';
      }
      ++p;
      continue;
    }
    int digCount = 0;
    for (size_t i = 0; i < len; ++i) {
      if (buf[i] >= '0' && buf[i] <= '9') ++digCount;
    }
    if (digCount >= 12) break;
    if (strcmp(buf, "0") == 0) {
      if (*p == '0') { ++p; continue; }
      buf[0] = *p;
      buf[1] = '\0';
      len = 1;
    } else if (strcmp(buf, "-0") == 0) {
      if (*p == '0') { ++p; continue; }
      buf[1] = *p;
      buf[2] = '\0';
      len = 2;
    } else if (len + 1 < bufSize) {
      buf[len++] = *p;
      buf[len] = '\0';
    }
    ++p;
  }
}

void stdPressDigit(EngineState & st, const char * digits) {
  if (st.errorState) stdAllClear(st);
  if (!st.entering) {
    strlcpy(st.inputBuf, "0", sizeof(st.inputBuf));
    st.entering = true;
    st.afterEquals = false;
  }
  st.gtRecalledOnce = false;
  appendDigitStr(st.inputBuf, sizeof(st.inputBuf), digits);
  st.curVal = parseAsciiDouble(st.inputBuf);
}

void stdBackspace(EngineState & st) {
  if (st.errorState) return;
  if (!st.entering) {
    syncInputBufFromValue(st, st.curVal);
    st.entering = true;
  }
  size_t len = strlen(st.inputBuf);
  if (len <= 1 || (len == 2 && st.inputBuf[0] == '-')) {
    strlcpy(st.inputBuf, "0", sizeof(st.inputBuf));
  } else {
    st.inputBuf[len - 1] = '\0';
  }
  st.curVal = parseAsciiDouble(st.inputBuf);
}

void stdToggleSign(EngineState & st) {
  if (st.errorState) return;
  if (st.entering) {
    if (st.inputBuf[0] == '-') {
      memmove(st.inputBuf, st.inputBuf + 1, strlen(st.inputBuf));
    } else if (strcmp(st.inputBuf, "0") != 0) {
      size_t len = strlen(st.inputBuf);
      if (len + 1 < sizeof(st.inputBuf)) {
        memmove(st.inputBuf + 1, st.inputBuf, len + 1);
        st.inputBuf[0] = '-';
      }
    }
    st.curVal = parseAsciiDouble(st.inputBuf);
  } else {
    st.curVal = -st.curVal;
    if (fabs(st.curVal) < 1e-12) st.curVal = 0.0;
  }
}

void stdPressOp(EngineState & st, uint8_t newOp) {
  if (st.errorState) return;
  st.gtRecalledOnce = false;

  if (st.pendingOp != OpNone && !st.entering && !st.afterEquals) {
    // Double operator press locks constant K in Casio/SHARP accounting style
    if (st.pendingOp == newOp) {
      st.kLocked = !st.kLocked;
      st.kOp = newOp;
      st.kVal = st.acc;
      snprintf(st.subDisplay, sizeof(st.subDisplay),
               st.kLocked ? "K Constant Locked (%s)" : "K Unlocked", opSym(newOp));
    } else {
      st.pendingOp = newOp;
      st.kLocked = false;
      char aStr[24];
      formatAccounting(st.acc, st.roundMode, st.decMode, false, aStr, sizeof(aStr));
      snprintf(st.subDisplay, sizeof(st.subDisplay), "%s %s ...", aStr, opSym(newOp));
    }
    return;
  }

  double rhs = commitStdInputValue(st);
  if (st.pendingOp != OpNone && !st.afterEquals) {
    bool err = false;
    double res = evalBinaryOp(st.acc, st.pendingOp, rhs, err);
    if (err || !isfinite(res) || fabs(res) > kMaxDisplayAbs) {
      st.errorState = true;
      strlcpy(st.subDisplay, "ERR (DIV/0 or OVERFLOW)", sizeof(st.subDisplay));
      return;
    }
    st.acc = res;
    st.curVal = applyRounding(res, st.roundMode, st.decMode);
  } else {
    st.acc = rhs;
  }

  st.pendingOp = newOp;
  st.entering = false;
  st.afterEquals = false;
  st.kLocked = false;

  char aStr[24];
  formatAccounting(st.acc, st.roundMode, st.decMode, false, aStr, sizeof(aStr));
  snprintf(st.subDisplay, sizeof(st.subDisplay), "%s %s", aStr, opSym(newOp));
}

void stdPressEquals(EngineState & st) {
  if (st.errorState) return;
  st.gtRecalledOnce = false;
  double rhs = commitStdInputValue(st);

  if (st.kLocked && st.kOp != OpNone) {
    double cur = st.entering ? rhs : st.curVal;
    bool err = false;
    double res = (st.kOp == OpDiv || st.kOp == OpSub)
        ? evalBinaryOp(cur, st.kOp, st.kVal, err)
        : evalBinaryOp(st.kVal, st.kOp, cur, err);
    if (err || !isfinite(res) || fabs(res) > kMaxDisplayAbs) {
      st.errorState = true;
      return;
    }
    double rounded = applyRounding(res, st.roundMode, st.decMode);
    char aStr[24], bStr[24], rStr[24], line[44];
    formatAccounting(cur, st.roundMode, st.decMode, false, aStr, sizeof(aStr));
    formatAccounting(st.kVal, st.roundMode, st.decMode, false, bStr, sizeof(bStr));
    formatAccounting(rounded, st.roundMode, st.decMode, false, rStr, sizeof(rStr));
    snprintf(line, sizeof(line), "[K] %s %s %s = %s", aStr, opSym(st.kOp), bStr, rStr);
    pushTapeLine(st, line);
    strlcpy(st.subDisplay, line, sizeof(st.subDisplay));
    st.curVal = rounded;
    st.acc = res;
    if (st.gtAutoAccum) st.gtVal += rounded;
    st.entering = false;
    st.afterEquals = true;
    return;
  }

  if (st.pendingOp == OpNone) {
    if (st.entering) {
      st.curVal = applyRounding(rhs, st.roundMode, st.decMode);
      st.acc = st.curVal;
      if (st.gtAutoAccum) st.gtVal += st.curVal;
      st.entering = false;
      st.afterEquals = true;
    }
    return;
  }

  double lhs = st.acc;
  bool err = false;
  double res = evalBinaryOp(lhs, st.pendingOp, rhs, err);
  if (err || !isfinite(res) || fabs(res) > kMaxDisplayAbs) {
    st.errorState = true;
    strlcpy(st.subDisplay, "ERR (DIV/0 or OVERFLOW)", sizeof(st.subDisplay));
    return;
  }
  double rounded = applyRounding(res, st.roundMode, st.decMode);

  char lStr[24], rStr[24], oStr[24], line[44];
  formatAccounting(lhs, st.roundMode, st.decMode, false, lStr, sizeof(lStr));
  formatAccounting(rhs, st.roundMode, st.decMode, false, rStr, sizeof(rStr));
  formatAccounting(rounded, st.roundMode, st.decMode, false, oStr, sizeof(oStr));
  snprintf(line, sizeof(line), "%s %s %s = %s", lStr, opSym(st.pendingOp), rStr, oStr);
  pushTapeLine(st, line);
  strlcpy(st.subDisplay, line, sizeof(st.subDisplay));

  st.acc = rounded;
  st.curVal = rounded;
  if (st.gtAutoAccum) st.gtVal += rounded;
  st.pendingOp = OpNone;
  st.entering = false;
  st.afterEquals = true;
}

void stdPressPercent(EngineState & st) {
  if (st.errorState) return;
  double rhs = commitStdInputValue(st);
  double res = 0.0;
  char line[44], aStr[24], bStr[24], rStr[24];
  if (st.pendingOp == OpAdd || st.pendingOp == OpSub) {
    double pctVal = st.acc * (rhs / 100.0);
    res = (st.pendingOp == OpAdd) ? (st.acc + pctVal) : (st.acc - pctVal);
    formatAccounting(st.acc, st.roundMode, st.decMode, false, aStr, sizeof(aStr));
    formatAccounting(rhs, RoundFloat, Dec0, false, bStr, sizeof(bStr));
    formatAccounting(res, st.roundMode, st.decMode, false, rStr, sizeof(rStr));
    snprintf(line, sizeof(line), "%s %s %s%% = %s", aStr, opSym(st.pendingOp), bStr, rStr);
  } else if (st.pendingOp == OpMul) {
    res = st.acc * (rhs / 100.0);
    formatAccounting(st.acc, st.roundMode, st.decMode, false, aStr, sizeof(aStr));
    formatAccounting(rhs, RoundFloat, Dec0, false, bStr, sizeof(bStr));
    formatAccounting(res, st.roundMode, st.decMode, false, rStr, sizeof(rStr));
    snprintf(line, sizeof(line), "%s * %s%% = %s", aStr, bStr, rStr);
  } else if (st.pendingOp == OpDiv) {
    if (fabs(rhs) < 1e-12) { st.errorState = true; return; }
    res = (st.acc / rhs) * 100.0;
    formatAccounting(res, st.roundMode, st.decMode, false, rStr, sizeof(rStr));
    snprintf(line, sizeof(line), "Ratio = %s%%", rStr);
  } else {
    res = rhs / 100.0;
    formatAccounting(res, st.roundMode, st.decMode, false, rStr, sizeof(rStr));
    snprintf(line, sizeof(line), "%s%%", rStr);
  }
  double rounded = applyRounding(res, st.roundMode, st.decMode);
  pushTapeLine(st, line);
  strlcpy(st.subDisplay, line, sizeof(st.subDisplay));
  st.curVal = rounded;
  st.acc = rounded;
  st.pendingOp = OpNone;
  st.entering = false;
  st.afterEquals = true;
}

void stdApplyTax(EngineState & st, bool includeTax) {
  if (st.errorState) return;
  double v = commitStdInputValue(st);
  double rate = activeTaxRate(st) / 100.0;
  char rateStr[16];
  formatTenthsPct(st.useReducedTax ? st.taxRateRedTenths : st.taxRateStdTenths, rateStr, sizeof(rateStr));

  double outVal = 0.0;
  double taxAmt = 0.0;
  char vStr[24], tStr[24], oStr[24], line[44];
  if (includeTax) {
    outVal = applyRounding(v * (1.0 + rate), st.roundMode, st.decMode);
    taxAmt = applyRounding(outVal - v, st.roundMode, st.decMode);
    formatAccounting(v, st.roundMode, st.decMode, false, vStr, sizeof(vStr));
    formatAccounting(taxAmt, st.roundMode, st.decMode, false, tStr, sizeof(tStr));
    formatAccounting(outVal, st.roundMode, st.decMode, false, oStr, sizeof(oStr));
    snprintf(st.subDisplay, sizeof(st.subDisplay), "Ex %s + Tax(%s%%) %s", vStr, rateStr, tStr);
    snprintf(line, sizeof(line), "Tax+(%s%%) %s -> %s", rateStr, vStr, oStr);
  } else {
    outVal = applyRounding(v / (1.0 + rate), st.roundMode, st.decMode);
    taxAmt = applyRounding(v - outVal, st.roundMode, st.decMode);
    formatAccounting(v, st.roundMode, st.decMode, false, vStr, sizeof(vStr));
    formatAccounting(taxAmt, st.roundMode, st.decMode, false, tStr, sizeof(tStr));
    formatAccounting(outVal, st.roundMode, st.decMode, false, oStr, sizeof(oStr));
    snprintf(st.subDisplay, sizeof(st.subDisplay), "Inc %s (Tax %s -> Ex)", vStr, tStr);
    snprintf(line, sizeof(line), "Tax-(%s%%) %s -> %s", rateStr, vStr, oStr);
  }
  pushTapeLine(st, line);
  st.curVal = outVal;
  st.acc = outVal;
  st.entering = false;
  st.afterEquals = true;
}

void stdToggleTaxRate(EngineState & st) {
  st.useReducedTax = !st.useReducedTax;
  char rateStr[16];
  formatTenthsPct(st.useReducedTax ? st.taxRateRedTenths : st.taxRateStdTenths, rateStr, sizeof(rateStr));
  snprintf(st.subDisplay, sizeof(st.subDisplay), "Active Tax: %s (%s%%)",
           st.useReducedTax ? "RED" : "STD", rateStr);
}

void stdGT(EngineState & st) {
  if (st.errorState) return;
  if (st.gtRecalledOnce) {
    st.gtVal = 0.0;
    st.gtRecalledOnce = false;
    strlcpy(st.subDisplay, "GT Cleared", sizeof(st.subDisplay));
  } else {
    st.curVal = applyRounding(st.gtVal, st.roundMode, st.decMode);
    st.acc = st.curVal;
    st.entering = false;
    st.afterEquals = true;
    st.gtRecalledOnce = true;
    strlcpy(st.subDisplay, "GT Recalled (Press again to Clear)", sizeof(st.subDisplay));
  }
}

void stdMemoryOp(EngineState & st, int which) {
  if (st.errorState) return;
  st.gtRecalledOnce = false;
  if (which == 0) { // M+
    double v = commitStdInputValue(st);
    st.memVal += v;
    st.entering = false;
    st.afterEquals = true;
    char mStr[24];
    formatAccounting(st.memVal, st.roundMode, st.decMode, false, mStr, sizeof(mStr));
    snprintf(st.subDisplay, sizeof(st.subDisplay), "M+ -> M = %s", mStr);
  } else if (which == 1) { // M-
    double v = commitStdInputValue(st);
    st.memVal -= v;
    st.entering = false;
    st.afterEquals = true;
    char mStr[24];
    formatAccounting(st.memVal, st.roundMode, st.decMode, false, mStr, sizeof(mStr));
    snprintf(st.subDisplay, sizeof(st.subDisplay), "M- -> M = %s", mStr);
  } else if (which == 2) { // MR
    st.curVal = applyRounding(st.memVal, st.roundMode, st.decMode);
    st.entering = false;
    strlcpy(st.subDisplay, "MR (Memory Recalled)", sizeof(st.subDisplay));
  } else { // MC
    st.memVal = 0.0;
    strlcpy(st.subDisplay, "MC (Memory Cleared)", sizeof(st.subDisplay));
  }
}

// --- Mode 1: Cost / Sell / Margin ---
void csCommitEdit(EngineState & st) {
  if (!st.csTyping) return;
  double v = parseAsciiDouble(st.csBuf);
  if (st.csField == 0) st.csCost = clampDouble(v, 0.0, kMaxDisplayAbs);
  else if (st.csField == 1) st.csSell = clampDouble(v, 0.0, kMaxDisplayAbs);
  else st.csMargin = clampDouble(v, -999.0, 99.9);
  st.csTyping = false;
}

void csPressDigit(EngineState & st, const char * d) {
  if (!st.csTyping) {
    strlcpy(st.csBuf, "0", sizeof(st.csBuf));
    st.csTyping = true;
  }
  appendDigitStr(st.csBuf, sizeof(st.csBuf), d);
}

void csBackspace(EngineState & st) {
  if (!st.csTyping) return;
  size_t n = strlen(st.csBuf);
  if (n <= 1) strlcpy(st.csBuf, "0", sizeof(st.csBuf));
  else st.csBuf[n - 1] = '\0';
}

void csZeroField(EngineState & st) {
  strlcpy(st.csBuf, "0", sizeof(st.csBuf));
  st.csTyping = false;
  if (st.csField == 0) st.csCost = 0.0;
  else if (st.csField == 1) st.csSell = 0.0;
  else st.csMargin = 0.0;
}

void csSolveField(EngineState & st) {
  csCommitEdit(st);
  if (st.csField == 2) {
    st.csMargin = (fabs(st.csSell) > 1e-9) ? ((st.csSell - st.csCost) / st.csSell * 100.0) : 0.0;
  } else if (st.csField == 1) {
    double denom = 1.0 - st.csMargin / 100.0;
    st.csSell = (fabs(denom) > 1e-6) ? applyRounding(st.csCost / denom, st.roundMode, st.decMode) : 0.0;
  } else {
    st.csCost = applyRounding(st.csSell * (1.0 - st.csMargin / 100.0), st.roundMode, st.decMode);
  }
}

double csProfit(const EngineState & st) { return st.csSell - st.csCost; }
double csMarkupPct(const EngineState & st) {
  return (fabs(st.csCost) > 1e-9) ? (csProfit(st) / st.csCost * 100.0) : 0.0;
}
double csRealMarginPct(const EngineState & st) {
  return (fabs(st.csSell) > 1e-9) ? (csProfit(st) / st.csSell * 100.0) : 0.0;
}
double csSellWithTax(const EngineState & st) {
  return applyRounding(st.csSell * (1.0 + activeTaxRate(st) / 100.0), st.roundMode, st.decMode);
}

// --- Mode 2: Invoice ---
void invPressDigit(EngineState & st, const char * d) {
  if (!st.invTyping) {
    strlcpy(st.invInputBuf, "0", sizeof(st.invInputBuf));
    st.invTyping = true;
  }
  appendDigitStr(st.invInputBuf, sizeof(st.invInputBuf), d);
}

void invBackspace(EngineState & st) {
  if (!st.invTyping) return;
  size_t n = strlen(st.invInputBuf);
  if (n <= 1) strlcpy(st.invInputBuf, "0", sizeof(st.invInputBuf));
  else st.invInputBuf[n - 1] = '\0';
}

void invCycleTaxCat(EngineState & st) {
  st.invTaxCat = (st.invTaxCat + 1) % 3;
}

void invAddItem(EngineState & st) {
  double amt = parseAsciiDouble(st.invInputBuf);
  if (amt > 0.0 && st.invCount < kMaxInvoiceItems) {
    st.invItems[st.invCount].amount = applyRounding(amt, st.roundMode, st.decMode);
    st.invItems[st.invCount].taxCat = st.invTaxCat;
    st.invSel = st.invCount;
    ++st.invCount;
    if (st.invSel >= st.invScroll + 4) st.invScroll = st.invSel - 3;
    strlcpy(st.invInputBuf, "0", sizeof(st.invInputBuf));
    st.invTyping = false;
  }
}

void invDeleteSelectedOrClear(EngineState & st) {
  if (st.invCount > 0 && st.invSel < st.invCount) {
    for (int i = st.invSel + 1; i < st.invCount; ++i) {
      st.invItems[i - 1] = st.invItems[i];
    }
    --st.invCount;
    if (st.invCount == 0) {
      st.invSel = 0;
      st.invScroll = 0;
    } else if (st.invSel >= st.invCount) {
      st.invSel = st.invCount - 1;
    }
    if (st.invScroll > st.invSel) st.invScroll = st.invSel;
  }
}

void invMoveSelection(EngineState & st, int delta) {
  if (st.invCount == 0) return;
  int next = (static_cast<int>(st.invSel) + st.invCount + delta) % st.invCount;
  st.invSel = static_cast<uint8_t>(next);
  if (st.invSel < st.invScroll) st.invScroll = st.invSel;
  if (st.invSel >= st.invScroll + 4) st.invScroll = st.invSel - 3;
}

void invComputeTotals(const EngineState & st,
                      double & sub0, double & tax0,
                      double & sub1, double & tax1,
                      double & sub2, double & grand) {
  sub0 = sub1 = sub2 = 0.0;
  for (int i = 0; i < st.invCount; ++i) {
    if (st.invItems[i].taxCat == 0) sub0 += st.invItems[i].amount;
    else if (st.invItems[i].taxCat == 1) sub1 += st.invItems[i].amount;
    else sub2 += st.invItems[i].amount;
  }
  tax0 = applyRounding(sub0 * (taxRateStd(st) / 100.0), st.roundMode, st.decMode);
  tax1 = applyRounding(sub1 * (taxRateRed(st) / 100.0), st.roundMode, st.decMode);
  grand = sub0 + tax0 + sub1 + tax1 + sub2;
}

// --- Mode 3: Finance Days & Loan ---
static long daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

static int daysInMonth(int y, int m) {
  static constexpr int kDim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int d = kDim[(m - 1) % 12];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) d = 29;
  return d;
}

void shiftDateDays(DateYMD & dt, int deltaDays) {
  int d = dt.d + deltaDays;
  while (d > daysInMonth(dt.y, dt.m)) {
    d -= daysInMonth(dt.y, dt.m);
    if (++dt.m > 12) { dt.m = 1; ++dt.y; }
  }
  while (d < 1) {
    if (--dt.m < 1) { dt.m = 12; --dt.y; }
    d += daysInMonth(dt.y, dt.m);
  }
  dt.d = static_cast<int8_t>(d);
}

void shiftDateMonths(DateYMD & dt, int deltaMonths) {
  int totalM = (dt.y * 12 + (dt.m - 1)) + deltaMonths;
  if (totalM < 1900 * 12) totalM = 1900 * 12;
  if (totalM > 2100 * 12 + 11) totalM = 2100 * 12 + 11;
  dt.y = static_cast<int16_t>(totalM / 12);
  dt.m = static_cast<int8_t>((totalM % 12) + 1);
  int mx = daysInMonth(dt.y, dt.m);
  if (dt.d > mx) dt.d = static_cast<int8_t>(mx);
}

long finElapsedDays(const EngineState & st) {
  long d0 = daysFromCivil(st.startDate.y, st.startDate.m, st.startDate.d);
  long d1 = daysFromCivil(st.endDate.y, st.endDate.m, st.endDate.d);
  long diff = d1 - d0;
  if (diff < 0) diff = -diff;
  return diff + (st.bothEndsInclusive ? 1 : 0);
}

double finSimpleInterest(const EngineState & st) {
  long days = finElapsedDays(st);
  double rate = static_cast<double>(st.finRateTenths) * 0.001;
  return applyRounding(st.finPrincipal * rate * (static_cast<double>(days) / 365.0),
                       st.roundMode, st.decMode);
}

double finSimpleTotal(const EngineState & st) {
  return st.finPrincipal + finSimpleInterest(st);
}

double finAnnuityMonthly(const EngineState & st) {
  int n = clampInt(st.finMonths, 1, 480);
  double mr = (static_cast<double>(st.finRateTenths) * 0.001) / 12.0;
  double pmt = 0.0;
  if (mr < 1e-12) {
    pmt = st.finPrincipal / static_cast<double>(n);
  } else {
    double f = pow(1.0 + mr, static_cast<double>(n));
    pmt = st.finPrincipal * (mr * f) / (f - 1.0);
  }
  return applyRounding(pmt, st.roundMode, st.decMode);
}

double finAnnuityTotalPay(const EngineState & st) {
  int n = clampInt(st.finMonths, 1, 480);
  return applyRounding(finAnnuityMonthly(st) * static_cast<double>(n), st.roundMode, st.decMode);
}

double finAnnuityTotalInt(const EngineState & st) {
  return applyRounding(finAnnuityTotalPay(st) - st.finPrincipal, st.roundMode, st.decMode);
}

} // namespace CashCalc
