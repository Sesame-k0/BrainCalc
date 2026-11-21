#include "cashcalc_controller.h"
#include "app.h"
#include "apps/apps_container.h"

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);
extern "C" void BrainClearCustomHelp();

namespace CashCalc {

static int digitFromEvent(Ion::Events::Event e) {
  if (e == Ion::Events::Zero) return 0;
  if (e == Ion::Events::One) return 1;
  if (e == Ion::Events::Two) return 2;
  if (e == Ion::Events::Three) return 3;
  if (e == Ion::Events::Four) return 4;
  if (e == Ion::Events::Five) return 5;
  if (e == Ion::Events::Six) return 6;
  if (e == Ion::Events::Seven) return 7;
  if (e == Ion::Events::Eight) return 8;
  if (e == Ion::Events::Nine) return 9;
  return -1;
}

CashCalcController::CashCalcController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_view(snapshot)
{
}

void CashCalcController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_snapshot->loadFromStorage();
  updateBrainHelp();
  m_view.markDirty();
}

void CashCalcController::viewDidDisappear() {
  ViewController::viewDidDisappear();
  m_snapshot->saveToStorage();
  BrainClearCustomHelp();
}

void CashCalcController::updateBrainHelp() {
  const EngineState & st = m_snapshot->engine();
  if (st.settingsOpen) {
    BrainNotifyCustomHelp(
      "税率・端数設定",
      "【上下】項目選択\n"
      "【左右】値変更\n"
      "【F1/OK】保存して閉じる\n\n"
      "標準税率・軽減税率を\n"
      "0.5%単位で調整可能。\n"
      "端数: F/切捨/切上/5/4\n"
      "桁: 0/2/3/4/ADD2");
    return;
  }

  switch (st.mode) {
    case ModeCalc:
      BrainNotifyCustomHelp(
        "経理実務電卓",
        "【PgUp/Dn】モード切替\n"
        "【F1】税率・端数設定\n"
        "【H/F8】税込へ変換\n"
        "【Z】税抜へ変換\n"
        "【C】標準/軽減税切替\n"
        "【K/F9】GT累計・クリア\n"
        "【L】M+  【V】M-\n"
        "【B】MR/MC 【X】%\n"
        "【Spc】00 【F4】C/AC");
      break;
    case ModeMargin:
      BrainNotifyCustomHelp(
        "原価・売価・利益",
        "【上下】入力項目移動\n"
        "【0-9/.】数値入力\n"
        "【OK/EXE】選択項目を逆算\n"
        "【F4】選択項目を0に\n"
        "【PgUp/Dn】モード切替\n\n"
        "原価と利益率から売価、\n"
        "原価と売価から粗利率を\n"
        "瞬時に自動算出します。");
      break;
    case ModeInvoice:
      BrainNotifyCustomHelp(
        "伝票・区分別集計",
        "【0-9】税抜金額入力\n"
        "【C/F9】税区分を切替\n"
        "  (標準 / 軽減 / 非課税)\n"
        "【OK/EXE】明細へ登録\n"
        "【上下】明細選択\n"
        "【F4】選択行を削除\n\n"
        "インボイス制度準拠で\n"
        "区分ごとに税額算出。");
      break;
    case ModeLoan:
    default:
      BrainNotifyCustomHelp(
        st.finSubMode == 0 ? "日数・単利計算" : "割賦・均等返済",
        st.finSubMode == 0
          ? "【F9】割賦返済へ切替\n"
            "【上下】項目を選択\n"
            "【左右】1日/微調整\n"
            "【+/-】1月/大幅変更\n\n"
            "片端・両端入れと\n"
            "365日単利利息を算出。"
          : "【F9】日数計算へ切替\n"
            "【上下】項目を選択\n"
            "【左右】微調整\n"
            "【+/-】大幅変更\n\n"
            "元利均等払いの毎月返済\n"
            "額と総利息を試算します。");
      break;
  }
}

