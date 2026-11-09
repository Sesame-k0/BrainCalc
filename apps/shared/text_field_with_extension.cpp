#include "text_field_with_extension.h"

namespace Shared {

void TextFieldWithExtension::willSetCursorLocation(const char * * location) {
  const char * current = text();
  const char * dot = strrchr(current, '.');
  const char * maxLocation = dot ? dot : (m_contentView.editedText() + strlen(current));
  if (*location < m_contentView.editedText()) {
    *location = m_contentView.editedText();
  }
  if (*location > maxLocation) {
    *location = maxLocation;
  }
}

bool TextFieldWithExtension::privateRemoveEndOfLine() {
  return removeTextBeforeExtension(false);
}

void TextFieldWithExtension::removeWholeText() {
  removeTextBeforeExtension(true);
  scrollToCursor();
}

bool TextFieldWithExtension::removeTextBeforeExtension(bool whole) {
  assert(isEditing());
  const char * current = text();
  const char * dot = strrchr(current, '.');
  const char * extension = dot ? dot : (m_contentView.editedText() + strlen(current));
  assert(extension >= m_contentView.editedText() && extension < m_contentView.editedText() + ContentView::k_maxBufferSize);
  char * destination = const_cast<char *>(whole ? m_contentView.editedText() : cursorLocation());
  if (destination >= extension) {
    return false;
  }
  assert(destination >= m_contentView.editedText());
  assert(destination < extension);
  m_contentView.willModifyTextBuffer();
  strlcpy(destination, extension, ContentView::k_maxBufferSize - (destination - m_contentView.editedText()));
  m_contentView.setCursorLocation(destination);
  m_contentView.didModifyTextBuffer();
  layoutSubviews();
  return true;
}

}
