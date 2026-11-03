#include "TapUI.h"
#include "target.h"
#include <helpers/TxtDataHelpers.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/sensors/LocationProvider.h>

#define COLOR_NAVY       0x1994
#define COLOR_WHITE      0xFFFF
#define COLOR_BLACK      0x0000
#define COLOR_GRAY_BG    0xE73C
#define COLOR_LIGHT_GRAY 0xDEFB
#define COLOR_MED_GRAY   0x8410
#define COLOR_DARK_GRAY  0x4208
#define COLOR_BLUE_KEY   0x22B9
#define COLOR_GREEN_BTN  0x05E0
#define COLOR_RED_BTN    0xD800
#define COLOR_ORANGE_BTN 0xFC00
#define COLOR_PURPLE_BTN 0x791B
#define COLOR_CYAN_BTN   0x04F7
#define COLOR_ACTIVE_TAB 0xFFFF
#define COLOR_INACT_TAB  0xC618

static const char* QUICK_RESPONSES[] = {
  "OK",
  "YES",
  "NO",
  "ON MY WAY",
  "CHECK IN",
  "HELP / SOS"
};
static const uint8_t NUM_QUICK_RESPONSES = 6;
static uint8_t s_quick_idx = 0;

static const float REGION_FREQS[] = {
  910.525f, // US Default
  906.875f, // US Mesh
  869.525f, // EU LongFast
  869.618f, // EU WisMesh
  915.000f, // AU915
  433.175f  // EU433
};
static const uint8_t NUM_REGION_FREQS = 6;

TapUI::TapUI()
  : _display(nullptr),
    _touch(&Wire, FT6336_DEFAULT_ADDR, 10, 17),
    _current_tab(TAB_CHAT),
    _kb_mode(KB_LOWER),
    _needs_full_redraw(true),
    _last_status_ms(0),
    _backlight_on(true),
    _active_channel(0),
    _active_contact_idx(-1),
    _show_rooms_subview(false),
    _editing_node_name(false),
    _msg_count(0),
    _text_len(0),
    _nodes_page(0),
    _radio_freq(910.525f),
    _radio_sf(10),
    _radio_bw(125.0f),
    _radio_cr(5),
    _radio_tx_power(22),
    _radio_rx_boost(true),
    _radio_status_expiry(0)
{
  _text_buffer[0] = 0;
  _radio_status_msg[0] = 0;
}

bool TapUI::begin(DisplayDriver* display) {
  _display = display;
  if (!_display) return false;

  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  _backlight_on = true;

  _touch.begin();
  _touch.setRotation(3);

  // Load existing radio parameters from the_mesh NodePrefs
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    if (prefs->freq > 100.0f) _radio_freq = prefs->freq;
    if (prefs->sf >= 7 && prefs->sf <= 12) _radio_sf = prefs->sf;
    if (prefs->bw > 10.0f) _radio_bw = prefs->bw;
    if (prefs->cr >= 5 && prefs->cr <= 8) _radio_cr = prefs->cr;
    if (prefs->tx_power_dbm != 0) _radio_tx_power = prefs->tx_power_dbm;
    _radio_rx_boost = (prefs->rx_boosted_gain != 0);
  }

  _needs_full_redraw = true;
  return true;
}

void TapUI::cycleTab() {
  int next = (int)_current_tab + 1;
  if (next > TAB_RADIO) next = TAB_CHAT;
  _current_tab = (TapTab)next;
  _editing_node_name = false;
  _needs_full_redraw = true;
}

void TapUI::toggleBacklight() {
  _backlight_on = !_backlight_on;
  digitalWrite(PIN_TFT_BL, _backlight_on ? HIGH : LOW);
}

void TapUI::appendChar(char c) {
  if (_text_len < MAX_MSG_TEXT_LEN - 1) {
    _text_buffer[_text_len++] = c;
    _text_buffer[_text_len] = 0;
    drawTextInputBar();
  }
}

void TapUI::deleteChar() {
  if (_text_len > 0) {
    _text_len--;
    _text_buffer[_text_len] = 0;
    drawTextInputBar();
  }
}

void TapUI::addReceivedMessage(const char* sender, const char* text, bool isDirect) {
  if (_msg_count >= MAX_UI_MESSAGES) {
    for (int i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
    _msg_count = MAX_UI_MESSAGES - 1;
  }
  UIMessage* m = &_messages[_msg_count++];
  strncpy(m->sender, sender ? sender : "Mesh", sizeof(m->sender) - 1);
  m->sender[sizeof(m->sender) - 1] = 0;
  strncpy(m->text, text ? text : "", sizeof(m->text) - 1);
  m->text[sizeof(m->text) - 1] = 0;
  m->is_outgoing = false;
  m->is_direct = isDirect;

  if (_current_tab == TAB_CHAT) {
    _needs_full_redraw = true;
  }
}

void TapUI::addSentMessage(const char* text) {
  if (_msg_count >= MAX_UI_MESSAGES) {
    for (int i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
    _msg_count = MAX_UI_MESSAGES - 1;
  }
  UIMessage* m = &_messages[_msg_count++];
  strcpy(m->sender, "You");
  strncpy(m->text, text ? text : "", sizeof(m->text) - 1);
  m->text[sizeof(m->text) - 1] = 0;
  m->is_outgoing = true;
  m->is_direct = (_active_contact_idx >= 0);

  if (_current_tab == TAB_CHAT) {
    _needs_full_redraw = true;
  }
}

void TapUI::sendMessage(const char* text) {
  if (!text || strlen(text) == 0) return;

  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      uint32_t ack = 0;
      uint32_t timeout = 0;
      the_mesh.sendMessage(c, the_mesh.getRTCClock()->getCurrentTimeUnique(), 0, text, ack, timeout);
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      the_mesh.sendGroupMessage(the_mesh.getRTCClock()->getCurrentTimeUnique(), ch.channel, the_mesh.getNodePrefs()->node_name, text, strlen(text));
    }
  }
  addSentMessage(text);
}

