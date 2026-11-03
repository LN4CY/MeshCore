#include "TapUI.h"
#include <helpers/ArduinoHelpers.h>

static const char* CANNED_MESSAGES[] = {
  "OK",
  "YES",
  "NO",
  "On my way",
  "Check in",
  "HELP / SOS",
  "Location check",
  "", // Keypad toggle
  ""  // Beacon
};

static const char* MULTITAP_CHARS[] = {
  ".?!1",
  "ABC2",
  "DEF3",
  "GHI4",
  "JKL5",
  "MNO6",
  "PQRS7",
  "TUV8",
  "WXYZ9"
};

TapUI::TapUI()
    : _display(nullptr),
      _current_tab(TAB_CHAT),
      _input_mode(INPUT_MODE_CANNED),
      _needs_full_redraw(true),
      _last_status_ms(0),
      _backlight_on(true),
      _active_channel(0),
      _active_contact_idx(-1),
      _msg_count(0),
      _text_len(0),
      _last_key_pressed(-1),
      _multitap_idx(0),
      _multitap_expiry(0) {
  _text_buffer[0] = 0;
}

bool TapUI::begin(DisplayDriver* display) {
  _display = display;

  // FT6336U touch controller: Rotation 3 (matching ST7789 DISPLAY_ROTATION=3 upright landscape)
  _touch.setRotation(3);
  _touch.setDimensions(320, 240);
  _touch.begin();

  _needs_full_redraw = true;
  _last_status_ms = 0;
  return true;
}

void TapUI::cycleTab() {
  _current_tab = (TapTab)((_current_tab + 1) % 4);
  _needs_full_redraw = true;
}

void TapUI::toggleBacklight() {
  _backlight_on = !_backlight_on;
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, _backlight_on ? HIGH : LOW);
}

void TapUI::addReceivedMessage(const char* sender, const char* text, bool isDirect) {
  if (!text || strlen(text) == 0) return;

  if (_msg_count < MAX_UI_MESSAGES) {
    _msg_count++;
  } else {
    for (uint8_t i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
  }

  uint8_t idx = _msg_count - 1;
  strncpy(_messages[idx].sender, sender ? sender : "Node", sizeof(_messages[idx].sender) - 1);
  _messages[idx].sender[sizeof(_messages[idx].sender) - 1] = 0;

  strncpy(_messages[idx].text, text, sizeof(_messages[idx].text) - 1);
  _messages[idx].text[sizeof(_messages[idx].text) - 1] = 0;

  _messages[idx].is_outgoing = false;
  _messages[idx].is_direct = isDirect;

  _needs_full_redraw = true;
}

void TapUI::addSentMessage(const char* text) {
  if (!text || strlen(text) == 0) return;

  if (_msg_count < MAX_UI_MESSAGES) {
    _msg_count++;
  } else {
    for (uint8_t i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
  }

  uint8_t idx = _msg_count - 1;
  strcpy(_messages[idx].sender, "You");
  strncpy(_messages[idx].text, text, sizeof(_messages[idx].text) - 1);
  _messages[idx].text[sizeof(_messages[idx].text) - 1] = 0;

  _messages[idx].is_outgoing = true;
  _messages[idx].is_direct = (_active_contact_idx >= 0);

  _needs_full_redraw = true;
}

void TapUI::sendMessage(const char* text) {
  if (!text || strlen(text) == 0) return;

  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      uint32_t ack = 0, timeout = 0;
      the_mesh.sendMessage(c, the_mesh.getRTCClock()->getCurrentTimeUnique(), 0, text, ack, timeout);
      addSentMessage(text);
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      the_mesh.sendGroupMessage(the_mesh.getRTCClock()->getCurrentTimeUnique(), ch.channel, the_mesh.getNodePrefs()->node_name, text, strlen(text));
      addSentMessage(text);
    }
  }
}

