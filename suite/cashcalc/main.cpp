#include "../common/lilium_ui.h"

#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// CeGCC winuser.h omits several desktop VK_OEM_* constants
#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS 0xBB
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif
#ifndef VK_OEM_PERIOD
#define VK_OEM_PERIOD 0xBE
#endif

namespace {

static constexpr int kModeCount = 4;
static constexpr int kMaxTapeLines = 64;
static constexpr int kMaxInvoiceItems = 64;
static constexpr double kMaxDisplayAbs = 999999999999.0; // 12 digits max

enum CalcMode {
  ModeStd = 0,
  ModeCostSell = 1,
  ModeInvoice = 2,
  ModeFinance = 3
};

enum RoundMode {
  RoundFloat = 0, // F
  RoundCut = 1,   // 切捨
  RoundUp = 2,    // 切上
  RoundHalf = 3   // 5/4 四捨五入
};

enum DecMode {
  Dec0 = 0,
  Dec2 = 1,
  Dec3 = 2,
  Dec4 = 3,
  DecAdd2 = 4
};

enum PendingOp {
  OpNone = 0,
  OpAdd,
  OpSub,
  OpMul,
  OpDiv
};

struct InvoiceItem {
  double amount;
  int taxCat; // 0=標準, 1=軽減, 2=非課税
};

struct DateYMD {
  int y;
  int m;
  int d;
};

struct CashCalcState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;
  wchar_t cfgPath[MAX_PATH] = L"";

  int calcMode = ModeStd;
  bool settingsOpen = false;
  int setRow = 0; // 0=StdTax, 1=RedTax, 2=Round, 3=Dec, 4=GTAuto

  // Accounting configuration
  double taxRateStd = 10.0;
  double taxRateRed = 8.0;
  bool useReducedTax = false;
  int roundMode = RoundHalf;
  int decMode = Dec0;
  bool gtAutoAccum = true;

  // Mode 0: Desktop Accounting Calculator
  double acc = 0.0;
  double curVal = 0.0;
  double memVal = 0.0;
  double gtVal = 0.0;
  int pendingOp = OpNone;
  bool entering = false;
  bool afterEquals = false;
  bool gtRecalledOnce = false;
  bool kLocked = false;
  int kOp = OpNone;
  double kVal = 0.0;
  bool errorState = false;
  char inputBuf[24] = "0";
  wchar_t subDisplay[96] = L"";
  wchar_t tapeLines[kMaxTapeLines][64] = {};
  int tapeCount = 0;

  // Mode 1: Cost / Sell / Margin
  int csField = 0; // 0=Cost, 1=Sell, 2=Margin%
  double csCost = 800.0;
  double csSell = 1000.0;
  double csMargin = 20.0;
  char csBuf[24] = "";
  bool csTyping = false;

  // Mode 2: Invoice Tax Aggregator
  InvoiceItem invItems[kMaxInvoiceItems] = {};
  int invCount = 0;
  int invSel = 0;
  int invScroll = 0;
  int invTaxCat = 0; // 0=標準, 1=軽減, 2=非課税
  char invInputBuf[24] = "0";
  bool invTyping = false;

  // Mode 3: Days/Interest & Installment Loan
  int finSubMode = 0; // 0=日数・単利利息, 1=割賦・均等返済
  int finRow = 0;
  DateYMD startDate = { 2026, 4, 1 };
  DateYMD endDate = { 2026, 11, 21 };
  bool bothEndsInclusive = false;
  double finPrincipal = 1000000.0;
  double finRatePct = 2.5;
  int finMonths = 24;

  int lastMin = -1;
  int lastBat = -1;
};

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

static int decPlacesForMode(int decMode) {
  switch (decMode) {
    case Dec0: return 0;
    case Dec2: return 2;
    case Dec3: return 3;
    case Dec4: return 4;
    case DecAdd2: return 2;
    default: return 0;
  }
}

static double pow10i(int n) {
  double r = 1.0;
  for (int i = 0; i < n; ++i) r *= 10.0;
  return r;
}

static double applyRounding(double val, int roundMode, int decMode) {
  if (!isfinite(val)) return 0.0;
  if (roundMode == RoundFloat) {
    if (fabs(val) < 1e-12) return 0.0;
    return val;
  }
  int dp = decPlacesForMode(decMode);
  double scale = pow10i(dp);
  double s = (val < 0.0) ? -1.0 : 1.0;
  double mag = fabs(val) * scale;
  double outMag = 0.0;
  if (roundMode == RoundCut) {
    outMag = floor(mag + 1e-9);
  } else if (roundMode == RoundUp) {
    double f = floor(mag + 1e-9);
    outMag = (mag - f > 1e-7) ? (f + 1.0) : f;
  } else {
    // RoundHalf (5/4)
    outMag = floor(mag + 0.5 + 1e-9);
  }
  double res = (s * outMag) / scale;
  if (fabs(res) < 1e-12) res = 0.0;
  return res;
}

static void insertCommasAndCopy(const char * rawNum, wchar_t * out, size_t maxLen) {
  // rawNum is e.g. "-1234567.89" or "1000"
  bool neg = (rawNum[0] == '-');
  const char * p = neg ? (rawNum + 1) : rawNum;
  const char * dot = strchr(p, '.');
  size_t intLen = dot ? static_cast<size_t>(dot - p) : strlen(p);

  wchar_t buf[64] = L"";
  size_t pos = 0;
  if (neg && pos + 1 < 63) buf[pos++] = L'-';
  for (size_t i = 0; i < intLen && pos + 2 < 63; ++i) {
    if (i > 0 && ((intLen - i) % 3) == 0) {
      buf[pos++] = L',';
    }
    buf[pos++] = static_cast<wchar_t>(p[i]);
  }
  if (dot && dot[1] != '\0') {
    for (size_t k = 0; dot[k] != '\0' && pos + 1 < 63; ++k) {
      buf[pos++] = static_cast<wchar_t>(dot[k]);
    }
  }
  buf[pos] = L'\0';
  wcsncpy(out, buf, maxLen - 1);
  out[maxLen - 1] = L'\0';
}

static void formatAccounting(double val, int roundMode, int decMode, bool forceDecimal,
                             wchar_t * out, size_t maxLen) {
  if (!isfinite(val) || fabs(val) > kMaxDisplayAbs) {
    wcsncpy(out, L"E (桁あふれ)", maxLen - 1);
    out[maxLen - 1] = L'\0';
    return;
  }
  double v = applyRounding(val, roundMode, decMode);
  char raw[64] = "";
  if (roundMode == RoundFloat && !forceDecimal) {
    _snprintf(raw, sizeof(raw), "%.6f", v);
    raw[sizeof(raw) - 1] = '\0';
    char * dot = strchr(raw, '.');
    if (dot) {
      size_t n = strlen(raw);
      while (n > 0 && raw[n - 1] == '0') raw[--n] = '\0';
      if (n > 0 && raw[n - 1] == '.') raw[--n] = '\0';
    }
    if (strcmp(raw, "-0") == 0) strcpy(raw, "0");
  } else {
    int dp = (roundMode == RoundFloat) ? 2 : decPlacesForMode(decMode);
    _snprintf(raw, sizeof(raw), "%.*f", dp, v);
    raw[sizeof(raw) - 1] = '\0';
  }
  insertCommasAndCopy(raw, out, maxLen);
}

static void formatPlainIntComma(double val, wchar_t * out, size_t maxLen) {
  formatAccounting(val, RoundHalf, Dec0, false, out, maxLen);
}

static void saveConfig(const CashCalcState * st) {
  if (!st || !st->cfgPath[0]) return;
  FILE * fp = _wfopen(st->cfgPath, L"wb");
  if (!fp) return;
  fprintf(fp, "# LiliumCashCalc Settings\n");
  fprintf(fp, "tax_std=%.2f\n", st->taxRateStd);
  fprintf(fp, "tax_red=%.2f\n", st->taxRateRed);
  fprintf(fp, "use_red=%d\n", st->useReducedTax ? 1 : 0);
  fprintf(fp, "round=%d\n", st->roundMode);
  fprintf(fp, "dec=%d\n", st->decMode);
  fprintf(fp, "gt_auto=%d\n", st->gtAutoAccum ? 1 : 0);
  fclose(fp);
}

static void loadConfig(CashCalcState * st) {
  if (!st || !st->cfgPath[0]) return;
  FILE * fp = _wfopen(st->cfgPath, L"rb");
  if (!fp) return;
  char line[128];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "tax_std=", 8) == 0) {
      st->taxRateStd = clampDouble(atof(line + 8), 0.0, 50.0);
    } else if (strncmp(line, "tax_red=", 8) == 0) {
      st->taxRateRed = clampDouble(atof(line + 8), 0.0, 50.0);
    } else if (strncmp(line, "use_red=", 8) == 0) {
      st->useReducedTax = (atoi(line + 8) != 0);
    } else if (strncmp(line, "round=", 6) == 0) {
      st->roundMode = clampInt(atoi(line + 6), 0, 3);
    } else if (strncmp(line, "dec=", 4) == 0) {
      st->decMode = clampInt(atoi(line + 4), 0, 4);
    } else if (strncmp(line, "gt_auto=", 8) == 0) {
      st->gtAutoAccum = (atoi(line + 8) != 0);
    }
  }
  fclose(fp);
}