void TapUI::update() {
  if (!_display) return;

  TouchPoint pt;
  if (_touch.poll(pt) && pt.justPressed) {
    uint16_t tx = pt.x;
    uint16_t ty = pt.y;
    Serial.printf("[TOUCH] Tap at X=%d, Y=%d\n", tx, ty);

    // Check tab bar touch: Y in 20..42
    if (ty >= 20 && ty <= 42) {
      handleTabTouch(tx, ty);
    } else {
      switch (_current_tab) {
        case TAB_CHAT:  handleChatTouch(tx, ty); break;
        case TAB_NODES: handleNodesTouch(tx, ty); break;
        case TAB_CHANS: handleChansTouch(tx, ty); break;
        case TAB_GPS:   handleGPSTouch(tx, ty); break;
        case TAB_RADIO: handleRadioTouch(tx, ty); break;
      }
    }
  }

  unsigned long now = millis();
  if (_needs_full_redraw) {
    _needs_full_redraw = false;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(0, 0, 320, 240);
    drawStatusBar();
    drawTabBar();
    switch (_current_tab) {
      case TAB_CHAT:  drawChatScreen(); break;
      case TAB_NODES: drawNodesScreen(); break;
      case TAB_CHANS: drawChansScreen(); break;
      case TAB_GPS:   drawGPSScreen(); break;
      case TAB_RADIO: drawRadioScreen(); break;
    }
    _last_status_ms = now;
  } else if (now - _last_status_ms > 4000) {
    drawStatusBar();
    _last_status_ms = now;
  }
}

void TapUI::drawStatusBar() {
  _display->setColor(COLOR_NAVY);
  _display->fillRect(0, 0, 320, 20);

  _display->setTextSize(1);
  _display->setColor(COLOR_WHITE);

  // Recipient info
  char recBuf[24];
  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      if (c.name[0] != 0) snprintf(recBuf, sizeof(recBuf), "@%s", c.name);
      else snprintf(recBuf, sizeof(recBuf), "@!%02X%02X%02X%02X", c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2], c.id.pub_key[3]);
    } else {
      strcpy(recBuf, "@Node");
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      snprintf(recBuf, sizeof(recBuf), "#%s", ch.name[0] ? ch.name : "Public");
    } else {
      strcpy(recBuf, "#Public");
    }
  }
  drawStr(4, 5, recBuf);

  // Center: [MESH ON] and GPS status
  _display->setColor(COLOR_GREEN_BTN);
  drawStr(110, 5, "[MESH ON]");

  LocationProvider* loc = sensors.getLocationProvider();
  if (loc && loc->isEnabled()) {
    if (loc->isValid()) {
      char gpsBuf[16];
      snprintf(gpsBuf, sizeof(gpsBuf), "GPS:3D(%ld)", loc->satellitesCount());
      _display->setColor(COLOR_GREEN_BTN);
      drawStr(180, 5, gpsBuf);
    } else {
      _display->setColor(COLOR_ORANGE_BTN);
      drawStr(180, 5, "GPS:SEARCH");
    }
  } else {
    _display->setColor(COLOR_LIGHT_GRAY);
    drawStr(185, 5, "GPS:OFF");
  }

  // Right: Battery voltage
  uint32_t mv = board.getBattMilliVolts();
  char batBuf[10];
  snprintf(batBuf, sizeof(batBuf), "%.2fV", (float)mv / 1000.0f);
  _display->setColor(COLOR_WHITE);
  drawStr(280, 5, batBuf);
}

void TapUI::drawTabBar() {
  _display->setColor(COLOR_GRAY_BG);
  _display->fillRect(0, 20, 320, 23);

  const char* tabs[5] = { "CHAT", "NODES", "CHANS", "GPS", "RADIO" };
  for (int i = 0; i < 5; i++) {
    int x = i * 64;
    bool isActive = (i == (int)_current_tab);
    _display->setColor(isActive ? COLOR_ACTIVE_TAB : COLOR_INACT_TAB);
    _display->fillRect(x + 1, 21, 62, 21);

    _display->setColor(isActive ? COLOR_NAVY : COLOR_DARK_GRAY);
    _display->drawRect(x + 1, 21, 62, 21);

    _display->setTextSize(1);
    int tw = strlen(tabs[i]) * 6;
    drawStr(x + (64 - tw) / 2, 27, tabs[i]);

    if (isActive) {
      _display->setColor(COLOR_NAVY);
      _display->fillRect(x + 2, 40, 60, 2);
    }
  }
}

void TapUI::handleTabTouch(uint16_t tx, uint16_t ty) {
  int tabIdx = tx / 64;
  if (tabIdx >= 0 && tabIdx <= 4) {
    if (_current_tab != (TapTab)tabIdx) {
      _current_tab = (TapTab)tabIdx;
      _editing_node_name = false;
      _needs_full_redraw = true;
    }
  }
}

