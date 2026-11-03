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

// Node Card Colors by Contact Type
#define COLOR_CARD_CLIENT_BG   0xFFFF
#define COLOR_CARD_CLIENT_BD   0x2A69
#define COLOR_CARD_REPEAT_BG   0xFFFA
#define COLOR_CARD_REPEAT_BD   0xFC00
#define COLOR_CARD_ROOM_BG     0xF7BF
#define COLOR_CARD_ROOM_BD     0x791B
#define COLOR_CARD_SENSOR_BG   0xEFFE
#define COLOR_CARD_SENSOR_BD   0x04F7

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

static const RFPreset RF_PRESETS[] = {
  { "US915 LongFast",  906.875f, 11, 250.0f, 5 },
  { "US915 MedFast",   906.875f,  9, 250.0f, 5 },
  { "US915 ShortFast", 906.875f,  7, 250.0f, 5 },
  { "US915 WisMesh",   910.525f, 10, 125.0f, 5 },
  { "EU868 LongFast",  869.525f, 11, 250.0f, 5 },
  { "EU868 WisMesh",   869.618f, 10, 125.0f, 5 },
  { "AU915 LongFast",  915.000f, 11, 250.0f, 5 },
  { "EU433 LongFast",  433.175f, 11, 250.0f, 5 },
  { "Custom",            0.000f,  0,   0.0f, 0 }
};
static const int NUM_RF_PRESETS = 9;

static const uint32_t GPS_BAUD_OPTIONS[] = { 9600, 19200, 38400, 57600, 115200 };
static const int NUM_GPS_BAUDS = 5;

static const uint32_t SLEEP_TIMEOUTS[] = { 30, 60, 120, 300, 0 }; // 0 = never
static const char* SLEEP_LABELS[] = { "30s", "1m", "2m", "5m", "Never" };
static const int NUM_SLEEP_OPTIONS = 5;

TapUI::TapUI()
  : _display(nullptr),
    _touch(&Wire, FT6336_DEFAULT_ADDR, 10, 17),
    _current_tab(TAB_CHAT),
    _kb_mode(KB_LOWER),
    _needs_full_redraw(true),
    _needs_chat_messages_redraw(false),
    _last_status_ms(0),
    _backlight_on(true),
    _screen_sleep_seconds(60), // 1 min default
    _last_activity_ms(0),
    _is_screen_sleeping(false),
    _touch_down_ms(0),
    _touch_is_down(false),
    _active_channel(0),
    _active_contact_idx(-1),
    _show_rooms_subview(false),
    _editing_node_name(false),
    _msg_count(0),
    _text_len(0),
    _nodes_page(0),
    _gps_baud(9600),
    _gps_pins_swapped(false),
    _selected_preset_idx(3), // Default US915 WisMesh
    _radio_freq(910.525f),
    _radio_sf(10),
    _radio_bw(125.0f),
    _radio_cr(5),
    _radio_tx_power(22),
    _radio_status_expiry(0)
{
  _text_buffer[0] = 0;
  _radio_status_msg[0] = 0;
  memset(_messages, 0, sizeof(_messages));
}

bool TapUI::begin(DisplayDriver* display) {
  _display = display;
  if (!_display) return false;

  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  _backlight_on = true;
  _last_activity_ms = millis();
  _is_screen_sleeping = false;

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

    // Detect if current params match an RF preset
    _selected_preset_idx = 8; // Default Custom
    for (int p = 0; p < NUM_RF_PRESETS - 1; p++) {
      if (fabs(_radio_freq - RF_PRESETS[p].freq) < 0.05f &&
          _radio_sf == RF_PRESETS[p].sf &&
          fabs(_radio_bw - RF_PRESETS[p].bw) < 5.0f &&
          _radio_cr == RF_PRESETS[p].cr) {
        _selected_preset_idx = p;
        break;
      }
    }
  }

  restartGPS();

  _needs_full_redraw = true;
  return true;
}

