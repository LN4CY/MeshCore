#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/FT6336Touch.h>
#include "../MyMesh.h"

#define MAX_UI_MESSAGES 12
#define MAX_MSG_TEXT_LEN 64

struct UIMessage {
  char sender[24];
  char text[MAX_MSG_TEXT_LEN];
  bool is_outgoing;
  bool is_direct;
};

enum TapTab {
  TAB_CHAT = 0,
  TAB_NODES = 1,
  TAB_CHANS = 2,
  TAB_GPS = 3,
  TAB_RADIO = 4
};

enum KeyboardMode {
  KB_LOWER = 0,
  KB_UPPER = 1,
  KB_SYMBOLS = 2
};

class TapUI {
private:
  DisplayDriver* _display;
  FT6336Touch _touch;

  TapTab _current_tab;
  KeyboardMode _kb_mode;
  bool _needs_full_redraw;
  unsigned long _last_status_ms;
  bool _backlight_on;

  // Active channel / DM target
  uint8_t _active_channel;
  int _active_contact_idx; // -1 for channel broadcast

  // Channels vs Rooms subview in TAB_CHANS
  bool _show_rooms_subview;

  // Editing node name mode
  bool _editing_node_name;

  // Messages log
  UIMessage _messages[MAX_UI_MESSAGES];
  uint8_t _msg_count;

  // Text buffer for full keyboard input
  char _text_buffer[MAX_MSG_TEXT_LEN];
  uint8_t _text_len;

  // Nodes scroll / page offset
  int _nodes_page;

  // Radio adjustment parameters (staged before apply)
  float _radio_freq;
  uint8_t _radio_sf;
  float _radio_bw;
  uint8_t _radio_cr;
  int8_t _radio_tx_power;
  bool _radio_rx_boost;
  char _radio_status_msg[32];
  unsigned long _radio_status_expiry;

  void drawStr(int x, int y, const char* str) {
    if (_display) _display->drawTextLeftAlign(x, y, str);
  }

  void drawStatusBar();
  void drawTabBar();
  void drawChatScreen();
  void drawNodesScreen();
  void drawChansScreen();
  void drawGPSScreen();
  void drawRadioScreen();
  void drawKeyboard();
  void drawTextInputBar();

  void handleTabTouch(uint16_t tx, uint16_t ty);
  void handleChatTouch(uint16_t tx, uint16_t ty);
  void handleNodesTouch(uint16_t tx, uint16_t ty);
  void handleChansTouch(uint16_t tx, uint16_t ty);
  void handleGPSTouch(uint16_t tx, uint16_t ty);
  void handleRadioTouch(uint16_t tx, uint16_t ty);
  void handleKeyboardTouch(uint16_t tx, uint16_t ty);

  void appendChar(char c);
  void deleteChar();

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
