#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/FT6336Touch.h>
#include "../MyMesh.h"

#define MAX_UI_MESSAGES 8
#define MAX_MSG_TEXT_LEN 64

struct UIMessage {
  char sender[20];
  char text[MAX_MSG_TEXT_LEN];
  bool is_outgoing;
  bool is_direct;
};

enum TapTab {
  TAB_CHAT = 0,
  TAB_NODES = 1,
  TAB_ROOMS = 2,
  TAB_CONFIG = 3
};

enum InputMode {
  INPUT_MODE_CANNED = 0,
  INPUT_MODE_KEYPAD = 1
};

class TapUI {
private:
  DisplayDriver* _display;
  FT6336Touch _touch;

  TapTab _current_tab;
  InputMode _input_mode;
  bool _needs_full_redraw;
  unsigned long _last_status_ms;
  bool _backlight_on;

  // Active channel / DM target
  uint8_t _active_channel;
  int _active_contact_idx; // -1 for channel broadcast

  // Messages log
  UIMessage _messages[MAX_UI_MESSAGES];
  uint8_t _msg_count;

  // Text buffer for keypad mode
  char _text_buffer[MAX_MSG_TEXT_LEN];
  uint8_t _text_len;
  int _last_key_pressed;
  uint8_t _multitap_idx;
  unsigned long _multitap_expiry;

  void drawStatusBar();
  void drawTabBar();
  void drawChatScreen();
  void drawNodesScreen();
  void drawRoomsScreen();
  void drawConfigScreen();

  void handleTabTouch(uint16_t tx, uint16_t ty);
  void handleChatTouch(uint16_t tx, uint16_t ty);
  void handleNodesTouch(uint16_t tx, uint16_t ty);
  void handleRoomsTouch(uint16_t tx, uint16_t ty);
  void handleConfigTouch(uint16_t tx, uint16_t ty);

  void sendCannedMessage(uint8_t idx);
  void handleKeypadKey(uint8_t keyIdx);

public:
  TapUI();

  bool begin(DisplayDriver* display);
  void update();

  void cycleTab();
  void toggleBacklight();
  void markNeedsRedraw() { _needs_full_redraw = true; }

  void addReceivedMessage(const char* sender, const char* text, bool isDirect);
  void addSentMessage(const char* text);
  void sendMessage(const char* text);

  uint8_t getActiveChannel() const { return _active_channel; }
};
