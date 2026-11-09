#include "python_text_area.h"
#include "app.h"
#include <escher/palette.h>
#include <ion/unicode/utf8_helper.h>
#include "../global_preferences.h"
#include <ctype.h>
#include <stdlib.h>
#include <algorithm>

namespace Code {

constexpr KDColor CommentColor = Palette::CodeComment;
constexpr KDColor NumberColor =  Palette::CodeNumber;
constexpr KDColor KeywordColor = Palette::CodeKeyword;
constexpr KDColor OperatorColor = Palette::CodeOperator;
constexpr KDColor StringColor = Palette::CodeString;
constexpr KDColor BackgroundColor = Palette::CodeBackground;
constexpr KDColor HighlightColor = Palette::CodeBackgroundSelected;
constexpr KDColor AutocompleteColor = KDColor::RGB24(0xC6C6C6);

const char * PythonTextArea::ContentView::textToAutocomplete() const {
  return nullptr;
}

void PythonTextArea::ContentView::loadSyntaxHighlighter() {
}

void PythonTextArea::ContentView::unloadSyntaxHighlighter() {
}

void PythonTextArea::ContentView::clearRect(KDContext * ctx, KDRect rect) const {
  ctx->fillRect(rect, BackgroundColor);
}

PythonTextArea::AutocompletionType PythonTextArea::autocompletionType(const char * autocompletionLocation, const char ** autocompletionLocationBeginning, const char ** autocompletionLocationEnd) const {
  const char * location = (autocompletionLocation == nullptr) ? m_contentView.cursorLocation() : autocompletionLocation;
  const char * text = m_contentView.editedText();
  if (!text || location <= text) {
    if (autocompletionLocationBeginning) *autocompletionLocationBeginning = location;
    if (autocompletionLocationEnd) *autocompletionLocationEnd = location;
    return AutocompletionType::NoIdentifier;
  }
  const char * p = location;
  while (p > text && (isalnum((unsigned char)*(p - 1)) || *(p - 1) == '_')) {
    p--;
  }
  if (autocompletionLocationBeginning) *autocompletionLocationBeginning = p;
  if (autocompletionLocationEnd) *autocompletionLocationEnd = location;
  if (p == location) {
    return AutocompletionType::NoIdentifier;
  }
  if (isalnum((unsigned char)*location) || *location == '_') {
    return AutocompletionType::MiddleOfIdentifier;
  }
  return AutocompletionType::EndOfIdentifier;
}

void PythonTextArea::ContentView::drawLine(KDContext * ctx, int line, const char * text, size_t byteLength, int fromColumn, int toColumn, const char * selectionStart, const char * selectionEnd) const {
  drawStringAt(
      ctx,
      line,
      0,
      text,
      byteLength,
      Palette::CodeText,
      BackgroundColor,
      selectionStart,
      selectionEnd,
      HighlightColor,
      false);
}

KDRect PythonTextArea::ContentView::dirtyRectFromPosition(const char * position, bool includeFollowingLines) const {
  /* Mark the whole line as dirty.
   * TextArea has a very conservative approach and only dirties the surroundings
   * of the current character. That works for plain text, but when doing syntax
   * highlighting, you may want to redraw the surroundings as well. For example,
   * if editing "def foo" into "df foo", you'll want to redraw "df". */
  KDRect baseDirtyRect = TextArea::ContentView::dirtyRectFromPosition(position, includeFollowingLines);
  return KDRect(
    bounds().x(),
    baseDirtyRect.y(),
    bounds().width(),
    baseDirtyRect.height()
  );
}

bool PythonTextArea::handleEvent(Ion::Events::Event event) {
  if (m_contentView.isAutocompleting()) {
    // Handle event with autocompletion
    if (event == Ion::Events::Right
        || event == Ion::Events::ShiftRight
        || event == Ion::Events::OK)
    {
      m_contentView.reloadRectFromPosition(m_contentView.cursorLocation(), false);
      acceptAutocompletion(event != Ion::Events::ShiftRight);
      if (event != Ion::Events::ShiftRight) {
        // Do not process the event more
        scrollToCursor();
        return true;
      }
    } else if (event == Ion::Events::Toolbox
        || event == Ion::Events::Var
        || event == Ion::Events::Shift
        || event == Ion::Events::Alpha
        || event == Ion::Events::OnOff)
    {
    } else if(event == Ion::Events::Up
        || event == Ion::Events::Down)
    {
      cycleAutocompletion(event == Ion::Events::Down);
      return true;
    } else {
      removeAutocompletion();
      m_contentView.reloadRectFromPosition(m_contentView.cursorLocation(), false);
      if (event == Ion::Events::Back) {
        // Do not process the event more
        return true;
      }
    }
  }
  bool result = TextArea::handleEvent(event);
  if (event == Ion::Events::Backspace && !m_contentView.isAutocompleting() && selectionIsEmpty()) {
    /* We want to add autocompletion when we are editing a word (after adding or
     * deleting text). So if nothing is selected, we add the autocompletion if
     * the event is backspace, as autocompletion has already been added if the
     * event added text, in handleEventWithText. */
    addAutocompletion();
  }
  return result;
}

bool PythonTextArea::handleEventWithText(const char * text, bool indentation, bool forceCursorRightOfText, bool shouldRemoveLastCharacter) {
  if (*text == 0) {
    return false;
  }
  if (m_contentView.isAutocompleting()) {
    removeAutocompletion();
  }
  bool result = TextArea::handleEventWithText(text, indentation, forceCursorRightOfText, shouldRemoveLastCharacter);
  addAutocompletion();
  return result;
}

void PythonTextArea::removeAutocompletion() {
  assert(m_contentView.isAutocompleting());
  removeAutocompletionText();
  m_contentView.setAutocompleting(false);
}

void PythonTextArea::removeAutocompletionText() {
  assert(m_contentView.isAutocompleting());
  assert(m_contentView.autocompletionEnd() != nullptr);
  const char * autocompleteStart = m_contentView.cursorLocation();
  const char * autocompleteEnd = m_contentView.autocompletionEnd();
  assert(autocompleteEnd != nullptr && autocompleteEnd > autocompleteStart);
  m_contentView.removeText(autocompleteStart, autocompleteEnd);
}

void PythonTextArea::addAutocompletion() {
  assert(!m_contentView.isAutocompleting());
  const char * autocompletionTokenBeginning = nullptr;
  const char * autocompletionLocation = const_cast<char *>(cursorLocation());
  m_autocompletionResultIndex = 0;
  if (autocompletionType(autocompletionLocation, &autocompletionTokenBeginning) != AutocompletionType::EndOfIdentifier) {
    // The cursor is not at the end of an identifier.
    return;
  }

  // First load variables and functions that complete the textToAutocomplete
  const int scriptIndex = m_contentView.pythonDelegate()->menuController()->editedScriptIndex();
  m_contentView.pythonDelegate()->variableBoxController()->loadFunctionsAndVariables(scriptIndex, autocompletionTokenBeginning, autocompletionLocation - autocompletionTokenBeginning);

  addAutocompletionTextAtIndex(0);
}

bool PythonTextArea::addAutocompletionTextAtIndex(int nextIndex, int * currentIndexToUpdate) {
  // If Autocomplete disable, skip this step
  if(!GlobalPreferences::sharedGlobalPreferences()->autocomplete()) {
    return false;
  }

  // The variable box should be loaded at this point
  const char * autocompletionTokenBeginning = nullptr;
  const char * autocompletionLocation = const_cast<char *>(cursorLocation());
  AutocompletionType type = autocompletionType(autocompletionLocation, &autocompletionTokenBeginning); // Done to get autocompletionTokenBeginning
  assert(type == AutocompletionType::EndOfIdentifier);
  (void)type; // Silence warnings
  VariableBoxController * varBox = m_contentView.pythonDelegate()->variableBoxController();
  int textToInsertLength = 0;
  bool addParentheses = false;
  const char * textToInsert = varBox->autocompletionAlternativeAtIndex(autocompletionLocation - autocompletionTokenBeginning, &textToInsertLength, &addParentheses, nextIndex, currentIndexToUpdate);

  if (textToInsert == nullptr) {
    return false;
  }

  if (textToInsertLength > 0) {
    // Try to insert the text (this might fail if the buffer is full)
    if (!m_contentView.isAbleToInsertTextAt(textToInsertLength, autocompletionLocation, false)) {
      return false;
    }
    m_contentView.insertTextAtLocation(textToInsert, const_cast<char *>(autocompletionLocation), textToInsertLength);
    autocompletionLocation += textToInsertLength;
    m_contentView.setAutocompleting(true);
    m_contentView.setAutocompletionEnd(autocompletionLocation);
  }

  // Try to insert the parentheses if needed
  const char * parentheses = ScriptNodeCell::k_parentheses;
  constexpr int parenthesesLength = 2;
  assert(strlen(parentheses) == parenthesesLength);
  /* If couldInsertText is false, we should not try to add the parentheses as
   * there was already not enough space to add the autocompletion. */
  if (addParentheses && m_contentView.isAbleToInsertTextAt(parenthesesLength, autocompletionLocation, false)) {
    m_contentView.insertTextAtLocation(parentheses, const_cast<char *>(autocompletionLocation), parenthesesLength);
    m_contentView.setAutocompleting(true);
    m_contentView.setAutocompletionEnd(autocompletionLocation + parenthesesLength);
    return true;
  }
  return (textToInsertLength > 0);
}

void PythonTextArea::cycleAutocompletion(bool downwards) {
  assert(m_contentView.isAutocompleting());
  removeAutocompletionText();
  addAutocompletionTextAtIndex(m_autocompletionResultIndex + (downwards ? 1 : -1), &m_autocompletionResultIndex);
}

void PythonTextArea::acceptAutocompletion(bool moveCursorToEndOfAutocompletion) {
  assert(m_contentView.isAutocompleting());

  // Save the cursor location
  const char * previousCursorLocation = cursorLocation();

  removeAutocompletion();

  m_contentView.pythonDelegate()->variableBoxController()->setSender(this);
  m_contentView.pythonDelegate()->variableBoxController()->insertAutocompletionResultAtIndex(m_autocompletionResultIndex);

  // insertAutocompletionResultAtIndex already added the autocompletion

  // If we did not want to move the cursor, restore its position.
  if (!moveCursorToEndOfAutocompletion) {
    setCursorLocation(previousCursorLocation);
  }
}

}