static void pushTapeLine(CashCalcState * st, const wchar_t * line) {
  if (!line || !line[0]) return;
  if (st->tapeCount >= kMaxTapeLines) {
    for (int i = 1; i < kMaxTapeLines; ++i) {
      wcscpy(st->tapeLines[i - 1], st->tapeLines[i]);
    }
    st->tapeCount = kMaxTapeLines - 1;
  }
  wcsncpy(st->tapeLines[st->tapeCount], line, 63);
  st->tapeLines[st->tapeCount][63] = L'\0';
  ++st->tapeCount;
}

static double activeTaxRate(const CashCalcState * st) {
  return st->useReducedTax ? st->taxRateRed : st->taxRateStd;
}

static const wchar_t * opSymbol(int op) {
  switch (op) {
    case OpAdd: return L"+";
    case OpSub: return L"-";
    case OpMul: return L"×";
    case OpDiv: return L"÷";
    default: return L"";
  }
}

static double evalBinaryOp(double a, int op, double b, bool & outErr) {
  outErr = false;
  double res = b;
  switch (op) {
    case OpAdd: res = a + b; break;
    case OpSub: res = a - b; break;
    case OpMul: res = a * b; break;
    case OpDiv:
      if (fabs(b) < 1e-12) {
        outErr = true;
        return 0.0;
      }
      res = a / b;
      break;
    default:
      res = b;
      break;
  }
  if (!isfinite(res) || fabs(res) > kMaxDisplayAbs) {
    outErr = true;
    return 0.0;
  }
  return res;
}

static void syncInputBufFromValue(CashCalcState * st, double v) {
  char tmp[32] = "";
  double rv = applyRounding(v, st->roundMode, st->decMode);
  if (!isfinite(rv) || fabs(rv) > kMaxDisplayAbs) {
    st->errorState = true;
    strcpy(st->inputBuf, "0");
    return;
  }
  if (st->roundMode == RoundFloat) {
    _snprintf(tmp, sizeof(tmp), "%.6f", rv);
    tmp[sizeof(tmp) - 1] = '\0';
    char * dot = strchr(tmp, '.');
    if (dot) {
      size_t n = strlen(tmp);
      while (n > 0 && tmp[n - 1] == '0') tmp[--n] = '\0';
      if (n > 0 && tmp[n - 1] == '.') tmp[--n] = '\0';
    }
  } else {
    _snprintf(tmp, sizeof(tmp), "%.*f", decPlacesForMode(st->decMode), rv);
    tmp[sizeof(tmp) - 1] = '\0';
  }
  strncpy(st->inputBuf, tmp, 23);
  st->inputBuf[23] = '\0';
}

static double commitStdInputValue(CashCalcState * st) {
  double v = atof(st->inputBuf);
  if (st->entering && st->decMode == DecAdd2 && strchr(st->inputBuf, '.') == nullptr) {
    v /= 100.0;
  }
  st->curVal = v;
  return v;
}

static void stdClearEntry(CashCalcState * st) {
  if (st->errorState) {
    st->errorState = false;
  }
  strcpy(st->inputBuf, "0");
  st->curVal = 0.0;
  st->entering = false;
  st->gtRecalledOnce = false;
  st->subDisplay[0] = L'\0';
}

static void stdAllClear(CashCalcState * st) {
  st->acc = 0.0;
  st->curVal = 0.0;
  st->gtVal = 0.0;
  st->pendingOp = OpNone;
  st->entering = false;
  st->afterEquals = false;
  st->gtRecalledOnce = false;
  st->kLocked = false;
  st->kOp = OpNone;
  st->kVal = 0.0;
  st->errorState = false;
  strcpy(st->inputBuf, "0");
  st->subDisplay[0] = L'\0';
}

static void stdAppendDigitStr(char * buf, const char * digits) {
  if (!buf || !digits) return;
  size_t len = strlen(buf);
  bool hasDot = (strchr(buf, '.') != nullptr);
  int maxChars = hasDot ? 14 : 12;
  if (strcmp(buf, "0") == 0 && strcmp(digits, ".") != 0) {
    if (strcmp(digits, "0") == 0 || strcmp(digits, "00") == 0) return;
    strncpy(buf, digits, 20);
    buf[20] = '\0';
    return;
  }
  if (strcmp(digits, ".") == 0) {
    if (!hasDot && len + 1 <= 15) strcat(buf, ".");
    return;
  }
  if (static_cast<int>(len + strlen(digits)) <= maxChars) {
    strcat(buf, digits);
  }
}

static void stdPressDigit(CashCalcState * st, const char * digits) {
  if (st->errorState) stdAllClear(st);
  if (!st->entering) {
    strcpy(st->inputBuf, "0");
    st->entering = true;
    st->afterEquals = false;
    st->gtRecalledOnce = false;
  }
  stdAppendDigitStr(st->inputBuf, digits);
  st->curVal = atof(st->inputBuf);
}

static void stdBackspace(CashCalcState * st) {
  if (st->errorState) { stdAllClear(st); return; }
  if (!st->entering) return;
  size_t len = strlen(st->inputBuf);
  if (len <= 1 || (len == 2 && st->inputBuf[0] == '-')) {
    strcpy(st->inputBuf, "0");
    st->curVal = 0.0;
  } else {
    st->inputBuf[len - 1] = '\0';
    st->curVal = atof(st->inputBuf);
  }
}

static void stdToggleSign(CashCalcState * st) {
  if (st->errorState) return;
  if (st->entering) {
    if (strcmp(st->inputBuf, "0") != 0) {
      if (st->inputBuf[0] == '-') {
        memmove(st->inputBuf, st->inputBuf + 1, strlen(st->inputBuf));
      } else if (strlen(st->inputBuf) + 1 < 22) {
        memmove(st->inputBuf + 1, st->inputBuf, strlen(st->inputBuf) + 1);
        st->inputBuf[0] = '-';
      }
      st->curVal = atof(st->inputBuf);
    }
  } else {
    st->curVal = -st->curVal;
    if (st->afterEquals) st->acc = st->curVal;
    syncInputBufFromValue(st, st->curVal);
  }
}

static void stdPressOp(CashCalcState * st, int newOp) {
  if (st->errorState) return;
  st->gtRecalledOnce = false;

  // Double-tap same operator without entering digits -> Lock Constant [K]
  if (!st->entering && !st->afterEquals && st->pendingOp == newOp) {
    st->kLocked = true;
    st->kOp = newOp;
    st->kVal = st->acc;
    _snwprintf(st->subDisplay, 96, L"定数Kロック (%ls)", opSymbol(newOp));
    st->subDisplay[95] = L'\0';
    return;
  }

  st->kLocked = false;
  if (st->entering && st->pendingOp != OpNone) {
    double rhs = commitStdInputValue(st);
    bool err = false;
    double res = evalBinaryOp(st->acc, st->pendingOp, rhs, err);
    if (err) {
      st->errorState = true;
      st->subDisplay[0] = L'\0';
      return;
    }
    res = applyRounding(res, st->roundMode, st->decMode);
    st->acc = res;
    st->curVal = res;
    syncInputBufFromValue(st, res);
  } else if (st->entering || st->afterEquals) {
    st->acc = st->entering ? commitStdInputValue(st) : st->curVal;
    st->acc = applyRounding(st->acc, st->roundMode, st->decMode);
  }

  st->pendingOp = newOp;
  st->entering = false;
  st->afterEquals = false;

  wchar_t aStr[32];
  formatAccounting(st->acc, st->roundMode, st->decMode, false, aStr, 32);
  _snwprintf(st->subDisplay, 96, L"%ls %ls", aStr, opSymbol(newOp));
  st->subDisplay[95] = L'\0';
}

static void stdPressEquals(CashCalcState * st) {
  if (st->errorState) return;
  st->gtRecalledOnce = false;

  double lhs = st->acc;
  double rhs = st->entering ? commitStdInputValue(st) : st->curVal;
  int op = st->pendingOp;

  if (st->kLocked && st->kOp != OpNone) {
    // Casio/Sharp K mode: new_input [=] -> applies constant
    op = st->kOp;
    if (op == OpMul || op == OpAdd) {
      lhs = st->kVal;
      rhs = st->entering ? rhs : st->curVal;
    } else {
      // K subtraction or division uses locked kVal as RHS
      lhs = st->entering ? rhs : st->curVal;
      rhs = st->kVal;
    }
  } else if (op == OpNone && st->afterEquals && st->kOp != OpNone) {
    // Repeat last operation on repeated '='
    op = st->kOp;
    lhs = st->curVal;
    rhs = st->kVal;
  } else if (op == OpNone) {
    double v = applyRounding(rhs, st->roundMode, st->decMode);
    st->acc = v;
    st->curVal = v;
    st->entering = false;
    st->afterEquals = true;
    syncInputBufFromValue(st, v);
    return;
  } else {
    st->kOp = op;
    st->kVal = rhs;
  }

  bool err = false;
  double res = evalBinaryOp(lhs, op, rhs, err);
  if (err) {
    st->errorState = true;
    st->subDisplay[0] = L'\0';
    return;
  }
  res = applyRounding(res, st->roundMode, st->decMode);

  wchar_t sL[32], sR[32], sRes[32], tapeBuf[64];
  formatAccounting(lhs, st->roundMode, st->decMode, false, sL, 32);
  formatAccounting(rhs, st->roundMode, st->decMode, false, sR, 32);
  formatAccounting(res, st->roundMode, st->decMode, false, sRes, 32);
  _snwprintf(st->subDisplay, 96, L"%ls %ls %ls =", sL, opSymbol(op), sR);
  st->subDisplay[95] = L'\0';
  _snwprintf(tapeBuf, 64, L"%ls %ls %ls = %ls", sL, opSymbol(op), sR, sRes);
  tapeBuf[63] = L'\0';
  pushTapeLine(st, tapeBuf);

  st->acc = res;
  st->curVal = res;
  if (!st->kLocked) st->pendingOp = OpNone;
  st->entering = false;
  st->afterEquals = true;
  if (st->gtAutoAccum) {
    st->gtVal = applyRounding(st->gtVal + res, st->roundMode, st->decMode);
  }
  syncInputBufFromValue(st, res);
}

