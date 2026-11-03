#include "UITask.h"

UITask::UITask(mesh::MainBoard* board, MultiSerialInterface* serial)
    : AbstractUITask(board, serial),
      _display(nullptr),
      _sensors(nullptr),
      _node_prefs(nullptr),
      _client(&mailbox_interface),
      _ui(&_client),
      _ui_task_handle(nullptr),
      _last_batt_check(0) {}

void UITask::uiTaskRunner(void* pvParameters) {
  UITask* task = (UITask*)pvParameters;
  while (1) {
    task->updateUI();
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;

  Serial.println("[BOOT] Initializing InterTaskMailboxInterface...");
  // 1. Initialize the inter-task mailbox queue
  mailbox_interface.begin(16);

  // 2. Register mailbox as a serial interface into MultiSerialInterface
  if (_interfaceManager) {
    _interfaceManager->addInterface(InterfaceType::Mailbox, &mailbox_interface);
    Serial.println("[BOOT] Mailbox registered with MultiSerialInterface.");
  }

  // 3. Initialize UI engine with the ST7789 display and sensors
  Serial.println("[BOOT] Initializing TapUI and FT6336 touch controller...");
  _ui.begin(display, sensors);
  _ui.setBatteryMilliVolts(getBattMilliVolts());

  // 4. On dual-core ESP32, pin UI to core 1.
  // On single-core nRF52840, running cooperatively via loop() ensures zero task starvation
  // and eliminates heap overhead.
#if defined(ESP32)
  xTaskCreatePinnedToCore(uiTaskRunner, "TapUI_Task", 3072, this, 1, &_ui_task_handle, 1);
#else
  _ui_task_handle = nullptr;
#endif
  Serial.println("[BOOT] UITask initialized successfully.");
}

void UITask::notify(UIEventType t) {
  _ui.markNeedsRedraw();
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  _ui.markNeedsRedraw();
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  _ui.markNeedsRedraw();
}

void UITask::onQueueSizeChanged(int msgcount) {
  _ui.markNeedsRedraw();
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  _ui.markNeedsRedraw();
}

void UITask::onControlDataRecv(const mesh::Packet* pkt) {
  _ui.markNeedsRedraw();
}

void UITask::onACKRecv(uint32_t ack_crc) {
  _ui.markNeedsRedraw();
}

void UITask::updateUI() {
  _ui.update();
}

void UITask::loop() {
  // Periodically update battery readings
  unsigned long now = millis();
  if (now - _last_batt_check > 5000) {
    _last_batt_check = now;
    _ui.setBatteryMilliVolts(getBattMilliVolts());
  }

  // If separate FreeRTOS task is not running, run cooperative update here
  if (!_ui_task_handle) {
    updateUI();
  }
}
