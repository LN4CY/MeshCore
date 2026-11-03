#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/ui/MomentaryButton.h>
#include <Arduino.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../MyMesh.h"
#include "TapUI.h"

extern MomentaryButton user_btn;

class UITask : public AbstractUITask {
private:
  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;

  TapUI _ui;

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial);

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
  TapUI& getUI() { return _ui; }

  // MyMesh::Listener overrides
  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int msgcount) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onControlDataRecv(const mesh::Packet* pkt) override;
  void onACKRecv(uint32_t ack_crc) override;
};
