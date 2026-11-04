#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/FT6336Touch.h>
#include "../MyMesh.h"

#define MAX_UI_MESSAGES 64
#define MAX_MSG_TEXT_LEN 64
#define MAX_SORTED_NODES 48

struct UIMessage {
  char sender[24];
  char text[MAX_MSG_TEXT_LEN];
  bool is_outgoing;
  bool is_direct;
  bool unread;
  uint8_t channel_idx;
  uint8_t contact_pubkey[4];
  char target_name[24];
  uint32_t timestamp;
};

struct DiscoveredNodeItem {
  char name[28];
  char id_hex[10];
  uint8_t type;
  bool is_fav;
  uint8_t unread_dm;
  int32_t lat, lon;
  int contact_idx;
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
  uint8_t path_hash_size; // 0 = default (1B), or 1..3B (path_hash_mode = path_hash_size - 1)
};

struct TapDeviceSettings {
  uint32_t magic;
  uint8_t brightness_pct;
  uint8_t ble_enabled;
  uint8_t gps_pins_swapped;
  uint8_t dark_mode; // 1 = dark theme (default), 0 = light theme
  uint32_t screen_sleep_secs;
  uint32_t gps_baud;
  int8_t tz_offset;
  uint8_t tz_configured;
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
  bool _needs_tabbar_redraw;
  unsigned long _last_status_ms;
  bool _backlight_on;
  uint8_t _brightness_pct;
  bool _ble_enabled;
  bool _dark_mode;
  int8_t _tz_offset;
  bool _tz_configured;

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

  // Messages log & scroll state
  UIMessage _messages[MAX_UI_MESSAGES];
  uint8_t _msg_count;
  int _chat_scroll_offset; // 0 = latest at bottom, >0 = scrolled up N messages

  // Persistent sorted contact list buffer (avoids 2.6 KB stack allocations in FreeRTOS loop task!)
  DiscoveredNodeItem _sorted_nodes[MAX_SORTED_NODES];
  int _chat_match_indices[MAX_UI_MESSAGES];

  // Text buffer for full keyboard input
  char _text_buffer[MAX_MSG_TEXT_LEN];
  uint8_t _text_len;

  // Nodes scroll / page offset
  int _nodes_page;

  // GPS Settings & Navigation state
  uint32_t _gps_baud;
  bool _gps_pins_swapped;
  uint8_t _gps_subview;      // 0 = Status & Config, 1 = Heading/Speed & Target Node Nav
  int _gps_nav_target_slot;  // Contact slot index (8..total_slots-1) for distance/bearing, or -1
  bool _gps_metric_units;    // true = km/h & km/m, false = mph & mi/ft
  bool _gps_fix_saved_once;
  unsigned long _last_gps_save_ms;
  unsigned long _last_gps_ui_refresh_ms;
  uint32_t _last_gps_card_hash;

  // Radio adjustment parameters & presets
  int _selected_preset_idx; // 0..NUM_PRESETS (last is Custom)
  float _radio_freq;
  uint8_t _radio_sf;
  float _radio_bw;
  uint8_t _radio_cr;
  int8_t _radio_tx_power;
  uint8_t _radio_path_hash_mode; // 0=1B, 1=2B, 2=3B
  char _radio_status_msg[42];
  unsigned long _radio_status_expiry;

  // Theme color helpers
  uint16_t colBg() const { return _dark_mode ? 0x0000 : 0xFFFF; }
  uint16_t colPanelBg() const { return _dark_mode ? 0x1082 : 0xE73C; }
  uint16_t colCardBg() const { return _dark_mode ? 0x18E3 : 0xDEFB; }
  uint16_t colKeyBg() const { return _dark_mode ? 0x2124 : 0xFFFF; }
  uint16_t colText() const { return _dark_mode ? 0xFFFF : 0x0000; }
  uint16_t colSubText() const { return _dark_mode ? 0xBDF7 : 0x4208; }
  uint16_t colMutedText() const { return _dark_mode ? 0x8410 : 0x8410; }
  uint16_t colTitle() const { return _dark_mode ? 0x07FF : 0x1994; }
  uint16_t colBorder() const { return _dark_mode ? 0x4A49 : 0x8410; }

  void drawStr(int x, int y, const char* str) {
    if (_display) _display->drawTextLeftAlign(x, y, str);
  }

  int buildSortedContactList(DiscoveredNodeItem* items, int max_items);

  void drawStatusBar();
  void drawTabBar();
  void drawChatScreen();
  void drawChatMessagesOnly();
  void drawNodesScreen();
  void drawChansScreen();
  void drawChannelEditor();
  void drawGPSScreen();
  void drawGPSStatusView();
  void drawGPSNavView();
  void drawGPSNavInterior();
  void drawLine(int x0, int y0, int x1, int y1, int thickness = 1);
  void drawCircleOutline(int cx, int cy, int r);
  void drawCompassPointer(int cx, int cy, float angle_deg, int r_tip, int r_tail, int half_w, uint16_t color);
  void refreshGPSDataOnly();
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
  void handleGPSStatusTouch(uint16_t tx, uint16_t ty);
  void handleGPSNavTouch(uint16_t tx, uint16_t ty);
  void syncGPSWithMesh();
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

  // Theme & Display management
  bool isDarkMode() const { return _dark_mode; }
  void setDarkMode(bool dark, bool persist = true);
  static uint8_t calcBatteryPercent(uint16_t mv);

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
  int8_t getTimezoneOffset() const;
  void setTimezoneOffset(int8_t tz, bool persist = true);
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

  // Contact Favorites & Unread helpers
  bool toggleContactFavorite(int contact_idx);
  bool setContactFavorite(int contact_idx, bool fav);
  int getTotalUnreadCount() const;
  int getChannelUnreadCount(uint8_t channel_idx) const;
  int getContactUnreadCount(const uint8_t* pubkey_prefix) const;
  uint8_t getMessageCount() const { return _msg_count; }
  const UIMessage& getMessage(uint8_t idx) const { return _messages[idx]; }
  void markAllMessagesRead();

  // Radio & GPS helpers
  void applyRFPreset(int preset_idx);
  void applyAndSaveRadioParams();
  uint8_t getPathHashMode() const { return _radio_path_hash_mode; }
  void setPathHashMode(uint8_t mode);
  int getSelectedPresetIdx() const { return _selected_preset_idx; }
  static int getNumPresets();
  static const RFPreset& getPreset(int idx);
  void restartGPS();
  uint32_t getGPSBaud() const { return _gps_baud; }
  void setGPSBaud(uint32_t baud, bool persist = true);
  bool areGPSPinsSwapped() const { return _gps_pins_swapped; }
  void setGPSPinsSwapped(bool swapped, bool persist = true);
  bool isGPSShareEnabled() const;
  void setGPSShareEnabled(bool share);
  static const char* degToCardinal(float deg);
  static double calcDistanceMeters(double lat1, double lon1, double lat2, double lon2);
  static double calcBearingDegrees(double lat1, double lon1, double lat2, double lon2);

  // Messaging
  void addReceivedMessage(const char* sender, const char* text, bool isDirect, const uint8_t* pubkey = nullptr);
  void addReceivedChannelMessage(uint8_t channel_idx, const char* channel_name, const char* sender, const char* text);
  void addSentMessage(const char* text);
  void sendMessage(const char* text);

  void handlePhysicalButtonPress();
  void powerOff();
};