void TapUI::sendCannedMessage(uint8_t idx) {
  if (idx < 6) {
    sendMessage(CANNED_MESSAGES[idx]);
  } else if (idx == 6) {
    // Send Location
    char locBuf[48];
    snprintf(locBuf, sizeof(locBuf), "Loc: %.4f, %.4f", the_mesh.getNodePrefs()->node_lat, the_mesh.getNodePrefs()->node_lon);
    sendMessage(locBuf);
  } else if (idx == 7) {
    // Switch to Keypad
    _input_mode = INPUT_MODE_KEYPAD;
    _needs_full_redraw = true;
  } else if (idx == 8) {
    // Send Advert Beacon
    the_mesh.advert();
    addSentMessage("[Broadcast Beacon Sent]");
  }
}

void TapUI::handleKeypadKey(uint8_t keyIdx) {
  unsigned long now = millis();

  // Multi-tap keys (0..8)
  if (keyIdx < 9) {
    const char* chars = MULTITAP_CHARS[keyIdx];
    size_t numChars = strlen(chars);

    if (_last_key_pressed == (int)keyIdx && now < _multitap_expiry && _text_len > 0) {
      // Cycle through characters on same key
      _multitap_idx = (_multitap_idx + 1) % numChars;
      _text_buffer[_text_len - 1] = chars[_multitap_idx];
    } else {
      // New key press
      if (_text_len < MAX_MSG_TEXT_LEN - 1) {
        _multitap_idx = 0;
        _text_buffer[_text_len++] = chars[0];
        _text_buffer[_text_len] = 0;
      }
    }
    _last_key_pressed = keyIdx;
    _multitap_expiry = now + 900; // 900ms window to cycle
    _needs_full_redraw = true;
  }
  // DEL key (key 9)
  else if (keyIdx == 9) {
    if (_text_len > 0) {
      _text_len--;
      _text_buffer[_text_len] = 0;
    }
    _last_key_pressed = -1;
    _needs_full_redraw = true;
  }
  // SPACE key (key 10)
  else if (keyIdx == 10) {
    if (_text_len < MAX_MSG_TEXT_LEN - 1) {
      _text_buffer[_text_len++] = ' ';
      _text_buffer[_text_len] = 0;
    }
    _last_key_pressed = -1;
    _needs_full_redraw = true;
  }
  // BACK TO CANNED MODE (key 11)
  else if (keyIdx == 11) {
    _input_mode = INPUT_MODE_CANNED;
    _last_key_pressed = -1;
    _needs_full_redraw = true;
  }
}

void TapUI::drawStatusBar() {
  _display->setColor(0x1994); // Navy Blue
  _display->fillRect(0, 0, 320, 20);

  _display->setColor(0xFFFF); // White
  _display->setTextSize(1);

  // Left: Active target / Node title
  char titleBuf[32];
  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      snprintf(titleBuf, sizeof(titleBuf), "DM: %.20s", c.name);
    } else {
      strcpy(titleBuf, "DM Chat");
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      snprintf(titleBuf, sizeof(titleBuf), "#%.20s", ch.name[0] ? ch.name : "Public");
    } else {
      strcpy(titleBuf, "#Public");
    }
  }
  _display->drawTextLeftAlign(6, 6, titleBuf);

  // Center: Connection Status
  _display->setColor(0x07E0); // Lime Green
  _display->drawTextCentered(160, 6, "[MESH ON]");

  // Right: Battery voltage
  char rightBuf[20];
  snprintf(rightBuf, sizeof(rightBuf), "%.2fV", board.getBattMilliVolts() / 1000.0f);
  _display->setColor(0xFFFF);
  _display->drawTextRightAlign(314, 6, rightBuf);

  _display->setColor(0x0113);
  _display->fillRect(0, 20, 320, 1);
}

