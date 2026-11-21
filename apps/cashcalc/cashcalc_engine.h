#ifndef CASHCALC_ENGINE_H
#define CASHCALC_ENGINE_H

#include <stddef.h>
#include <stdint.h>

namespace CashCalc {

static constexpr int kModeCount = 4;
static constexpr int kMaxInvoiceItems = 24;
static constexpr double kMaxDisplayAbs = 999999999999.0; // 12 digits max

enum CalcMode : uint8_t {
  ModeCalc = 0,
  ModeMargin = 1,
  ModeInvoice = 2,
  ModeLoan = 3
};

enum RoundMode : uint8_t {
  RoundFloat = 0, // F
  RoundCut = 1,   // CUT
  RoundUp = 2,    // UP
  RoundHalf = 3   // 5/4
};

enum DecMode : uint8_t {
  Dec0 = 0,
  Dec2 = 1,
  Dec3 = 2,
  Dec4 = 3,
  DecAdd2 = 4
};

enum PendingOp : uint8_t {
  OpNone = 0,
  OpAdd,
  OpSub,
  OpMul,
  OpDiv
};

struct InvoiceItem {
  double amount;
  uint8_t taxCat; // 0=Std, 1=Red, 2=Exempt
};

struct DateYMD {
  int16_t y;
  int8_t m;
  int8_t d;
};

struct EngineState {
  uint8_t mode;
  bool settingsOpen;
  uint8_t setRow;

  // Tax & rounding configuration
  int16_t taxRateStdTenths; // e.g. 100 -> 10.0%
  int16_t taxRateRedTenths; // e.g. 80  -> 8.0%
  bool useReducedTax;
  uint8_t roundMode;
  uint8_t decMode;
  bool gtAutoAccum;

  // Mode 0: Desktop Accounting Calculator
  double acc;
  double curVal;
  double memVal;
  double gtVal;
  uint8_t pendingOp;
  bool entering;
  bool afterEquals;
  bool gtRecalledOnce;
  bool kLocked;
  uint8_t kOp;
  double kVal;
  bool errorState;
  char inputBuf[22];
  char subDisplay[48];
  char tapeLines[2][44];

  // Mode 1: Cost / Sell / Margin
  uint8_t csField; // 0=Cost, 1=Sell, 2=Margin%
  double csCost;
  double csSell;
  double csMargin;
  char csBuf[22];
  bool csTyping;

  // Mode 2: Invoice Tax Aggregator
  InvoiceItem invItems[kMaxInvoiceItems];
  uint8_t invCount;
  uint8_t invSel;
  uint8_t invScroll;
  uint8_t invTaxCat;
  char invInputBuf[22];
  bool invTyping;

  // Mode 3: Days / Simple Interest & Annuity Loan
  uint8_t finSubMode; // 0=Days+SimpleInt, 1=Annuity
  uint8_t finRow;
  DateYMD startDate;
  DateYMD endDate;
  bool bothEndsInclusive;
  double finPrincipal;
  int16_t finRateTenths; // e.g. 25 -> 2.5%
  int16_t finMonths;
};

void initDefaults(EngineState & st);

int decPlacesForMode(uint8_t decMode);
double applyRounding(double val, uint8_t roundMode, uint8_t decMode);
double parseAsciiDouble(const char * s);
void formatCommasRaw(const char * rawNum, char * out, size_t maxLen);
void formatAccounting(double val, uint8_t roundMode, uint8_t decMode, bool forceDecimal,
                      char * out, size_t maxLen);
void formatFixedDec(double val, int decimals, char * out, size_t maxLen);
void formatTenthsPct(int tenths, char * out, size_t maxLen);
void formatYMD(const DateYMD & dt, char * out, size_t maxLen);

double activeTaxRate(const EngineState & st);
double taxRateStd(const EngineState & st);
double taxRateRed(const EngineState & st);
const char * roundModeLabel(uint8_t r);
const char * decModeLabel(uint8_t d);

// Mode 0 operations
void stdClearEntry(EngineState & st);
void stdAllClear(EngineState & st);
void stdPressDigit(EngineState & st, const char * digits);
void stdBackspace(EngineState & st);
void stdToggleSign(EngineState & st);
void stdPressOp(EngineState & st, uint8_t newOp);
void stdPressEquals(EngineState & st);
void stdPressPercent(EngineState & st);
void stdApplyTax(EngineState & st, bool includeTax);
void stdToggleTaxRate(EngineState & st);
void stdGT(EngineState & st);
void stdMemoryOp(EngineState & st, int which); // 0=M+, 1=M-, 2=MR, 3=MC

// Mode 1 operations
void csCommitEdit(EngineState & st);
void csPressDigit(EngineState & st, const char * d);
void csBackspace(EngineState & st);
void csZeroField(EngineState & st);
void csSolveField(EngineState & st);
double csProfit(const EngineState & st);
double csMarkupPct(const EngineState & st);
double csRealMarginPct(const EngineState & st);
double csSellWithTax(const EngineState & st);

// Mode 2 operations
void invPressDigit(EngineState & st, const char * d);
void invBackspace(EngineState & st);
void invCycleTaxCat(EngineState & st);
void invAddItem(EngineState & st);
void invDeleteSelectedOrClear(EngineState & st);
void invMoveSelection(EngineState & st, int delta);
void invComputeTotals(const EngineState & st,
                      double & sub0, double & tax0,
                      double & sub1, double & tax1,
                      double & sub2, double & grand);

// Mode 3 operations
void shiftDateDays(DateYMD & dt, int deltaDays);
void shiftDateMonths(DateYMD & dt, int deltaMonths);
long finElapsedDays(const EngineState & st);
double finSimpleInterest(const EngineState & st);
double finSimpleTotal(const EngineState & st);
double finAnnuityMonthly(const EngineState & st);
double finAnnuityTotalPay(const EngineState & st);
double finAnnuityTotalInt(const EngineState & st);

} // namespace CashCalc

#endif // CASHCALC_ENGINE_H