static void stdPressPercent(CashCalcState * st) {
  if (st->errorState) return;
  double rhs = st->entering ? commitStdInputValue(st) : st->curVal;
  double res = rhs / 100.0;
  wchar_t tapeBuf[64], sL[32], sR[32], sRes[32];

  if (st->pendingOp == OpMul) {
    res = st->acc * (rhs / 100.0);
    formatAccounting(st->acc, st->roundMode, st->decMode, false, sL, 32);
    formatAccounting(rhs, RoundFloat, Dec0, false, sR, 32);
  } else if (st->pendingOp == OpAdd) {
    double addAmt = st->acc * (rhs / 100.0);
    res = st->acc + addAmt;
    formatAccounting(st->acc, st->roundMode, st->decMode, false, sL, 32);
    formatAccounting(rhs, RoundFloat, Dec0, false, sR, 32);
  } else if (st->pendingOp == OpSub) {
    double subAmt = st->acc * (rhs / 100.0);
    res = st->acc - subAmt;
    formatAccounting(st->acc, st->roundMode, st->decMode, false, sL, 32);
    formatAccounting(rhs, RoundFloat, Dec0, false, sR, 32);
  } else if (st->pendingOp == OpDiv) {
    if (fabs(rhs) < 1e-12) { st->errorState = true; return; }
    res = st->acc / (rhs / 100.0);
    formatAccounting(st->acc, st->roundMode, st->decMode, false, sL, 32);
    formatAccounting(rhs, RoundFloat, Dec0, false, sR, 32);
  } else {
    formatAccounting(rhs, RoundFloat, Dec0, false, sL, 32);
    wcscpy(sR, L"");
  }

  res = applyRounding(res, st->roundMode, st->decMode);
  formatAccounting(res, st->roundMode, st->decMode, false, sRes, 32);
  if (st->pendingOp != OpNone) {
    _snwprintf(tapeBuf, 64, L"%ls %ls %ls%% = %ls", sL, opSymbol(st->pendingOp), sR, sRes);
    _snwprintf(st->subDisplay, 96, L"%ls %ls %ls%% =", sL, opSymbol(st->pendingOp), sR);
  } else {
    _snwprintf(tapeBuf, 64, L"%ls%% = %ls", sL, sRes);
    _snwprintf(st->subDisplay, 96, L"%ls%% =", sL);
  }
  tapeBuf[63] = L'\0';
  st->subDisplay[95] = L'\0';
  pushTapeLine(st, tapeBuf);

  st->acc = res;
  st->curVal = res;
  st->pendingOp = OpNone;
  st->entering = false;
  st->afterEquals = true;
  if (st->gtAutoAccum) st->gtVal = applyRounding(st->gtVal + res, st->roundMode, st->decMode);
  syncInputBufFromValue(st, res);
}

static void stdApplyTax(CashCalcState * st, bool includeTax) {
  if (st->errorState) return;
  double baseInput = st->entering ? commitStdInputValue(st) : st->curVal;
  double rate = activeTaxRate(st) / 100.0;
  double outVal = 0.0;
  double taxAmt = 0.0;
  double bodyVal = 0.0;

  if (includeTax) {
    bodyVal = baseInput;
    outVal = applyRounding(bodyVal * (1.0 + rate), st->roundMode, st->decMode);
    taxAmt = applyRounding(outVal - bodyVal, st->roundMode, st->decMode);
  } else {
    double rawBody = baseInput / (1.0 + rate);
    bodyVal = applyRounding(rawBody, st->roundMode, st->decMode);
    outVal = bodyVal;
    taxAmt = applyRounding(baseInput - bodyVal, st->roundMode, st->decMode);
  }

  wchar_t bStr[28], tStr[28], oStr[28], tapeBuf[64];
  formatAccounting(bodyVal, st->roundMode, st->decMode, false, bStr, 28);
  formatAccounting(taxAmt, st->roundMode, st->decMode, false, tStr, 28);
  formatAccounting(outVal, st->roundMode, st->decMode, false, oStr, 28);

  if (includeTax) {
    _snwprintf(st->subDisplay, 96, L"税抜%ls + 税(%.1f%%)%ls = 税込", bStr, activeTaxRate(st), tStr);
    _snwprintf(tapeBuf, 64, L"税込(%.1f%%) %ls -> %ls (税%ls)", activeTaxRate(st), bStr, oStr, tStr);
  } else {
    _snwprintf(st->subDisplay, 96, L"税込%ls - 税(%.1f%%)%ls = 税抜",
               bStr, activeTaxRate(st), tStr);
    // Display actual gross amount in subDisplay cleanly
    wchar_t gStr[28];
    formatAccounting(baseInput, st->roundMode, st->decMode, false, gStr, 28);
    _snwprintf(st->subDisplay, 96, L"税込%ls -> 税抜%ls (税%ls)", gStr, oStr, tStr);
    _snwprintf(tapeBuf, 64, L"税抜(%.1f%%) %ls -> %ls (税%ls)", activeTaxRate(st), gStr, oStr, tStr);
  }
  st->subDisplay[95] = L'\0';
  tapeBuf[63] = L'\0';
  pushTapeLine(st, tapeBuf);

  st->acc = outVal;
  st->curVal = outVal;
  st->entering = false;
  st->afterEquals = true;
  syncInputBufFromValue(st, outVal);
}

static void stdMemoryOp(CashCalcState * st, int which) {
  // 0=M+, 1=M-, 2=MR, 3=MC, 4=GT
  if (st->errorState) return;
  if (which == 0 || which == 1) {
    double v = st->entering ? commitStdInputValue(st) : st->curVal;
    if (st->pendingOp != OpNone && st->entering) {
      bool err = false;
      v = evalBinaryOp(st->acc, st->pendingOp, v, err);
      if (err) { st->errorState = true; return; }
      v = applyRounding(v, st->roundMode, st->decMode);
      st->acc = v;
      st->curVal = v;
      st->pendingOp = OpNone;
      syncInputBufFromValue(st, v);
    }
    st->memVal = applyRounding(st->memVal + ((which == 0) ? v : -v), st->roundMode, st->decMode);
    st->entering = false;
    st->afterEquals = true;
    wchar_t mStr[28];
    formatAccounting(st->memVal, st->roundMode, st->decMode, false, mStr, 28);
    _snwprintf(st->subDisplay, 96, L"M%ls実行 -> メモリ合計 %ls", (which == 0) ? L"+" : L"-", mStr);
    st->subDisplay[95] = L'\0';
  } else if (which == 2) {
    st->curVal = st->memVal;
    st->entering = false;
    st->afterEquals = true;
    syncInputBufFromValue(st, st->memVal);
    wcscpy(st->subDisplay, L"MR: メモリ呼出");
  } else if (which == 3) {
    st->memVal = 0.0;
    wcscpy(st->subDisplay, L"MC: メモリクリア");
  } else if (which == 4) {
    if (st->gtRecalledOnce) {
      st->gtVal = 0.0;
      st->gtRecalledOnce = false;
      wcscpy(st->subDisplay, L"GT: 累計クリア");
    } else {
      st->curVal = st->gtVal;
      st->acc = st->gtVal;
      st->entering = false;
      st->afterEquals = true;
      st->gtRecalledOnce = true;
      syncInputBufFromValue(st, st->gtVal);
      wcscpy(st->subDisplay, L"GT呼出 (もう1回でクリア)");
    }
  }
}

// Calendar days helper (Howard Hinnant days_from_civil, leap-year accurate)
static long daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned doy = (153u * (static_cast<unsigned>(m) + (m > 2 ? -3u : 9u)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097L + static_cast<long>(doe) - 719468L;
}

static int daysInMonth(int y, int m) {
  static constexpr int md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1 || m > 12) return 30;
  int d = md[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) d = 29;
  return d;
}

static void shiftDateDays(DateYMD & dt, int deltaDays) {
  // Simple step adjustment
  int step = (deltaDays >= 0) ? 1 : -1;
  int n = abs(deltaDays);
  for (int i = 0; i < n; ++i) {
    dt.d += step;
    if (dt.d > daysInMonth(dt.y, dt.m)) {
      dt.d = 1;
      if (++dt.m > 12) { dt.m = 1; ++dt.y; }
    } else if (dt.d < 1) {
      if (--dt.m < 1) { dt.m = 12; --dt.y; }
      dt.d = daysInMonth(dt.y, dt.m);
    }
  }
  dt.y = clampInt(dt.y, 1980, 2099);
}