void TapUI::drawTabBar() {
  const char* tabs[] = {"CHAT", "NODES", "ROOMS", "CONFIG"};
  int tabWidth = 80;

  for (int i = 0; i < 4; i++) {
    int x = i * tabWidth;
    int y = 21;
    bool isActive = (_current_tab == (TapTab)i);

    ColorVal bkg = isActive ? 0xFFFF : 0xDEFB;
    ColorVal border = isActive ? 0x0276 : 0xBDD7;
    ColorVal txtColor = isActive ? 0x0276 : 0x4208;

    _display->setColor(bkg);
    _display->fillRect(x, y, tabWidth, 34);

    _display->setColor(border);
    _display->drawRect(x, y, tabWidth, 34);

    if (isActive) {
      _display->setColor(0x0276); // Bold underline
      _display->fillRect(x + 2, y + 30, tabWidth - 4, 4);
    }

    _display->setColor(txtColor);
    _display->setTextSize(1);
    _display->drawTextCentered(x + tabWidth / 2, y + 12, tabs[i]);
  }
}

void TapUI::drawChatScreen() {
  // Clear chat message area (Y = 56 to 134, 78px)
  _display->setColor(0xFFFF);
  _display->fillRect(0, 56, 320, 78);

  if (_msg_count == 0) {
    _display->setColor(0x0276);
    _display->setTextSize(1);
    _display->drawTextCentered(160, 78, "WisMesh Tap Ready");
    _display->setColor(0x7BEF);
    _display->drawTextCentered(160, 102, "Tap a quick button below to transmit");
  } else {
    // Show up to the last 3 messages
    uint8_t startIdx = _msg_count > 3 ? (_msg_count - 3) : 0;
    int curY = 58;

    for (uint8_t i = startIdx; i < _msg_count; i++) {
      const UIMessage& msg = _messages[i];

      if (msg.is_outgoing) {
        _display->setColor(0xDEFB);
        _display->fillRect(40, curY, 276, 22);
        _display->setColor(0x0276);
        _display->drawRect(40, curY, 276, 22);

        _display->setColor(0x0276);
        _display->setTextSize(1);
        _display->drawTextLeftAlign(44, curY + 3, "You:");

        _display->setColor(0x0000);
        _display->drawTextEllipsized(74, curY + 3, 205, msg.text);

        _display->setColor(0x05E0);
        _display->drawTextRightAlign(312, curY + 3, "[OK]");
      } else {
        _display->setColor(0xF7DE);
        _display->fillRect(4, curY, 276, 22);
        _display->setColor(0xBDD7);
        _display->drawRect(4, curY, 276, 22);

        _display->setColor(0x04C5);
        _display->setTextSize(1);
        char senderLabel[24];
        snprintf(senderLabel, sizeof(senderLabel), "%s:", msg.sender);
        _display->drawTextLeftAlign(8, curY + 3, senderLabel);

        int nameW = _display->getTextWidth(senderLabel) + 4;
        _display->setColor(0x0000);
        _display->drawTextEllipsized(8 + nameW, curY + 3, 268 - nameW, msg.text);
      }
      curY += 25;
    }
  }

  // Divider above input area
  _display->setColor(0xBDD7);
  _display->fillRect(0, 134, 320, 1);

  // Bottom Input / Action Area (Y = 135 to 240)
  if (_input_mode == INPUT_MODE_CANNED) {
    // Background
    _display->setColor(0xDEFB);
    _display->fillRect(0, 135, 320, 105);

    // Row 0: OK, YES, NO (Height: 32px)
    // OK (Emerald Green)
    _display->setColor(0x05E0);
    _display->fillRect(4, 137, 100, 32);
    _display->setColor(0xFFFF);
    _display->drawRect(4, 137, 100, 32);
    _display->drawTextCentered(54, 148, "OK");

    // YES (Royal Blue)
    _display->setColor(0x0276);
    _display->fillRect(110, 137, 100, 32);
    _display->setColor(0xFFFF);
    _display->drawRect(110, 137, 100, 32);
    _display->drawTextCentered(160, 148, "YES");

    // NO (Vibrant Red)
    _display->setColor(0xD800);
    _display->fillRect(216, 137, 100, 32);
    _display->setColor(0xFFFF);
    _display->drawRect(216, 137, 100, 32);
    _display->drawTextCentered(266, 148, "NO");

    // Row 1: ON MY WAY, CHECK IN, HELP / SOS (Height: 32px)
    // ON MY WAY
    _display->setColor(0xFFFF);
    _display->fillRect(4, 172, 100, 32);
    _display->setColor(0x0276);
    _display->drawRect(4, 172, 100, 32);
    _display->drawTextCentered(54, 183, "ON MY WAY");

    // CHECK IN
    _display->setColor(0xFFFF);
    _display->fillRect(110, 172, 100, 32);
    _display->setColor(0x0276);
    _display->drawRect(110, 172, 100, 32);
    _display->drawTextCentered(160, 183, "CHECK IN");

    // HELP / SOS (Red)
    _display->setColor(0xD800);
    _display->fillRect(216, 172, 100, 32);
    _display->setColor(0xFFFF);
    _display->drawRect(216, 172, 100, 32);
    _display->drawTextCentered(266, 183, "HELP / SOS");

    // Row 2: LOCATION, KEYPAD >, BEACON (Height: 30px)
    // LOCATION
    _display->setColor(0xFFFF);
    _display->fillRect(4, 207, 100, 30);
    _display->setColor(0x7BEF);
    _display->drawRect(4, 207, 100, 30);
    _display->setColor(0x0000);
    _display->drawTextCentered(54, 217, "LOCATION");

    // KEYPAD >
    _display->setColor(0xFD20); // Amber
    _display->fillRect(110, 207, 100, 30);
    _display->setColor(0x0000);
    _display->drawRect(110, 207, 100, 30);
    _display->drawTextCentered(160, 217, "KEYPAD >");

    // BEACON
    _display->setColor(0x03D9); // Cyan
    _display->fillRect(216, 207, 100, 30);
    _display->setColor(0xFFFF);
    _display->drawRect(216, 207, 100, 30);
    _display->drawTextCentered(266, 217, "BEACON");
  }
  else {
    // KEYPAD MODE
    // Background
    _display->setColor(0xDEFB);
    _display->fillRect(0, 135, 320, 105);

    // Input preview bar (Y = 136 to 157, 21px)
    _display->setColor(0xFFFF);
    _display->fillRect(4, 136, 230, 21);
    _display->setColor(0x0276);
    _display->drawRect(4, 136, 230, 21);

    _display->setColor(0x0000);
    char prevBuf[MAX_MSG_TEXT_LEN + 4];
    snprintf(prevBuf, sizeof(prevBuf), "> %s_", _text_buffer);
    _display->drawTextEllipsized(8, 142, 222, prevBuf);

    // Big green SEND button beside input bar
    _display->setColor(0x05E0);
    _display->fillRect(238, 136, 78, 21);
    _display->setColor(0xFFFF);
    _display->drawRect(238, 136, 78, 21);
    _display->drawTextCentered(277, 142, "SEND");

    // Row 0: 1 .?! | 2 ABC | 3 DEF | DEL (Y = 160, h = 24)
    const char* row0[] = {"1 .?!", "2 ABC", "3 DEF", "DEL"};
    for (int c = 0; c < 4; c++) {
      int x = 4 + c * 78;
      ColorVal bkg = (c == 3) ? 0xD800 : 0xFFFF;
      ColorVal txt = (c == 3) ? 0xFFFF : 0x0000;
      _display->setColor(bkg);
      _display->fillRect(x, 160, 74, 24);
      _display->setColor(0x8C71);
      _display->drawRect(x, 160, 74, 24);
      _display->setColor(txt);
      _display->drawTextCentered(x + 37, 167, row0[c]);
    }

    // Row 1: 4 GHI | 5 JKL | 6 MNO | SPACE (Y = 186, h = 24)
    const char* row1[] = {"4 GHI", "5 JKL", "6 MNO", "SPACE"};
    for (int c = 0; c < 4; c++) {
      int x = 4 + c * 78;
      _display->setColor(0xFFFF);
      _display->fillRect(x, 186, 74, 24);
      _display->setColor(0x8C71);
      _display->drawRect(x, 186, 74, 24);
      _display->setColor(0x0000);
      _display->drawTextCentered(x + 37, 193, row1[c]);
    }

    // Row 2: 7 PQRS | 8 TUV | 9 WXYZ | < CANNED (Y = 212, h = 26)
    const char* row2[] = {"7 PQRS", "8 TUV", "9 WXYZ", "< CANNED"};
    for (int c = 0; c < 4; c++) {
      int x = 4 + c * 78;
      ColorVal bkg = (c == 3) ? 0x0276 : 0xFFFF;
      ColorVal txt = (c == 3) ? 0xFFFF : 0x0000;
      _display->setColor(bkg);
      _display->fillRect(x, 212, 74, 26);
      _display->setColor(0x8C71);
      _display->drawRect(x, 212, 74, 26);
      _display->setColor(txt);
      _display->drawTextCentered(x + 37, 220, row2[c]);
    }
  }
}

