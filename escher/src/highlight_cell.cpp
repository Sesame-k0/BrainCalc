#include <escher/highlight_cell.h>

extern "C" void BrainClearHelp();

HighlightCell::HighlightCell() :
  View(),
  m_highlighted(false)
{
}

void HighlightCell::setHighlighted(bool highlight) {
  if (m_highlighted != highlight) {
    m_highlighted = highlight;
    if (!highlight) {
      BrainClearHelp();
    }
    reloadCell();
  }
}

void HighlightCell::reloadCell() {
  markRectAsDirty(bounds());
}
