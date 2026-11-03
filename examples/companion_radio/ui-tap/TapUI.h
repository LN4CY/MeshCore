#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/FT6336Touch.h>
#include <helpers/SensorManager.h>
#include "TapClient.h"
#include "TapVirtualKeyboard.h"

enum TapTab {
  TAB_CHAT = 0,
  TAB_CONTACTS = 1,
  TAB_CHANNELS = 2,
  TAB_INFO = 3
};

class TapUI {
private:
  DisplayDriver* _display;
  FT6336Touch _touch;
  TapClient* _client;
  SensorManager* _sensors;
  TapVirtualKeyboard _keyboard;

  TapTab _current_tab;
  uint8_t _contact_scroll_offset;
  unsigned long _last_status_ms;
  bool _needs_full_redraw;

  // Cached status values
  uint16_t _batt_mv;
  bool _has_gps;

  void drawStatusBar();
  void drawTabBar();
  void drawChatScreen();
  void drawContactsScreen();
  void drawChannelsScreen();
  void drawInfoScreen();

  void handleTabTouch(uint16_t tx, uint16_t ty);
  void handleContactsTouch(uint16_t tx, uint16_t ty);
  void handleChannelsTouch(uint16_t tx, uint16_t ty);
  void handleInfoTouch(uint16_t tx, uint16_t ty);

public:
  TapUI(TapClient* client = nullptr);

  bool begin(DisplayDriver* display, SensorManager* sensors = nullptr);
  void update(); // Main UI tick (called from UI task)

  void setBatteryMilliVolts(uint16_t mv) { _batt_mv = mv; }
  void markNeedsRedraw() { _needs_full_redraw = true; }
};