// -------------------------------------------------------------
// CHAT SCREEN (Full Keyboard + 1-Line Input + Message History)
// -------------------------------------------------------------
void TapUI::drawChatScreen() {
  // Clear chat area: Y in 44..116
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 73);

  if (_editing_node_name) {
    _display->setColor(COLOR_NAVY);
    _display->setTextSize(1);
    drawStr(10, 48, "[EDITING NODE NAME]");
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(10, 65, "Type new node name below on keyboard.");
    drawStr(10, 80, "Tap green SEND button to save to radio.");
  } else {
    // Show up to 4 recent messages
    int startIdx = 0;
    if (_msg_count > 4) startIdx = _msg_count - 4;
    int y = 46;
    if (_msg_count == 0) {
      _display->setColor(COLOR_MED_GRAY);
      _display->setTextSize(1);
      drawStr(40, 65, "WisMesh Tap Ready - Type message below");
      drawStr(55, 82, "Tap QUICK or keys to transmit on LoRa");
    } else {
      for (int i = startIdx; i < _msg_count; i++) {
        UIMessage* m = &_messages[i];
        if (m->is_outgoing) {
          _display->setColor(COLOR_NAVY);
          char line[70];
          snprintf(line, sizeof(line), "You: %s", m->text);
          drawStr(8, y, line);
        } else {
          _display->setColor(COLOR_DARK_GRAY);
          char line[70];
          snprintf(line, sizeof(line), "%s: %s", m->sender, m->text);
          drawStr(8, y, line);
        }
        y += 16;
      }
    }
  }

  // Draw 1-Line Input Bar
  drawTextInputBar();

  // Draw Full QWERTY Keyboard
  drawKeyboard();
}

void TapUI::drawTextInputBar() {
  // Box: Y in 118..138
  _display->setColor(COLOR_WHITE);
  _display->fillRect(1, 118, 318, 21);

  _display->setColor(COLOR_NAVY);
  _display->drawRect(1, 118, 318, 21);

  _display->setTextSize(1);
  if (_text_len == 0) {
    _display->setColor(COLOR_MED_GRAY);
    if (_editing_node_name) {
      drawStr(8, 124, "> Enter new node name...");
    } else {
      drawStr(8, 124, "> Type message here...");
    }
  } else {
    _display->setColor(COLOR_BLACK);
    char displayLine[MAX_MSG_TEXT_LEN + 4];
    snprintf(displayLine, sizeof(displayLine), "> %s_", _text_buffer);
    drawStr(8, 124, displayLine);
  }
}

