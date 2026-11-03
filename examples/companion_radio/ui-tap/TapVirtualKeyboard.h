#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>

#define KBD_Y_START       135
#define KBD_ROW_HEIGHT    24
#define KBD_MAX_INPUT_LEN 100

class TapVirtualKeyboard {
private:
  char _text_buffer[KBD_MAX_INPUT_LEN + 1];
  uint8_t _text_len;
  bool _symbols_mode;
  bool _send_triggered;

  int8_t _highlighted_row;
  int8_t _highlighted_col;
  unsigned long _highlight_expiry;

  void drawKey(DisplayDriver* display, int x, int y, int w, int h, const char* label, bool highlighted = false, ColorVal customBkg = 0);

public:
  TapVirtualKeyboard();

  void draw(DisplayDriver* display, bool fullRedraw = false);
  bool handleTouch(uint16_t tx, uint16_t ty, bool justPressed);

  const char* getText() const { return _text_buffer; }
  uint8_t getTextLength() const { return _text_len; }
  void clearText();

  bool isSendTriggered() {
    bool res = _send_triggered;
    _send_triggered = false;
    return res;
  }
};