void TapUI::drawNodesScreen() {
  _display->setColor(0xF7DE);
  _display->fillRect(0, 56, 320, 184);

  int count = the_mesh.getNumContacts();
  if (count <= 0) {
    _display->setColor(0x0276);
    _display->setTextSize(1);
    _display->drawTextCentered(160, 100, "No mesh contacts discovered yet.");
    _display->setColor(0x7BEF);
    _display->drawTextCentered(160, 120, "Nearby nodes appear automatically when heard.");

    // Big button to send beacon
    _display->setColor(0x0276);
    _display->fillRect(60, 150, 200, 36);
    _display->setColor(0xFFFF);
    _display->drawRect(60, 150, 200, 36);
    _display->drawTextCentered(160, 163, "BROADCAST BEACON");
    return;
  }

  int y = 60;
  for (int i = 0; i < count && i < 4; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;

    bool isSelected = (_active_contact_idx == i);

    _display->setColor(0xFFFF);
    _display->fillRect(4, y, 312, 38);
    _display->setColor(isSelected ? 0x0276 : 0xBDD7);
    _display->drawRect(4, y, 312, 38);

    // Node Name
    _display->setColor(0x0276);
    _display->setTextSize(1);
    char nameBuf[32];
    snprintf(nameBuf, sizeof(nameBuf), "%.16s", c.name[0] ? c.name : "Unknown");
    _display->drawTextLeftAlign(12, y + 14, nameBuf);

    // Key prefix
    char keyBuf[16];
    snprintf(keyBuf, sizeof(keyBuf), "[%02X%02X%02X]", c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2]);
    _display->setColor(0x7BEF);
    _display->drawTextLeftAlign(150, y + 14, keyBuf);

    // Action button
    _display->setColor(0x0276);
    _display->fillRect(240, y + 6, 68, 26);
    _display->setColor(0xFFFF);
    _display->drawTextCentered(274, y + 14, "DM CHAT");

    y += 44;
  }
}

