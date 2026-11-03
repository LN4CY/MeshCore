#include "TapUI.h"

TapUI::TapUI(TapClient* client)
    : _display(nullptr),
      _client(client),
      _sensors(nullptr),
      _current_tab(TAB_CHAT),
      _contact_scroll_offset(0),
      _last_redraw_ms(0),
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
  return true;
}

void TapUI::drawStatusBar() {
  _display->setColor(0x0012); // Deep navy blue
  _display->fillRect(0, 0, 320, 20);

  _display->setColor(0xFFFF);
  _display->setTextSize(1);

  // Left: Active target
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
    _display->setColor(0x07E0); // Green connected dot
    _display->drawTextCentered(160, 6, "[MESH ON]");
  } else {
    _display->setColor(0xFD20); // Orange connecting
    _display->drawTextCentered(160, 6, "[CONNECTING]");
  }

  // Right: Battery voltage & Unread badge
  char rightBuf[24];
  if (_client && _client->getUnreadCount() > 0) {
    snprintf(rightBuf, sizeof(rightBuf), "*%d* %.2fV", _client->getUnreadCount(), _batt_mv / 1000.0f);
    _display->setColor(0xFFE0); // Yellow unread
  } else {
    snprintf(rightBuf, sizeof(rightBuf), "%.2fV", _batt_mv / 1000.0f);
    _display->setColor(0xFFFF);
  }
  _display->drawTextRightAlign(314, 6, rightBuf);

  // Thin dividing line
  _display->setColor(0x4208);
  _display->fillRect(0, 19, 320, 1);
}

void TapUI::drawTabBar() {
  const char* tabs[] = {"CHAT", "CONTACTS", "CHANNELS", "INFO"};
  int tabWidth = 80;

  for (int i = 0; i < 4; i++) {
    int x = i * tabWidth;
    int y = 20;
    bool isActive = (_current_tab == (TapTab)i);

    ColorVal bkg = isActive ? 0x2125 : 0x10A2;
    ColorVal border = isActive ? 0x07E0 : 0x4208;
    ColorVal txtColor = isActive ? 0xFFFF : 0xAD55;

    _display->setColor(bkg);
    _display->fillRect(x, y, tabWidth, 20);

    _display->setColor(border);
    _display->drawRect(x, y, tabWidth, 20);

    _display->setColor(txtColor);
    _display->setTextSize(1);
    _display->drawTextCentered(x + tabWidth / 2, y + 6, tabs[i]);
  }
}

void TapUI::drawChatScreen() {
  // Clear chat message area (Y = 40 to 111, 71px high)
  _display->setColor(0x0000); // Black background
  _display->fillRect(0, 40, 320, 72);

  if (!_client || _client->getMessageCount() == 0) {
    _display->setColor(0x6B4D);
    _display->setTextSize(1);
    _display->drawTextCentered(160, 65, "No messages yet. Type below to send!");
  } else {
    // Show up to the last 3 messages that fit in 71px
    uint8_t total = _client->getMessageCount();
    uint8_t startIdx = total > 3 ? (total - 3) : 0;
    int curY = 42;

    for (uint8_t i = startIdx; i < total; i++) {
      const TapChatMessage& msg = _client->getMessage(i);

      if (msg.is_outgoing) {
        // Outgoing bubble (Dark blue)
        _display->setColor(0x08CD);
        _display->fillRect(50, curY, 266, 21);
        _display->setColor(0x2BD9);
        _display->drawRect(50, curY, 266, 21);

        _display->setColor(0x07FF); // Cyan sender
        _display->setTextSize(1);
        _display->drawTextLeftAlign(54, curY + 2, "You:");

        _display->setColor(0xFFFF);
        _display->drawTextEllipsized(84, curY + 2, 195, msg.text);

        // ACK status
        _display->setColor(msg.is_delivered ? 0x07E0 : 0xFFE0);
        _display->drawTextRightAlign(312, curY + 2, msg.is_delivered ? "[OK]" : "[..]");
      } else {
        // Incoming bubble (Dark charcoal)
        _display->setColor(0x2104);
        _display->fillRect(4, curY, 266, 21);
        _display->setColor(0x52AA);
        _display->drawRect(4, curY, 266, 21);

        _display->setColor(0x07E0); // Green sender
        _display->setTextSize(1);
        char senderLabel[28];
        snprintf(senderLabel, sizeof(senderLabel), "%s:", msg.sender);
        _display->drawTextLeftAlign(8, curY + 2, senderLabel);

        int nameW = _display->getTextWidth(senderLabel) + 4;
        _display->setColor(0xFFFF);
        _display->drawTextEllipsized(8 + nameW, curY + 2, 260 - nameW, msg.text);
      }

      curY += 23;
    }
  }

  // Draw Input Bar & Keyboard
  _keyboard.draw(_display, _needs_full_redraw);
}