void TapUI::restartGPS() {
  Serial1.end();
  if (!_gps_pins_swapped) {
    Serial1.setPins(PIN_SERIAL1_RX, PIN_SERIAL1_TX);
  } else {
    Serial1.setPins(PIN_SERIAL1_TX, PIN_SERIAL1_RX);
  }
  Serial1.begin(_gps_baud);

  LocationProvider* loc = sensors.getLocationProvider();
  if (loc) {
    loc->begin();
  }
}

void TapUI::wakeScreen() {
  _is_screen_sleeping = false;
  digitalWrite(PIN_TFT_BL, HIGH);
  _backlight_on = true;
  _last_activity_ms = millis();
  _needs_full_redraw = true;
}

void TapUI::cycleTab() {
  wakeScreen();
  int next = (int)_current_tab + 1;
  if (next > TAB_RADIO) next = TAB_CHAT;
  _current_tab = (TapTab)next;
  _editing_node_name = false;
  _needs_full_redraw = true;
}

void TapUI::toggleBacklight() {
  wakeScreen();
  _backlight_on = !_backlight_on;
  digitalWrite(PIN_TFT_BL, _backlight_on ? HIGH : LOW);
}

void TapUI::handlePhysicalButtonPress() {
  wakeScreen();
  cycleTab();
}