void TapUI::drawRoomsScreen() {
  _display->setColor(0xF7DE);
  _display->fillRect(0, 56, 320, 184);

  int y = 60;
  for (uint8_t i = 0; i < 4; i++) {
    ChannelDetails ch;
    the_mesh.getChannel(i, ch);

    bool isActive = (_active_channel == i && _active_contact_idx < 0);

    ColorVal bkg = isActive ? 0xDEFB : 0xFFFF;
    ColorVal border = isActive ? 0x05E0 : 0xBDD7;

    _display->setColor(bkg);
    _display->fillRect(6, y, 308, 40);
    _display->setColor(border);
    _display->drawRect(6, y, 308, 40);

    _display->setColor(0x0000);
    _display->setTextSize(1);
    char chName[32];
    if (i == 0) {
      strcpy(chName, "Channel #0: Public Broadcast");
    } else {
      snprintf(chName, sizeof(chName), "Channel #%d: %.16s", i, ch.name[0] ? ch.name : "Group");
    }
    _display->drawTextLeftAlign(16, y + 15, chName);

    if (isActive) {
      _display->setColor(0x05E0);
      _display->drawTextRightAlign(300, y + 15, "[ACTIVE]");
    } else {
      _display->setColor(0x7BEF);
      _display->drawTextRightAlign(300, y + 15, "SELECT");
    }

    y += 44;
  }
}