void TapUI::drawKeyboard() {
  // Keyboard area: Y in 140..239 (height 99)
  _display->setColor(COLOR_GRAY_BG);
  _display->fillRect(0, 140, 320, 100);

  _display->setTextSize(1);

  // Row 0: 10 keys
  const char* r0_lower = "qwertyuiop";
  const char* r0_upper = "QWERTYUIOP";
  const char* r0_sym   = "1234567890";
  const char* r0 = (_kb_mode == KB_SYMBOLS) ? r0_sym : ((_kb_mode == KB_UPPER) ? r0_upper : r0_lower);

  for (int k = 0; k < 10; k++) {
    int kx = 1 + k * 32;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(kx, 142, 30, 23);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(kx, 142, 30, 23);

    _display->setColor(COLOR_BLACK);
    char buf[2] = { r0[k], 0 };
    drawStr(kx + 11, 149, buf);
  }

  // Row 1: 9 keys
  const char* r1_lower = "asdfghjkl";
  const char* r1_upper = "ASDFGHJKL";
  const char* r1_sym   = "@#$%&*-+/";
  const char* r1 = (_kb_mode == KB_SYMBOLS) ? r1_sym : ((_kb_mode == KB_UPPER) ? r1_upper : r1_lower);

  for (int k = 0; k < 9; k++) {
    int kx = 17 + k * 32;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(kx, 167, 30, 23);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(kx, 167, 30, 23);

    _display->setColor(COLOR_BLACK);
    char buf[2] = { r1[k], 0 };
    drawStr(kx + 11, 174, buf);
  }

  // Row 2: Shift, 7 keys, DEL
  // Shift / Mode button: X in 1..36
  _display->setColor((_kb_mode == KB_UPPER) ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(1, 192, 36, 23);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(1, 192, 36, 23);
  _display->setColor((_kb_mode == KB_UPPER) ? COLOR_WHITE : COLOR_BLACK);
  drawStr(7, 199, "CAP");

  const char* r2_lower = "zxcvbnm";
  const char* r2_upper = "ZXCVBNM";
  const char* r2_sym   = "()!?:,.";
  const char* r2 = (_kb_mode == KB_SYMBOLS) ? r2_sym : ((_kb_mode == KB_UPPER) ? r2_upper : r2_lower);

  for (int k = 0; k < 7; k++) {
    int kx = 39 + k * 32;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(kx, 192, 30, 23);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(kx, 192, 30, 23);

    _display->setColor(COLOR_BLACK);
    char buf[2] = { r2[k], 0 };
    drawStr(kx + 11, 199, buf);
  }

  // DEL button: X in 264..319
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(264, 192, 55, 23);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(264, 192, 55, 23);
  _display->setColor(COLOR_WHITE);
  drawStr(280, 199, "DEL");

  // Row 3: ?123, QUICK, SPACE, SEND
  // ?123 toggle: X in 1..44
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(1, 217, 44, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(1, 217, 44, 22);
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_WHITE : COLOR_BLACK);
  drawStr(6, 224, (_kb_mode == KB_SYMBOLS) ? "ABC" : "?123");

  // QUICK canned button: X in 47..96
  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(47, 217, 50, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(47, 217, 50, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(54, 224, "QUICK");

  // SPACE bar: X in 99..239
  _display->setColor(COLOR_WHITE);
  _display->fillRect(99, 217, 140, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(99, 217, 140, 22);
  _display->setColor(COLOR_DARK_GRAY);
  drawStr(150, 224, "SPACE");

  // SEND button: X in 242..319
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(242, 217, 77, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(242, 217, 77, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(264, 224, "SEND");
}

void TapUI::handleChatTouch(uint16_t tx, uint16_t ty) {
  // Keyboard touches: Y in 140..239
  if (ty >= 140) {
    handleKeyboardTouch(tx, ty);
  }
}

void TapUI::handleKeyboardTouch(uint16_t tx, uint16_t ty) {
  const char* r0_lower = "qwertyuiop";
  const char* r0_upper = "QWERTYUIOP";
  const char* r0_sym   = "1234567890";
  const char* r0 = (_kb_mode == KB_SYMBOLS) ? r0_sym : ((_kb_mode == KB_UPPER) ? r0_upper : r0_lower);

  const char* r1_lower = "asdfghjkl";
  const char* r1_upper = "ASDFGHJKL";
  const char* r1_sym   = "@#$%&*-+/";
  const char* r1 = (_kb_mode == KB_SYMBOLS) ? r1_sym : ((_kb_mode == KB_UPPER) ? r1_upper : r1_lower);

  const char* r2_lower = "zxcvbnm";
  const char* r2_upper = "ZXCVBNM";
  const char* r2_sym   = "()!?:,.";
  const char* r2 = (_kb_mode == KB_SYMBOLS) ? r2_sym : ((_kb_mode == KB_UPPER) ? r2_upper : r2_lower);

  // Row 0: Y in 142..165
  if (ty >= 142 && ty <= 165) {
    for (int k = 0; k < 10; k++) {
      int kx = 1 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r0[k]);
        return;
      }
    }
  }

  // Row 1: Y in 167..190
  if (ty >= 167 && ty <= 190) {
    for (int k = 0; k < 9; k++) {
      int kx = 17 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r1[k]);
        return;
      }
    }
  }

  // Row 2: Y in 192..215
  if (ty >= 192 && ty <= 215) {
    // Shift toggle: X in 1..36
    if (tx >= 1 && tx <= 36) {
      if (_kb_mode == KB_LOWER) _kb_mode = KB_UPPER;
      else if (_kb_mode == KB_UPPER) _kb_mode = KB_LOWER;
      drawKeyboard();
      return;
    }
    // 7 keys: X in 39..262
    for (int k = 0; k < 7; k++) {
      int kx = 39 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r2[k]);
        return;
      }
    }
    // DEL key: X in 264..319
    if (tx >= 264 && tx <= 319) {
      deleteChar();
      return;
    }
  }

  // Row 3: Y in 217..239
  if (ty >= 217 && ty <= 239) {
    // Mode toggle (?123 / ABC): X in 1..44
    if (tx >= 1 && tx <= 44) {
      if (_kb_mode == KB_SYMBOLS) _kb_mode = KB_LOWER;
      else _kb_mode = KB_SYMBOLS;
      drawKeyboard();
      return;
    }
    // QUICK canned cycle: X in 47..96
    if (tx >= 47 && tx <= 96) {
      const char* phrase = QUICK_RESPONSES[s_quick_idx % NUM_QUICK_RESPONSES];
      s_quick_idx++;
      strncpy(_text_buffer, phrase, sizeof(_text_buffer) - 1);
      _text_buffer[sizeof(_text_buffer) - 1] = 0;
      _text_len = strlen(_text_buffer);
      drawTextInputBar();
      return;
    }
    // SPACE bar: X in 99..239
    if (tx >= 99 && tx <= 239) {
      appendChar(' ');
      return;
    }
    // SEND button: X in 242..319
    if (tx >= 242 && tx <= 319) {
      if (_text_len > 0) {
        if (_editing_node_name) {
          NodePrefs* prefs = the_mesh.getNodePrefs();
          if (prefs) {
            strncpy(prefs->node_name, _text_buffer, sizeof(prefs->node_name) - 1);
            prefs->node_name[sizeof(prefs->node_name) - 1] = 0;
            the_mesh.savePrefs();
          }
          _editing_node_name = false;
          _text_len = 0;
          _text_buffer[0] = 0;
          _current_tab = TAB_RADIO;
          _needs_full_redraw = true;
        } else {
          sendMessage(_text_buffer);
          _text_len = 0;
          _text_buffer[0] = 0;
          drawChatScreen();
        }
      }
      return;
    }
  }
}

// -------------------------------------------------------------
// NODES SCREEN (Mesh Node Discovery & Direct Messaging)
// -------------------------------------------------------------
struct DiscoveredNodeItem {
  char name[24];
  char id_hex[10];
  uint8_t type;
  int32_t lat, lon;
  int contact_idx;
};