bool CashCalcController::handleEvent(Ion::Events::Event event) {
  EngineState & st = m_snapshot->engine();

  if (st.settingsOpen) {
    if (event == Ion::Events::Toolbox || event == Ion::Events::Back ||
        event == Ion::Events::OK || event == Ion::Events::EXE) {
      st.settingsOpen = false;
      m_snapshot->saveToStorage();
      updateBrainHelp();
      m_view.markDirty();
      return true;
    }
    if (event == Ion::Events::Up) {
      st.setRow = (st.setRow + 4) % 5;
      m_view.markDirty();
      return true;
    }
    if (event == Ion::Events::Down) {
      st.setRow = (st.setRow + 1) % 5;
      m_view.markDirty();
      return true;
    }
    if (event == Ion::Events::Left || event == Ion::Events::Right) {
      int dir = (event == Ion::Events::Right) ? 1 : -1;
      if (st.setRow == 0) {
        int next = st.taxRateStdTenths + dir * 5;
        if (next < 0) next = 0;
        if (next > 500) next = 500;
        st.taxRateStdTenths = static_cast<int16_t>(next);
      } else if (st.setRow == 1) {
        int next = st.taxRateRedTenths + dir * 5;
        if (next < 0) next = 0;
        if (next > 500) next = 500;
        st.taxRateRedTenths = static_cast<int16_t>(next);
      } else if (st.setRow == 2) {
        st.roundMode = (st.roundMode + 4 + dir) % 4;
      } else if (st.setRow == 3) {
        st.decMode = (st.decMode + 5 + dir) % 5;
      } else {
        st.gtAutoAccum = !st.gtAutoAccum;
      }
      m_snapshot->saveToStorage();
      m_view.markDirty();
      return true;
    }
    return true;
  }

  // Global CashCalc shortcuts
  if (event == Ion::Events::Toolbox) {
    st.settingsOpen = true;
    updateBrainHelp();
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LeftParenthesis || event == Ion::Events::RightParenthesis) {
    int dir = (event == Ion::Events::RightParenthesis) ? 1 : -1;
    st.mode = (st.mode + kModeCount + dir) % kModeCount;
    st.csTyping = false;
    st.invTyping = false;
    updateBrainHelp();
    m_view.markDirty();
    return true;
  }

  if (st.mode == ModeCalc) {
    int dig = digitFromEvent(event);
    if (dig >= 0) {
      char d[2] = { static_cast<char>('0' + dig), '\0' };
      stdPressDigit(st, d);
    } else if (event == Ion::Events::Dot) {
      stdPressDigit(st, ".");
    } else if (event == Ion::Events::Space) {
      stdPressDigit(st, "00");
    } else if (event == Ion::Events::Backspace) {
      stdBackspace(st);
    } else if (event == Ion::Events::Plus) {
      stdPressOp(st, OpAdd);
    } else if (event == Ion::Events::Minus) {
      stdPressOp(st, OpSub);
    } else if (event == Ion::Events::Multiplication) {
      stdPressOp(st, OpMul);
    } else if (event == Ion::Events::Division) {
      stdPressOp(st, OpDiv);
    } else if (event == Ion::Events::Equal || event == Ion::Events::OK || event == Ion::Events::EXE) {
      stdPressEquals(st);
    } else if (event == Ion::Events::Power || event == Ion::Events::ShiftEXE) {
      stdApplyTax(st, true);
    } else if (event == Ion::Events::Sqrt) {
      stdApplyTax(st, false);
    } else if (event == Ion::Events::Imaginary) {
      stdToggleTaxRate(st);
    } else if (event == Ion::Events::Ans || event == Ion::Events::Var) {
      stdGT(st);
    } else if (event == Ion::Events::Sto) {
      stdMemoryOp(st, 0); // M+
    } else if (event == Ion::Events::Pi) {
      stdMemoryOp(st, 1); // M-
    } else if (event == Ion::Events::Log) {
      static int sMemToggle = 0;
      stdMemoryOp(st, (sMemToggle++ & 1) == 0 ? 2 : 3); // MR -> MC
    } else if (event == Ion::Events::XNT) {
      stdPressPercent(st);
    } else if (event == Ion::Events::Comma) {
      stdToggleSign(st);
    } else if (event == Ion::Events::Clear) {
      if (st.entering && strcmp(st.inputBuf, "0") != 0) stdClearEntry(st);
      else stdAllClear(st);
    } else if (event == Ion::Events::Left) {
      st.mode = (st.mode + kModeCount - 1) % kModeCount;
      updateBrainHelp();
    } else if (event == Ion::Events::Right) {
      st.mode = (st.mode + 1) % kModeCount;
      updateBrainHelp();
    } else {
      return false;
    }
    m_view.markDirty();
    return true;
  }

  if (st.mode == ModeMargin) {
    int dig = digitFromEvent(event);
    if (dig >= 0) {
      char d[2] = { static_cast<char>('0' + dig), '\0' };
      csPressDigit(st, d);
    } else if (event == Ion::Events::Dot) {
      csPressDigit(st, ".");
    } else if (event == Ion::Events::Backspace) {
      csBackspace(st);
    } else if (event == Ion::Events::Up) {
      csCommitEdit(st);
      st.csField = (st.csField + 2) % 3;
    } else if (event == Ion::Events::Down) {
      csCommitEdit(st);
      st.csField = (st.csField + 1) % 3;
    } else if (event == Ion::Events::Clear) {
      csZeroField(st);
    } else if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      csSolveField(st);
    } else if (event == Ion::Events::Left) {
      csCommitEdit(st);
      st.mode = (st.mode + kModeCount - 1) % kModeCount;
      updateBrainHelp();
    } else if (event == Ion::Events::Right) {
      csCommitEdit(st);
      st.mode = (st.mode + 1) % kModeCount;
      updateBrainHelp();
    } else {
      return false;
    }
    m_view.markDirty();
    return true;
  }

  if (st.mode == ModeInvoice) {
    int dig = digitFromEvent(event);
    if (dig >= 0) {
      char d[2] = { static_cast<char>('0' + dig), '\0' };
      invPressDigit(st, d);
    } else if (event == Ion::Events::Space) {
      invPressDigit(st, "00");
    } else if (event == Ion::Events::Backspace) {
      invBackspace(st);
    } else if (event == Ion::Events::Var || event == Ion::Events::Imaginary) {
      invCycleTaxCat(st);
    } else if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      invAddItem(st);
    } else if (event == Ion::Events::Up) {
      invMoveSelection(st, -1);
    } else if (event == Ion::Events::Down) {
      invMoveSelection(st, 1);
    } else if (event == Ion::Events::Clear) {
      invDeleteSelectedOrClear(st);
    } else if (event == Ion::Events::Left) {
      st.mode = (st.mode + kModeCount - 1) % kModeCount;
      updateBrainHelp();
    } else if (event == Ion::Events::Right) {
      st.mode = (st.mode + 1) % kModeCount;
      updateBrainHelp();
    } else {
      return false;
    }
    m_view.markDirty();
    return true;
  }

  // Mode 3: Days & Loan
  if (event == Ion::Events::Var || event == Ion::Events::OK || event == Ion::Events::EXE) {
    st.finSubMode = (st.finSubMode + 1) % 2;
    st.finRow = 0;
    updateBrainHelp();
    m_view.markDirty();
    return true;
  }
  int maxRows = (st.finSubMode == 0) ? 5 : 3;
  if (event == Ion::Events::Up) {
    st.finRow = (st.finRow + maxRows - 1) % maxRows;
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Down) {
    st.finRow = (st.finRow + 1) % maxRows;
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Left || event == Ion::Events::Right ||
      event == Ion::Events::Plus || event == Ion::Events::Minus) {
    int dir = (event == Ion::Events::Right || event == Ion::Events::Plus) ? 1 : -1;
    bool big = (event == Ion::Events::Plus || event == Ion::Events::Minus);
    if (st.finSubMode == 0) {
      if (st.finRow == 0) { if (big) shiftDateMonths(st.startDate, dir); else shiftDateDays(st.startDate, dir); }
      else if (st.finRow == 1) { if (big) shiftDateMonths(st.endDate, dir); else shiftDateDays(st.endDate, dir); }
      else if (st.finRow == 2) st.bothEndsInclusive = !st.bothEndsInclusive;
      else if (st.finRow == 3) {
        st.finPrincipal += dir * (big ? 100000.0 : 10000.0);
        if (st.finPrincipal < 0.0) st.finPrincipal = 0.0;
        if (st.finPrincipal > 1e10) st.finPrincipal = 1e10;
      } else {
        int nr = st.finRateTenths + dir * (big ? 10 : 1);
        if (nr < 0) nr = 0;
        if (nr > 500) nr = 500;
        st.finRateTenths = static_cast<int16_t>(nr);
      }
    } else {
      if (st.finRow == 0) {
        st.finPrincipal += dir * (big ? 100000.0 : 10000.0);
        if (st.finPrincipal < 0.0) st.finPrincipal = 0.0;
        if (st.finPrincipal > 1e10) st.finPrincipal = 1e10;
      } else if (st.finRow == 1) {
        int nr = st.finRateTenths + dir * (big ? 10 : 1);
        if (nr < 0) nr = 0;
        if (nr > 500) nr = 500;
        st.finRateTenths = static_cast<int16_t>(nr);
      } else {
        int nm = st.finMonths + dir * (big ? 12 : 1);
        if (nm < 1) nm = 1;
        if (nm > 480) nm = 480;
        st.finMonths = static_cast<int16_t>(nm);
      }
    }
    m_view.markDirty();
    return true;
  }

  return false;
}

} // namespace CashCalc