void TapUI::drawConfigScreen() {
  _display->setColor(0xFFFF);
  _display->fillRect(0, 56, 320, 184);

  _display->setColor(0x0276);
  _display->setTextSize(1);
  _display->drawTextLeftAlign(10, 62, "WisMesh Tap V1 Handheld Communicator");

  _display->setColor(0x0000);
  char buf[64];
  snprintf(buf, sizeof(buf), "Node Name: %s", the_mesh.getNodePrefs()->node_name);
  _display->drawTextLeftAlign(10, 78, buf);

  snprintf(buf, sizeof(buf), "Freq: %.3f MHz | Bat: %d mV", the_mesh.getNodePrefs()->freq, board.getBattMilliVolts());
  _display->drawTextLeftAlign(10, 94, buf);

  snprintf(buf, sizeof(buf), "BLE Pairing PIN: %06lu", (unsigned long)the_mesh.getBLEPin());
  _display->setColor(0x0276);
  _display->drawTextLeftAlign(10, 110, buf);

  // Button 1: Preset Name Cycle ($300 \times 32\text{ px}$)
  _display->setColor(0xDEFB);
  _display->fillRect(10, 126, 300, 32);
  _display->setColor(0x0276);
  _display->drawRect(10, 126, 300, 32);
  _display->drawTextCentered(160, 137, "CYCLE PRESET NAME");

  // Button 2: Broadcast Beacon ($300 \times 32\text{ px}$)
  _display->setColor(0x05E0); // Emerald Green
  _display->fillRect(10, 163, 300, 32);
  _display->setColor(0xFFFF);
  _display->drawRect(10, 163, 300, 32);
  _display->drawTextCentered(160, 174, "BROADCAST ADVERT BEACON");

  // Button 3: Reboot Device ($300 \times 32\text{ px}$)
  _display->setColor(0xD800); // Red
  _display->fillRect(10, 200, 300, 32);
  _display->setColor(0xFFFF);
  _display->drawRect(10, 200, 300, 32);
  _display->drawTextCentered(160, 211, "REBOOT DEVICE");
}

void TapUI::handleTabTouch(uint16_t tx, uint16_t ty) {
  int tabIdx = tx / 80;
  if (tabIdx >= 0 && tabIdx < 4) {
    _current_tab = (TapTab)tabIdx;
    _needs_full_redraw = true;
  }
}

void TapUI::handleChatTouch(uint16_t tx, uint16_t ty) {
  if (ty < 135) return;

  if (_input_mode == INPUT_MODE_CANNED) {
    // 3 rows of 3 buttons:
    // col 0: 4..104, col 1: 110..210, col 2: 216..316
    int col = -1;
    if (tx >= 4 && tx < 105) col = 0;
    else if (tx >= 110 && tx < 211) col = 1;
    else if (tx >= 216 && tx < 317) col = 2;

    int row = -1;
    if (ty >= 137 && ty < 169) row = 0;
    else if (ty >= 172 && ty < 204) row = 1;
    else if (ty >= 207 && ty < 238) row = 2;

    if (col >= 0 && row >= 0) {
      uint8_t btnIdx = row * 3 + col;
      sendCannedMessage(btnIdx);
    }
  }
  else {
    // KEYPAD MODE
    // Check SEND button (x=238..316, y=136..157)
    if (tx >= 238 && tx <= 316 && ty >= 136 && ty <= 157) {
      if (_text_len > 0) {
        sendMessage(_text_buffer);
        _text_len = 0;
        _text_buffer[0] = 0;
        _last_key_pressed = -1;
      }
      return;
    }

    // Grid of 4 cols x 3 rows
    if (ty >= 160 && ty < 238) {
      int col = (tx - 4) / 78;
      if (col < 0) col = 0;
      if (col > 3) col = 3;

      int row = -1;
      if (ty >= 160 && ty < 185) row = 0;
      else if (ty >= 186 && ty < 211) row = 1;
      else if (ty >= 212 && ty < 238) row = 2;

      if (row >= 0) {
        uint8_t keyIdx = row * 4 + col;
        handleKeypadKey(keyIdx);
      }
    }
  }
}

