#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/FT6336Touch.h>
#include "../MyMesh.h"

#define MAX_UI_MESSAGES 24
#define MAX_MSG_TEXT_LEN 64

struct UIMessage {
  char sender[24];
  char text[MAX_MSG_TEXT_LEN];
  bool is_outgoing;
  bool is_direct;
  uint8_t channel_idx;
  uint8_t contact_pubkey[4];
  char target_name[24];
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

struct RFPreset {
  const char* name;
  float freq;
  uint8_t sf;
  float bw;
  uint8_t cr;
};

class TapUI {
private:
  DisplayDriver* _display;
  FT6336Touch _touch;

  TapTab _current_tab;
  KeyboardMode _kb_mode;
  bool _needs_full_redraw;
  bool _needs_chat_messages_redraw;
  unsigned long _last_status_ms;
  bool _backlight_on;

  // Power & Sleep Management
  uint32_t _screen_sleep_seconds; // 30, 60, 120, 300, 0=never
  unsigned long _last_activity_ms;
  bool _is_screen_sleeping;
  unsigned long _touch_down_ms;
  bool _touch_is_down;

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

  // GPS Settings
  uint32_t _gps_baud;
  bool _gps_pins_swapped;

  // Radio adjustment parameters & presets
  int _selected_preset_idx; // 0..NUM_PRESETS (last is Custom)
  float _radio_freq;
  uint8_t _radio_sf;
  float _radio_bw;
  uint8_t _radio_cr;
  int8_t _radio_tx_power;
  char _radio_status_msg[32];
  unsigned long _radio_status_expiry;

  void drawStr(int x, int y, const char* str) {
    if (_display) _display->drawTextLeftAlign(x, y, str);
  }

  void drawStatusBar();
  void drawTabBar();
  void drawChatScreen();
  void drawChatMessagesOnly();
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
  void applyRFPreset(int preset_idx);
  void restartGPS();

public:
  TapUI();

  bool begin(DisplayDriver* display);
  void update();

  void cycleTab();
  void toggleBacklight();
  void wakeScreen();
  void markNeedsRedraw() { _needs_full_redraw = true; }
  void markNeedsChatRedraw() { _needs_chat_messages_redraw = true; }
  TapTab getCurrentTab() const { return _current_tab; }

  void addReceivedMessage(const char* sender, const char* text, bool isDirect, const uint8_t* pubkey = nullptr);
  void addReceivedChannelMessage(uint8_t channel_idx, const char* channel_name, const char* sender, const char* text);
  void addSentMessage(const char* text);
  void sendMessage(const char* text);

  void handlePhysicalButtonPress();
  void powerOff();

  uint8_t getActiveChannel() const { return _active_channel; }
};
