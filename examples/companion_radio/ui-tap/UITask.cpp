#include "UITask.h"

UITask::UITask(mesh::MainBoard* board, MultiSerialInterface* serial)
    : AbstractUITask(board, serial),
      _display(nullptr),
      _sensors(nullptr),
      _node_prefs(nullptr) {}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;

  Serial.println("[BOOT] Initializing TapUI and FT6336U touch controller...");
  _ui.begin(display);

  // Initialize physical user button (WB_A1 / AIN1 / Pin 31)
  user_btn.begin();
  Serial.printf("[BOOT] Physical button initialized on AIN1 (Pin %d), initial level=%s\r\n",
                user_btn.getPin(), digitalRead(user_btn.getPin()) == LOW ? "LOW (PRESSED)" : "HIGH (IDLE)");

  Serial.println("[BOOT] UITask initialized successfully.");
}

void UITask::notify(UIEventType t) {
  // Silent notify, no full redraw to prevent glitches
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  Serial.printf("[UI] Direct Message from %s: %s\r\n", from.name, text ? text : "");
  _ui.addReceivedMessage(from.name, text ? text : "", true, from.id.pub_key);
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  Serial.printf("[UI] Channel Message on #%s: %s\r\n", channel_details.name, text ? text : "");
  char sender[24];
  strcpy(sender, channel_details.name[0] ? channel_details.name : "Public");

  uint8_t ch_idx = 0;
  for (int i = 0; i < 4; i++) {
    ChannelDetails ch;
    if (the_mesh.getChannel(i, ch)) {
      if (strcmp(ch.name, channel_details.name) == 0) {
        ch_idx = i;
        break;
      }
    }
  }

  _ui.addReceivedChannelMessage(ch_idx, channel_details.name, sender, text ? text : "");
}

void UITask::onQueueSizeChanged(int msgcount) {
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  Serial.printf("[UI] Discovered contact: %s (lat=%ld lon=%ld)\r\n",
                contact.name, (long)contact.gps_lat, (long)contact.gps_lon);
  if (_ui.getCurrentTab() == TAB_NODES || _ui.getCurrentTab() == TAB_GPS) {
    _ui.markNeedsRedraw();
  }
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
}

void UITask::onACKRecv(uint32_t ack_crc) {
}

bool UITask::onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) {
  struct LPPSizeHelper : public CayenneLPP {
    LPPSizeHelper() : CayenneLPP(0) {}
    uint8_t sizeOf(uint8_t t) { return getTypeSize(t); }
  } lpp;

  // Scan CayenneLPP telemetry response for LPP_GPS (type 136, 9 bytes)
  int i = 0;
  while (i + 2 <= len) {
    uint8_t ch = data[i];
    uint8_t type = data[i + 1];
    (void)ch;
    if (type == LPP_GPS && i + 11 <= len) {
      int32_t raw_lat = ((int32_t)(int8_t)data[i + 2] << 16) |
                        ((int32_t)data[i + 3] << 8) |
                        ((int32_t)data[i + 4]);
      int32_t raw_lon = ((int32_t)(int8_t)data[i + 5] << 16) |
                        ((int32_t)data[i + 6] << 8) |
                        ((int32_t)data[i + 7]);
      ContactInfo* live = the_mesh.lookupContactByPubKey(from.id.pub_key, PUB_KEY_SIZE);
      if (live && (raw_lat != 0 || raw_lon != 0)) {
        live->gps_lat = raw_lat * 100; // 1e-4 deg -> 1e-6 deg
        live->gps_lon = raw_lon * 100;
        _ui.markNeedsRedraw();
      }
      break;
    }
    uint8_t sz = lpp.sizeOf(type);
    if (sz == 0) break;
    i += 2 + sz;
  }
  return false; // allow companion app to receive binary/telemetry response too
}

void UITask::loop() {
  // Raw button transition debug logging on AIN1 (Pin 31)
  static int last_raw_btn = HIGH;
  static uint32_t raw_down_ms = 0;
  int raw_btn = digitalRead(user_btn.getPin());
  if (raw_btn != last_raw_btn) {
    last_raw_btn = raw_btn;
    if (raw_btn == LOW) {
      raw_down_ms = millis();
      Serial.printf("[BUTTON-DBG] AIN1 (Pin %d) PRESSED (LOW)\r\n", user_btn.getPin());
    } else {
      Serial.printf("[BUTTON-DBG] AIN1 (Pin %d) RELEASED (HIGH) after %lu ms\r\n",
                    user_btn.getPin(), (unsigned long)(millis() - raw_down_ms));
    }
  }

  // Check physical user button
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    Serial.println("[BUTTON] Click -> Tab Cycle");
    _ui.handlePhysicalButtonPress();
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    Serial.println("[BUTTON] Double Click -> Toggle Backlight");
    _ui.toggleBacklight();
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    Serial.println("[BUTTON] 3000ms Super Long Press -> Power Off");
    board.powerOff();
  }

  // Update UI engine
  _ui.update();
}
