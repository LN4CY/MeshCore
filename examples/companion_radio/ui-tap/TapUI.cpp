#include "TapUI.h"

TapUI::TapUI(TapClient* client)
    : _display(nullptr),
      _client(client),
      _sensors(nullptr),
      _current_tab(TAB_CHAT),
      _contact_scroll_offset(0),
      _last_status_ms(0),
      _needs_full_redraw(true),
      _batt_mv(4000),
      _has_gps(false) {}

bool TapUI::begin(DisplayDriver* display, SensorManager* sensors) {
  _display = display;
  _sensors = sensors;

  // Initialize Touch (FT6336 on Wire, address 0x38, landscape rotation 1)
  _touch.setRotation(1);
  _touch.setDimensions(320, 240);
  _touch.begin();

  if (_client) {
    _client->begin();
  }

  _needs_full_redraw = true;
  _last_status_ms = 0;
  return true;
}

void TapUI::drawStatusBar() {
  // Status Bar: Y = 0 to 21 (22px high)
  _display->setColor(0x1994); // Crisp Navy Blue
  _display->fillRect(0, 0, 320, 21);

  _display->setColor(0xFFFF); // Bright White text
  _display->setTextSize(1);

  // Left: Active target / Node title
  char titleBuf[32];
  if (_client && _client->getActiveContactIdx() >= 0) {
    int idx = _client->getActiveContactIdx();
    const TapContact& c = _client->getContact(idx);
    snprintf(titleBuf, sizeof(titleBuf), "DM: %s", c.name);
  } else if (_client) {
    const TapChannel& ch = _client->getChannel(_client->getActiveChannel());
    snprintf(titleBuf, sizeof(titleBuf), "%s", ch.name);
  } else {
    strcpy(titleBuf, "WisMesh Tap");
  }
  _display->drawTextLeftAlign(6, 6, titleBuf);

  // Center: Connection / Node info
  if (_client && _client->isConnected()) {
    _display->setColor(0x07E0); // Vibrant Green
    _display->drawTextCentered(160, 6, "[MESH ON]");
  } else {
    _display->setColor(0xFD20); // Bright Amber
    _display->drawTextCentered(160, 6, "[CONNECTING]");
  }

  // Right: Battery voltage & Unread badge
  char rightBuf[24];
  if (_client && _client->getUnreadCount() > 0) {
    snprintf(rightBuf, sizeof(rightBuf), "*%d* %.2fV", _client->getUnreadCount(), _batt_mv / 1000.0f);
    _display->setColor(0xFFE0); // Bright Yellow unread badge
  } else {
    snprintf(rightBuf, sizeof(rightBuf), "%.2fV", _batt_mv / 1000.0f);
    _display->setColor(0xFFFF);
  }
  _display->drawTextRightAlign(314, 6, rightBuf);

  // Bottom dividing line
  _display->setColor(0x0113); // Deep navy accent line
  _display->fillRect(0, 21, 320, 1);
}

void TapUI::drawTabBar() {
  // Tab Bar: Y = 22 to 43 (22px high)
  const char* tabs[] = {"CHAT", "CONTACTS", "CHANNELS", "INFO"};
  int tabWidth = 80;

  for (int i = 0; i < 4; i++) {
    int x = i * tabWidth;
    int y = 22;
    bool isActive = (_current_tab == (TapTab)i);

    ColorVal bkg = isActive ? 0xFFFF : 0xDEFB;     // Active: Pure White, Inactive: Light Silver
    ColorVal border = isActive ? 0x0276 : 0xBDD7;  // Active: Royal Blue, Inactive: Soft gray
    ColorVal txtColor = isActive ? 0x0276 : 0x4208;// Active: Royal Blue, Inactive: Dark slate

    _display->setColor(bkg);
    _display->fillRect(x, y, tabWidth, 22);

    _display->setColor(border);
    _display->drawRect(x, y, tabWidth, 22);

    if (isActive) {
      // Underline highlight for active tab
      _display->setColor(0x0276);
      _display->fillRect(x + 2, y + 19, tabWidth - 4, 3);
    }

    _display->setColor(txtColor);
    _display->setTextSize(1);
    _display->drawTextCentered(x + tabWidth / 2, y + 6, tabs[i]);
  }
}