void TapUI::drawNodesScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  int total_slots = the_mesh.getTotalContactSlots();
  DiscoveredNodeItem items[32];
  int count = 0;

  // BaseChatMesh contacts start at MAX_ANON_CONTACTS (8)
  for (int i = 8; i < total_slots && count < 32; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.type == 0) continue; // ADV_TYPE_NONE

    if (c.name[0] != 0) {
      strncpy(items[count].name, c.name, 23);
      items[count].name[23] = 0;
    } else {
      snprintf(items[count].name, sizeof(items[count].name), "!%02X%02X%02X%02X",
               c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2], c.id.pub_key[3]);
    }
    mesh::Utils::toHex(items[count].id_hex, c.id.pub_key, 4);
    items[count].type = c.type;
    items[count].lat = c.gps_lat;
    items[count].lon = c.gps_lon;
    items[count].contact_idx = i;
    count++;
  }

  // Header row
  _display->setColor(COLOR_NAVY);
  _display->setTextSize(1);
  char titleBuf[32];
  snprintf(titleBuf, sizeof(titleBuf), "Discovered Mesh Nodes (%d)", count);
  drawStr(10, 50, titleBuf);

  // Discovery beacon button: X in 210..310, Y in 47..65
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(210, 47, 100, 19);
  _display->setColor(COLOR_WHITE);
  drawStr(222, 53, "[DISCOVER]");

  if (count == 0) {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(20, 85, "No mesh contacts discovered yet.");
    drawStr(20, 105, "Make sure other nodes are powered and in range.");
    drawStr(20, 125, "Tap [DISCOVER] above to broadcast a discovery probe.");

    // Big discover button
    _display->setColor(COLOR_GREEN_BTN);
    _display->fillRect(40, 155, 240, 36);
    _display->setColor(COLOR_DARK_GRAY);
    _display->drawRect(40, 155, 240, 36);
    _display->setColor(COLOR_WHITE);
    drawStr(68, 168, "SEND DISCOVERY BEACON");
    return;
  }

  int itemsPerPage = 3;
  int maxPages = (count + itemsPerPage - 1) / itemsPerPage;
  if (_nodes_page >= maxPages) _nodes_page = 0;

  int start = _nodes_page * itemsPerPage;
  int end = start + itemsPerPage;
  if (end > count) end = count;

  int y = 70;
  for (int i = start; i < end; i++) {
    DiscoveredNodeItem* item = &items[i];

    // Card background
    _display->setColor(COLOR_LIGHT_GRAY);
    _display->fillRect(8, y, 304, 46);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(8, y, 304, 46);

    // Node name
    _display->setColor(COLOR_NAVY);
    _display->setTextSize(1);
    drawStr(16, y + 6, item->name);

    // Type badge
    const char* badge = "[CLIENT]";
    uint16_t badgeColor = COLOR_BLUE_KEY;
    if (item->type == 2) { badge = "[REPEATER]"; badgeColor = COLOR_ORANGE_BTN; }
    else if (item->type == 3) { badge = "[ROOM]"; badgeColor = COLOR_PURPLE_BTN; }
    else if (item->type == 4) { badge = "[SENSOR]"; badgeColor = COLOR_CYAN_BTN; }

    _display->setColor(badgeColor);
    drawStr(120, y + 6, badge);

    // Node ID / Loc
    _display->setColor(COLOR_DARK_GRAY);
    char sub[40];
    if (item->lat != 0) {
      snprintf(sub, sizeof(sub), "ID: !%s | Loc: %.2f, %.2f", item->id_hex, (float)item->lat / 1e6f, (float)item->lon / 1e6f);
    } else {
      snprintf(sub, sizeof(sub), "ID: !%s | Mesh Node", item->id_hex);
    }
    drawStr(16, y + 26, sub);

    // DM Button: X in 250..304, Y in y+6..y+40
    _display->setColor(COLOR_GREEN_BTN);
    _display->fillRect(250, y + 8, 54, 30);
    _display->setColor(COLOR_DARK_GRAY);
    _display->drawRect(250, y + 8, 54, 30);
    _display->setColor(COLOR_WHITE);
    drawStr(268, y + 17, "DM");

    y += 50;
  }

  // Paging controls at bottom: Y in 218..238
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(8, 218, 60, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(8, 218, 60, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(18, 224, "< PREV");

  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", _nodes_page + 1, maxPages);
  _display->setColor(COLOR_DARK_GRAY);
  drawStr(130, 224, pageBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(252, 218, 60, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(252, 218, 60, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(264, 224, "NEXT >");
}

void TapUI::handleNodesTouch(uint16_t tx, uint16_t ty) {
  // Discover button at top right: X in 210..310, Y in 47..65
  if (tx >= 210 && tx <= 310 && ty >= 47 && ty <= 65) {
    uint8_t cmd_bytes[6];
    cmd_bytes[0] = 0x01 | 1; // CTL_TYPE_NODE_DISCOVER_REQ
    cmd_bytes[1] = 0xFF;     // Repeaters
    uint32_t tag = 0;
    the_mesh.getRNG()->random((uint8_t*)&tag, 4);
    memcpy(&cmd_bytes[2], &tag, 4);
    mesh::Packet* req = the_mesh.createControlData(cmd_bytes, sizeof(cmd_bytes));
    if (req) {
      the_mesh.sendZeroHop(req);
    }
    _needs_full_redraw = true;
    return;
  }

  // Prev / Next paging: Y in 218..238
  if (ty >= 218 && ty <= 238) {
    if (tx >= 8 && tx <= 68 && _nodes_page > 0) {
      _nodes_page--;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 252 && tx <= 312) {
      _nodes_page++;
      _needs_full_redraw = true;
      return;
    }
  }

  // Row DM button touches: Y in 70..215
  int total_slots = the_mesh.getTotalContactSlots();
  int itemsPerPage = 3;
  int start = _nodes_page * itemsPerPage;
  int validCount = 0;
  int y = 70;

  for (int i = 8; i < total_slots; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.type == 0) continue;

    if (validCount >= start && validCount < start + itemsPerPage) {
      // Check if DM button or card tapped
      if (ty >= y && ty <= y + 46) {
        _active_contact_idx = i;
        _current_tab = TAB_CHAT;
        _needs_full_redraw = true;
        return;
      }
      y += 50;
    }
    validCount++;
  }
}

// -------------------------------------------------------------
// CHANNELS & ROOMS SCREEN
// -------------------------------------------------------------
void TapUI::drawChansScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  // Sub-tabs: [ CHANNELS ] vs [ ROOM SERVERS (BBS) ]
  _display->setColor(!_show_rooms_subview ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(10, 48, 145, 24);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 48, 145, 24);
  _display->setColor(!_show_rooms_subview ? COLOR_WHITE : COLOR_BLACK);
  _display->setTextSize(1);
  drawStr(42, 56, "CHANNELS (4)");

  _display->setColor(_show_rooms_subview ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(165, 48, 145, 24);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(165, 48, 145, 24);
  _display->setColor(_show_rooms_subview ? COLOR_WHITE : COLOR_BLACK);
  drawStr(182, 56, "ROOM SERVERS (BBS)");

  if (!_show_rooms_subview) {
    // Show Channels List
    int y = 80;
    for (int i = 0; i < 4; i++) {
      ChannelDetails ch;
      the_mesh.getChannel(i, ch);

      bool isActive = (_active_contact_idx == -1 && _active_channel == i);

      _display->setColor(COLOR_LIGHT_GRAY);
      _display->fillRect(10, y, 300, 36);
      _display->setColor(isActive ? COLOR_GREEN_BTN : COLOR_MED_GRAY);
      _display->drawRect(10, y, 300, 36);

      _display->setColor(COLOR_NAVY);
      _display->setTextSize(1);
      char chName[32];
      if (i == 0) snprintf(chName, sizeof(chName), "Ch #0: #%s (Primary)", ch.name[0] ? ch.name : "Public");
      else snprintf(chName, sizeof(chName), "Ch #%d: #%s", i, ch.name[0] ? ch.name : "Group");
      drawStr(20, y + 13, chName);

      // Select / Active Button
      if (isActive) {
        _display->setColor(COLOR_GREEN_BTN);
        _display->fillRect(225, y + 6, 75, 24);
        _display->setColor(COLOR_WHITE);
        drawStr(238, y + 13, "ACTIVE");
      } else {
        _display->setColor(COLOR_BLUE_KEY);
        _display->fillRect(225, y + 6, 75, 24);
        _display->setColor(COLOR_WHITE);
        drawStr(238, y + 13, "SELECT");
      }
      y += 40;
    }
  } else {
    // Show Room Servers
    int y = 80;
    int roomCount = 0;
    int total_slots = the_mesh.getTotalContactSlots();

    for (int i = 8; i < total_slots; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.type == 3) { // ADV_TYPE_ROOM
        _display->setColor(COLOR_LIGHT_GRAY);
        _display->fillRect(10, y, 300, 38);
        _display->setColor(COLOR_PURPLE_BTN);
        _display->drawRect(10, y, 300, 38);

        _display->setColor(COLOR_NAVY);
        drawStr(20, y + 8, c.name[0] ? c.name : "BBS Room Server");

        _display->setColor(COLOR_DARK_GRAY);
        char idBuf[20];
        mesh::Utils::toHex(idBuf, c.id.pub_key, 4);
        char sub[32];
        snprintf(sub, sizeof(sub), "Room ID: !%s", idBuf);
        drawStr(20, y + 22, sub);

        _display->setColor(COLOR_PURPLE_BTN);
        _display->fillRect(220, y + 7, 80, 24);
        _display->setColor(COLOR_WHITE);
        drawStr(232, y + 14, "CONNECT");

        y += 44;
        roomCount++;
      }
    }

    if (roomCount == 0) {
      _display->setColor(COLOR_DARK_GRAY);
      drawStr(15, 90, "MeshCore Room Servers are dedicated bulletin");
      drawStr(15, 106, "boards (BBS) for public asynchronous posts.");
      drawStr(15, 130, "No Room Server discovered in radio range yet.");

      _display->setColor(COLOR_PURPLE_BTN);
      _display->fillRect(40, 160, 240, 36);
      _display->setColor(COLOR_WHITE);
      drawStr(70, 173, "SEARCH FOR ROOM SERVERS");
    }
  }
}

void TapUI::handleChansTouch(uint16_t tx, uint16_t ty) {
  // Sub-tabs: Y in 48..72
  if (ty >= 48 && ty <= 72) {
    if (tx >= 10 && tx <= 155) {
      _show_rooms_subview = false;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 165 && tx <= 310) {
      _show_rooms_subview = true;
      _needs_full_redraw = true;
      return;
    }
  }

  if (!_show_rooms_subview) {
    // Channel selection: Y in 80..240
    int y = 80;
    for (int i = 0; i < 4; i++) {
      if (ty >= y && ty <= y + 36) {
        _active_channel = i;
        _active_contact_idx = -1; // Broadcast channel mode
        _current_tab = TAB_CHAT;
        _needs_full_redraw = true;
        return;
      }
      y += 40;
    }
  }
}

// -------------------------------------------------------------
// GPS SCREEN (Meshtastic Simple UI Style)
// -------------------------------------------------------------
void TapUI::drawGPSScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  LocationProvider* loc = sensors.getLocationProvider();
  bool hasLoc = (loc != nullptr && loc->isEnabled());
  bool fixValid = (hasLoc && loc->isValid());

  // Status Banner
  _display->setColor(fixValid ? COLOR_GREEN_BTN : (hasLoc ? COLOR_ORANGE_BTN : COLOR_LIGHT_GRAY));
  _display->fillRect(10, 48, 300, 26);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 48, 300, 26);

  _display->setTextSize(1);
  _display->setColor(fixValid ? COLOR_WHITE : (hasLoc ? COLOR_BLACK : COLOR_DARK_GRAY));
  if (fixValid) {
    drawStr(75, 56, "[ 3D GPS FIX ACQUIRED ]");
  } else if (hasLoc) {
    drawStr(65, 56, "[ SEARCHING SATELLITES... ]");
  } else {
    drawStr(70, 56, "[ GPS RECEIVER STANDBY ]");
  }

  // Telemetry Card: Y in 80..184
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(10, 80, 300, 104);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(10, 80, 300, 104);

  _display->setTextSize(1);
  _display->setColor(COLOR_NAVY);

  char buf[64];
  long sats = (loc && loc->isEnabled()) ? loc->satellitesCount() : 0;
  snprintf(buf, sizeof(buf), "Satellites Tracked: %ld in view", sats);
  drawStr(20, 88, buf);

  double lat = 0.0, lon = 0.0;
  long alt = 0;
  if (loc && loc->isEnabled()) {
    lat = (double)loc->getLatitude() / 1e6;
    lon = (double)loc->getLongitude() / 1e6;
    alt = loc->getAltitude();
  }

  _display->setColor(COLOR_BLACK);
  if (fixValid) {
    snprintf(buf, sizeof(buf), "Latitude:   %.6f deg %c", fabs(lat), lat >= 0 ? 'N' : 'S');
    drawStr(20, 104, buf);

    snprintf(buf, sizeof(buf), "Longitude:  %.6f deg %c", fabs(lon), lon >= 0 ? 'E' : 'W');
    drawStr(20, 120, buf);

    snprintf(buf, sizeof(buf), "Altitude:   %ld m (%ld ft)", alt, (long)(alt * 3.28084));
    drawStr(20, 136, buf);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(20, 104, "Latitude:   Waiting for fix...");
    drawStr(20, 120, "Longitude:  Waiting for fix...");
    drawStr(20, 136, "Altitude:   Waiting for fix...");
  }

  char diag[40];
  if (loc) loc->formatDiagnostics(diag, sizeof(diag));
  else strcpy(diag, "Serial1 NMEA (9600 baud)");
  snprintf(buf, sizeof(buf), "Diagnostics: %s", diag);
  _display->setColor(COLOR_MED_GRAY);
  drawStr(20, 154, buf);

  // Buttons: Broadcast Location & GPS toggle
  // Broadcast Location: X in 10..200, Y in 192..230
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(10, 192, 190, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 192, 190, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(32, 206, "BROADCAST LOCATION");

  // GPS Toggle button: X in 210..310, Y in 192..230
  _display->setColor(hasLoc ? COLOR_GREEN_BTN : COLOR_MED_GRAY);
  _display->fillRect(210, 192, 100, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(210, 192, 100, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(226, 206, hasLoc ? "GPS: ON" : "GPS: OFF");
}

void TapUI::handleGPSTouch(uint16_t tx, uint16_t ty) {
  // Broadcast Location button: X in 10..200, Y in 192..230
  if (tx >= 10 && tx <= 200 && ty >= 192 && ty <= 230) {
    LocationProvider* loc = sensors.getLocationProvider();
    char locMsg[64];
    if (loc && loc->isEnabled() && loc->isValid()) {
      snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f, %.5f (Alt: %ldm)",
               (double)loc->getLatitude() / 1e6, (double)loc->getLongitude() / 1e6, loc->getAltitude());
    } else {
      snprintf(locMsg, sizeof(locMsg), "[LOC] Ping from WisMesh Tap");
    }
    sendMessage(locMsg);
    _current_tab = TAB_CHAT;
    _needs_full_redraw = true;
    return;
  }

  // GPS On/Off Toggle: X in 210..310, Y in 192..230
  if (tx >= 210 && tx <= 310 && ty >= 192 && ty <= 230) {
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc) {
      if (loc->isEnabled()) loc->stop();
      else loc->begin();
    }
    _needs_full_redraw = true;
    return;
  }
}

// -------------------------------------------------------------
// RADIO CONFIG SCREEN (Single Radio Parameter Controls)
// -------------------------------------------------------------
void TapUI::drawRadioScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  _display->setTextSize(1);

  // 1. Node Name row: Y in 47..68
  _display->setColor(COLOR_NAVY);
  char nameBuf[40];
  snprintf(nameBuf, sizeof(nameBuf), "Node Name: %s", the_mesh.getNodePrefs()->node_name);
  drawStr(10, 52, nameBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(235, 47, 75, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(252, 53, "[EDIT]");

  // 2. Frequency row: Y in 72..93
  _display->setColor(COLOR_BLACK);
  char freqBuf[32];
  snprintf(freqBuf, sizeof(freqBuf), "Freq: %.3f MHz", _radio_freq);
  drawStr(10, 77, freqBuf);

  // [-125k] button: X in 140..190
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(140, 72, 50, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(140, 72, 50, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(146, 78, "-125k");

  // [+125k] button: X in 195..245
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(195, 72, 50, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(195, 72, 50, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(201, 78, "+125k");

  // [PRESET] button: X in 250..310
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(250, 72, 60, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(250, 72, 60, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(258, 78, "REGION");

  // 3. Spreading Factor row: Y in 97..118
  char sfBuf[32];
  snprintf(sfBuf, sizeof(sfBuf), "Spreading Factor: SF%u", _radio_sf);
  drawStr(10, 102, sfBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(220, 97, 40, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(220, 97, 40, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(236, 103, "-");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(268, 97, 40, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(268, 97, 40, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(284, 103, "+");

  // 4. Bandwidth row: Y in 122..143
  char bwBuf[32];
  snprintf(bwBuf, sizeof(bwBuf), "Bandwidth: %.1f kHz", _radio_bw);
  drawStr(10, 127, bwBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(220, 122, 40, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(220, 122, 40, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(236, 128, "-");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(268, 122, 40, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(268, 122, 40, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(284, 128, "+");

  // 5. Coding Rate & TX Power row: Y in 147..168
  char crBuf[32];
  snprintf(crBuf, sizeof(crBuf), "CR: 4/%u  |  TX: %d dBm", _radio_cr, _radio_tx_power);
  drawStr(10, 152, crBuf);

  // CR toggle button
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(180, 147, 58, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(180, 147, 58, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(188, 153, "CHG CR");

  // TX toggle button
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(245, 147, 65, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(245, 147, 65, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(253, 153, "CHG TX");

  // 6. Identity hex / status notification: Y in 172..192
  if (millis() < _radio_status_expiry) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(10, 175, _radio_status_msg);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    char idHex[20];
    mesh::Utils::toHex(idHex, the_mesh.self_id.pub_key, 6);
    char idBuf[40];
    snprintf(idBuf, sizeof(idBuf), "Pubkey Prefix: %s...", idHex);
    drawStr(10, 175, idBuf);
  }

  // 7. Action Buttons: Y in 198..234
  // [ APPLY & SAVE ] (X in 10..120)
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(10, 198, 110, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 198, 110, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(22, 211, "APPLY & SAVE");

  // [ BEACON ] (X in 128..218)
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(128, 198, 90, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(128, 198, 90, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(148, 211, "BEACON");

  // [ REBOOT ] (X in 226..310)
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(226, 198, 84, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(226, 198, 84, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(248, 211, "REBOOT");
}

void TapUI::handleRadioTouch(uint16_t tx, uint16_t ty) {
  // [EDIT NAME]: X in 235..310, Y in 47..68
  if (tx >= 235 && tx <= 310 && ty >= 47 && ty <= 68) {
    _editing_node_name = true;
    strncpy(_text_buffer, the_mesh.getNodePrefs()->node_name, sizeof(_text_buffer) - 1);
    _text_buffer[sizeof(_text_buffer) - 1] = 0;
    _text_len = strlen(_text_buffer);
    _current_tab = TAB_CHAT;
    _needs_full_redraw = true;
    return;
  }

  // Frequency adjustments: Y in 72..93
  if (ty >= 72 && ty <= 93) {
    if (tx >= 140 && tx <= 190) { // -125k
      _radio_freq -= 0.125f;
      if (_radio_freq < 400.0f) _radio_freq = 400.0f;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 195 && tx <= 245) { // +125k
      _radio_freq += 0.125f;
      if (_radio_freq > 930.0f) _radio_freq = 930.0f;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 250 && tx <= 310) { // Cycle region presets
      static uint8_t s_reg_idx = 0;
      s_reg_idx = (s_reg_idx + 1) % NUM_REGION_FREQS;
      _radio_freq = REGION_FREQS[s_reg_idx];
      _needs_full_redraw = true;
      return;
    }
  }

  // SF adjustments: Y in 97..118
  if (ty >= 97 && ty <= 118) {
    if (tx >= 220 && tx <= 260 && _radio_sf > 7) {
      _radio_sf--;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 268 && tx <= 308 && _radio_sf < 12) {
      _radio_sf++;
      _needs_full_redraw = true;
      return;
    }
  }

  // Bandwidth adjustments: Y in 122..143
  if (ty >= 122 && ty <= 143) {
    if (tx >= 220 && tx <= 260) {
      if (_radio_bw == 500.0f) _radio_bw = 250.0f;
      else if (_radio_bw == 250.0f) _radio_bw = 125.0f;
      else if (_radio_bw == 125.0f) _radio_bw = 62.5f;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 268 && tx <= 308) {
      if (_radio_bw == 62.5f) _radio_bw = 125.0f;
      else if (_radio_bw == 125.0f) _radio_bw = 250.0f;
      else if (_radio_bw == 250.0f) _radio_bw = 500.0f;
      _needs_full_redraw = true;
      return;
    }
  }

  // CR & TX adjustments: Y in 147..168
  if (ty >= 147 && ty <= 168) {
    if (tx >= 180 && tx <= 238) { // Toggle CR: 5, 6, 7, 8
      _radio_cr++;
      if (_radio_cr > 8) _radio_cr = 5;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 245 && tx <= 310) { // Toggle TX: 14, 17, 20, 22
      if (_radio_tx_power == 14) _radio_tx_power = 17;
      else if (_radio_tx_power == 17) _radio_tx_power = 20;
      else if (_radio_tx_power == 20) _radio_tx_power = 22;
      else _radio_tx_power = 14;
      _needs_full_redraw = true;
      return;
    }
  }

  // Action Buttons: Y in 198..234
  if (ty >= 198 && ty <= 234) {
    // [ APPLY & SAVE ]: X in 10..120
    if (tx >= 10 && tx <= 120) {
      NodePrefs* prefs = the_mesh.getNodePrefs();
      if (prefs) {
        prefs->freq = _radio_freq;
        prefs->sf = _radio_sf;
        prefs->bw = _radio_bw;
        prefs->cr = _radio_cr;
        prefs->tx_power_dbm = _radio_tx_power;
        the_mesh.savePrefs();
      }
      radio_driver.setParams(_radio_freq, _radio_bw, _radio_sf, _radio_cr);
      radio_driver.setTxPower(_radio_tx_power);

      strcpy(_radio_status_msg, "Radio Applied & Saved!");
      _radio_status_expiry = millis() + 4000;
      _needs_full_redraw = true;
      return;
    }

    // [ BEACON ]: X in 128..218
    if (tx >= 128 && tx <= 218) {
      the_mesh.advert();
      strcpy(_radio_status_msg, "Advert Beacon Broadcasted!");
      _radio_status_expiry = millis() + 3000;
      _needs_full_redraw = true;
      return;
    }

    // [ REBOOT ]: X in 226..310
    if (tx >= 226 && tx <= 310) {
      NVIC_SystemReset();
      return;
    }
  }
}
