#ifndef KHYCAS_VIEW_H
#define KHYCAS_VIEW_H

#include <escher/view.h>

namespace Khycas {

class KhycasView : public View {
public:
  KhycasView();
  void drawRect(KDContext * ctx, KDRect rect) const override;
  void setSampleResults(const char * expr1, const char * ans1, const char * expr2, const char * ans2);
private:
  char m_expr1[64];
  char m_ans1[128];
  char m_expr2[64];
  char m_ans2[128];
};

}

#endif
