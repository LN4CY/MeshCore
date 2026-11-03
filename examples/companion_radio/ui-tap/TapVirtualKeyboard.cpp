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

void TapVirtualKeyboard::drawKey(DisplayDriver* display, int x, int y, int w, int h, const char* label, bool highlighted, ColorVal customBkg, ColorVal customTxt) {
  ColorVal bkg;
  ColorVal border;
  ColorVal txtColor;

  if (highlighted) {
    bkg = 0xFFE0;      // Bright Yellow flash
    border = 0x0000;   // Black border
    txtColor = 0x0000; // Black text
  } else if (customBkg) {
    bkg = customBkg;
    border = 0x5ACB;
    txtColor = customTxt ? customTxt : 0xFFFF;
  } else {
    bkg = 0xFFFF;      // Clean White keycap
    border = 0x8C71;   // Medium gray outline
    txtColor = 0x0000; // High-contrast black label
  }

  display->setColor(bkg);
  display->fillRect(x, y, w, h);
  display->setColor(border);
  display->drawRect(x, y, w, h);

  display->setColor(txtColor);
  display->setTextSize(1);
  display->drawTextCentered(x + w / 2, y + (h - 8) / 2, label);
}

void TapVirtualKeyboard::draw(DisplayDriver* display, bool fullRedraw) {
  // Clear key highlight if expired
  if (_highlighted_row >= 0 && millis() > _highlight_expiry) {
    _highlighted_row = -1;
    _highlighted_col = -1;
  }

  // 1. Draw Input Text Bar (Y = 114 to 134, 21px)
  display->setColor(0xFFFF); // Clean White background
  display->fillRect(0, 114, 320, 21);
  display->setColor(0x0276); // Royal Blue border
  display->drawRect(0, 114, 320, 21);

  if (_text_len == 0) {
    display->setColor(0x9CD3); // Placeholder gray
    display->setTextSize(1);
    display->drawTextLeftAlign(8, 120, "> Type message here...");
  } else {
    display->setColor(0x0000); // Black text
    display->setTextSize(1);
    char preview[KBD_MAX_INPUT_LEN + 4];
    snprintf(preview, sizeof(preview), "> %s_", _text_buffer);
    display->drawTextEllipsized(8, 120, 304, preview);
  }

  // 2. Background for keyboard container
  display->setColor(0xDEFB); // Clean light silver/gray
  display->fillRect(0, KBD_Y_START, 320, 240 - KBD_Y_START);

  // Row 0: 10 keys (width: 31px each, pitch: 32px)
  for (int c = 0; c < 10; c++) {
    int x = c * 32;
    int y = KBD_Y_START + 1;
    bool hl = (_highlighted_row == 0 && _highlighted_col == c);
    const char* lbl = _symbols_mode ? KEYS_ROW0_SYM[c] : KEYS_ROW0_ALPHA[c];
    drawKey(display, x, y, 31, 23, lbl, hl);
  }

  // Row 1: 9 keys (centered: x offset = 8, width: 33px each, pitch: 34px)
  for (int c = 0; c < 9; c++) {
    int x = 8 + c * 34;
    int y = KBD_Y_START + 26;
    bool hl = (_highlighted_row == 1 && _highlighted_col == c);
    const char* lbl = _symbols_mode ? KEYS_ROW1_SYM[c] : KEYS_ROW1_ALPHA[c];
    drawKey(display, x, y, 33, 23, lbl, hl);
  }

  // Row 2: Mode Toggle (46px), 7 letter keys (31px each), DEL (46px)
  bool hlMode = (_highlighted_row == 2 && _highlighted_col == 0);
  drawKey(display, 1, KBD_Y_START + 51, 46, 23, _symbols_mode ? "ABC" : "123", hlMode, 0x0276 /* Royal Blue */, 0xFFFF /* White */);

  for (int c = 0; c < 7; c++) {
    int x = 49 + c * 32;
    int y = KBD_Y_START + 51;
    bool hl = (_highlighted_row == 2 && _highlighted_col == (c + 1));
    const char* lbl = _symbols_mode ? KEYS_ROW2_SYM[c] : KEYS_ROW2_ALPHA[c];
    drawKey(display, x, y, 31, 23, lbl, hl);
  }

  bool hlDel = (_highlighted_row == 2 && _highlighted_col == 8);
  drawKey(display, 273, KBD_Y_START + 51, 46, 23, "DEL", hlDel, 0xD800 /* Vibrant Red */, 0xFFFF /* White */);

  // Row 3: CLR (48px), SPACE (178px), SEND (87px)
  bool hlClr = (_highlighted_row == 3 && _highlighted_col == 0);
  drawKey(display, 1, KBD_Y_START + 76, 48, 27, "CLR", hlClr, 0xCE79 /* Slate Gray */, 0x0000 /* Black */);

  bool hlSpace = (_highlighted_row == 3 && _highlighted_col == 1);
  drawKey(display, 51, KBD_Y_START + 76, 179, 27, "SPACE", hlSpace);

  bool hlSend = (_highlighted_row == 3 && _highlighted_col == 2);
  drawKey(display, 232, KBD_Y_START + 76, 87, 27, "SEND", hlSend, 0x05E0 /* Emerald Green */, 0xFFFF /* White */);
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
      _highlight_expiry = millis() + 100;
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
        _highlight_expiry = millis() + 100;
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
      _highlight_expiry = millis() + 100;
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
        _highlight_expiry = millis() + 100;
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
      _highlight_expiry = millis() + 100;
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
      _highlight_expiry = millis() + 100;
      return true;
    }
    // SPACE
    else if (tx >= 51 && tx < 231) {
      if (_text_len < KBD_MAX_INPUT_LEN) {
        _text_buffer[_text_len++] = ' ';
        _text_buffer[_text_len] = 0;
      }
      _highlighted_row = 3;
      _highlighted_col = 1;
      _highlight_expiry = millis() + 100;
      return true;
    }
    // SEND
    else if (tx >= 232) {
      if (_text_len > 0) {
        _send_triggered = true;
      }
      _highlighted_row = 3;
      _highlighted_col = 2;
      _highlight_expiry = millis() + 100;
      return true;
    }
  }

  return false;
}