static void shiftDateMonths(DateYMD & dt, int deltaMonths) {
  int totalM = dt.y * 12 + (dt.m - 1) + deltaMonths;
  totalM = clampInt(totalM, 1980 * 12, 2099 * 12 + 11);
  dt.y = totalM / 12;
  dt.m = (totalM % 12) + 1;
  dt.d = clampInt(dt.d, 1, daysInMonth(dt.y, dt.m));
}

// =============================================================================
// Mode Renderers
// =============================================================================

struct KeyDef {
  const wchar_t * label;
  int actionId;
  int style; // 0=digit, 1=func/top, 2=op, 3=eq/accent, 4=danger/clear, 5=tax
};

enum KeypadAction {
  ActTaxInc = 100, ActTaxExc, ActTaxToggle, ActGT, ActMPlus, ActMMinus,
  ActMC, ActMR, ActDiv, ActBS, ActSign, ActMul, ActC, ActPct, ActSub,
  ActAC, Act00, ActDot, ActEq, ActAdd,
  ActDigit0 = 0, ActDigit1 = 1, ActDigit2 = 2, ActDigit3 = 3, ActDigit4 = 4,
  ActDigit5 = 5, ActDigit6 = 5 + 1, ActDigit7 = 7, ActDigit8 = 8, ActDigit9 = 9
};

static constexpr KeyDef kKeypad[5][6] = {
  { { L"税込",  ActTaxInc,    5 }, { L"税抜", ActTaxExc,   5 }, { L"税率", ActTaxToggle, 5 },
    { L"GT",    ActGT,        1 }, { L"M+",   ActMPlus,    1 }, { L"M-",   ActMMinus,    1 } },
  { { L"MC",    ActMC,        1 }, { L"MR",   ActMR,       1 }, { L"7",    ActDigit7,    0 },
    { L"8",     ActDigit8,    0 }, { L"9",    ActDigit9,   0 }, { L"÷",    ActDiv,       2 } },
  { { L"BS",    ActBS,        1 }, { L"+/-",  ActSign,     1 }, { L"4",    ActDigit4,    0 },
    { L"5",     ActDigit5,    0 }, { L"6",    ActDigit6,   0 }, { L"×",    ActMul,       2 } },
  { { L"C",     ActC,         4 }, { L"%",    ActPct,      1 }, { L"1",    ActDigit1,    0 },
    { L"2",     ActDigit2,    0 }, { L"3",    ActDigit3,   0 }, { L"-",    ActSub,       2 } },
  { { L"AC",    ActAC,        4 }, { L"00",   Act00,       0 }, { L"0",    ActDigit0,    0 },
    { L".",     ActDot,       0 }, { L"=",    ActEq,       3 }, { L"+",    ActAdd,       2 } }
};

static void renderMode0StdCalc(LiliumUI::Canvas & c, const CashCalcState * st) {
  // LCD Readout (x=12..628, y=74..148)
  c.drawBox(12, 74, LiliumUI::kMainW - 24, 74, 0xEBF3F0, LiliumUI::kColTealDark);

  // Top status flags inside LCD
  static constexpr const wchar_t * kRoundTag[4] = { L"F", L"切捨", L"切上", L"5/4" };
  static constexpr const wchar_t * kDecTag[5] = { L"0桁", L"2桁", L"3桁", L"4桁", L"ADD2" };

  int fx = 20;
  if (fabs(st->memVal) > 1e-12) {
    c.drawBox(fx, 78, 22, 16, LiliumUI::kColTealDark, LiliumUI::kColTealDark);
    c.drawText(fx, 78, 22, 16, LiliumUI::kColWhite, L"M", 1, LiliumUI::Align::Center, true);
    fx += 26;
  }
  if (fabs(st->gtVal) > 1e-12) {
    c.drawBox(fx, 78, 28, 16, LiliumUI::kColTeal, LiliumUI::kColTealDark);
    c.drawText(fx, 78, 28, 16, LiliumUI::kColWhite, L"GT", 1, LiliumUI::Align::Center, true);
    fx += 32;
  }
  if (st->kLocked) {
    c.drawBox(fx, 78, 22, 16, LiliumUI::kColWarnBg, LiliumUI::kColWarnBorder);
    c.drawText(fx, 78, 22, 16, LiliumUI::kColWarnText, L"K", 1, LiliumUI::Align::Center, true);
    fx += 26;
  }
  wchar_t taxFlag[32];
  _snwprintf(taxFlag, 32, L"%ls%.1f%%", st->useReducedTax ? L"軽減" : L"標準", activeTaxRate(st));
  taxFlag[31] = L'\0';
  c.drawBox(fx, 78, 92, 16,
            st->useReducedTax ? LiliumUI::kColWarnBg : LiliumUI::kColWhite,
            st->useReducedTax ? LiliumUI::kColWarnBorder : LiliumUI::kColTealBorder);
  c.drawText(fx, 78, 92, 16,
             st->useReducedTax ? LiliumUI::kColWarnText : LiliumUI::kColTealDeep,
             taxFlag, 1, LiliumUI::Align::Center, true);

  wchar_t rndFlag[32];
  _snwprintf(rndFlag, 32, L"%ls / %ls", kRoundTag[st->roundMode], kDecTag[st->decMode]);
  rndFlag[31] = L'\0';
  c.drawText(430, 78, 188, 16, LiliumUI::kColMuted, rndFlag, 1, LiliumUI::Align::Right, true);

  // Main 12-digit readout (y=96..126, scale=3 -> 24px crisp pixels!)
  wchar_t mainStr[64] = L"0";
  if (st->errorState) {
    wcscpy(mainStr, L"E (エラー)");
  } else if (st->entering) {
    insertCommasAndCopy(st->inputBuf, mainStr, 64);
  } else {
    formatAccounting(st->curVal, st->roundMode, st->decMode, false, mainStr, 64);
  }
  c.drawText(20, 96, LiliumUI::kMainW - 44, 30,
             st->errorState ? LiliumUI::kColDangerText : LiliumUI::kColCharcoal,
             mainStr, 3, LiliumUI::Align::Right, true);

  // Sub-line expression / breakdown
  c.drawText(20, 127, LiliumUI::kMainW - 44, 18, LiliumUI::kColTealDeep,
             st->subDisplay, 2, LiliumUI::Align::Right, true);

  // Mini 2-line History Tape (y=152..192)
  c.drawBox(12, 152, LiliumUI::kMainW - 24, 40, LiliumUI::kColWhite, LiliumUI::kColBorder);
  if (st->tapeCount == 0) {
    c.drawText(20, 152, LiliumUI::kMainW - 40, 40, LiliumUI::kColLightMuted,
               L"計算履歴テープ (演算結果がここに記録されます)", 2, LiliumUI::Align::Left, true);
  } else {
    int first = (st->tapeCount >= 2) ? (st->tapeCount - 2) : 0;
    for (int i = 0; i < 2 && (first + i) < st->tapeCount; ++i) {
      c.drawText(20, 154 + i * 18, LiliumUI::kMainW - 40, 18,
                 (first + i == st->tapeCount - 1) ? LiliumUI::kColText : LiliumUI::kColMuted,
                 st->tapeLines[first + i], 2, LiliumUI::Align::Left, true);
    }
  }

  // 6x5 Accounting Keypad (y=196..434)
  for (int r = 0; r < 5; ++r) {
    int ky = 198 + r * 48;
    for (int col = 0; col < 6; ++col) {
      int kx = 12 + col * 103;
      const KeyDef & kd = kKeypad[r][col];
      uint32_t bg = LiliumUI::kColWhite;
      uint32_t bd = LiliumUI::kColBorder;
      uint32_t tx = LiliumUI::kColText;
      if (kd.style == 1) { bg = 0xE9EEF4; bd = LiliumUI::kColBorderStrong; tx = LiliumUI::kColTextSub; }
      else if (kd.style == 2) { bg = LiliumUI::kColTealSoft; bd = LiliumUI::kColTealBorder; tx = LiliumUI::kColTealDeep; }
      else if (kd.style == 3) { bg = LiliumUI::kColTeal; bd = LiliumUI::kColTealDark; tx = LiliumUI::kColWhite; }
      else if (kd.style == 4) { bg = LiliumUI::kColDangerBg; bd = LiliumUI::kColDangerBorder; tx = LiliumUI::kColDangerText; }
      else if (kd.style == 5) { bg = LiliumUI::kColWarnBg; bd = LiliumUI::kColWarnBorder; tx = LiliumUI::kColWarnText; }
      c.drawBox(kx, ky, 99, 43, bg, bd);
      c.drawText(kx, ky, 99, 43, tx, kd.label, 2, LiliumUI::Align::Center, true);
    }
  }
}