void TapUI::powerOff() {
  if (_display) {
    _display->setColor(COLOR_NAVY);
    _display->fillRect(0, 0, 320, 240);
    _display->setColor(COLOR_WHITE);
    _display->setTextSize(1);
    drawStr(90, 110, "Shutting down WisMesh Tap...");
    drawStr(80, 130, "Entering Ultra-Low Power Sleep");
  }
  delay(500);
  digitalWrite(PIN_TFT_BL, LOW);
  board.powerOff();
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

void TapUI::applyRFPreset(int preset_idx) {
  if (preset_idx < 0 || preset_idx >= NUM_RF_PRESETS - 1) return;
  _selected_preset_idx = preset_idx;
  _radio_freq = RF_PRESETS[preset_idx].freq;
  _radio_sf   = RF_PRESETS[preset_idx].sf;
  _radio_bw   = RF_PRESETS[preset_idx].bw;
  _radio_cr   = RF_PRESETS[preset_idx].cr;
  _needs_full_redraw = true;
}

void TapUI::addReceivedMessage(const char* sender, const char* text, bool isDirect, const uint8_t* pubkey) {
  _last_activity_ms = millis();
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
  m->channel_idx = 0;
  if (pubkey) memcpy(m->contact_pubkey, pubkey, 4);
  else memset(m->contact_pubkey, 0, 4);
  strncpy(m->target_name, sender ? sender : "", sizeof(m->target_name) - 1);
  m->target_name[sizeof(m->target_name) - 1] = 0;

  if (_current_tab == TAB_CHAT) {
    _needs_chat_messages_redraw = true;
  }
}

void TapUI::addReceivedChannelMessage(uint8_t channel_idx, const char* channel_name, const char* sender, const char* text) {
  _last_activity_ms = millis();
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
  m->is_direct = false;
  m->channel_idx = channel_idx;
  memset(m->contact_pubkey, 0, 4);
  strncpy(m->target_name, channel_name ? channel_name : "Public", sizeof(m->target_name) - 1);
  m->target_name[sizeof(m->target_name) - 1] = 0;

  if (_current_tab == TAB_CHAT && _active_contact_idx == -1 && _active_channel == channel_idx) {
    _needs_chat_messages_redraw = true;
  }
}

void TapUI::addSentMessage(const char* text) {
  _last_activity_ms = millis();
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

  if (_active_contact_idx >= 0) {
    m->is_direct = true;
    m->channel_idx = 0;
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      memcpy(m->contact_pubkey, c.id.pub_key, 4);
      strncpy(m->target_name, c.name, sizeof(m->target_name) - 1);
      m->target_name[sizeof(m->target_name) - 1] = 0;
    }
  } else {
    m->is_direct = false;
    m->channel_idx = _active_channel;
    memset(m->contact_pubkey, 0, 4);
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      strncpy(m->target_name, ch.name[0] ? ch.name : "Public", sizeof(m->target_name) - 1);
      m->target_name[sizeof(m->target_name) - 1] = 0;
    }
  }

  if (_current_tab == TAB_CHAT) {
    _needs_chat_messages_redraw = true;
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

  unsigned long now = millis();

  // Screen sleep inactivity timer
  if (_screen_sleep_seconds > 0 && !_is_screen_sleeping && (now - _last_activity_ms > _screen_sleep_seconds * 1000UL)) {
    _is_screen_sleeping = true;
    digitalWrite(PIN_TFT_BL, LOW);
  }

  TouchPoint pt;
  if (_touch.poll(pt)) {
    if (pt.isPressed) {
      if (!_touch_is_down) {
        _touch_is_down = true;
        _touch_down_ms = now;
      }
    } else {
      _touch_is_down = false;
    }

    if (pt.justPressed) {
      _last_activity_ms = now;

      // If screen was sleeping, wake it up and consume the first tap
      if (_is_screen_sleeping) {
        wakeScreen();
        return;
      }

      uint16_t tx = pt.x;
      uint16_t ty = pt.y;

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
  }

  // Check 3-second long touch in Status Bar (Y < 20) for Power Off
  if (_touch_is_down && (now - _touch_down_ms >= 3000)) {
    _touch_is_down = false;
    powerOff();
    return;
  }

  // Smooth selective redraws
  if (_needs_full_redraw) {
    _needs_full_redraw = false;
    _needs_chat_messages_redraw = false;

    // Redraw screen content area cleanly
    _display->setColor(COLOR_WHITE);
    _display->fillRect(0, 44, 320, 196);

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
  } else if (_needs_chat_messages_redraw) {
    _needs_chat_messages_redraw = false;
    if (_current_tab == TAB_CHAT) {
      drawChatMessagesOnly();
    }
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
// CHAT SCREEN (Shows ONLY Selected Contact / Channel)
// -------------------------------------------------------------
void TapUI::drawChatScreen() {
  drawChatMessagesOnly();
  drawTextInputBar();
  drawKeyboard();
}

void TapUI::drawChatMessagesOnly() {
  // Clear strictly chat history area: Y in 44..116
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 73);

  if (_editing_node_name) {
    _display->setColor(COLOR_NAVY);
    _display->setTextSize(1);
    drawStr(10, 48, "[EDITING NODE NAME]");
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(10, 65, "Type new node name below on keyboard.");
    drawStr(10, 80, "Tap green SEND button to save to radio.");
    return;
  }

  // Filter messages for currently selected target
  ContactInfo active_c;
  bool hasActiveContact = false;
  if (_active_contact_idx >= 0) {
    hasActiveContact = the_mesh.getContactByIdx(_active_contact_idx, active_c);
  }

  // Gather matching messages
  int matchIndices[MAX_UI_MESSAGES];
  int matchCount = 0;

  for (int i = 0; i < _msg_count; i++) {
    UIMessage* m = &_messages[i];
    if (_active_contact_idx >= 0 && hasActiveContact) {
      // Must match direct message with this contact
      if (m->is_direct) {
        if (memcmp(m->contact_pubkey, active_c.id.pub_key, 4) == 0 ||
            strcmp(m->sender, active_c.name) == 0 ||
            strcmp(m->target_name, active_c.name) == 0) {
          matchIndices[matchCount++] = i;
        }
      }
    } else {
      // Must match active channel
      if (!m->is_direct && m->channel_idx == _active_channel) {
        matchIndices[matchCount++] = i;
      }
    }
  }

  if (matchCount == 0) {
    _display->setColor(COLOR_MED_GRAY);
    _display->setTextSize(1);
    if (_active_contact_idx >= 0 && hasActiveContact) {
      char targetPrompt[64];
      snprintf(targetPrompt, sizeof(targetPrompt), "Direct Chat with @%s", active_c.name[0] ? active_c.name : "Node");
      drawStr(30, 65, targetPrompt);
      drawStr(45, 82, "Type message below to send private text");
    } else {
      ChannelDetails ch;
      the_mesh.getChannel(_active_channel, ch);
      char chPrompt[64];
      snprintf(chPrompt, sizeof(chPrompt), "Channel #%s Broadcast", ch.name[0] ? ch.name : "Public");
      drawStr(40, 65, chPrompt);
      drawStr(45, 82, "Type message below to transmit on LoRa");
    }
    return;
  }

  // Display up to 4 recent matching messages
  int startIdx = 0;
  if (matchCount > 4) startIdx = matchCount - 4;
  int y = 46;

  for (int k = startIdx; k < matchCount; k++) {
    UIMessage* m = &_messages[matchIndices[k]];
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
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(1, 217, 44, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(1, 217, 44, 22);
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_WHITE : COLOR_BLACK);
  drawStr(6, 224, (_kb_mode == KB_SYMBOLS) ? "ABC" : "?123");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(47, 217, 50, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(47, 217, 50, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(54, 224, "QUICK");

  _display->setColor(COLOR_WHITE);
  _display->fillRect(99, 217, 140, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(99, 217, 140, 22);
  _display->setColor(COLOR_DARK_GRAY);
  drawStr(150, 224, "SPACE");

  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(242, 217, 77, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(242, 217, 77, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(264, 224, "SEND");
}

void TapUI::handleChatTouch(uint16_t tx, uint16_t ty) {
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
    if (tx >= 1 && tx <= 36) {
      if (_kb_mode == KB_LOWER) _kb_mode = KB_UPPER;
      else if (_kb_mode == KB_UPPER) _kb_mode = KB_LOWER;
      drawKeyboard();
      return;
    }
    for (int k = 0; k < 7; k++) {
      int kx = 39 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r2[k]);
        return;
      }
    }
    if (tx >= 264 && tx <= 319) {
      deleteChar();
      return;
    }
  }

  // Row 3: Y in 217..239
  if (ty >= 217 && ty <= 239) {
    if (tx >= 1 && tx <= 44) {
      if (_kb_mode == KB_SYMBOLS) _kb_mode = KB_LOWER;
      else _kb_mode = KB_SYMBOLS;
      drawKeyboard();
      return;
    }
    if (tx >= 47 && tx <= 96) {
      const char* phrase = QUICK_RESPONSES[s_quick_idx % NUM_QUICK_RESPONSES];
      s_quick_idx++;
      strncpy(_text_buffer, phrase, sizeof(_text_buffer) - 1);
      _text_buffer[sizeof(_text_buffer) - 1] = 0;
      _text_len = strlen(_text_buffer);
      drawTextInputBar();
      return;
    }
    if (tx >= 99 && tx <= 239) {
      appendChar(' ');
      return;
    }
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
          drawTextInputBar();
          drawChatMessagesOnly();
        }
      }
      return;
    }
  }
}

// -------------------------------------------------------------
// NODES SCREEN (Card Colored by Contact Box Type - No Text Overlap)
// -------------------------------------------------------------
struct DiscoveredNodeItem {
  char name[28];
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

  for (int i = 8; i < total_slots && count < 32; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.type == 0) continue; // ADV_TYPE_NONE

    if (c.name[0] != 0) {
      strncpy(items[count].name, c.name, 27);
      items[count].name[27] = 0;
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
  snprintf(titleBuf, sizeof(titleBuf), "Discovered Nodes (%d)", count);
  drawStr(10, 50, titleBuf);

  // Discover button at top right
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(210, 47, 100, 19);
  _display->setColor(COLOR_WHITE);
  drawStr(222, 53, "[DISCOVER]");

  if (count == 0) {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(20, 85, "No mesh contacts discovered yet.");
    drawStr(20, 105, "Make sure other nodes are powered and in range.");
    drawStr(20, 125, "Tap [DISCOVER] above to broadcast a discovery probe.");

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

    // Pick box background and border color by contact type
    uint16_t boxBg = COLOR_CARD_CLIENT_BG;
    uint16_t boxBd = COLOR_CARD_CLIENT_BD;

    if (item->type == 2) { // Repeater
      boxBg = COLOR_CARD_REPEAT_BG;
      boxBd = COLOR_CARD_REPEAT_BD;
    } else if (item->type == 3) { // Room Server
      boxBg = COLOR_CARD_ROOM_BG;
      boxBd = COLOR_CARD_ROOM_BD;
    } else if (item->type == 4) { // Sensor
      boxBg = COLOR_CARD_SENSOR_BG;
      boxBd = COLOR_CARD_SENSOR_BD;
    }

    // Card background & colored border
    _display->setColor(boxBg);
    _display->fillRect(8, y, 304, 46);
    _display->setColor(boxBd);
    _display->drawRect(8, y, 304, 46);

    // Left role accent bar (5px thick)
    _display->fillRect(8, y, 6, 46);

    // Node name - full width, no overlapping badge
    _display->setColor(COLOR_NAVY);
    _display->setTextSize(1);
    drawStr(18, y + 6, item->name);

    // Node ID / Loc
    _display->setColor(COLOR_DARK_GRAY);
    char sub[45];
    if (item->lat != 0) {
      snprintf(sub, sizeof(sub), "ID: !%s | Loc: %.2f, %.2f", item->id_hex, (float)item->lat / 1e6f, (float)item->lon / 1e6f);
    } else {
      snprintf(sub, sizeof(sub), "ID: !%s", item->id_hex);
    }
    drawStr(18, y + 26, sub);

    // DM Button: X in 250..304, Y in y+8..y+38
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
  if (tx >= 210 && tx <= 310 && ty >= 47 && ty <= 65) {
    uint8_t cmd_bytes[6];
    cmd_bytes[0] = 0x01 | 1;
    cmd_bytes[1] = 0xFF;
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
    int y = 80;
    int roomCount = 0;
    int total_slots = the_mesh.getTotalContactSlots();

    for (int i = 8; i < total_slots; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.type == 3) { // ADV_TYPE_ROOM
        _display->setColor(COLOR_CARD_ROOM_BG);
        _display->fillRect(10, y, 300, 38);
        _display->setColor(COLOR_CARD_ROOM_BD);
        _display->drawRect(10, y, 300, 38);
        _display->fillRect(10, y, 6, 38);

        _display->setColor(COLOR_NAVY);
        drawStr(22, y + 8, c.name[0] ? c.name : "BBS Room Server");

        _display->setColor(COLOR_DARK_GRAY);
        char idBuf[20];
        mesh::Utils::toHex(idBuf, c.id.pub_key, 4);
        char sub[32];
        snprintf(sub, sizeof(sub), "Room ID: !%s", idBuf);
        drawStr(22, y + 22, sub);

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
    int y = 80;
    for (int i = 0; i < 4; i++) {
      if (ty >= y && ty <= y + 36) {
        _active_channel = i;
        _active_contact_idx = -1;
        _current_tab = TAB_CHAT;
        _needs_full_redraw = true;
        return;
      }
      y += 40;
    }
  }
}

// -------------------------------------------------------------
// GPS SCREEN (Baud Rate Settings & Real-Time Diagnostics)
// -------------------------------------------------------------
void TapUI::drawGPSScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  LocationProvider* loc = sensors.getLocationProvider();
  bool hasLoc = (loc != nullptr && loc->isEnabled());
  bool fixValid = (hasLoc && loc->isValid());

  // Status Banner: Y in 48..74
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

  // Telemetry Card: Y in 78..184
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(10, 78, 300, 106);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(10, 78, 300, 106);

  _display->setTextSize(1);
  _display->setColor(COLOR_NAVY);

  char buf[70];
  long sats = (loc && loc->isEnabled()) ? loc->satellitesCount() : 0;
  snprintf(buf, sizeof(buf), "Satellites Tracked: %ld in view", sats);
  drawStr(18, 84, buf);

  double lat = 0.0, lon = 0.0;
  long alt = 0;
  if (loc && loc->isEnabled()) {
    lat = (double)loc->getLatitude() / 1e6;
    lon = (double)loc->getLongitude() / 1e6;
    alt = loc->getAltitude();
  }

  _display->setColor(COLOR_BLACK);
  if (fixValid) {
    snprintf(buf, sizeof(buf), "Latitude:  %.6f deg %c", fabs(lat), lat >= 0 ? 'N' : 'S');
    drawStr(18, 98, buf);
    snprintf(buf, sizeof(buf), "Longitude: %.6f deg %c", fabs(lon), lon >= 0 ? 'E' : 'W');
    drawStr(18, 112, buf);
    snprintf(buf, sizeof(buf), "Altitude:  %ld m (%ld ft)", alt, (long)(alt * 3.28084));
    drawStr(18, 126, buf);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(18, 98, "Latitude:  Waiting for fix...");
    drawStr(18, 112, "Longitude: Waiting for fix...");
    drawStr(18, 126, "Altitude:  Waiting for fix...");
  }

  // Diagnostics row
  char diag[45];
  if (loc) loc->formatDiagnostics(diag, sizeof(diag));
  else strcpy(diag, "en:1 sat:0 fix:0");
  snprintf(buf, sizeof(buf), "Diag: %s", diag);
  _display->setColor(COLOR_MED_GRAY);
  drawStr(18, 142, buf);

  // Baud & Pin Settings row: Y in 158..178
  _display->setColor(COLOR_NAVY);
  snprintf(buf, sizeof(buf), "Baud: %lu", _gps_baud);
  drawStr(18, 163, buf);

  // [CHG BAUD] button: X in 110..185
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(110, 158, 75, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(118, 164, "CHG BAUD");

  // [PINS: RX/TX] button: X in 195..302
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(195, 158, 107, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(202, 164, _gps_pins_swapped ? "PINS: RX16/TX15" : "PINS: RX15/TX16");

  // Action Buttons: Y in 190..232
  // [ BROADCAST LOCATION ]: X in 10..200
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(10, 190, 190, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 190, 190, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(32, 204, "BROADCAST LOCATION");

  // [ GPS ON/OFF ]: X in 210..310
  _display->setColor(hasLoc ? COLOR_GREEN_BTN : COLOR_MED_GRAY);
  _display->fillRect(210, 190, 100, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(210, 190, 100, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(226, 204, hasLoc ? "GPS: ON" : "GPS: OFF");
}

void TapUI::handleGPSTouch(uint16_t tx, uint16_t ty) {
  // [CHG BAUD] button: X in 110..185, Y in 158..180
  if (tx >= 110 && tx <= 185 && ty >= 158 && ty <= 180) {
    int bIdx = 0;
    for (int i = 0; i < NUM_GPS_BAUDS; i++) {
      if (GPS_BAUD_OPTIONS[i] == _gps_baud) {
        bIdx = (i + 1) % NUM_GPS_BAUDS;
        break;
      }
    }
    _gps_baud = GPS_BAUD_OPTIONS[bIdx];
    restartGPS();
    _needs_full_redraw = true;
    return;
  }

  // [PINS SWAP] button: X in 195..302, Y in 158..180
  if (tx >= 195 && tx <= 302 && ty >= 158 && ty <= 180) {
    _gps_pins_swapped = !_gps_pins_swapped;
    restartGPS();
    _needs_full_redraw = true;
    return;
  }

  // Broadcast Location button: X in 10..200, Y in 190..232
  if (tx >= 10 && tx <= 200 && ty >= 190 && ty <= 232) {
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

  // GPS On/Off Toggle: X in 210..310, Y in 190..232
  if (tx >= 210 && tx <= 310 && ty >= 190 && ty <= 232) {
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
// RADIO CONFIG SCREEN (RF Presets, Custom, Sleep & Power Off)
// -------------------------------------------------------------
void TapUI::drawRadioScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  _display->setTextSize(1);

  // 1. Node Name row: Y in 46..66
  _display->setColor(COLOR_NAVY);
  char nameBuf[40];
  snprintf(nameBuf, sizeof(nameBuf), "Node: %s", the_mesh.getNodePrefs()->node_name);
  drawStr(10, 51, nameBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(240, 46, 70, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(255, 52, "[EDIT]");

  // 2. RF Presets Cycler: Y in 69..91
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(10, 69, 300, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(10, 69, 300, 22);

  // [< PRESET]
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(11, 70, 32, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(18, 76, "<");

  // Preset Name centered
  _display->setColor(COLOR_NAVY);
  const char* pName = (_selected_preset_idx < NUM_RF_PRESETS) ? RF_PRESETS[_selected_preset_idx].name : "Custom";
  char pBuf[40];
  snprintf(pBuf, sizeof(pBuf), "Preset: %s", pName);
  drawStr(55, 76, pBuf);

  // [PRESET >]
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(277, 70, 32, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(290, 76, ">");

  // 3. Frequency row: Y in 94..114
  _display->setColor(COLOR_BLACK);
  char freqBuf[32];
  snprintf(freqBuf, sizeof(freqBuf), "Freq: %.3f MHz", _radio_freq);
  drawStr(10, 99, freqBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(195, 94, 50, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(195, 94, 50, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(201, 100, "-125k");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(255, 94, 55, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(255, 94, 55, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(262, 100, "+125k");

  // 4. SF & Bandwidth row: Y in 118..138
  char sfbwBuf[40];
  snprintf(sfbwBuf, sizeof(sfbwBuf), "SF: %u  |  BW: %.1f kHz", _radio_sf, _radio_bw);
  drawStr(10, 123, sfbwBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(195, 118, 50, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(195, 118, 50, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(204, 124, "CHG SF");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(255, 118, 55, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(255, 118, 55, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(264, 124, "CHG BW");

  // 5. CR, TX Power & Screen Sleep row: Y in 142..162
  char crtxBuf[40];
  snprintf(crtxBuf, sizeof(crtxBuf), "CR: 4/%u | TX: %d dBm", _radio_cr, _radio_tx_power);
  drawStr(10, 147, crtxBuf);

  // Screen Sleep button: X in 195..310
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(195, 142, 115, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(195, 142, 115, 20);
  _display->setColor(COLOR_BLACK);
  char sleepLabel[24];
  int sIdx = 1;
  for (int i = 0; i < NUM_SLEEP_OPTIONS; i++) {
    if (SLEEP_TIMEOUTS[i] == _screen_sleep_seconds) { sIdx = i; break; }
  }
  snprintf(sleepLabel, sizeof(sleepLabel), "Sleep: %s", SLEEP_LABELS[sIdx]);
  drawStr(205, 148, sleepLabel);

  // 6. Pubkey / status notification row: Y in 166..186
  if (millis() < _radio_status_expiry) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(10, 170, _radio_status_msg);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    char idHex[20];
    mesh::Utils::toHex(idHex, the_mesh.self_id.pub_key, 6);
    char idBuf[40];
    snprintf(idBuf, sizeof(idBuf), "Pubkey: !%s...", idHex);
    drawStr(10, 170, idBuf);
  }

  // 7. Action Buttons: Y in 192..232
  // [ APPLY & SAVE ]: X in 10..115
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(10, 192, 105, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 192, 105, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(18, 206, "APPLY & SAVE");

  // [ BEACON ]: X in 122..205
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(122, 192, 83, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(122, 192, 83, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(140, 206, "BEACON");

  // [ POWER OFF ]: X in 212..310
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(212, 192, 98, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(212, 192, 98, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(226, 206, "POWER OFF");
}

void TapUI::handleRadioTouch(uint16_t tx, uint16_t ty) {
  // [EDIT NAME]: X in 240..310, Y in 46..66
  if (tx >= 240 && tx <= 310 && ty >= 46 && ty <= 66) {
    _editing_node_name = true;
    strncpy(_text_buffer, the_mesh.getNodePrefs()->node_name, sizeof(_text_buffer) - 1);
    _text_buffer[sizeof(_text_buffer) - 1] = 0;
    _text_len = strlen(_text_buffer);
    _current_tab = TAB_CHAT;
    _needs_full_redraw = true;
    return;
  }

  // Preset Prev/Next: Y in 69..91
  if (ty >= 69 && ty <= 91) {
    if (tx >= 10 && tx <= 50) { // Prev preset
      int p = _selected_preset_idx - 1;
      if (p < 0) p = NUM_RF_PRESETS - 1;
      if (p == NUM_RF_PRESETS - 1) _selected_preset_idx = p;
      else applyRFPreset(p);
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 270 && tx <= 310) { // Next preset
      int p = (_selected_preset_idx + 1) % NUM_RF_PRESETS;
      if (p == NUM_RF_PRESETS - 1) _selected_preset_idx = p;
      else applyRFPreset(p);
      _needs_full_redraw = true;
      return;
    }
  }

  // Freq Steppers: Y in 94..114
  if (ty >= 94 && ty <= 114) {
    if (tx >= 195 && tx <= 245) { // -125k
      _radio_freq -= 0.125f;
      if (_radio_freq < 400.0f) _radio_freq = 400.0f;
      _selected_preset_idx = 8; // Custom
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 255 && tx <= 310) { // +125k
      _radio_freq += 0.125f;
      if (_radio_freq > 930.0f) _radio_freq = 930.0f;
      _selected_preset_idx = 8; // Custom
      _needs_full_redraw = true;
      return;
    }
  }

  // SF & BW toggles: Y in 118..138
  if (ty >= 118 && ty <= 138) {
    if (tx >= 195 && tx <= 245) { // CHG SF
      _radio_sf++;
      if (_radio_sf > 12) _radio_sf = 7;
      _selected_preset_idx = 8; // Custom
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 255 && tx <= 310) { // CHG BW
      if (_radio_bw == 500.0f) _radio_bw = 62.5f;
      else if (_radio_bw == 62.5f) _radio_bw = 125.0f;
      else if (_radio_bw == 125.0f) _radio_bw = 250.0f;
      else _radio_bw = 500.0f;
      _selected_preset_idx = 8; // Custom
      _needs_full_redraw = true;
      return;
    }
  }

  // Sleep setting toggle: X in 195..310, Y in 142..162
  if (tx >= 195 && tx <= 310 && ty >= 142 && ty <= 162) {
    int sIdx = 1;
    for (int i = 0; i < NUM_SLEEP_OPTIONS; i++) {
      if (SLEEP_TIMEOUTS[i] == _screen_sleep_seconds) {
        sIdx = (i + 1) % NUM_SLEEP_OPTIONS;
        break;
      }
    }
    _screen_sleep_seconds = SLEEP_TIMEOUTS[sIdx];
    _needs_full_redraw = true;
    return;
  }

  // Action Buttons: Y in 192..232
  if (ty >= 192 && ty <= 232) {
    // [ APPLY & SAVE ]: X in 10..115
    if (tx >= 10 && tx <= 115) {
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

    // [ BEACON ]: X in 122..205
    if (tx >= 122 && tx <= 205) {
      the_mesh.advert();
      strcpy(_radio_status_msg, "Advert Beacon Broadcasted!");
      _radio_status_expiry = millis() + 3000;
      _needs_full_redraw = true;
      return;
    }

    // [ POWER OFF ]: X in 212..310
    if (tx >= 212 && tx <= 310) {
      powerOff();
      return;
    }
  }
}
