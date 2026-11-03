#include "TapVirtualKeyboard.h"

// Key layouts
static const char* KEYS_ROW0_ALPHA[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
static const char* KEYS_ROW0_SYM[]   = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

static const char* KEYS_ROW1_ALPHA[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L"};
static const char* KEYS_ROW1_SYM[]   = {"-", "/", ":", ";", "(", ")", "$", "&", "@"};

static const char* KEYS_ROW2_ALPHA[] = {"Z", "X", "C", "V", "B", "N", "M"};
static const char* KEYS_ROW2_SYM[]   = {".", ",", "?", "!", "'", "\"", "_"};

TapVirtualKeyboard::TapVirtualKeyboard()
    : _text_len(0),
      _symbols_mode(false),
      _send_triggered(false),
      _highlighted_row(-1),
      _highlighted_col(-1),
      _highlight_expiry(0) {
  _text_buffer[0] = 0;
}

void TapVirtualKeyboard::clearText() {
  _text_len = 0;
  _text_buffer[0] = 0;
}

void TapVirtualKeyboard::drawKey(DisplayDriver* display, int x, int y, int w, int h, const char* label, bool highlighted, ColorVal customBkg) {
  ColorVal bkg = highlighted ? 0xFFE0 /* Yellow */ : (customBkg ? customBkg : 0x2104 /* Dark gray */);
  ColorVal border = highlighted ? 0xFFFF : 0x632C; // Gray border
  ColorVal txtColor = highlighted ? 0x0000 : (customBkg ? 0x0000 : 0xFFFF);

  display->setColor(bkg);
  display->fillRect(x, y, w, h);
  display->setColor(border);
  display->drawRect(x, y, w, h);

  display->setColor(txtColor);
  display->setTextSize(1);
  display->drawTextCentered(x + w / 2, y + (h - 8) / 2, label);
}

void TapVirtualKeyboard::draw(DisplayDriver* display, bool fullRedraw) {
  // Clear highlight if expired
  if (_highlighted_row >= 0 && millis() > _highlight_expiry) {
    _highlighted_row = -1;
    _highlighted_col = -1;
  }

  // Draw Input Text Bar (Y = 112 to 133, 22px)
  display->setColor(0x18E3); // Dark slate input background
  display->fillRect(0, 112, 320, 22);
  display->setColor(0x07E0); // Green accent border
  display->drawRect(0, 112, 320, 22);

  display->setColor(0xFFFF);
  display->setTextSize(1);
  char preview[KBD_MAX_INPUT_LEN + 4];
  snprintf(preview, sizeof(preview), "> %s_", _text_buffer);
  display->drawTextEllipsized(6, 118, 308, preview);

  // Background for keyboard container
  display->setColor(0x10A2); // Charcoal background
  display->fillRect(0, KBD_Y_START, 320, 240 - KBD_Y_START);

  // Row 0: 10 keys (width: 31px each, gap: 1px)
  for (int c = 0; c < 10; c++) {
    int x = c * 32;
    int y = KBD_Y_START + 1;
    bool hl = (_highlighted_row == 0 && _highlighted_col == c);
    const char* lbl = _symbols_mode ? KEYS_ROW0_SYM[c] : KEYS_ROW0_ALPHA[c];
    drawKey(display, x, y, 31, 23, lbl, hl);
  }

  // Row 1: 9 keys (centered: x offset = 8, width: 33px each)
  for (int c = 0; c < 9; c++) {
    int x = 8 + c * 34;
    int y = KBD_Y_START + 26;
    bool hl = (_highlighted_row == 1 && _highlighted_col == c);
    const char* lbl = _symbols_mode ? KEYS_ROW1_SYM[c] : KEYS_ROW1_ALPHA[c];
    drawKey(display, x, y, 33, 23, lbl, hl);
  }

  // Row 2: Mode Toggle (46px), 7 letter keys (31px each), DEL (46px)
  // Mode key
  bool hlMode = (_highlighted_row == 2 && _highlighted_col == 0);
  drawKey(display, 1, KBD_Y_START + 51, 46, 23, _symbols_mode ? "ABC" : "123", hlMode, 0x4A69);

  // 7 characters
  for (int c = 0; c < 7; c++) {
    int x = 49 + c * 32;
    int y = KBD_Y_START + 51;
    bool hl = (_highlighted_row == 2 && _highlighted_col == (c + 1));
    const char* lbl = _symbols_mode ? KEYS_ROW2_SYM[c] : KEYS_ROW2_ALPHA[c];
    drawKey(display, x, y, 31, 23, lbl, hl);
  }

  // DEL key
  bool hlDel = (_highlighted_row == 2 && _highlighted_col == 8);
  drawKey(display, 273, KBD_Y_START + 51, 46, 23, "DEL", hlDel, 0xB000 /* Dark Red */);

  // Row 3: CLR (48px), SPACE (178px), SEND (88px)
  bool hlClr = (_highlighted_row == 3 && _highlighted_col == 0);
  drawKey(display, 1, KBD_Y_START + 76, 48, 27, "CLR", hlClr, 0x52AA);

  bool hlSpace = (_highlighted_row == 3 && _highlighted_col == 1);
  drawKey(display, 52, KBD_Y_START + 76, 178, 27, "SPACE", hlSpace);

  bool hlSend = (_highlighted_row == 3 && _highlighted_col == 2);
  drawKey(display, 233, KBD_Y_START + 76, 86, 27, "SEND", hlSend, 0x07E0 /* Vibrant Green */);
}

bool TapVirtualKeyboard::handleTouch(uint16_t tx, uint16_t ty, bool justPressed) {
  if (!justPressed || ty < KBD_Y_START || ty >= 240) {
    return false;
  }

  int relY = ty - KBD_Y_START;

  // Row 0 (0..24)
  if (relY < 25) {
    int col = tx / 32;
    if (col >= 0 && col < 10) {
      const char* lbl = _symbols_mode ? KEYS_ROW0_SYM[col] : KEYS_ROW0_ALPHA[col];
      if (_text_len < KBD_MAX_INPUT_LEN) {
        _text_buffer[_text_len++] = lbl[0];
        _text_buffer[_text_len] = 0;
      }
      _highlighted_row = 0;
      _highlighted_col = col;
      _highlight_expiry = millis() + 80;
      return true;
    }
  }
  // Row 1 (25..50)
  else if (relY < 51) {
    if (tx >= 8 && tx < 314) {
      int col = (tx - 8) / 34;
      if (col >= 0 && col < 9) {
        const char* lbl = _symbols_mode ? KEYS_ROW1_SYM[col] : KEYS_ROW1_ALPHA[col];
        if (_text_len < KBD_MAX_INPUT_LEN) {
          _text_buffer[_text_len++] = lbl[0];
          _text_buffer[_text_len] = 0;
        }
        _highlighted_row = 1;
        _highlighted_col = col;
        _highlight_expiry = millis() + 80;
        return true;
      }
    }
  }
  // Row 2 (51..75)
  else if (relY < 76) {
    // Mode toggle
    if (tx < 48) {
      _symbols_mode = !_symbols_mode;
      _highlighted_row = 2;
      _highlighted_col = 0;
      _highlight_expiry = millis() + 80;
      return true;
    }
    // Letters
    else if (tx >= 49 && tx < 273) {
      int col = (tx - 49) / 32;
      if (col >= 0 && col < 7) {
        const char* lbl = _symbols_mode ? KEYS_ROW2_SYM[col] : KEYS_ROW2_ALPHA[col];
        if (_text_len < KBD_MAX_INPUT_LEN) {
          _text_buffer[_text_len++] = lbl[0];
          _text_buffer[_text_len] = 0;
        }
        _highlighted_row = 2;
        _highlighted_col = col + 1;
        _highlight_expiry = millis() + 80;
        return true;
      }
    }
    // DEL key
    else if (tx >= 273) {
      if (_text_len > 0) {
        _text_len--;
        _text_buffer[_text_len] = 0;
      }
      _highlighted_row = 2;
      _highlighted_col = 8;
      _highlight_expiry = millis() + 80;
      return true;
    }
  }
  // Row 3 (76..105)
  else {
    // CLR key
    if (tx < 50) {
      clearText();
      _highlighted_row = 3;
      _highlighted_col = 0;
      _highlight_expiry = millis() + 80;
      return true;
    }
    // SPACE
    else if (tx >= 52 && tx < 232) {
      if (_text_len < KBD_MAX_INPUT_LEN) {
        _text_buffer[_text_len++] = ' ';
        _text_buffer[_text_len] = 0;
      }
      _highlighted_row = 3;
      _highlighted_col = 1;
      _highlight_expiry = millis() + 80;
      return true;
    }
    // SEND
    else if (tx >= 233) {
      if (_text_len > 0) {
        _send_triggered = true;
      }
      _highlighted_row = 3;
      _highlighted_col = 2;
      _highlight_expiry = millis() + 80;
      return true;
    }
  }

  return false;
}
