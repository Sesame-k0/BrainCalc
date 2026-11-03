#include "llcubic_guide_controller.h"
#include <escher/palette.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Settings {

static const char * const k_guideLines[] = {
  "【LLCubic 3D立体描画ガイド】",
  "[概要] 3次元空間に数式や立体を描画・観察",
  "[視点回転] 矢印キー(↑↓←→)で360°回転",
  "[ズーム] PgUp/PgDn (+/-) で拡大・縮小",
  "[正面視] 1:X正面 2:Y正面 3:Z真上 4:斜め",
  "[表示切替] Space: 面描/線描切替",
  "[式入力] z=f(x,y) や X,Y,Z(u,v) を入力",
  "  XNTキーで xとy (またはuとv) を交互入力",
  "  sin, cos, sqrt, ^2 などの関数も対応",
  "[3Dトレース] 6 または Tキーで座標値を直読",
  "[断面スライス] 7キーで立体を輪切り切断",
  "[範囲設定] 5 または AキーでAxes範囲変更",
  "[視点リセット] Enterキーで初期位置に戻る",
  "[立体追加] Expressionsタブで「+」を選択"
};

LLCubicGuideController::LLCubicGuideController(Responder * parentResponder) :
  GenericSubController(parentResponder)
{
}

void LLCubicGuideController::viewWillAppear() {
  GenericSubController::viewWillAppear();
  m_selectableTableView.selectCellAtLocation(0, 0);
  m_selectableTableView.reloadData();
  BrainNotifyHelpText("Plot3D_View", nullptr);
}

HighlightCell * LLCubicGuideController::reusableCell(int index, int type) {
  assert(index >= 0 && index < k_numberOfGuideRows);
  return &m_cells[index];
}

int LLCubicGuideController::reusableCellCount(int type) {
  return k_numberOfGuideRows;
}

void LLCubicGuideController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);
  if (index >= 0 && index < k_numberOfGuideRows) {
    myCell->setText(k_guideLines[index]);
    if (index == 0) {
      myCell->setTextColor(Palette::YellowDark);
    } else if (index == 6 || index == 7 || index == 8) {
      myCell->setTextColor(Palette::PrimaryText);
    } else {
      myCell->setTextColor(KDColorBlack);
    }
  }
}

bool LLCubicGuideController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Back || event == Ion::Events::Clear || event == Ion::Events::Backspace) {
    stackController()->pop();
    return true;
  }
  return GenericSubController::handleEvent(event);
}

} // namespace Settings
