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

enum TapEditMode {
  EDIT_NONE = 0,
  EDIT_NODE_NAME,
  EDIT_RADIO_FREQ,
  EDIT_BLE_PIN,
  EDIT_CLOCK_TIME,
  EDIT_CHAN_NAME,
  EDIT_CHAN_KEY
};

struct RFPreset {
  const char* name;
  float freq;
  uint8_t sf;
  float bw;
  uint8_t cr;
};

struct TapDeviceSettings {
  uint32_t magic;
  uint8_t brightness_pct;
  uint8_t ble_enabled;
  uint8_t gps_pins_swapped;
  uint8_t reserved;
  uint32_t screen_sleep_secs;
  uint32_t gps_baud;
};

class TapUI {
private:
  DisplayDriver* _display;
  FT6336Touch _touch;

  TapTab _current_tab;
  TapTab _return_tab;
  KeyboardMode _kb_mode;
  TapEditMode _edit_mode;
  bool _needs_full_redraw;
  bool _needs_chat_messages_redraw;
  unsigned long _last_status_ms;
  bool _backlight_on;
  uint8_t _brightness_pct;
  bool _ble_enabled;

  // Power & Sleep Management
  uint32_t _screen_sleep_seconds; // 30, 60, 120, 300, 0=never
  unsigned long _last_activity_ms;
  bool _is_screen_sleeping;
  unsigned long _touch_down_ms;
  bool _touch_is_down;

  // Active channel / DM target
  uint8_t _active_channel;
  int _active_contact_idx; // -1 for channel broadcast

  // Channels & Editor state in TAB_CHANS
  bool _show_rooms_subview;
  int _chans_page;          // 0..2 (3 channels per page across 8 channels)
  int _editing_chan_idx;    // -1 when not editing a channel, 0..7 when configuring
  ChannelDetails _edit_chan_draft;
  uint8_t _edit_chan_key_len; // 16 or 32 bytes

  // Subview in TAB_RADIO: 0 = RF & LoRa, 1 = Device & Sys
  uint8_t _radio_subview;

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
  char _radio_status_msg[42];
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
  void drawChannelEditor();
  void drawGPSScreen();
  void drawRadioScreen();
  void drawRadioRFView();
  void drawRadioDeviceView();
  void drawKeyboard();
  void drawTextInputBar();

  void handleTabTouch(uint16_t tx, uint16_t ty);
  void handleChatTouch(uint16_t tx, uint16_t ty);
  void handleNodesTouch(uint16_t tx, uint16_t ty);
  void handleChansTouch(uint16_t tx, uint16_t ty);
  void handleChannelEditorTouch(uint16_t tx, uint16_t ty);
  void handleGPSTouch(uint16_t tx, uint16_t ty);
  void handleRadioTouch(uint16_t tx, uint16_t ty);
  void handleRadioRFTouch(uint16_t tx, uint16_t ty);
  void handleRadioDeviceTouch(uint16_t tx, uint16_t ty);
  void handleKeyboardTouch(uint16_t tx, uint16_t ty);

  void startKeyboardEdit(TapEditMode mode, const char* initial_text, KeyboardMode kb, TapTab return_tab);
  void commitKeyboardEdit();
  void cancelKeyboardEdit();

  void appendChar(char c);
  void deleteChar();

public:
  TapUI();

  bool begin(DisplayDriver* display);
  void update();

  // Settings persistence
  void loadDeviceSettings();
  void saveDeviceSettings();
  void syncRadioParamsFromPrefs();

  // Tab & Screen management
  void cycleTab();
  void toggleBacklight();
  void wakeScreen();
  void markNeedsRedraw() { _needs_full_redraw = true; }
  void markNeedsChatRedraw() { _needs_chat_messages_redraw = true; }
  TapTab getCurrentTab() const { return _current_tab; }
  void showStatusNotification(const char* msg, uint32_t duration_ms = 3500);

  // Brightness & Sleep
  uint8_t getBrightnessPct() const { return _brightness_pct; }
  void setBrightnessPct(uint8_t pct, bool persist = true);
  uint32_t getScreenSleepSeconds() const { return _screen_sleep_seconds; }
  void setScreenSleepSeconds(uint32_t secs, bool persist = true);

  // BLE & PIN controls
  bool isBLEEnabled() const { return _ble_enabled; }
  void setBLEEnabled(bool en, bool persist = true);
  void setBLEPinLive(uint32_t pin);
  void clearBLEBonds();

  // Clock & Timezone helpers
  void formatLocalDateTime(char* out, size_t max_len, bool include_date = true, bool include_tz = false) const;
  bool parseAndSetLocalDateTime(const char* str);
  bool syncClockFromGPS();
  void adjustClockSeconds(int32_t delta_secs);

  // Channel helpers
  uint8_t getActiveChannel() const { return _active_channel; }
  void setActiveChannel(uint8_t idx);
  bool setChannelHashtag(uint8_t idx, const char* hashtag_name);
  bool setChannelPublic(uint8_t idx);
  bool setChannelRandom(uint8_t idx, const char* name = nullptr);
  bool setChannelCustomKey(uint8_t idx, const char* name, const char* key_str);
  bool clearChannel(uint8_t idx);
  void persistChannels();

  // Radio & GPS helpers
  void applyRFPreset(int preset_idx);
  void applyAndSaveRadioParams();
  int getSelectedPresetIdx() const { return _selected_preset_idx; }
  static int getNumPresets();
  static const RFPreset& getPreset(int idx);
  void restartGPS();
  uint32_t getGPSBaud() const { return _gps_baud; }
  void setGPSBaud(uint32_t baud, bool persist = true);
  bool areGPSPinsSwapped() const { return _gps_pins_swapped; }
  void setGPSPinsSwapped(bool swapped, bool persist = true);

  // Messaging
  void addReceivedMessage(const char* sender, const char* text, bool isDirect, const uint8_t* pubkey = nullptr);
  void addReceivedChannelMessage(uint8_t channel_idx, const char* channel_name, const char* sender, const char* text);
  void addSentMessage(const char* text);
  void sendMessage(const char* text);

  void handlePhysicalButtonPress();
  void powerOff();
};
