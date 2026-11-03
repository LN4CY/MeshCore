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
  _ui.markNeedsRedraw();
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  Serial.printf("[UI] Direct Message from %s: %s\r\n", from.name, text ? text : "");
  _ui.addReceivedMessage(from.name, text ? text : "", true);
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  Serial.printf("[UI] Channel Message on #%s: %s\r\n", channel_details.name, text ? text : "");
  _ui.addReceivedMessage(channel_details.name, text ? text : "", false);
}

void UITask::onQueueSizeChanged(int msgcount) {
  _ui.markNeedsRedraw();
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  Serial.printf("[UI] Discovered contact: %s\r\n", contact.name);
  _ui.markNeedsRedraw();
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
  _ui.markNeedsRedraw();
}

void UITask::onACKRecv(uint32_t ack_crc) {
  _ui.markNeedsRedraw();
}

void UITask::loop() {
  // Check physical user button
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    Serial.println("[BUTTON] Click -> Next Tab");
    _ui.cycleTab();
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    Serial.println("[BUTTON] Double Click -> Toggle Backlight");
    _ui.toggleBacklight();
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    Serial.println("[BUTTON] Long Press -> Broadcast Advert Beacon");
    the_mesh.advert();
    _ui.markNeedsRedraw();
  }

  // Update UI engine
  _ui.update();
}