void TapUI::drawChatScreen() {
  // Clear chat message area (Y = 44 to 113, 70px high)
  _display->setColor(0xFFFF); // Clean White background
  _display->fillRect(0, 44, 320, 70);

  if (!_client || _client->getMessageCount() == 0) {
    _display->setColor(0x0276); // Royal Blue
    _display->setTextSize(1);
    _display->drawTextCentered(160, 64, "WisMesh Tap Ready");
    _display->setColor(0x7BEF); // Mid gray
    _display->drawTextCentered(160, 84, "Type below & tap SEND to transmit over LoRa");
  } else {
    // Show up to the last 3 messages that fit in 70px
    uint8_t total = _client->getMessageCount();
    uint8_t startIdx = total > 3 ? (total - 3) : 0;
    int curY = 46;

    for (uint8_t i = startIdx; i < total; i++) {
      const TapChatMessage& msg = _client->getMessage(i);

      if (msg.is_outgoing) {
        // Outgoing bubble (Soft light blue)
        _display->setColor(0xDEFB);
        _display->fillRect(50, curY, 266, 21);
        _display->setColor(0x0276);
        _display->drawRect(50, curY, 266, 21);

        _display->setColor(0x0276); // Navy sender
        _display->setTextSize(1);
        _display->drawTextLeftAlign(54, curY + 2, "You:");

        _display->setColor(0x0000); // Black message text
        _display->drawTextEllipsized(84, curY + 2, 195, msg.text);

        // ACK status
        _display->setColor(msg.is_delivered ? 0x05E0 : 0xFD20);
        _display->drawTextRightAlign(312, curY + 2, msg.is_delivered ? "[OK]" : "[..]");
      } else {
        // Incoming bubble (Warm light cream)
        _display->setColor(0xF7DE);
        _display->fillRect(4, curY, 266, 21);
        _display->setColor(0xBDD7);
        _display->drawRect(4, curY, 266, 21);

        _display->setColor(0x04C5); // Forest green sender
        _display->setTextSize(1);
        char senderLabel[28];
        snprintf(senderLabel, sizeof(senderLabel), "%s:", msg.sender);
        _display->drawTextLeftAlign(8, curY + 2, senderLabel);

        int nameW = _display->getTextWidth(senderLabel) + 4;
        _display->setColor(0x0000); // Black message text
        _display->drawTextEllipsized(8 + nameW, curY + 2, 260 - nameW, msg.text);
      }

      curY += 23;
    }
  }

  // Draw Input Bar & Keyboard
  _keyboard.draw(_display, _needs_full_redraw);
}

void TapUI::drawContactsScreen() {
  // Clear full content area (Y = 44 to 240, 196px)
  _display->setColor(0xF7DE); // Clean off-white background
  _display->fillRect(0, 44, 320, 196);

  if (!_client || _client->getContactCount() == 0) {
    _display->setColor(0x0276);
    _display->setTextSize(1);
    _display->drawTextCentered(160, 110, "Scanning mesh for contacts...");
    _display->setColor(0x7BEF);
    _display->drawTextCentered(160, 130, "Nearby nodes will appear automatically.");
    return;
  }

  uint8_t count = _client->getContactCount();
  int y = 46;

  for (uint8_t i = 0; i < count && i < 7; i++) {
    const TapContact& c = _client->getContact(i);

    // Row container card
    _display->setColor(0xFFFF); // Pure White card
    _display->fillRect(4, y, 312, 25);
    _display->setColor(0xBDD7); // Card border
    _display->drawRect(4, y, 312, 25);

    // Node icon & Name
    _display->setColor(0x0276); // Royal Blue
    _display->setTextSize(1);
    _display->drawTextLeftAlign(10, y + 8, c.name);

    // Key prefix
    char keyBuf[16];
    snprintf(keyBuf, sizeof(keyBuf), "[%02X%02X%02X]", c.pubkey_prefix[0], c.pubkey_prefix[1], c.pubkey_prefix[2]);
    _display->setColor(0x7BEF);
    _display->drawTextLeftAlign(160, y + 8, keyBuf);

    // Action button
    _display->setColor(0x0276); // Royal Blue
    _display->fillRect(260, y + 3, 50, 19);
    _display->setColor(0xFFFF);
    _display->drawTextCentered(285, y + 8, "DM");

    y += 27;
  }
}

void TapUI::drawChannelsScreen() {
  _display->setColor(0xF7DE);
  _display->fillRect(0, 44, 320, 196);

  int y = 48;
  for (uint8_t i = 0; i < 4; i++) {
    bool isActive = (_client && _client->getActiveChannel() == i && _client->getActiveContactIdx() < 0);

    ColorVal bkg = isActive ? 0xDEFB : 0xFFFF;
    ColorVal border = isActive ? 0x05E0 : 0xBDD7;

    _display->setColor(bkg);
    _display->fillRect(8, y, 304, 38);
    _display->setColor(border);
    _display->drawRect(8, y, 304, 38);

    _display->setColor(0x0000); // Black channel name
    _display->setTextSize(1);
    char chName[32];
    if (i == 0) {
      strcpy(chName, "Channel #0: Public Broadcast");
    } else {
      snprintf(chName, sizeof(chName), "Channel #%d: Group %d", i, i);
    }
    _display->drawTextLeftAlign(18, y + 14, chName);

    if (isActive) {
      _display->setColor(0x05E0); // Emerald Green
      _display->drawTextRightAlign(300, y + 14, "[ACTIVE]");
    } else {
      _display->setColor(0x7BEF);
      _display->drawTextRightAlign(300, y + 14, "SELECT");
    }

    y += 44;
  }
}