void TapUI::handleNodesTouch(uint16_t tx, uint16_t ty) {
  int count = the_mesh.getNumContacts();
  if (count <= 0) {
    // Check beacon button (x=60..260, y=150..186)
    if (tx >= 60 && tx <= 260 && ty >= 150 && ty <= 186) {
      the_mesh.advert();
      _needs_full_redraw = true;
    }
    return;
  }

  if (ty >= 60) {
    int row = (ty - 60) / 44;
    if (row >= 0 && row < count) {
      _active_contact_idx = row;
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
    }
  }
}

void TapUI::handleRoomsTouch(uint16_t tx, uint16_t ty) {
  if (ty >= 60) {
    int row = (ty - 60) / 44;
    if (row >= 0 && row < 4) {
      _active_channel = row;
      _active_contact_idx = -1; // return to channel broadcast
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
    }
  }
}

void TapUI::handleConfigTouch(uint16_t tx, uint16_t ty) {
  // Button 1: Preset Name (y=126..158)
  if (ty >= 126 && ty <= 158) {
    const char* presets[] = {"WisMeshTap1", "WisMeshTap2", "FieldTap-A", "Radio-1"};
    static uint8_t presetIdx = 0;
    presetIdx = (presetIdx + 1) % 4;
    strcpy(the_mesh.getNodePrefs()->node_name, presets[presetIdx]);
    the_mesh.savePrefs();
    _needs_full_redraw = true;
  }
  // Button 2: Broadcast Beacon (y=163..195)
  else if (ty >= 163 && ty <= 195) {
    the_mesh.advert();
    addSentMessage("[Broadcast Beacon Sent]");
    _current_tab = TAB_CHAT;
    _needs_full_redraw = true;
  }
  // Button 3: Reboot Device (y=200..232)
  else if (ty >= 200 && ty <= 232) {
    NVIC_SystemReset();
  }
}

void TapUI::update() {
  if (!_display) return;

  // Poll touch panel
  TouchPoint pt;
  if (_touch.poll(pt) && pt.justPressed) {
    Serial.printf("[TOUCH] Tap at X=%d, Y=%d\r\n", pt.x, pt.y);

    if (pt.y >= 21 && pt.y < 55) {
      handleTabTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_CHAT) {
      handleChatTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_NODES) {
      handleNodesTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_ROOMS) {
      handleRoomsTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_CONFIG) {
      handleConfigTouch(pt.x, pt.y);
    }
  }

  // Periodic Status Bar refresh (every 4s)
  unsigned long now = millis();
  if (now - _last_status_ms >= 4000) {
    _last_status_ms = now;
    drawStatusBar();
  }

  // Full Screen redraw on state changes
  if (_needs_full_redraw) {
    _needs_full_redraw = false;

    drawStatusBar();
    drawTabBar();

    switch (_current_tab) {
      case TAB_CHAT:
        drawChatScreen();
        break;
      case TAB_NODES:
        drawNodesScreen();
        break;
      case TAB_ROOMS:
        drawRoomsScreen();
        break;
      case TAB_CONFIG:
        drawConfigScreen();
        break;
    }
  }
}
