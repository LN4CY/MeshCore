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

  // Initialize physical user button (WB_SW1 / Pin 33)
  user_btn.begin();
  Serial.println("[BOOT] Physical button initialized.");

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
  Serial.printf("[UI] Discovered contact: %s\r\n", contact.name);
  if (_ui.getCurrentTab() == TAB_NODES) {
    _ui.markNeedsRedraw();
  }
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
}

void UITask::onACKRecv(uint32_t ack_crc) {
}

void UITask::loop() {
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