void TapUI::drawInfoScreen() {
  _display->setColor(0xFFFF); // Clean White
  _display->fillRect(0, 44, 320, 196);

  _display->setColor(0x0276); // Royal Blue
  _display->setTextSize(1);
  _display->drawTextLeftAlign(12, 50, "WisMesh Tap V1 Handheld Communicator");

  _display->setColor(0x0000); // Black text
  _display->drawTextLeftAlign(12, 68, "Core: Nordic nRF52840 (64MHz Cortex-M4F)");
  _display->drawTextLeftAlign(12, 84, "Radio: Semtech SX1262 LoRa");

  char buf[64];
  if (_client) {
    snprintf(buf, sizeof(buf), "Node Name: %s", _client->getSelfName());
    _display->drawTextLeftAlign(12, 100, buf);

    const uint8_t* pk = _client->getSelfPubKey();
    snprintf(buf, sizeof(buf), "Pubkey: %02X%02X%02X%02X%02X%02X...", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5]);
    _display->drawTextLeftAlign(12, 116, buf);
  }

  snprintf(buf, sizeof(buf), "Battery: %d mV (%.2f V)", _batt_mv, _batt_mv / 1000.0f);
  _display->drawTextLeftAlign(12, 132, buf);

  _display->setColor(0x7BEF);
  _display->drawTextLeftAlign(12, 150, "Architecture: FreeRTOS Task Mailbox IPC");
  _display->drawTextLeftAlign(12, 166, "Firmware: MeshCore Standalone UI v1.0");

  // Reboot button
  _display->setColor(0xD800); // Vibrant Red
  _display->fillRect(100, 190, 120, 32);
  _display->setColor(0x8000);
  _display->drawRect(100, 190, 120, 32);
  _display->setColor(0xFFFF); // White text
  _display->drawTextCentered(160, 201, "REBOOT DEVICE");
}

void TapUI::handleTabTouch(uint16_t tx, uint16_t ty) {
  int tabIdx = tx / 80;
  if (tabIdx >= 0 && tabIdx < 4) {
    _current_tab = (TapTab)tabIdx;
    _needs_full_redraw = true;
    if (_current_tab == TAB_CHAT && _client) {
      _client->clearUnread();
    }
  }
}

void TapUI::handleContactsTouch(uint16_t tx, uint16_t ty) {
  if (ty >= 46) {
    int row = (ty - 46) / 27;
    if (_client && row < _client->getContactCount()) {
      _client->setActiveContactIdx(row);
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
    }
  }
}

void TapUI::handleChannelsTouch(uint16_t tx, uint16_t ty) {
  if (ty >= 48) {
    int row = (ty - 48) / 44;
    if (_client && row >= 0 && row < 4) {
      _client->setActiveChannel(row);
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
    }
  }
}

void TapUI::handleInfoTouch(uint16_t tx, uint16_t ty) {
  // Check Reboot button (X=100..220, Y=190..222)
  if (tx >= 100 && tx <= 220 && ty >= 190 && ty <= 222) {
    NVIC_SystemReset();
  }
}

void TapUI::update() {
  if (!_display) return;

  // 1. Poll incoming frames from MeshCore over mailbox
  if (_client) {
    _client->poll();
  }

  // 2. Check if user pressed SEND on virtual keyboard
  if (_keyboard.isSendTriggered()) {
    const char* txt = _keyboard.getText();
    if (strlen(txt) > 0 && _client) {
      if (_client->getActiveContactIdx() >= 0) {
        int idx = _client->getActiveContactIdx();
        const TapContact& c = _client->getContact(idx);
        _client->sendDirectText(c.pubkey_prefix, txt);
      } else {
        _client->sendChannelText(_client->getActiveChannel(), txt);
      }
      _keyboard.clearText();
      _needs_full_redraw = true;
    }
  }

  // 3. Poll touch screen
  TouchPoint pt;
  if (_touch.poll(pt) && pt.justPressed) {
    Serial.printf("[TOUCH] Tap at X=%d, Y=%d\r\n", pt.x, pt.y);
    if (pt.y >= 22 && pt.y < 44) {
      handleTabTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_CHAT) {
      if (pt.y >= 114) {
        if (_keyboard.handleTouch(pt.x, pt.y, true)) {
          _needs_full_redraw = true;
        }
      }
    } else if (_current_tab == TAB_CONTACTS) {
      handleContactsTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_CHANNELS) {
      handleChannelsTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_INFO) {
      handleInfoTouch(pt.x, pt.y);
    }
  }

  // 4. Rate-limited status bar refresh (every 4 seconds, updates only top 22 pixels)
  unsigned long now = millis();
  if (now - _last_status_ms >= 4000) {
    _last_status_ms = now;
    drawStatusBar();
  }

  // 5. Full screen redraw ONLY when state has changed
  if (_needs_full_redraw) {
    _needs_full_redraw = false;

    drawStatusBar();
    drawTabBar();

    switch (_current_tab) {
      case TAB_CHAT:
        drawChatScreen();
        break;
      case TAB_CONTACTS:
        drawContactsScreen();
        break;
      case TAB_CHANNELS:
        drawChannelsScreen();
        break;
      case TAB_INFO:
        drawInfoScreen();
        break;
    }
  }
}