void TapUI::drawContactsScreen() {
  // Clear full content area (Y = 40 to 240, 200px)
  _display->setColor(0x0821);
  _display->fillRect(0, 40, 320, 200);

  if (!_client || _client->getContactCount() == 0) {
    _display->setColor(0xAD55);
    _display->setTextSize(1);
    _display->drawTextCentered(160, 110, "Scanning mesh for contacts...");
    _display->drawTextCentered(160, 130, "Nearby nodes will appear automatically.");
    return;
  }

  uint8_t count = _client->getContactCount();
  int y = 42;

  for (uint8_t i = 0; i < count && i < 7; i++) {
    const TapContact& c = _client->getContact(i);

    // Row container
    _display->setColor(0x18C3);
    _display->fillRect(4, y, 312, 25);
    _display->setColor(0x4208);
    _display->drawRect(4, y, 312, 25);

    // Node icon & Name
    _display->setColor(0x07E0); // Green
    _display->setTextSize(1);
    _display->drawTextLeftAlign(10, y + 8, c.name);

    // Key prefix
    char keyBuf[16];
    snprintf(keyBuf, sizeof(keyBuf), "[%02X%02X%02X]", c.pubkey_prefix[0], c.pubkey_prefix[1], c.pubkey_prefix[2]);
    _display->setColor(0xAD55);
    _display->drawTextLeftAlign(160, y + 8, keyBuf);

    // Action button
    _display->setColor(0x001F); // Blue
    _display->fillRect(260, y + 3, 50, 19);
    _display->setColor(0xFFFF);
    _display->drawTextCentered(285, y + 8, "DM");

    y += 27;
  }
}

void TapUI::drawChannelsScreen() {
  _display->setColor(0x0821);
  _display->fillRect(0, 40, 320, 200);

  int y = 44;
  for (uint8_t i = 0; i < 4; i++) {
    bool isActive = (_client && _client->getActiveChannel() == i && _client->getActiveContactIdx() < 0);

    ColorVal bkg = isActive ? 0x08CE : 0x18C3;
    ColorVal border = isActive ? 0x07E0 : 0x4208;

    _display->setColor(bkg);
    _display->fillRect(8, y, 304, 38);
    _display->setColor(border);
    _display->drawRect(8, y, 304, 38);

    _display->setColor(0xFFFF);
    _display->setTextSize(1);
    char chName[32];
    if (i == 0) {
      strcpy(chName, "Channel #0: Public Broadcast");
    } else {
      snprintf(chName, sizeof(chName), "Channel #%d: Group %d", i, i);
    }
    _display->drawTextLeftAlign(18, y + 14, chName);

    if (isActive) {
      _display->setColor(0x07E0);
      _display->drawTextRightAlign(300, y + 14, "[ACTIVE]");
    } else {
      _display->setColor(0xAD55);
      _display->drawTextRightAlign(300, y + 14, "SELECT");
    }

    y += 44;
  }
}

void TapUI::drawInfoScreen() {
  _display->setColor(0x0821);
  _display->fillRect(0, 40, 320, 200);

  _display->setColor(0xFFFF);
  _display->setTextSize(1);

  _display->drawTextLeftAlign(12, 48, "Device: RAKwireless WisMesh Tap V1");
  _display->drawTextLeftAlign(12, 64, "Core: Nordic nRF52840 (64MHz Cortex-M4F)");
  _display->drawTextLeftAlign(12, 80, "Radio: Semtech SX1262 LoRa");

  char buf[64];
  if (_client) {
    snprintf(buf, sizeof(buf), "Node Name: %s", _client->getSelfName());
    _display->drawTextLeftAlign(12, 96, buf);

    const uint8_t* pk = _client->getSelfPubKey();
    snprintf(buf, sizeof(buf), "Pubkey: %02X%02X%02X%02X%02X%02X...", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5]);
    _display->drawTextLeftAlign(12, 112, buf);
  }

  snprintf(buf, sizeof(buf), "Battery: %d mV (%.2f V)", _batt_mv, _batt_mv / 1000.0f);
  _display->drawTextLeftAlign(12, 128, buf);

  _display->drawTextLeftAlign(12, 144, "Architecture: FreeRTOS Task Mailbox IPC");
  _display->drawTextLeftAlign(12, 160, "Firmware: MeshCore Standalone UI v1.0");

  // Reboot button
  _display->setColor(0xB000); // Red
  _display->fillRect(100, 185, 120, 32);
  _display->setColor(0xFFFF);
  _display->drawRect(100, 185, 120, 32);
  _display->drawTextCentered(160, 195, "REBOOT DEVICE");
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
  if (ty >= 42) {
    int row = (ty - 42) / 27;
    if (_client && row < _client->getContactCount()) {
      _client->setActiveContactIdx(row);
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
    }
  }
}

void TapUI::handleChannelsTouch(uint16_t tx, uint16_t ty) {
  if (ty >= 44) {
    int row = (ty - 44) / 44;
    if (_client && row >= 0 && row < 4) {
      _client->setActiveChannel(row);
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
    }
  }
}

void TapUI::handleInfoTouch(uint16_t tx, uint16_t ty) {
  // Check Reboot button (X=100..220, Y=185..217)
  if (tx >= 100 && tx <= 220 && ty >= 185 && ty <= 217) {
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
    if (pt.y >= 20 && pt.y < 40) {
      handleTabTouch(pt.x, pt.y);
    } else if (_current_tab == TAB_CHAT) {
      if (pt.y >= 112) {
        if (_keyboard.handleTouch(pt.x, pt.y, true)) {
          // Input updated
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

  // 4. Render UI frame periodically or on change
  unsigned long now = millis();
  if (_needs_full_redraw || (now - _last_redraw_ms > 120)) {
    _last_redraw_ms = now;

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

    _needs_full_redraw = false;
  }
}