static void renderMode1CostSell(LiliumUI::Canvas & c, const CashCalcState * st) {
  c.drawBox(12, 74, LiliumUI::kMainW - 24, 184, LiliumUI::kColWhite, LiliumUI::kColBorder);
  const wchar_t * labels[3] = { L"原価 (COST)", L"売価 (SELL)", L"粗利率 (MARGIN %)" };
  double vals[3] = { st->csCost, st->csSell, st->csMargin };

  for (int i = 0; i < 3; ++i) {
    int ry = 84 + i * 56;
    bool act = (st->csField == i);
    c.drawBox(22, ry, 596, 48,
              act ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
              act ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    if (act) c.fillRect(22, ry, 5, 48, LiliumUI::kColTealDark);
    c.drawText(34, ry, 210, 48, LiliumUI::kColTextSub, labels[i], 2, LiliumUI::Align::Left, true);

    wchar_t vStr[48];
    if (act && st->csTyping) {
      insertCommasAndCopy(st->csBuf[0] ? st->csBuf : "0", vStr, 48);
    } else if (i == 2) {
      _snwprintf(vStr, 48, L"%.2f %%", vals[i]);
      vStr[47] = L'\0';
    } else {
      formatAccounting(vals[i], st->roundMode, st->decMode, false, vStr, 48);
    }
    c.drawText(260, ry, 344, 48,
               act ? LiliumUI::kColTealDeep : LiliumUI::kColText,
               vStr, 3, LiliumUI::Align::Right, true);
  }

  // Calculated breakdown summary
  double profit = st->csSell - st->csCost;
  double markup = (fabs(st->csCost) > 1e-9) ? (profit / st->csCost * 100.0) : 0.0;
  double realMargin = (fabs(st->csSell) > 1e-9) ? (profit / st->csSell * 100.0) : 0.0;
  double sellTax = applyRounding(st->csSell * (1.0 + activeTaxRate(st) / 100.0), st->roundMode, st->decMode);

  c.drawBox(12, 266, LiliumUI::kMainW - 24, 166, 0xEBF3F0, LiliumUI::kColTealDark);
  c.drawText(24, 272, 300, 22, LiliumUI::kColTealDeep, L"■ 原価・売価・利益 内訳", 2, LiliumUI::Align::Left, true);

  wchar_t pStr[32], sTaxStr[32], line1[96], line2[96];
  formatAccounting(profit, st->roundMode, st->decMode, false, pStr, 32);
  formatAccounting(sellTax, st->roundMode, st->decMode, false, sTaxStr, 32);
  _snwprintf(line1, 96, L"粗利額: %ls    粗利率(利益/売価): %.2f%%", pStr, realMargin);
  line1[95] = L'\0';
  _snwprintf(line2, 96, L"値入率(利益/原価): %.2f%%   税込売価(%.1f%%): %ls",
             markup, activeTaxRate(st), sTaxStr);
  line2[95] = L'\0';

  c.drawText(24, 304, 592, 28, LiliumUI::kColCharcoal, line1, 2, LiliumUI::Align::Left, true);
  c.drawText(24, 338, 592, 28, LiliumUI::kColTealDeep, line2, 2, LiliumUI::Align::Left, true);

  // Action button [選択項目を計算]
  c.drawBox(24, 378, 280, 42, LiliumUI::kColTeal, LiliumUI::kColTealDark);
  c.drawText(24, 378, 280, 42, LiliumUI::kColWhite, L"[Enter] この項目を逆算", 2, LiliumUI::Align::Center, true);
  c.drawBox(320, 378, 296, 42, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(320, 378, 296, 42, LiliumUI::kColMuted, L"[↑↓]項目選択  数字入力で更新", 2, LiliumUI::Align::Center, true);
}

static void renderMode2Invoice(LiliumUI::Canvas & c, const CashCalcState * st) {
  static constexpr const wchar_t * kCatNames[3] = { L"標準税", L"軽減税", L"非課税" };

  // Input row (y=74..116)
  c.drawBox(12, 74, 310, 42, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  wchar_t inStr[48];
  insertCommasAndCopy(st->invInputBuf[0] ? st->invInputBuf : "0", inStr, 48);
  c.drawText(20, 74, 292, 42, LiliumUI::kColCharcoal, inStr, 2, LiliumUI::Align::Right, true);

  c.drawBox(328, 74, 150, 42,
            st->invTaxCat == 1 ? LiliumUI::kColWarnBg : (st->invTaxCat == 0 ? LiliumUI::kColTealSoft : LiliumUI::kColBtnBg),
            LiliumUI::kColBorderStrong);
  c.drawText(328, 74, 150, 42, LiliumUI::kColText, kCatNames[st->invTaxCat], 2, LiliumUI::Align::Center, true);

  c.drawBox(484, 74, 144, 42, LiliumUI::kColTeal, LiliumUI::kColTealDark);
  c.drawText(484, 74, 144, 42, LiliumUI::kColWhite, L"[Ent]登録", 2, LiliumUI::Align::Center, true);

  // Invoice tape items list (y=122..308)
  c.drawBox(12, 122, LiliumUI::kMainW - 24, 186, LiliumUI::kColWhite, LiliumUI::kColBorder);
  if (st->invCount == 0) {
    c.drawText(24, 194, LiliumUI::kMainW - 48, 36, LiliumUI::kColLightMuted,
               L"明細なし (税抜金額を入力し [F5]区分 -> [Enter]追加)", 2, LiliumUI::Align::Center, true);
  } else {
    const int vis = 5;
    for (int r = 0; r < vis; ++r) {
      int idx = st->invScroll + r;
      if (idx >= st->invCount) break;
      int ry = 128 + r * 35;
      bool sel = (idx == st->invSel);
      const InvoiceItem & it = st->invItems[idx];
      c.drawBox(20, ry, 600, 32,
                sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
      wchar_t noStr[16], amtStr[32];
      _snwprintf(noStr, 16, L"#%02d", idx + 1);
      noStr[15] = L'\0';
      formatPlainIntComma(it.amount, amtStr, 32);
      c.drawText(28, ry, 54, 32, LiliumUI::kColMuted, noStr, 2, LiliumUI::Align::Left, true);
      c.drawText(90, ry, 100, 32,
                 (it.taxCat == 1) ? LiliumUI::kColWarnText : LiliumUI::kColTealDeep,
                 kCatNames[it.taxCat], 2, LiliumUI::Align::Left, true);
      c.drawText(210, ry, 396, 32, LiliumUI::kColText, amtStr, 2, LiliumUI::Align::Right, true);
    }
  }

  // Invoice Tax-Bracket Totals (Japan Qualified Invoice rule: tax rounded per bracket subtotal)
  double sub0 = 0.0, sub1 = 0.0, sub2 = 0.0;
  for (int i = 0; i < st->invCount; ++i) {
    if (st->invItems[i].taxCat == 0) sub0 += st->invItems[i].amount;
    else if (st->invItems[i].taxCat == 1) sub1 += st->invItems[i].amount;
    else sub2 += st->invItems[i].amount;
  }
  double tax0 = applyRounding(sub0 * (st->taxRateStd / 100.0), st->roundMode, st->decMode);
  double tax1 = applyRounding(sub1 * (st->taxRateRed / 100.0), st->roundMode, st->decMode);
  double grand = sub0 + tax0 + sub1 + tax1 + sub2;

  c.drawBox(12, 314, LiliumUI::kMainW - 24, 120, 0xEBF3F0, LiliumUI::kColTealDark);
  wchar_t s0[24], t0[24], s1[24], t1[24], s2[24], gStr[32];
  formatPlainIntComma(sub0, s0, 24); formatPlainIntComma(tax0, t0, 24);
  formatPlainIntComma(sub1, s1, 24); formatPlainIntComma(tax1, t1, 24);
  formatPlainIntComma(sub2, s2, 24);
  formatAccounting(grand, st->roundMode, st->decMode, false, gStr, 32);

  wchar_t r1[96], r2[96];
  _snwprintf(r1, 96, L"標準(%.0f%%) 計:%ls  税:%ls   軽減(%.0f%%) 計:%ls  税:%ls",
             st->taxRateStd, s0, t0, st->taxRateRed, s1, t1);
  r1[95] = L'\0';
  _snwprintf(r2, 96, L"非課税:%ls  (%d点)", s2, st->invCount);
  r2[95] = L'\0';
  c.drawText(22, 322, 600, 24, LiliumUI::kColText, r1, 2, LiliumUI::Align::Left, true);
  c.drawText(22, 350, 600, 24, LiliumUI::kColMuted, r2, 2, LiliumUI::Align::Left, true);
  c.drawText(22, 384, 220, 42, LiliumUI::kColTealDeep, L"税込 総合計:", 2, LiliumUI::Align::Left, true);
  c.drawText(240, 382, 374, 44, LiliumUI::kColCharcoal, gStr, 3, LiliumUI::Align::Right, true);
}

static void renderMode3Finance(LiliumUI::Canvas & c, const CashCalcState * st) {
  // Sub-mode switcher bar (y=74..104)
  for (int m = 0; m < 2; ++m) {
    int bx = 12 + m * 310;
    bool act = (st->finSubMode == m);
    c.drawBox(bx, 74, 304, 30,
              act ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              act ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(bx, 74, 304, 30,
               act ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
               (m == 0) ? L"① 日数・単利利息 ([F5]切替)" : L"② 割賦・均等返済 ([F5]切替)",
               2, LiliumUI::Align::Center, true);
  }

  c.drawBox(12, 110, LiliumUI::kMainW - 24, 324, LiliumUI::kColWhite, LiliumUI::kColBorder);

  if (st->finSubMode == 0) {
    const wchar_t * lbs[5] = { L"開始日", L"終了日", L"数え方", L"元本 (円)", L"年利 (%)" };
    for (int r = 0; r < 5; ++r) {
      int ry = 118 + r * 40;
      bool sel = (st->finRow == r);
      c.drawBox(22, ry, 596, 35,
                sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
      c.drawText(34, ry, 180, 35, LiliumUI::kColTextSub, lbs[r], 2, LiliumUI::Align::Left, true);
      wchar_t val[64] = L"";
      if (r == 0) _snwprintf(val, 64, L"%04d/%02d/%02d", st->startDate.y, st->startDate.m, st->startDate.d);
      else if (r == 1) _snwprintf(val, 64, L"%04d/%02d/%02d", st->endDate.y, st->endDate.m, st->endDate.d);
      else if (r == 2) wcscpy(val, st->bothEndsInclusive ? L"両端入れ (+1日)" : L"片端入れ (差日数)");
      else if (r == 3) formatPlainIntComma(st->finPrincipal, val, 64);
      else _snwprintf(val, 64, L"%.2f %%", st->finRatePct);
      val[63] = L'\0';
      c.drawText(220, ry, 384, 35, LiliumUI::kColTealDeep, val, 2, LiliumUI::Align::Right, true);
    }

    long d0 = daysFromCivil(st->startDate.y, st->startDate.m, st->startDate.d);
    long d1 = daysFromCivil(st->endDate.y, st->endDate.m, st->endDate.d);
    long diff = d1 - d0;
    if (diff < 0) diff = -diff;
    long days = diff + (st->bothEndsInclusive ? 1 : 0);
    double interest = applyRounding(st->finPrincipal * (st->finRatePct / 100.0) * (static_cast<double>(days) / 365.0),
                                    st->roundMode, st->decMode);
    double total = st->finPrincipal + interest;

    c.drawBox(22, 326, 596, 98, 0xEBF3F0, LiliumUI::kColTealDark);
    wchar_t iStr[32], tStr[32], r1[96], r2[96];
    formatAccounting(interest, st->roundMode, st->decMode, false, iStr, 32);
    formatAccounting(total, st->roundMode, st->decMode, false, tStr, 32);
    _snwprintf(r1, 96, L"経過日数: %ld 日   (単利日割: %ls)", days, iStr);
    r1[95] = L'\0';
    _snwprintf(r2, 96, L"元利合計: %ls", tStr);
    r2[95] = L'\0';
    c.drawText(34, 336, 572, 30, LiliumUI::kColTealDeep, r1, 2, LiliumUI::Align::Left, true);
    c.drawText(34, 372, 572, 38, LiliumUI::kColCharcoal, r2, 3, LiliumUI::Align::Right, true);
  } else {
    const wchar_t * lbs[3] = { L"元本 (円)", L"実質年率 (%)", L"支払回数 (月)" };
    for (int r = 0; r < 3; ++r) {
      int ry = 122 + r * 48;
      bool sel = (st->finRow == r);
      c.drawBox(22, ry, 596, 40,
                sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
      c.drawText(34, ry, 200, 40, LiliumUI::kColTextSub, lbs[r], 2, LiliumUI::Align::Left, true);
      wchar_t val[64] = L"";
      if (r == 0) formatPlainIntComma(st->finPrincipal, val, 64);
      else if (r == 1) _snwprintf(val, 64, L"%.2f %%", st->finRatePct);
      else _snwprintf(val, 64, L"%d 回 (%d年%d月)", st->finMonths, st->finMonths / 12, st->finMonths % 12);
      val[63] = L'\0';
      c.drawText(240, ry, 364, 40, LiliumUI::kColTealDeep, val, 2, LiliumUI::Align::Right, true);
    }

    int n = clampInt(st->finMonths, 1, 480);
    double mr = (st->finRatePct / 100.0) / 12.0;
    double pmt = 0.0;
    if (mr < 1e-12) {
      pmt = st->finPrincipal / static_cast<double>(n);
    } else {
      double f = pow(1.0 + mr, static_cast<double>(n));
      pmt = st->finPrincipal * (mr * f) / (f - 1.0);
    }
    pmt = applyRounding(pmt, st->roundMode, st->decMode);
    double totalPay = applyRounding(pmt * static_cast<double>(n), st->roundMode, st->decMode);
    double totalInt = applyRounding(totalPay - st->finPrincipal, st->roundMode, st->decMode);

    c.drawBox(22, 276, 596, 148, 0xEBF3F0, LiliumUI::kColTealDark);
    wchar_t pStr[32], totStr[32], intStr[32], l1[96], l2[96];
    formatAccounting(pmt, st->roundMode, st->decMode, false, pStr, 32);
    formatAccounting(totalPay, st->roundMode, st->decMode, false, totStr, 32);
    formatAccounting(totalInt, st->roundMode, st->decMode, false, intStr, 32);
    _snwprintf(l1, 96, L"総返済額: %ls   (利息合計: %ls)", totStr, intStr);
    l1[95] = L'\0';
    _snwprintf(l2, 96, L"毎月返済: %ls", pStr);
    l2[95] = L'\0';
    c.drawText(34, 290, 572, 28, LiliumUI::kColTealDeep, L"■ 元利均等返済 シミュレーション", 2, LiliumUI::Align::Left, true);
    c.drawText(34, 326, 572, 28, LiliumUI::kColText, l1, 2, LiliumUI::Align::Left, true);
    c.drawText(34, 368, 572, 42, LiliumUI::kColCharcoal, l2, 3, LiliumUI::Align::Right, true);
  }
}

static void renderSettingsModal(LiliumUI::Canvas & c, const CashCalcState * st) {
  c.darkenScreen();
  const int mx = 40, my = 52, mw = 560, mh = 376;
  c.drawBox(mx, my, mw, mh, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawThickFrame(mx, my, mw, mh, 2, LiliumUI::kColTealDark);
  c.fillRect(mx, my, mw, 36, LiliumUI::kColTeal);
  c.drawText(mx + 14, my, mw - 28, 36, LiliumUI::kColWhite,
             L"【 税率・端数処理・桁数設定 】", 2, LiliumUI::Align::Left, true);

  static constexpr const wchar_t * kRoundNames[4] = {
    L"F (浮動小数・丸めなし)", L"CUT (切捨て)", L"UP (切上げ)", L"5/4 (四捨五入)"
  };
  static constexpr const wchar_t * kDecNames[5] = {
    L"0 (整数)", L"2 (小数2桁)", L"3 (小数3桁)", L"4 (小数4桁)", L"ADD2 (自動小数2位)"
  };
  const wchar_t * lbs[5] = {
    L"標準税率 (%)", L"軽減税率 (%)", L"端数処理方式", L"小数桁指定", L"= 押下時 GT自動累計"
  };

  for (int r = 0; r < 5; ++r) {
    int ry = my + 50 + r * 52;
    bool sel = (st->setRow == r);
    c.drawBox(mx + 18, ry, mw - 36, 44,
              sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt,
              sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(mx + 28, ry, 210, 44, LiliumUI::kColTextSub, lbs[r], 2, LiliumUI::Align::Left, true);

    wchar_t val[64] = L"";
    if (r == 0) _snwprintf(val, 64, L"<  %.1f %%  >", st->taxRateStd);
    else if (r == 1) _snwprintf(val, 64, L"<  %.1f %%  >", st->taxRateRed);
    else if (r == 2) wcscpy(val, kRoundNames[st->roundMode]);
    else if (r == 3) wcscpy(val, kDecNames[st->decMode]);
    else wcscpy(val, st->gtAutoAccum ? L"ON (自動加算)" : L"OFF (加算しない)");
    val[63] = L'\0';
    c.drawText(mx + 240, ry, 294, 44, LiliumUI::kColTealDeep, val, 2, LiliumUI::Align::Right, true);
  }

  c.drawBox(mx + 190, my + 318, 180, 40, LiliumUI::kColTeal, LiliumUI::kColTealDark);
  c.drawText(mx + 190, my + 318, 180, 40, LiliumUI::kColWhite, L"閉じる ([Esc])", 2, LiliumUI::Align::Center, true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  CashCalcState * st = static_cast<CashCalcState *>(userData);

  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

  static constexpr const wchar_t * kModeTabs[kModeCount] = {
    L"1:実務電卓", L"2:原価売価", L"3:伝票集計", L"4:日数割賦"
  };
  wchar_t badge[40];
  _snwprintf(badge, 40, L"税%.1f%%/%.1f%%", st->taxRateStd, st->taxRateRed);
  badge[39] = L'\0';
  LiliumUI::renderTopHeader(c, L"LILIUM CASHCALC", badge);

  // Mode strip (y=44..70)
  for (int m = 0; m < kModeCount; ++m) {
    int tx = 12 + m * 155;
    bool act = (st->calcMode == m);
    c.drawBox(tx, 44, 151, 26,
              act ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              act ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(tx, 44, 151, 26,
               act ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
               kModeTabs[m], 2, LiliumUI::Align::Center, true);
  }

  if (st->calcMode == ModeStd) renderMode0StdCalc(c, st);
  else if (st->calcMode == ModeCostSell) renderMode1CostSell(c, st);
  else if (st->calcMode == ModeInvoice) renderMode2Invoice(c, st);
  else renderMode3Finance(c, st);

  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  const wchar_t * foot = (st->calcMode == ModeStd)
      ? L"[F1]税・端数設定 [F2]モード [F3]税込 [F4]税抜 [F5]税率"
      : (st->calcMode == ModeCostSell) ? L"[↑↓]項目 [Ent]逆算 [0-9/.]入力 [F1]設定 [F2]モード"
      : (st->calcMode == ModeInvoice)  ? L"[0-9]金額 [F5]税区分 [Ent]登録 [↑↓]選択 [Del]削除"
                                       : L"[F5]計算種別切替 [↑↓]項目 [←→]変更 [PgUp/Dn]大幅";
  c.drawText(10, 440, LiliumUI::kMainW - 20, 40, LiliumUI::kColTextSub, foot, 2, LiliumUI::Align::Left, true);

  wchar_t mStr[28], gStr[28], sbBody[384];
  formatAccounting(st->memVal, st->roundMode, st->decMode, false, mStr, 28);
  formatAccounting(st->gtVal, st->roundMode, st->decMode, false, gStr, 28);
  _snwprintf(sbBody, 384,
             L"【税設定】\n標準:%.1f%%\n軽減:%.1f%%\n【メモリ / GT】\nM : %ls\nGT: %ls\n【Brain配列】\nQ-P:1-0 ASDF:+-*/\nG:GT M:M+ X:%%\n[F1]:詳細設定",
             st->taxRateStd, st->taxRateRed, mStr, gStr);
  sbBody[383] = L'\0';
  LiliumUI::renderSidebar(c, kModeTabs[st->calcMode], L"経理電卓ガイド", sbBody,
                          L"[F1]税・端数設定", L"[F2]モード切替", L"戻　る", false);

  if (st->settingsOpen) {
    renderSettingsModal(c, st);
  }
}

static void executeKeypadAction(CashCalcState * st, int act) {
  if (act >= ActDigit0 && act <= ActDigit9) {
    char d[2] = { static_cast<char>('0' + act), '\0' };
    stdPressDigit(st, d);
    return;
  }
  switch (act) {
    case Act00: stdPressDigit(st, "00"); break;
    case ActDot: stdPressDigit(st, "."); break;
    case ActAdd: stdPressOp(st, OpAdd); break;
    case ActSub: stdPressOp(st, OpSub); break;
    case ActMul: stdPressOp(st, OpMul); break;
    case ActDiv: stdPressOp(st, OpDiv); break;
    case ActEq:  stdPressEquals(st); break;
    case ActPct: stdPressPercent(st); break;
    case ActBS:  stdBackspace(st); break;
    case ActSign: stdToggleSign(st); break;
    case ActC:   stdClearEntry(st); break;
    case ActAC:  stdAllClear(st); break;
    case ActTaxInc: stdApplyTax(st, true); break;
    case ActTaxExc: stdApplyTax(st, false); break;
    case ActTaxToggle:
      st->useReducedTax = !st->useReducedTax;
      saveConfig(st);
      break;
    case ActMPlus:  stdMemoryOp(st, 0); break;
    case ActMMinus: stdMemoryOp(st, 1); break;
    case ActMR:     stdMemoryOp(st, 2); break;
    case ActMC:     stdMemoryOp(st, 3); break;
    case ActGT:     stdMemoryOp(st, 4); break;
    default: break;
  }
}

static int digitFromKey(WPARAM vk) {
  if (vk >= '0' && vk <= '9') return static_cast<int>(vk - '0');
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<int>(vk - VK_NUMPAD0);
  // SHARP Brain top row Q..P -> 1..9, 0
  switch (vk) {
    case 'Q': return 1;
    case 'W': return 2;
    case 'E': return 3;
    case 'R': return 4;
    case 'T': return 5;
    case 'Y': return 6;
    case 'U': return 7;
    case 'I': return 8;
    case 'O': return 9;
    case 'P': return 0;
    default: return -1;
  }
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  CashCalcState * st = static_cast<CashCalcState *>(userData);

  if (st->settingsOpen) {
    if (vkCode == VK_ESCAPE || vkCode == VK_BACK || vkCode == VK_F1 || vkCode == VK_RETURN) {
      st->settingsOpen = false;
      saveConfig(st);
    } else if (vkCode == VK_UP) {
      st->setRow = (st->setRow + 4) % 5;
    } else if (vkCode == VK_DOWN) {
      st->setRow = (st->setRow + 1) % 5;
    } else if (vkCode == VK_LEFT || vkCode == VK_RIGHT || vkCode == VK_SPACE) {
      int dir = (vkCode == VK_LEFT) ? -1 : 1;
      if (st->setRow == 0) st->taxRateStd = clampDouble(st->taxRateStd + dir * 0.5, 0.0, 50.0);
      else if (st->setRow == 1) st->taxRateRed = clampDouble(st->taxRateRed + dir * 0.5, 0.0, 50.0);
      else if (st->setRow == 2) st->roundMode = (st->roundMode + 4 + dir) % 4;
      else if (st->setRow == 3) st->decMode = (st->decMode + 5 + dir) % 5;
      else st->gtAutoAccum = !st->gtAutoAccum;
      saveConfig(st);
    }
    st->window.requestRepaint(true);
    return;
  }

  if (vkCode == VK_F1) {
    st->settingsOpen = true;
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_F2) {
    st->calcMode = (st->calcMode + 1) % kModeCount;
    st->csTyping = false;
    st->invTyping = false;
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_HOME || vkCode == VK_F10) {
    saveConfig(st);
    st->window.requestQuit();
    return;
  }

  if (st->calcMode == ModeStd) {
    int dig = digitFromKey(vkCode);
    if (dig >= 0) {
      executeKeypadAction(st, dig);
      st->window.requestRepaint(true);
      return;
    }
    switch (vkCode) {
      case VK_OEM_PERIOD: case VK_DECIMAL: case 'L': executeKeypadAction(st, ActDot); break;
      case VK_SPACE: executeKeypadAction(st, Act00); break;
      case VK_ADD: case VK_OEM_PLUS: case 'A': executeKeypadAction(st, ActAdd); break;
      case VK_SUBTRACT: case VK_OEM_MINUS: case 'S': executeKeypadAction(st, ActSub); break;
      case VK_MULTIPLY: case 'D': executeKeypadAction(st, ActMul); break;
      case VK_DIVIDE: case VK_OEM_2: case 'F': executeKeypadAction(st, ActDiv); break;
      case VK_RETURN: executeKeypadAction(st, ActEq); break;
      case VK_BACK: executeKeypadAction(st, ActBS); break;
      case 'C': case VK_DELETE: executeKeypadAction(st, ActC); break;
      case VK_F9: executeKeypadAction(st, ActAC); break;
      case 'X': executeKeypadAction(st, ActPct); break;
      case 'G': executeKeypadAction(st, ActGT); break;
      case 'M': executeKeypadAction(st, ActMPlus); break;
      case 'N': executeKeypadAction(st, ActMMinus); break;
      case 'R': executeKeypadAction(st, ActMR); break;
      case 'K': executeKeypadAction(st, ActMC); break;
      case 'V': executeKeypadAction(st, ActSign); break;
      case VK_F3: executeKeypadAction(st, ActTaxInc); break;
      case VK_F4: executeKeypadAction(st, ActTaxExc); break;
      case VK_F5: executeKeypadAction(st, ActTaxToggle); break;
      case VK_ESCAPE:
        saveConfig(st);
        st->window.requestQuit();
        return;
      default: break;
    }
    st->window.requestRepaint(true);
    return;
  }

  if (st->calcMode == ModeCostSell) {
    auto commitCsEdit = [&]() {
      if (!st->csTyping) return;
      double v = atof(st->csBuf);
      if (st->csField == 0) st->csCost = v;
      else if (st->csField == 1) st->csSell = v;
      else st->csMargin = clampDouble(v, -999.0, 99.9);
      st->csTyping = false;
    };
    int dig = digitFromKey(vkCode);
    if (dig >= 0) {
      if (!st->csTyping) { strcpy(st->csBuf, "0"); st->csTyping = true; }
      char d[2] = { static_cast<char>('0' + dig), '\0' };
      stdAppendDigitStr(st->csBuf, d);
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_OEM_PERIOD || vkCode == VK_DECIMAL || vkCode == 'L') {
      if (!st->csTyping) { strcpy(st->csBuf, "0"); st->csTyping = true; }
      stdAppendDigitStr(st->csBuf, ".");
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_BACK && st->csTyping) {
      size_t n = strlen(st->csBuf);
      if (n <= 1) strcpy(st->csBuf, "0"); else st->csBuf[n - 1] = '\0';
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_UP) { commitCsEdit(); st->csField = (st->csField + 2) % 3; }
    else if (vkCode == VK_DOWN) { commitCsEdit(); st->csField = (st->csField + 1) % 3; }
    else if (vkCode == 'C' || vkCode == VK_DELETE) {
      strcpy(st->csBuf, "0");
      st->csTyping = false;
      if (st->csField == 0) st->csCost = 0.0;
      else if (st->csField == 1) st->csSell = 0.0;
      else st->csMargin = 0.0;
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      commitCsEdit();
      if (st->csField == 2) {
        st->csMargin = (fabs(st->csSell) > 1e-9) ? ((st->csSell - st->csCost) / st->csSell * 100.0) : 0.0;
      } else if (st->csField == 1) {
        double denom = 1.0 - st->csMargin / 100.0;
        st->csSell = (fabs(denom) > 1e-6) ? applyRounding(st->csCost / denom, st->roundMode, st->decMode) : 0.0;
      } else {
        st->csCost = applyRounding(st->csSell * (1.0 - st->csMargin / 100.0), st->roundMode, st->decMode);
      }
    } else if (vkCode == VK_ESCAPE) {
      saveConfig(st);
      st->window.requestQuit();
      return;
    }
    st->window.requestRepaint(true);
    return;
  }

  if (st->calcMode == ModeInvoice) {
    int dig = digitFromKey(vkCode);
    if (dig >= 0) {
      if (!st->invTyping) { strcpy(st->invInputBuf, "0"); st->invTyping = true; }
      char d[2] = { static_cast<char>('0' + dig), '\0' };
      stdAppendDigitStr(st->invInputBuf, d);
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_SPACE) {
      if (!st->invTyping) { strcpy(st->invInputBuf, "0"); st->invTyping = true; }
      stdAppendDigitStr(st->invInputBuf, "00");
      st->window.requestRepaint(true);
      return;
    }
    if (vkCode == VK_BACK) {
      size_t n = strlen(st->invInputBuf);
      if (n <= 1) strcpy(st->invInputBuf, "0"); else st->invInputBuf[n - 1] = '\0';
    } else if (vkCode == VK_F5 || vkCode == VK_TAB) {
      st->invTaxCat = (st->invTaxCat + 1) % 3;
    } else if (vkCode == VK_RETURN || vkCode == VK_F3) {
      double amt = atof(st->invInputBuf);
      if (amt > 0.0 && st->invCount < kMaxInvoiceItems) {
        st->invItems[st->invCount].amount = applyRounding(amt, st->roundMode, st->decMode);
        st->invItems[st->invCount].taxCat = st->invTaxCat;
        st->invSel = st->invCount;
        ++st->invCount;
        if (st->invSel >= st->invScroll + 5) st->invScroll = st->invSel - 4;
        strcpy(st->invInputBuf, "0");
        st->invTyping = false;
      }
    } else if (vkCode == VK_UP && st->invCount > 0) {
      st->invSel = (st->invSel + st->invCount - 1) % st->invCount;
      if (st->invSel < st->invScroll) st->invScroll = st->invSel;
      if (st->invSel >= st->invScroll + 5) st->invScroll = st->invSel - 4;
    } else if (vkCode == VK_DOWN && st->invCount > 0) {
      st->invSel = (st->invSel + 1) % st->invCount;
      if (st->invSel < st->invScroll) st->invScroll = st->invSel;
      if (st->invSel >= st->invScroll + 5) st->invScroll = st->invSel - 4;
    } else if (vkCode == VK_DELETE || vkCode == VK_F9) {
      if (st->invCount > 0 && st->invSel >= 0 && st->invSel < st->invCount) {
        for (int i = st->invSel + 1; i < st->invCount; ++i) st->invItems[i - 1] = st->invItems[i];
        --st->invCount;
        if (st->invSel >= st->invCount && st->invCount > 0) st->invSel = st->invCount - 1;
        if (st->invScroll > st->invSel) st->invScroll = st->invSel;
      }
    } else if (vkCode == 'C') {
      st->invCount = 0;
      st->invSel = 0;
      st->invScroll = 0;
      strcpy(st->invInputBuf, "0");
      st->invTyping = false;
    } else if (vkCode == VK_ESCAPE) {
      saveConfig(st);
      st->window.requestQuit();
      return;
    }
    st->window.requestRepaint(true);
    return;
  }

  // Mode 3: Finance
  if (vkCode == VK_F5) {
    st->finSubMode = (st->finSubMode + 1) % 2;
    st->finRow = 0;
    st->window.requestRepaint(true);
    return;
  }
  int maxRows = (st->finSubMode == 0) ? 5 : 3;
  if (vkCode == VK_UP) st->finRow = (st->finRow + maxRows - 1) % maxRows;
  else if (vkCode == VK_DOWN) st->finRow = (st->finRow + 1) % maxRows;
  else if (vkCode == VK_LEFT || vkCode == VK_RIGHT || vkCode == VK_PRIOR || vkCode == VK_NEXT) {
    int dir = (vkCode == VK_RIGHT || vkCode == VK_NEXT) ? 1 : -1;
    bool big = (vkCode == VK_PRIOR || vkCode == VK_NEXT);
    if (st->finSubMode == 0) {
      if (st->finRow == 0) { if (big) shiftDateMonths(st->startDate, dir); else shiftDateDays(st->startDate, dir); }
      else if (st->finRow == 1) { if (big) shiftDateMonths(st->endDate, dir); else shiftDateDays(st->endDate, dir); }
      else if (st->finRow == 2) st->bothEndsInclusive = !st->bothEndsInclusive;
      else if (st->finRow == 3) st->finPrincipal = clampDouble(st->finPrincipal + dir * (big ? 100000.0 : 10000.0), 0.0, 1e10);
      else st->finRatePct = clampDouble(st->finRatePct + dir * (big ? 1.0 : 0.1), 0.0, 50.0);
    } else {
      if (st->finRow == 0) st->finPrincipal = clampDouble(st->finPrincipal + dir * (big ? 100000.0 : 10000.0), 0.0, 1e10);
      else if (st->finRow == 1) st->finRatePct = clampDouble(st->finRatePct + dir * (big ? 1.0 : 0.1), 0.0, 50.0);
      else st->finMonths = clampInt(st->finMonths + dir * (big ? 12 : 1), 1, 480);
    }
  } else if (vkCode == VK_ESCAPE) {
    saveConfig(st);
    st->window.requestQuit();
    return;
  }
  st->window.requestRepaint(true);
}

static void onClick(int x, int y, void * userData) {
  CashCalcState * st = static_cast<CashCalcState *>(userData);

  if (st->settingsOpen) {
    const int mx = 40, my = 52;
    if (y >= my + 318 && y < my + 358 && x >= mx + 190 && x < mx + 370) {
      st->settingsOpen = false;
      saveConfig(st);
      st->window.requestRepaint(true);
      return;
    }
    for (int r = 0; r < 5; ++r) {
      int ry = my + 50 + r * 52;
      if (x >= mx + 18 && x < mx + 542 && y >= ry && y < ry + 44) {
        st->setRow = r;
        onKeyDown(VK_RIGHT, false, userData);
        return;
      }
    }
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    saveConfig(st);
    st->window.requestQuit();
    return;
  }

  // Mode strip
  if (y >= 44 && y < 70) {
    for (int m = 0; m < kModeCount; ++m) {
      int tx = 12 + m * 155;
      if (x >= tx && x < tx + 151) {
        st->calcMode = m;
        st->csTyping = false;
        st->invTyping = false;
        st->window.requestRepaint(true);
        return;
      }
    }
  }

  if (st->calcMode == ModeStd) {
    for (int r = 0; r < 5; ++r) {
      int ky = 198 + r * 48;
      if (y >= ky && y < ky + 43) {
        for (int col = 0; col < 6; ++col) {
          int kx = 12 + col * 103;
          if (x >= kx && x < kx + 99) {
            executeKeypadAction(st, kKeypad[r][col].actionId);
            st->window.requestRepaint(true);
            return;
          }
        }
      }
    }
  } else if (st->calcMode == ModeCostSell) {
    for (int i = 0; i < 3; ++i) {
      int ry = 84 + i * 56;
      if (y >= ry && y < ry + 48 && x >= 22 && x < 618) {
        st->csField = i;
        st->csTyping = false;
        st->window.requestRepaint(true);
        return;
      }
    }
    if (y >= 378 && y < 420 && x >= 24 && x < 304) {
      onKeyDown(VK_RETURN, false, userData);
    }
  } else if (st->calcMode == ModeInvoice) {
    if (y >= 74 && y < 116) {
      if (x >= 328 && x < 478) { onKeyDown(VK_F5, false, userData); return; }
      if (x >= 484 && x < 628) { onKeyDown(VK_RETURN, false, userData); return; }
    }
  } else if (st->calcMode == ModeFinance) {
    if (y >= 74 && y < 104) {
      for (int m = 0; m < 2; ++m) {
        int bx = 12 + m * 310;
        if (x >= bx && x < bx + 304) {
          st->finSubMode = m;
          st->finRow = 0;
          st->window.requestRepaint(true);
          return;
        }
      }
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  CashCalcState * st = static_cast<CashCalcState *>(userData);
  SYSTEMTIME tm;
  GetLocalTime(&tm);
  int bat = 100;
  bool chg = false;
  LiliumUI::queryBattery(bat, chg);
  if (tm.wMinute != st->lastMin || bat != st->lastBat) {
    st->lastMin = tm.wMinute;
    st->lastBat = bat;
    static constexpr const wchar_t * kModeNames[kModeCount] = {
      L"1:実務電卓", L"2:原価売価", L"3:伝票集計", L"4:日数割賦"
    };
    LiliumUI::renderSidebarStatus(c, kModeNames[st->calcMode]);
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * rawMem = calloc(1, sizeof(CashCalcState));
  if (!rawMem) return 1;
  CashCalcState * state = new (rawMem) CashCalcState();
  state->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  wchar_t exeDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  LiliumUI::joinPath(exeDir, L"cashcalc.cfg", state->cfgPath, MAX_PATH);
  loadConfig(state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;

  int rc = 1;
  if (state->window.create(L"LiliumCashCalcWinClass",
                           L"Lilium CashCalc (SHARP Brain 800x480)",
                           cb, state, state->manageTaskBar)) {
    rc = state->window.runLoop();
  }

  saveConfig(state);
  state->~CashCalcState();
  free(rawMem);
  return rc;
}
