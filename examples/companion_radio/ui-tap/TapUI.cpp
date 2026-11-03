#include "TapUI.h"
#include "target.h"
#include <helpers/TxtDataHelpers.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/sensors/LocationProvider.h>
#include <InternalFileSystem.h>
#include <bluefruit.h>
#include <time.h>

extern unsigned int encode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);
extern unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);
#include "../DataStore.h"
#if defined(BLE_PIN_CODE) && defined(NRF52_PLATFORM)
#include <helpers/nrf52/SerialBLEInterface.h>
extern SerialBLEInterface bluetooth_interface;
#endif
#include "UITask.h"

using namespace Adafruit_LittleFS_Namespace;

extern UITask ui_task;
extern DataStore store;

#define TAP_SETTINGS_FILE  "/tap_settings.bin"
#define TAP_SETTINGS_MAGIC 0x54415031UL // "TAP1"

#define COLOR_NAVY       0x1994
#define COLOR_WHITE      0xFFFF
#define COLOR_BLACK      0x0000
#define COLOR_GRAY_BG    0xE73C
#define COLOR_LIGHT_GRAY 0xDEFB
#define COLOR_MED_GRAY   0x8410
#define COLOR_DARK_GRAY  0x4208
#define COLOR_BLUE_KEY   0x22B9
#define COLOR_GREEN_BTN  0x05E0
#define COLOR_RED_BTN    0xD800
#define COLOR_ORANGE_BTN 0xFC00
#define COLOR_PURPLE_BTN 0x791B
#define COLOR_CYAN_BTN   0x04F7
#define COLOR_ACTIVE_TAB 0xFFFF
#define COLOR_INACT_TAB  0xC618

// Node Card Colors by Contact Type
#define COLOR_CARD_CLIENT_BG   0xFFFF
#define COLOR_CARD_CLIENT_BD   0x2A69
#define COLOR_CARD_REPEAT_BG   0xFFFA
#define COLOR_CARD_REPEAT_BD   0xFC00
#define COLOR_CARD_ROOM_BG     0xF7BF
#define COLOR_CARD_ROOM_BD     0x791B
#define COLOR_CARD_SENSOR_BG   0xEFFE
#define COLOR_CARD_SENSOR_BD   0x04F7

static const char* QUICK_RESPONSES[] = {
  "OK",
  "YES",
  "NO",
  "ON MY WAY",
  "CHECK IN",
  "HELP / SOS"
};
static const uint8_t NUM_QUICK_RESPONSES = 6;
static uint8_t s_quick_idx = 0;

static const RFPreset RF_PRESETS[] = {
  { "US915 LongFast",  906.875f, 11, 250.0f, 5 },
  { "US915 MedFast",   906.875f,  9, 250.0f, 5 },
  { "US915 ShortFast", 906.875f,  7, 250.0f, 5 },
  { "US915 WisMesh",   910.525f, 10, 125.0f, 5 },
  { "EU868 LongFast",  869.525f, 11, 250.0f, 5 },
  { "EU868 WisMesh",   869.618f, 10, 125.0f, 5 },
  { "AU915 LongFast",  915.000f, 11, 250.0f, 5 },
  { "EU433 LongFast",  433.175f, 11, 250.0f, 5 },
  { "Custom",            0.000f,  0,   0.0f, 0 }
};
static const int NUM_RF_PRESETS = 9;

static const uint32_t GPS_BAUD_OPTIONS[] = { 9600, 19200, 38400, 57600, 115200 };
static const int NUM_GPS_BAUDS = 5;

static const uint32_t SLEEP_TIMEOUTS[] = { 30, 60, 120, 300, 0 }; // 0 = never
static const char* SLEEP_LABELS[] = { "30s", "1m", "2m", "5m", "Never" };
static const int NUM_SLEEP_OPTIONS = 5;

static const uint8_t BRIGHTNESS_STEPS[] = { 10, 25, 50, 75, 100 };
static const int NUM_BRIGHTNESS_STEPS = 5;

static bool isChannelSecretZero(const uint8_t* secret, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (secret[i] != 0) return false;
  }
  return true;
}

int TapUI::getNumPresets() {
  return NUM_RF_PRESETS;
}

const RFPreset& TapUI::getPreset(int idx) {
  if (idx < 0 || idx >= NUM_RF_PRESETS) return RF_PRESETS[NUM_RF_PRESETS - 1];
  return RF_PRESETS[idx];
}

TapUI::TapUI()
  : _display(nullptr),
    _touch(&Wire, FT6336_DEFAULT_ADDR, 10, 17),
    _current_tab(TAB_CHAT),
    _return_tab(TAB_CHAT),
    _kb_mode(KB_LOWER),
    _edit_mode(EDIT_NONE),
    _needs_full_redraw(true),
    _needs_chat_messages_redraw(false),
    _last_status_ms(0),
    _backlight_on(true),
    _brightness_pct(100),
    _ble_enabled(true),
    _screen_sleep_seconds(60), // 1 min default
    _last_activity_ms(0),
    _is_screen_sleeping(false),
    _touch_down_ms(0),
    _touch_is_down(false),
    _active_channel(0),
    _active_contact_idx(-1),
    _show_rooms_subview(false),
    _chans_page(0),
    _editing_chan_idx(-1),
    _edit_chan_key_len(16),
    _radio_subview(0),
    _msg_count(0),
    _text_len(0),
    _nodes_page(0),
    _gps_baud(9600),
    _gps_pins_swapped(false),
    _selected_preset_idx(3), // Default US915 WisMesh
    _radio_freq(910.525f),
    _radio_sf(10),
    _radio_bw(125.0f),
    _radio_cr(5),
    _radio_tx_power(22),
    _radio_status_expiry(0)
{
  _text_buffer[0] = 0;
  _radio_status_msg[0] = 0;
  memset(_messages, 0, sizeof(_messages));
  memset(&_edit_chan_draft, 0, sizeof(_edit_chan_draft));
}

void TapUI::loadDeviceSettings() {
  File file(InternalFS);
  if (file.open(TAP_SETTINGS_FILE, FILE_O_READ)) {
    TapDeviceSettings s;
    if (file.read(&s, sizeof(s)) == sizeof(s) && s.magic == TAP_SETTINGS_MAGIC) {
      if (s.brightness_pct >= 5 && s.brightness_pct <= 100) {
        _brightness_pct = s.brightness_pct;
      }
      _ble_enabled = (s.ble_enabled != 0);
      _gps_pins_swapped = (s.gps_pins_swapped != 0);
      _screen_sleep_seconds = s.screen_sleep_secs;
      if (s.gps_baud >= 4800 && s.gps_baud <= 921600) {
        _gps_baud = s.gps_baud;
      }
    }
    file.close();
  }
}

void TapUI::saveDeviceSettings() {
  TapDeviceSettings s;
  s.magic = TAP_SETTINGS_MAGIC;
  s.brightness_pct = _brightness_pct;
  s.ble_enabled = _ble_enabled ? 1 : 0;
  s.gps_pins_swapped = _gps_pins_swapped ? 1 : 0;
  s.reserved = 0;
  s.screen_sleep_secs = _screen_sleep_seconds;
  s.gps_baud = _gps_baud;

  if (InternalFS.exists(TAP_SETTINGS_FILE)) {
    InternalFS.remove(TAP_SETTINGS_FILE);
  }
  File file(InternalFS);
  if (file.open(TAP_SETTINGS_FILE, FILE_O_WRITE)) {
    file.write((const uint8_t*)&s, sizeof(s));
    file.close();
  }
}

void TapUI::syncRadioParamsFromPrefs() {
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    if (prefs->freq > 100.0f) _radio_freq = prefs->freq;
    if (prefs->sf >= 5 && prefs->sf <= 12) _radio_sf = prefs->sf;
    if (prefs->bw > 5.0f) _radio_bw = prefs->bw;
    if (prefs->cr >= 5 && prefs->cr <= 8) _radio_cr = prefs->cr;
    if (prefs->tx_power_dbm != 0) _radio_tx_power = prefs->tx_power_dbm;

    _selected_preset_idx = NUM_RF_PRESETS - 1; // Custom
    for (int p = 0; p < NUM_RF_PRESETS - 1; p++) {
      if (fabs(_radio_freq - RF_PRESETS[p].freq) < 0.05f &&
          _radio_sf == RF_PRESETS[p].sf &&
          fabs(_radio_bw - RF_PRESETS[p].bw) < 5.0f &&
          _radio_cr == RF_PRESETS[p].cr) {
        _selected_preset_idx = p;
        break;
      }
    }
  }
}

bool TapUI::begin(DisplayDriver* display) {
  _display = display;
  if (!_display) return false;

  loadDeviceSettings();

  board.setBacklightBrightness(_brightness_pct);
  board.setBacklight(true);
  _backlight_on = true;
  _last_activity_ms = millis();
  _is_screen_sleeping = false;

  _touch.begin();
  _touch.setRotation(3);

  syncRadioParamsFromPrefs();

#if defined(BLE_PIN_CODE) && defined(NRF52_PLATFORM)
  if (!_ble_enabled && bluetooth_interface.isEnabled()) {
    bluetooth_interface.disable();
  }
#endif

  restartGPS();

  _needs_full_redraw = true;
  return true;
}

void TapUI::showStatusNotification(const char* msg, uint32_t duration_ms) {
  if (!msg) return;
  strncpy(_radio_status_msg, msg, sizeof(_radio_status_msg) - 1);
  _radio_status_msg[sizeof(_radio_status_msg) - 1] = 0;
  _radio_status_expiry = millis() + duration_ms;
  _needs_full_redraw = true;
}

void TapUI::setBrightnessPct(uint8_t pct, bool persist) {
  if (pct < 5) pct = 5;
  if (pct > 100) pct = 100;
  _brightness_pct = pct;
  board.setBacklightBrightness(_brightness_pct);
  if (!_is_screen_sleeping && _backlight_on) {
    board.setBacklight(true);
  }
  if (persist) saveDeviceSettings();
  _needs_full_redraw = true;
}

void TapUI::setScreenSleepSeconds(uint32_t secs, bool persist) {
  _screen_sleep_seconds = secs;
  _last_activity_ms = millis();
  if (persist) saveDeviceSettings();
  _needs_full_redraw = true;
}

void TapUI::setBLEEnabled(bool en, bool persist) {
  _ble_enabled = en;
#if defined(BLE_PIN_CODE) && defined(NRF52_PLATFORM)
  if (_ble_enabled) {
    if (!bluetooth_interface.isEnabled()) {
      bluetooth_interface.enable();
    }
  } else {
    if (bluetooth_interface.isEnabled()) {
      bluetooth_interface.disable();
    }
  }
#endif
  if (persist) saveDeviceSettings();
  _needs_full_redraw = true;
}

void TapUI::setBLEPinLive(uint32_t pin) {
  if (pin < 100000 || pin > 999999) return;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    prefs->ble_pin = pin;
  }
  the_mesh.setBLEPin(pin);
  the_mesh.savePrefs();

  char pinStr[8];
  snprintf(pinStr, sizeof(pinStr), "%06lu", (unsigned long)pin);
  Bluefruit.Security.setPIN(pinStr);
  _needs_full_redraw = true;
}

void TapUI::clearBLEBonds() {
  Bluefruit.Periph.clearBonds();
  showStatusNotification("BLE Bonds Cleared!");
}

void TapUI::formatLocalDateTime(char* out, size_t max_len, bool include_date, bool include_tz) const {
  uint32_t utc = the_mesh.getRTCClock()->getCurrentTime();
  // Default to NodePrefs.adc_multiplier or store timezone in RTC? Let's use simple UTC or local offset
  // Wait: does NodePrefs have timezone? No, let's check if we store utc_offset in TapDeviceSettings or static!
  time_t local_t = (time_t)utc;
  struct tm tm_info;
  gmtime_r(&local_t, &tm_info);

  if (include_date && include_tz) {
    snprintf(out, max_len, "%04d-%02d-%02d %02d:%02d:%02d UTC",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
  } else if (include_date) {
    snprintf(out, max_len, "%04d-%02d-%02d %02d:%02d:%02d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
  } else {
    snprintf(out, max_len, "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
  }
}

bool TapUI::parseAndSetLocalDateTime(const char* str) {
  if (!str) return false;
  int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
  int matched = sscanf(str, "%d-%d-%d %d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc);
  if (matched < 5) {
    // Also support YYYY/MM/DD HH:MM(:SS)
    matched = sscanf(str, "%d/%d/%d %d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc);
  }
  if (matched < 5) return false;
  if (yr < 2020 || yr > 2099 || mo < 1 || mo > 12 || dy < 1 || dy > 31) return false;
  if (hr < 0 || hr > 23 || mn < 0 || mn > 59 || sc < 0 || sc > 59) return false;

  // Convert Y-M-D H:M:S to Unix epoch (UTC)
  static const int days_before_month[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
  uint32_t days = (yr - 1970) * 365 + (yr - 1969) / 4 - (yr - 1901) / 100 + (yr - 1601) / 400;
  days += days_before_month[mo - 1];
  bool is_leap = ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0));
  if (is_leap && mo > 2) days += 1;
  days += (dy - 1);

  uint32_t epoch = days * 86400UL + hr * 3600UL + mn * 60UL + sc;
  the_mesh.getRTCClock()->setCurrentTime(epoch);
  _needs_full_redraw = true;
  return true;
}

bool TapUI::syncClockFromGPS() {
  LocationProvider* loc = sensors.getLocationProvider();
  if (!loc) return false;
  loc->syncTime();
  uint32_t ts = loc->getTimestamp();
  if (ts > 1700000000UL) {
    the_mesh.getRTCClock()->setCurrentTime(ts);
    _needs_full_redraw = true;
    return true;
  }
  return false;
}

void TapUI::adjustClockSeconds(int32_t delta_secs) {
  uint32_t cur = the_mesh.getRTCClock()->getCurrentTime();
  if ((int64_t)cur + delta_secs > 1500000000LL) {
    the_mesh.getRTCClock()->setCurrentTime((uint32_t)((int64_t)cur + delta_secs));
    _needs_full_redraw = true;
  }
}

void TapUI::setActiveChannel(uint8_t idx) {
  if (idx < MAX_GROUP_CHANNELS) {
    _active_channel = idx;
    _active_contact_idx = -1;
    _needs_full_redraw = true;
  }
}

void TapUI::persistChannels() {
  store.saveChannels(&the_mesh);
}

bool TapUI::setChannelHashtag(uint8_t idx, const char* hashtag_name) {
  if (idx >= MAX_GROUP_CHANNELS || !hashtag_name || !hashtag_name[0]) return false;
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  if (hashtag_name[0] == '#') {
    strncpy(ch.name, hashtag_name, sizeof(ch.name) - 1);
  } else {
    snprintf(ch.name, sizeof(ch.name), "#%s", hashtag_name);
  }
  ch.name[sizeof(ch.name) - 1] = 0;
  memset(ch.channel.secret, 0, sizeof(ch.channel.secret));
  mesh::Utils::sha256(ch.channel.secret, 16, (const uint8_t*)ch.name, strlen(ch.name));
  if (the_mesh.setChannel(idx, ch)) {
    persistChannels();
    _needs_full_redraw = true;
    return true;
  }
  return false;
}

bool TapUI::setChannelPublic(uint8_t idx) {
  if (idx >= MAX_GROUP_CHANNELS) return false;
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  strcpy(ch.name, "Public");
  decode_base64((const unsigned char*)"izOH6cXN6mrJ5e26oRXNcg==", 24, ch.channel.secret);
  if (the_mesh.setChannel(idx, ch)) {
    persistChannels();
    _needs_full_redraw = true;
    return true;
  }
  return false;
}

bool TapUI::setChannelRandom(uint8_t idx, const char* name) {
  if (idx >= MAX_GROUP_CHANNELS) return false;
  ChannelDetails ch;
  the_mesh.getChannel(idx, ch);
  if (name && name[0]) {
    strncpy(ch.name, name, sizeof(ch.name) - 1);
    ch.name[sizeof(ch.name) - 1] = 0;
  } else if (ch.name[0] == 0) {
    snprintf(ch.name, sizeof(ch.name), "Chan%u", idx);
  }
  memset(ch.channel.secret, 0, sizeof(ch.channel.secret));
  the_mesh.getRNG()->random(ch.channel.secret, 16);
  if (the_mesh.setChannel(idx, ch)) {
    persistChannels();
    _needs_full_redraw = true;
    return true;
  }
  return false;
}

bool TapUI::setChannelCustomKey(uint8_t idx, const char* name, const char* key_str) {
  if (idx >= MAX_GROUP_CHANNELS || !name || !key_str) return false;
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  strncpy(ch.name, name, sizeof(ch.name) - 1);
  ch.name[sizeof(ch.name) - 1] = 0;

  size_t klen = strlen(key_str);
  bool parsed = false;
  if (klen == 32) {
    parsed = mesh::Utils::fromHex(ch.channel.secret, 16, key_str);
  } else if (klen == 64) {
    parsed = mesh::Utils::fromHex(ch.channel.secret, 32, key_str);
  } else if (klen >= 22 && klen <= 46) {
    uint8_t tmp[48];
    memset(tmp, 0, sizeof(tmp));
    unsigned int out_len = decode_base64((const unsigned char*)key_str, klen, tmp);
    if (out_len == 16 || out_len == 32) {
      memcpy(ch.channel.secret, tmp, out_len);
      parsed = true;
    }
  }
  if (!parsed) return false;

  if (the_mesh.setChannel(idx, ch)) {
    persistChannels();
    _needs_full_redraw = true;
    return true;
  }
  return false;
}

bool TapUI::clearChannel(uint8_t idx) {
  if (idx >= MAX_GROUP_CHANNELS) return false;
  if (idx == 0) {
    return setChannelPublic(0);
  }
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  if (the_mesh.setChannel(idx, ch)) {
    persistChannels();
    if (_active_channel == idx) _active_channel = 0;
    _needs_full_redraw = true;
    return true;
  }
  return false;
}

void TapUI::restartGPS() {
  Serial1.end();
  if (!_gps_pins_swapped) {
    Serial1.setPins(PIN_SERIAL1_RX, PIN_SERIAL1_TX);
  } else {
    Serial1.setPins(PIN_SERIAL1_TX, PIN_SERIAL1_RX);
  }
  Serial1.begin(_gps_baud);

  LocationProvider* loc = sensors.getLocationProvider();
  if (loc) {
    loc->begin();
  }
}

void TapUI::setGPSBaud(uint32_t baud, bool persist) {
  _gps_baud = baud;
  restartGPS();
  if (persist) saveDeviceSettings();
  _needs_full_redraw = true;
}

void TapUI::setGPSPinsSwapped(bool swapped, bool persist) {
  _gps_pins_swapped = swapped;
  restartGPS();
  if (persist) saveDeviceSettings();
  _needs_full_redraw = true;
}

void TapUI::wakeScreen() {
  _is_screen_sleeping = false;
  board.setBacklight(true);
  _backlight_on = true;
  _last_activity_ms = millis();
  _needs_full_redraw = true;
}

void TapUI::cycleTab() {
  wakeScreen();
  int next = (int)_current_tab + 1;
  if (next > TAB_RADIO) next = TAB_CHAT;
  _current_tab = (TapTab)next;
  _edit_mode = EDIT_NONE;
  _editing_chan_idx = -1;
  _needs_full_redraw = true;
}

void TapUI::toggleBacklight() {
  wakeScreen();
  _backlight_on = !_backlight_on;
  board.setBacklight(_backlight_on);
}

void TapUI::handlePhysicalButtonPress() {
  wakeScreen();
  cycleTab();
}

void TapUI::powerOff() {
  if (_display) {
    _display->setColor(COLOR_NAVY);
    _display->fillRect(0, 0, 320, 240);
    _display->setColor(COLOR_WHITE);
    _display->setTextSize(1);
    drawStr(90, 110, "Shutting down WisMesh Tap...");
    drawStr(80, 130, "Entering Ultra-Low Power Sleep");
  }
  delay(500);
  board.setBacklight(false);
  board.powerOff();
}

void TapUI::appendChar(char c) {
  if (_text_len < MAX_MSG_TEXT_LEN - 1) {
    _text_buffer[_text_len++] = c;
    _text_buffer[_text_len] = 0;
    drawTextInputBar();
  }
}

void TapUI::deleteChar() {
  if (_text_len > 0) {
    _text_len--;
    _text_buffer[_text_len] = 0;
    drawTextInputBar();
  }
}

void TapUI::startKeyboardEdit(TapEditMode mode, const char* initial_text, KeyboardMode kb, TapTab return_tab) {
  _edit_mode = mode;
  _return_tab = return_tab;
  _kb_mode = kb;
  if (initial_text) {
    strncpy(_text_buffer, initial_text, sizeof(_text_buffer) - 1);
    _text_buffer[sizeof(_text_buffer) - 1] = 0;
    _text_len = strlen(_text_buffer);
  } else {
    _text_buffer[0] = 0;
    _text_len = 0;
  }
  _current_tab = TAB_CHAT;
  _needs_full_redraw = true;
}

void TapUI::cancelKeyboardEdit() {
  TapTab ret = _return_tab;
  _edit_mode = EDIT_NONE;
  _text_len = 0;
  _text_buffer[0] = 0;
  _current_tab = ret;
  _needs_full_redraw = true;
}

void TapUI::commitKeyboardEdit() {
  TapEditMode mode = _edit_mode;
  TapTab ret = _return_tab;

  if (mode == EDIT_NODE_NAME) {
    if (_text_len > 0) {
      NodePrefs* prefs = the_mesh.getNodePrefs();
      if (prefs) {
        strncpy(prefs->node_name, _text_buffer, sizeof(prefs->node_name) - 1);
        prefs->node_name[sizeof(prefs->node_name) - 1] = 0;
        the_mesh.savePrefs();
        showStatusNotification("Node Name Saved!");
      }
    }
  } else if (mode == EDIT_RADIO_FREQ) {
    float f = atof(_text_buffer);
    if (f >= 150.0f && f <= 960.0f) {
      _radio_freq = f;
      _selected_preset_idx = NUM_RF_PRESETS - 1; // Custom
      showStatusNotification("Freq Updated (Tap APPLY)");
    } else {
      showStatusNotification("Invalid Freq (150-960 MHz)");
    }
  } else if (mode == EDIT_BLE_PIN) {
    uint32_t pin = (uint32_t)atol(_text_buffer);
    if (pin >= 100000 && pin <= 999999) {
      setBLEPinLive(pin);
      showStatusNotification("BLE PIN Updated & Saved!");
    } else {
      showStatusNotification("PIN must be 6 digits!");
    }
  } else if (mode == EDIT_CLOCK_TIME) {
    if (parseAndSetLocalDateTime(_text_buffer)) {
      showStatusNotification("RTC Clock Updated!");
    } else {
      showStatusNotification("Use YYYY-MM-DD HH:MM:SS");
    }
  } else if (mode == EDIT_CHAN_NAME) {
    if (_text_len > 0) {
      strncpy(_edit_chan_draft.name, _text_buffer, sizeof(_edit_chan_draft.name) - 1);
      _edit_chan_draft.name[sizeof(_edit_chan_draft.name) - 1] = 0;
      if (_edit_chan_draft.name[0] == '#') {
        memset(_edit_chan_draft.channel.secret, 0, sizeof(_edit_chan_draft.channel.secret));
        mesh::Utils::sha256(_edit_chan_draft.channel.secret, 16,
                            (const uint8_t*)_edit_chan_draft.name, strlen(_edit_chan_draft.name));
        _edit_chan_key_len = 16;
      }
    }
  } else if (mode == EDIT_CHAN_KEY) {
    size_t klen = strlen(_text_buffer);
    uint8_t new_secret[32];
    memset(new_secret, 0, sizeof(new_secret));
    if (klen == 32 && mesh::Utils::fromHex(new_secret, 16, _text_buffer)) {
      memcpy(_edit_chan_draft.channel.secret, new_secret, 32);
      _edit_chan_key_len = 16;
    } else if (klen == 64 && mesh::Utils::fromHex(new_secret, 32, _text_buffer)) {
      memcpy(_edit_chan_draft.channel.secret, new_secret, 32);
      _edit_chan_key_len = 32;
    } else {
      uint8_t tmp[48];
      memset(tmp, 0, sizeof(tmp));
      unsigned int out_len = decode_base64((const unsigned char*)_text_buffer, klen, tmp);
      if (out_len == 16 || out_len == 32) {
        memcpy(_edit_chan_draft.channel.secret, tmp, out_len);
        _edit_chan_key_len = (uint8_t)out_len;
      }
    }
  }

  _edit_mode = EDIT_NONE;
  _text_len = 0;
  _text_buffer[0] = 0;
  _current_tab = ret;
  _needs_full_redraw = true;
}

void TapUI::applyRFPreset(int preset_idx) {
  if (preset_idx < 0 || preset_idx >= NUM_RF_PRESETS - 1) return;
  _selected_preset_idx = preset_idx;
  _radio_freq = RF_PRESETS[preset_idx].freq;
  _radio_sf   = RF_PRESETS[preset_idx].sf;
  _radio_bw   = RF_PRESETS[preset_idx].bw;
  _radio_cr   = RF_PRESETS[preset_idx].cr;
  _needs_full_redraw = true;
}

void TapUI::applyAndSaveRadioParams() {
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    prefs->freq = _radio_freq;
    prefs->sf = _radio_sf;
    prefs->bw = _radio_bw;
    prefs->cr = _radio_cr;
    prefs->tx_power_dbm = _radio_tx_power;
    the_mesh.savePrefs();
  }
  radio_driver.setParams(_radio_freq, _radio_bw, _radio_sf, _radio_cr);
  radio_driver.setTxPower(_radio_tx_power);
  showStatusNotification("Radio Applied & Saved!");
}

void TapUI::addReceivedMessage(const char* sender, const char* text, bool isDirect, const uint8_t* pubkey) {
  _last_activity_ms = millis();
  if (_msg_count >= MAX_UI_MESSAGES) {
    for (int i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
    _msg_count = MAX_UI_MESSAGES - 1;
  }
  UIMessage* m = &_messages[_msg_count++];
  strncpy(m->sender, sender ? sender : "Mesh", sizeof(m->sender) - 1);
  m->sender[sizeof(m->sender) - 1] = 0;
  strncpy(m->text, text ? text : "", sizeof(m->text) - 1);
  m->text[sizeof(m->text) - 1] = 0;
  m->is_outgoing = false;
  m->is_direct = isDirect;
  m->channel_idx = 0;
  if (pubkey) memcpy(m->contact_pubkey, pubkey, 4);
  else memset(m->contact_pubkey, 0, 4);
  strncpy(m->target_name, sender ? sender : "", sizeof(m->target_name) - 1);
  m->target_name[sizeof(m->target_name) - 1] = 0;

  if (_current_tab == TAB_CHAT) {
    _needs_chat_messages_redraw = true;
  }
}

void TapUI::addReceivedChannelMessage(uint8_t channel_idx, const char* channel_name, const char* sender, const char* text) {
  _last_activity_ms = millis();
  if (_msg_count >= MAX_UI_MESSAGES) {
    for (int i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
    _msg_count = MAX_UI_MESSAGES - 1;
  }
  UIMessage* m = &_messages[_msg_count++];
  strncpy(m->sender, sender ? sender : "Mesh", sizeof(m->sender) - 1);
  m->sender[sizeof(m->sender) - 1] = 0;
  strncpy(m->text, text ? text : "", sizeof(m->text) - 1);
  m->text[sizeof(m->text) - 1] = 0;
  m->is_outgoing = false;
  m->is_direct = false;
  m->channel_idx = channel_idx;
  memset(m->contact_pubkey, 0, 4);
  strncpy(m->target_name, channel_name ? channel_name : "Public", sizeof(m->target_name) - 1);
  m->target_name[sizeof(m->target_name) - 1] = 0;

  if (_current_tab == TAB_CHAT && _active_contact_idx == -1 && _active_channel == channel_idx) {
    _needs_chat_messages_redraw = true;
  }
}

void TapUI::addSentMessage(const char* text) {
  _last_activity_ms = millis();
  if (_msg_count >= MAX_UI_MESSAGES) {
    for (int i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
    _msg_count = MAX_UI_MESSAGES - 1;
  }
  UIMessage* m = &_messages[_msg_count++];
  strcpy(m->sender, "You");
  strncpy(m->text, text ? text : "", sizeof(m->text) - 1);
  m->text[sizeof(m->text) - 1] = 0;
  m->is_outgoing = true;

  if (_active_contact_idx >= 0) {
    m->is_direct = true;
    m->channel_idx = 0;
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      memcpy(m->contact_pubkey, c.id.pub_key, 4);
      strncpy(m->target_name, c.name, sizeof(m->target_name) - 1);
      m->target_name[sizeof(m->target_name) - 1] = 0;
    }
  } else {
    m->is_direct = false;
    m->channel_idx = _active_channel;
    memset(m->contact_pubkey, 0, 4);
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      strncpy(m->target_name, ch.name[0] ? ch.name : "Public", sizeof(m->target_name) - 1);
      m->target_name[sizeof(m->target_name) - 1] = 0;
    }
  }

  if (_current_tab == TAB_CHAT) {
    _needs_chat_messages_redraw = true;
  }
}

void TapUI::sendMessage(const char* text) {
  if (!text || strlen(text) == 0) return;

  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      uint32_t ack = 0;
      uint32_t timeout = 0;
      the_mesh.sendMessage(c, the_mesh.getRTCClock()->getCurrentTimeUnique(), 0, text, ack, timeout);
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      the_mesh.sendGroupMessage(the_mesh.getRTCClock()->getCurrentTimeUnique(), ch.channel, the_mesh.getNodePrefs()->node_name, text, strlen(text));
    }
  }
  addSentMessage(text);
}

void TapUI::update() {
  if (!_display) return;

  unsigned long now = millis();

  // Screen sleep inactivity timer
  if (_screen_sleep_seconds > 0 && !_is_screen_sleeping && (now - _last_activity_ms > _screen_sleep_seconds * 1000UL)) {
    _is_screen_sleeping = true;
    board.setBacklight(false);
  }

  TouchPoint pt;
  _touch.poll(pt);  // NOTE: poll() returns false on release, so always inspect pt fields
  _touch_is_down = pt.isPressed;
  {
    if (pt.justPressed) {
      _last_activity_ms = now;

      // If screen was sleeping, wake it up and consume the first tap
      if (_is_screen_sleeping) {
        wakeScreen();
        return;
      }

      uint16_t tx = pt.x;
      uint16_t ty = pt.y;

      // Check tab bar touch: Y in 20..42
      if (ty >= 20 && ty <= 42) {
        handleTabTouch(tx, ty);
      } else {
        switch (_current_tab) {
          case TAB_CHAT:  handleChatTouch(tx, ty); break;
          case TAB_NODES: handleNodesTouch(tx, ty); break;
          case TAB_CHANS: handleChansTouch(tx, ty); break;
          case TAB_GPS:   handleGPSTouch(tx, ty); break;
          case TAB_RADIO: handleRadioTouch(tx, ty); break;
        }
      }
    }
  }

  // Smooth selective redraws
  if (_needs_full_redraw) {
    _needs_full_redraw = false;
    _needs_chat_messages_redraw = false;

    // Redraw screen content area cleanly
    _display->setColor(COLOR_WHITE);
    _display->fillRect(0, 44, 320, 196);

    drawStatusBar();
    drawTabBar();

    switch (_current_tab) {
      case TAB_CHAT:  drawChatScreen(); break;
      case TAB_NODES: drawNodesScreen(); break;
      case TAB_CHANS: drawChansScreen(); break;
      case TAB_GPS:   drawGPSScreen(); break;
      case TAB_RADIO: drawRadioScreen(); break;
    }
    _last_status_ms = now;
  } else if (_needs_chat_messages_redraw) {
    _needs_chat_messages_redraw = false;
    if (_current_tab == TAB_CHAT) {
      drawChatMessagesOnly();
    }
  } else if (now - _last_status_ms > 4000) {
    drawStatusBar();
    _last_status_ms = now;
  }
}

void TapUI::drawStatusBar() {
  _display->setColor(COLOR_NAVY);
  _display->fillRect(0, 0, 320, 20);

  _display->setTextSize(1);
  _display->setColor(COLOR_WHITE);

  // Left: Recipient info (truncated to ~12 chars so status items fit cleanly)
  char recBuf[18];
  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      if (c.name[0] != 0) snprintf(recBuf, sizeof(recBuf), "@%.11s", c.name);
      else snprintf(recBuf, sizeof(recBuf), "@!%02X%02X%02X", c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2]);
    } else {
      strcpy(recBuf, "@Node");
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      if (ch.name[0] == '#') snprintf(recBuf, sizeof(recBuf), "%.12s", ch.name);
      else snprintf(recBuf, sizeof(recBuf), "#%.11s", ch.name[0] ? ch.name : "Public");
    } else {
      strcpy(recBuf, "#Public");
    }
  }
  drawStr(4, 5, recBuf);

  // Center-Left: BLE status
  if (!_ble_enabled) {
    _display->setColor(COLOR_LIGHT_GRAY);
    drawStr(88, 5, "BT:OFF");
  } else if (ui_task.hasConnection()) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(88, 5, "BT:CONN");
  } else if (the_mesh.getBLEPin() != 0) {
    char pinBuf[16];
    snprintf(pinBuf, sizeof(pinBuf), "PIN:%06lu", (unsigned long)the_mesh.getBLEPin());
    _display->setColor(COLOR_ORANGE_BTN);
    drawStr(84, 5, pinBuf);
  } else {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(88, 5, "BT:ON");
  }

  // Center-Right: GPS status
  LocationProvider* loc = sensors.getLocationProvider();
  if (loc && loc->isEnabled()) {
    if (loc->isValid()) {
      char gpsBuf[14];
      snprintf(gpsBuf, sizeof(gpsBuf), "GPS:%ld", loc->satellitesCount());
      _display->setColor(COLOR_GREEN_BTN);
      drawStr(152, 5, gpsBuf);
    } else {
      _display->setColor(COLOR_ORANGE_BTN);
      drawStr(152, 5, "GPS:..");
    }
  } else {
    _display->setColor(COLOR_LIGHT_GRAY);
    drawStr(152, 5, "GPS:--");
  }

  // Clock HH:MM
  char clkBuf[10];
  formatLocalDateTime(clkBuf, sizeof(clkBuf), false, false);
  _display->setColor(COLOR_WHITE);
  drawStr(204, 5, clkBuf);

  // Right: Battery voltage
  uint32_t mv = board.getBattMilliVolts();
  char batBuf[10];
  snprintf(batBuf, sizeof(batBuf), "%.2fV", (float)mv / 1000.0f);
  _display->setColor(COLOR_WHITE);
  drawStr(278, 5, batBuf);
}

void TapUI::drawTabBar() {
  _display->setColor(COLOR_GRAY_BG);
  _display->fillRect(0, 20, 320, 23);

  const char* tabs[5] = { "CHAT", "NODES", "CHANS", "GPS", "RADIO" };
  for (int i = 0; i < 5; i++) {
    int x = i * 64;
    bool isActive = (i == (int)_current_tab);
    _display->setColor(isActive ? COLOR_ACTIVE_TAB : COLOR_INACT_TAB);
    _display->fillRect(x + 1, 21, 62, 21);

    _display->setColor(isActive ? COLOR_NAVY : COLOR_DARK_GRAY);
    _display->drawRect(x + 1, 21, 62, 21);

    _display->setTextSize(1);
    int tw = strlen(tabs[i]) * 6;
    drawStr(x + (64 - tw) / 2, 27, tabs[i]);

    if (isActive) {
      _display->setColor(COLOR_NAVY);
      _display->fillRect(x + 2, 40, 60, 2);
    }
  }
}

void TapUI::handleTabTouch(uint16_t tx, uint16_t ty) {
  int tabIdx = tx / 64;
  if (tabIdx >= 0 && tabIdx <= 4) {
    if (_current_tab != (TapTab)tabIdx) {
      _current_tab = (TapTab)tabIdx;
      _edit_mode = EDIT_NONE;
      _editing_chan_idx = -1;
      _needs_full_redraw = true;
    }
  }
}

// -------------------------------------------------------------
// CHAT SCREEN & GENERALIZED KEYBOARD INPUT
// -------------------------------------------------------------
void TapUI::drawChatScreen() {
  drawChatMessagesOnly();
  drawTextInputBar();
  drawKeyboard();
}

void TapUI::drawChatMessagesOnly() {
  // Clear strictly chat history area: Y in 44..116
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 73);

  if (_edit_mode != EDIT_NONE) {
    _display->setColor(COLOR_NAVY);
    _display->setTextSize(1);
    const char* title = "[EDITING SETTING]";
    const char* line1 = "Type value below on keyboard.";
    const char* line2 = "Tap green SAVE button when done.";

    switch (_edit_mode) {
      case EDIT_NODE_NAME:
        title = "[EDIT NODE NAME]";
        line1 = "Enter your MeshCore node name (max 31 chars).";
        break;
      case EDIT_RADIO_FREQ:
        title = "[SET LORA FREQUENCY (MHz)]";
        line1 = "Enter frequency in MHz (e.g. 910.525 or 869.525).";
        break;
      case EDIT_BLE_PIN:
        title = "[SET 6-DIGIT BLE PAIRING PIN]";
        line1 = "Enter a 6-digit number (100000 - 999999).";
        break;
      case EDIT_CLOCK_TIME:
        title = "[SET RTC DATE & TIME (UTC)]";
        line1 = "Format: YYYY-MM-DD HH:MM:SS";
        break;
      case EDIT_CHAN_NAME:
        title = "[SET CHANNEL NAME]";
        line1 = "Prefix with # (e.g. #mesh) for auto SHA256 key.";
        break;
      case EDIT_CHAN_KEY:
        title = "[SET CHANNEL KEY (HEX OR BASE64)]";
        line1 = "Enter 32-char Hex or 24-char Base64 PSK.";
        break;
      default: break;
    }

    drawStr(8, 48, title);

    // [CANCEL] button in top right of edit area: X in 245..312, Y in 46..64
    _display->setColor(COLOR_RED_BTN);
    _display->fillRect(245, 46, 67, 18);
    _display->setColor(COLOR_WHITE);
    drawStr(255, 51, "CANCEL");

    _display->setColor(COLOR_DARK_GRAY);
    drawStr(8, 70, line1);
    drawStr(8, 86, line2);
    return;
  }

  // Filter messages for currently selected target
  ContactInfo active_c;
  bool hasActiveContact = false;
  if (_active_contact_idx >= 0) {
    hasActiveContact = the_mesh.getContactByIdx(_active_contact_idx, active_c);
  }

  // Gather matching messages
  int matchIndices[MAX_UI_MESSAGES];
  int matchCount = 0;

  for (int i = 0; i < _msg_count; i++) {
    UIMessage* m = &_messages[i];
    if (_active_contact_idx >= 0 && hasActiveContact) {
      if (m->is_direct) {
        if (memcmp(m->contact_pubkey, active_c.id.pub_key, 4) == 0 ||
            strcmp(m->sender, active_c.name) == 0 ||
            strcmp(m->target_name, active_c.name) == 0) {
          matchIndices[matchCount++] = i;
        }
      }
    } else {
      if (!m->is_direct && m->channel_idx == _active_channel) {
        matchIndices[matchCount++] = i;
      }
    }
  }

  if (matchCount == 0) {
    _display->setColor(COLOR_MED_GRAY);
    _display->setTextSize(1);
    if (_active_contact_idx >= 0 && hasActiveContact) {
      char targetPrompt[64];
      snprintf(targetPrompt, sizeof(targetPrompt), "Direct Chat with @%s", active_c.name[0] ? active_c.name : "Node");
      drawStr(30, 65, targetPrompt);
      drawStr(45, 82, "Type message below to send private text");
    } else {
      ChannelDetails ch;
      the_mesh.getChannel(_active_channel, ch);
      char chPrompt[64];
      if (ch.name[0] == '#') {
        snprintf(chPrompt, sizeof(chPrompt), "Channel %s Broadcast (Ch #%u)", ch.name, _active_channel);
      } else {
        snprintf(chPrompt, sizeof(chPrompt), "Channel #%s Broadcast (Ch #%u)", ch.name[0] ? ch.name : "Public", _active_channel);
      }
      drawStr(30, 65, chPrompt);
      drawStr(45, 82, "Type message below to transmit on LoRa");
    }
    return;
  }

  // Display up to 4 recent matching messages
  int startIdx = 0;
  if (matchCount > 4) startIdx = matchCount - 4;
  int y = 46;

  for (int k = startIdx; k < matchCount; k++) {
    UIMessage* m = &_messages[matchIndices[k]];
    if (m->is_outgoing) {
      _display->setColor(COLOR_NAVY);
      char line[70];
      snprintf(line, sizeof(line), "You: %s", m->text);
      drawStr(8, y, line);
    } else {
      _display->setColor(COLOR_DARK_GRAY);
      char line[70];
      snprintf(line, sizeof(line), "%s: %s", m->sender, m->text);
      drawStr(8, y, line);
    }
    y += 16;
  }
}

void TapUI::drawTextInputBar() {
  // Box: Y in 118..138
  _display->setColor(COLOR_WHITE);
  _display->fillRect(1, 118, 318, 21);

  _display->setColor(COLOR_NAVY);
  _display->drawRect(1, 118, 318, 21);

  _display->setTextSize(1);
  if (_text_len == 0) {
    _display->setColor(COLOR_MED_GRAY);
    if (_edit_mode != EDIT_NONE) {
      drawStr(8, 124, "> Enter value...");
    } else {
      drawStr(8, 124, "> Type message here...");
    }
  } else {
    _display->setColor(COLOR_BLACK);
    char displayLine[MAX_MSG_TEXT_LEN + 4];
    snprintf(displayLine, sizeof(displayLine), "> %s_", _text_buffer);
    drawStr(8, 124, displayLine);
  }
}

void TapUI::drawKeyboard() {
  // Keyboard area: Y in 140..239 (height 99)
  _display->setColor(COLOR_GRAY_BG);
  _display->fillRect(0, 140, 320, 100);

  _display->setTextSize(1);

  // Row 0: 10 keys
  const char* r0_lower = "qwertyuiop";
  const char* r0_upper = "QWERTYUIOP";
  const char* r0_sym   = "1234567890";
  const char* r0 = (_kb_mode == KB_SYMBOLS) ? r0_sym : ((_kb_mode == KB_UPPER) ? r0_upper : r0_lower);

  for (int k = 0; k < 10; k++) {
    int kx = 1 + k * 32;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(kx, 142, 30, 23);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(kx, 142, 30, 23);

    _display->setColor(COLOR_BLACK);
    char buf[2] = { r0[k], 0 };
    drawStr(kx + 11, 149, buf);
  }

  // Row 1: 9 keys
  const char* r1_lower = "asdfghjkl";
  const char* r1_upper = "ASDFGHJKL";
  const char* r1_sym   = "@#$%&*-+/";
  const char* r1 = (_kb_mode == KB_SYMBOLS) ? r1_sym : ((_kb_mode == KB_UPPER) ? r1_upper : r1_lower);

  for (int k = 0; k < 9; k++) {
    int kx = 17 + k * 32;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(kx, 167, 30, 23);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(kx, 167, 30, 23);

    _display->setColor(COLOR_BLACK);
    char buf[2] = { r1[k], 0 };
    drawStr(kx + 11, 174, buf);
  }

  // Row 2: Shift, 7 keys, DEL
  _display->setColor((_kb_mode == KB_UPPER) ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(1, 192, 36, 23);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(1, 192, 36, 23);
  _display->setColor((_kb_mode == KB_UPPER) ? COLOR_WHITE : COLOR_BLACK);
  drawStr(7, 199, "CAP");

  const char* r2_lower = "zxcvbnm";
  const char* r2_upper = "ZXCVBNM";
  const char* r2_sym   = "()!?:,.";
  const char* r2 = (_kb_mode == KB_SYMBOLS) ? r2_sym : ((_kb_mode == KB_UPPER) ? r2_upper : r2_lower);

  for (int k = 0; k < 7; k++) {
    int kx = 39 + k * 32;
    _display->setColor(COLOR_WHITE);
    _display->fillRect(kx, 192, 30, 23);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(kx, 192, 30, 23);

    _display->setColor(COLOR_BLACK);
    char buf[2] = { r2[k], 0 };
    drawStr(kx + 11, 199, buf);
  }

  // DEL button: X in 264..319
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(264, 192, 55, 23);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(264, 192, 55, 23);
  _display->setColor(COLOR_WHITE);
  drawStr(280, 199, "DEL");

  // Row 3: ?123, QUICK/CLEAR, SPACE, SEND/SAVE
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(1, 217, 44, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(1, 217, 44, 22);
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_WHITE : COLOR_BLACK);
  drawStr(6, 224, (_kb_mode == KB_SYMBOLS) ? "ABC" : "?123");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(47, 217, 50, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(47, 217, 50, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(54, 224, (_edit_mode != EDIT_NONE) ? "CLEAR" : "QUICK");

  _display->setColor(COLOR_WHITE);
  _display->fillRect(99, 217, 140, 22);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(99, 217, 140, 22);
  _display->setColor(COLOR_DARK_GRAY);
  drawStr(150, 224, "SPACE");

  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(242, 217, 77, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(242, 217, 77, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(264, 224, (_edit_mode != EDIT_NONE) ? "SAVE" : "SEND");
}

void TapUI::handleChatTouch(uint16_t tx, uint16_t ty) {
  if (_edit_mode != EDIT_NONE && tx >= 240 && tx <= 318 && ty >= 44 && ty <= 68) {
    cancelKeyboardEdit();
    return;
  }
  if (ty >= 140) {
    handleKeyboardTouch(tx, ty);
  }
}

void TapUI::handleKeyboardTouch(uint16_t tx, uint16_t ty) {
  const char* r0_lower = "qwertyuiop";
  const char* r0_upper = "QWERTYUIOP";
  const char* r0_sym   = "1234567890";
  const char* r0 = (_kb_mode == KB_SYMBOLS) ? r0_sym : ((_kb_mode == KB_UPPER) ? r0_upper : r0_lower);

  const char* r1_lower = "asdfghjkl";
  const char* r1_upper = "ASDFGHJKL";
  const char* r1_sym   = "@#$%&*-+/";
  const char* r1 = (_kb_mode == KB_SYMBOLS) ? r1_sym : ((_kb_mode == KB_UPPER) ? r1_upper : r1_lower);

  const char* r2_lower = "zxcvbnm";
  const char* r2_upper = "ZXCVBNM";
  const char* r2_sym   = "()!?:,.";
  const char* r2 = (_kb_mode == KB_SYMBOLS) ? r2_sym : ((_kb_mode == KB_UPPER) ? r2_upper : r2_lower);

  // Row 0: Y in 142..165
  if (ty >= 142 && ty <= 165) {
    for (int k = 0; k < 10; k++) {
      int kx = 1 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r0[k]);
        return;
      }
    }
  }

  // Row 1: Y in 167..190
  if (ty >= 167 && ty <= 190) {
    for (int k = 0; k < 9; k++) {
      int kx = 17 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r1[k]);
        return;
      }
    }
  }

  // Row 2: Y in 192..215
  if (ty >= 192 && ty <= 215) {
    if (tx >= 1 && tx <= 36) {
      if (_kb_mode == KB_LOWER) _kb_mode = KB_UPPER;
      else if (_kb_mode == KB_UPPER) _kb_mode = KB_LOWER;
      drawKeyboard();
      return;
    }
    for (int k = 0; k < 7; k++) {
      int kx = 39 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r2[k]);
        return;
      }
    }
    if (tx >= 264 && tx <= 319) {
      deleteChar();
      return;
    }
  }

  // Row 3: Y in 217..239
  if (ty >= 217 && ty <= 239) {
    if (tx >= 1 && tx <= 44) {
      if (_kb_mode == KB_SYMBOLS) _kb_mode = KB_LOWER;
      else _kb_mode = KB_SYMBOLS;
      drawKeyboard();
      return;
    }
    if (tx >= 47 && tx <= 96) {
      if (_edit_mode != EDIT_NONE) {
        _text_len = 0;
        _text_buffer[0] = 0;
      } else {
        const char* phrase = QUICK_RESPONSES[s_quick_idx % NUM_QUICK_RESPONSES];
        s_quick_idx++;
        strncpy(_text_buffer, phrase, sizeof(_text_buffer) - 1);
        _text_buffer[sizeof(_text_buffer) - 1] = 0;
        _text_len = strlen(_text_buffer);
      }
      drawTextInputBar();
      return;
    }
    if (tx >= 99 && tx <= 239) {
      appendChar(' ');
      return;
    }
    if (tx >= 242 && tx <= 319) {
      if (_edit_mode != EDIT_NONE) {
        commitKeyboardEdit();
      } else if (_text_len > 0) {
        sendMessage(_text_buffer);
        _text_len = 0;
        _text_buffer[0] = 0;
        drawTextInputBar();
        drawChatMessagesOnly();
      }
      return;
    }
  }
}

// -------------------------------------------------------------
// NODES SCREEN
// -------------------------------------------------------------
struct DiscoveredNodeItem {
  char name[28];
  char id_hex[10];
  uint8_t type;
  int32_t lat, lon;
  int contact_idx;
};

void TapUI::drawNodesScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  int total_slots = the_mesh.getTotalContactSlots();
  DiscoveredNodeItem items[32];
  int count = 0;

  for (int i = 8; i < total_slots && count < 32; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.type == 0) continue; // ADV_TYPE_NONE

    if (c.name[0] != 0) {
      strncpy(items[count].name, c.name, 27);
      items[count].name[27] = 0;
    } else {
      snprintf(items[count].name, sizeof(items[count].name), "!%02X%02X%02X%02X",
               c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2], c.id.pub_key[3]);
    }
    mesh::Utils::toHex(items[count].id_hex, c.id.pub_key, 4);
    items[count].type = c.type;
    items[count].lat = c.gps_lat;
    items[count].lon = c.gps_lon;
    items[count].contact_idx = i;
    count++;
  }

  // Header row
  _display->setColor(COLOR_NAVY);
  _display->setTextSize(1);
  char titleBuf[32];
  snprintf(titleBuf, sizeof(titleBuf), "Discovered Nodes (%d)", count);
  drawStr(10, 50, titleBuf);

  // Discover button at top right
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(210, 47, 100, 19);
  _display->setColor(COLOR_WHITE);
  drawStr(222, 53, "[DISCOVER]");

  if (count == 0) {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(20, 85, "No mesh contacts discovered yet.");
    drawStr(20, 105, "Make sure other nodes are powered and in range.");
    drawStr(20, 125, "Tap [DISCOVER] above to broadcast a discovery probe.");

    _display->setColor(COLOR_GREEN_BTN);
    _display->fillRect(40, 155, 240, 36);
    _display->setColor(COLOR_DARK_GRAY);
    _display->drawRect(40, 155, 240, 36);
    _display->setColor(COLOR_WHITE);
    drawStr(68, 168, "SEND DISCOVERY BEACON");
    return;
  }

  int itemsPerPage = 3;
  int maxPages = (count + itemsPerPage - 1) / itemsPerPage;
  if (_nodes_page >= maxPages) _nodes_page = 0;

  int start = _nodes_page * itemsPerPage;
  int end = start + itemsPerPage;
  if (end > count) end = count;

  int y = 70;
  for (int i = start; i < end; i++) {
    DiscoveredNodeItem* item = &items[i];

    uint16_t boxBg = COLOR_CARD_CLIENT_BG;
    uint16_t boxBd = COLOR_CARD_CLIENT_BD;

    if (item->type == 2) {
      boxBg = COLOR_CARD_REPEAT_BG;
      boxBd = COLOR_CARD_REPEAT_BD;
    } else if (item->type == 3) {
      boxBg = COLOR_CARD_ROOM_BG;
      boxBd = COLOR_CARD_ROOM_BD;
    } else if (item->type == 4) {
      boxBg = COLOR_CARD_SENSOR_BG;
      boxBd = COLOR_CARD_SENSOR_BD;
    }

    _display->setColor(boxBg);
    _display->fillRect(8, y, 304, 46);
    _display->setColor(boxBd);
    _display->drawRect(8, y, 304, 46);
    _display->fillRect(8, y, 6, 46);

    _display->setColor(COLOR_NAVY);
    _display->setTextSize(1);
    drawStr(18, y + 6, item->name);

    _display->setColor(COLOR_DARK_GRAY);
    char sub[45];
    if (item->lat != 0) {
      snprintf(sub, sizeof(sub), "ID: !%s | Loc: %.2f, %.2f", item->id_hex, (float)item->lat / 1e6f, (float)item->lon / 1e6f);
    } else {
      snprintf(sub, sizeof(sub), "ID: !%s", item->id_hex);
    }
    drawStr(18, y + 26, sub);

    _display->setColor(COLOR_GREEN_BTN);
    _display->fillRect(250, y + 8, 54, 30);
    _display->setColor(COLOR_DARK_GRAY);
    _display->drawRect(250, y + 8, 54, 30);
    _display->setColor(COLOR_WHITE);
    drawStr(268, y + 17, "DM");

    y += 50;
  }

  // Paging controls at bottom: Y in 218..238
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(8, 218, 60, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(8, 218, 60, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(18, 224, "< PREV");

  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", _nodes_page + 1, maxPages);
  _display->setColor(COLOR_DARK_GRAY);
  drawStr(130, 224, pageBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(252, 218, 60, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(252, 218, 60, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(264, 224, "NEXT >");
}

void TapUI::handleNodesTouch(uint16_t tx, uint16_t ty) {
  if ((tx >= 210 && tx <= 310 && ty >= 47 && ty <= 65) ||
      (tx >= 40 && tx <= 280 && ty >= 155 && ty <= 191)) {
    uint8_t cmd_bytes[6];
    cmd_bytes[0] = 0x01 | 1;
    cmd_bytes[1] = 0xFF;
    uint32_t tag = 0;
    the_mesh.getRNG()->random((uint8_t*)&tag, 4);
    memcpy(&cmd_bytes[2], &tag, 4);
    mesh::Packet* req = the_mesh.createControlData(cmd_bytes, sizeof(cmd_bytes));
    if (req) {
      the_mesh.sendZeroHop(req);
    }
    the_mesh.advert();
    _needs_full_redraw = true;
    return;
  }

  if (ty >= 218 && ty <= 238) {
    if (tx >= 8 && tx <= 68 && _nodes_page > 0) {
      _nodes_page--;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 252 && tx <= 312) {
      _nodes_page++;
      _needs_full_redraw = true;
      return;
    }
  }

  int total_slots = the_mesh.getTotalContactSlots();
  int itemsPerPage = 3;
  int start = _nodes_page * itemsPerPage;
  int validCount = 0;
  int y = 70;

  for (int i = 8; i < total_slots; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(i, c)) continue;
    if (c.type == 0) continue;

    if (validCount >= start && validCount < start + itemsPerPage) {
      if (ty >= y && ty <= y + 46) {
        _active_contact_idx = i;
        _current_tab = TAB_CHAT;
        _needs_full_redraw = true;
        return;
      }
      y += 50;
    }
    validCount++;
  }
}

// -------------------------------------------------------------
// CHANNELS & ROOMS SCREEN + CHANNEL CONFIG EDITOR (All 8 Channels)
// -------------------------------------------------------------
void TapUI::drawChansScreen() {
  if (_editing_chan_idx >= 0 && _editing_chan_idx < MAX_GROUP_CHANNELS) {
    drawChannelEditor();
    return;
  }

  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  // Sub-tab selector: CHANNELS (8) | ROOM SERVERS (BBS)
  _display->setColor(!_show_rooms_subview ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(8, 46, 148, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(8, 46, 148, 22);
  _display->setColor(!_show_rooms_subview ? COLOR_WHITE : COLOR_BLACK);
  _display->setTextSize(1);
  drawStr(36, 53, "CHANNELS (0-7)");

  _display->setColor(_show_rooms_subview ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(164, 46, 148, 22);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(164, 46, 148, 22);
  _display->setColor(_show_rooms_subview ? COLOR_WHITE : COLOR_BLACK);
  drawStr(182, 53, "ROOM SERVERS (BBS)");

  if (!_show_rooms_subview) {
    int itemsPerPage = 3;
    int maxPages = (MAX_GROUP_CHANNELS + itemsPerPage - 1) / itemsPerPage;
    if (_chans_page < 0) _chans_page = 0;
    if (_chans_page >= maxPages) _chans_page = maxPages - 1;

    int start = _chans_page * itemsPerPage;
    int end = start + itemsPerPage;
    if (end > MAX_GROUP_CHANNELS) end = MAX_GROUP_CHANNELS;

    int y = 72;
    for (int i = start; i < end; i++) {
      ChannelDetails ch;
      memset(&ch, 0, sizeof(ch));
      the_mesh.getChannel(i, ch);

      bool isEmpty = (ch.name[0] == 0 && isChannelSecretZero(ch.channel.secret, 16) && i > 0);
      bool isActive = (_active_contact_idx == -1 && _active_channel == i);

      _display->setColor(COLOR_LIGHT_GRAY);
      _display->fillRect(8, y, 304, 44);
      _display->setColor(isActive ? COLOR_GREEN_BTN : COLOR_MED_GRAY);
      _display->drawRect(8, y, 304, 44);

      _display->setColor(COLOR_NAVY);
      _display->setTextSize(1);
      char chTitle[34];
      if (isEmpty) {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: (Unconfigured)", i);
      } else if (ch.name[0] == '#') {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: %s", i, ch.name);
      } else {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: #%s", i, ch.name[0] ? ch.name : "Public");
      }
      drawStr(14, y + 7, chTitle);

      // Show key preview / type on line 2
      _display->setColor(COLOR_DARK_GRAY);
      char keyInfo[34];
      if (isEmpty) {
        strcpy(keyInfo, "Tap [CFG] to set #tag or PSK");
      } else {
        char hexPreview[10];
        mesh::Utils::toHex(hexPreview, ch.channel.secret, 4);
        if (ch.name[0] == '#') {
          snprintf(keyInfo, sizeof(keyInfo), "Key: SHA256 (%s..)", hexPreview);
        } else {
          snprintf(keyInfo, sizeof(keyInfo), "Key: PSK (%s..)", hexPreview);
        }
      }
      drawStr(14, y + 25, keyInfo);

      // [SELECT / ACTIVE] button: X in 194..252, Y in y+8..y+36
      _display->setColor(isActive ? COLOR_GREEN_BTN : COLOR_BLUE_KEY);
      _display->fillRect(194, y + 8, 58, 28);
      _display->setColor(COLOR_DARK_GRAY);
      _display->drawRect(194, y + 8, 58, 28);
      _display->setColor(COLOR_WHITE);
      drawStr(isActive ? 205 : 205, y + 18, isActive ? "ACTIVE" : "SELECT");

      // [CFG] button: X in 256..306, Y in y+8..y+36
      _display->setColor(COLOR_PURPLE_BTN);
      _display->fillRect(256, y + 8, 50, 28);
      _display->setColor(COLOR_DARK_GRAY);
      _display->drawRect(256, y + 8, 50, 28);
      _display->setColor(COLOR_WHITE);
      drawStr(266, y + 18, "CONFIG");

      y += 48;
    }

    // Paging bar at bottom: Y in 218..238
    _display->setColor(COLOR_LIGHT_GRAY);
    _display->fillRect(8, 218, 65, 20);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(8, 218, 65, 20);
    _display->setColor(COLOR_BLACK);
    drawStr(18, 224, "< PREV");

    char pBuf[28];
    snprintf(pBuf, sizeof(pBuf), "Ch %d-%d (Page %d/%d)", start, end - 1, _chans_page + 1, maxPages);
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(105, 224, pBuf);

    _display->setColor(COLOR_LIGHT_GRAY);
    _display->fillRect(247, 218, 65, 20);
    _display->setColor(COLOR_MED_GRAY);
    _display->drawRect(247, 218, 65, 20);
    _display->setColor(COLOR_BLACK);
    drawStr(259, 224, "NEXT >");
  } else {
    int y = 76;
    int roomCount = 0;
    int total_slots = the_mesh.getTotalContactSlots();

    for (int i = 8; i < total_slots && roomCount < 3; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.type == 3) { // ADV_TYPE_ROOM
        _display->setColor(COLOR_CARD_ROOM_BG);
        _display->fillRect(10, y, 300, 38);
        _display->setColor(COLOR_CARD_ROOM_BD);
        _display->drawRect(10, y, 300, 38);
        _display->fillRect(10, y, 6, 38);

        _display->setColor(COLOR_NAVY);
        drawStr(22, y + 8, c.name[0] ? c.name : "BBS Room Server");

        _display->setColor(COLOR_DARK_GRAY);
        char idBuf[20];
        mesh::Utils::toHex(idBuf, c.id.pub_key, 4);
        char sub[32];
        snprintf(sub, sizeof(sub), "Room ID: !%s", idBuf);
        drawStr(22, y + 22, sub);

        _display->setColor(COLOR_PURPLE_BTN);
        _display->fillRect(220, y + 7, 80, 24);
        _display->setColor(COLOR_WHITE);
        drawStr(232, y + 14, "CONNECT");

        y += 44;
        roomCount++;
      }
    }

    if (roomCount == 0) {
      _display->setColor(COLOR_DARK_GRAY);
      drawStr(15, 90, "MeshCore Room Servers are dedicated bulletin");
      drawStr(15, 106, "boards (BBS) for public asynchronous posts.");
      drawStr(15, 130, "No Room Server discovered in radio range yet.");

      _display->setColor(COLOR_PURPLE_BTN);
      _display->fillRect(40, 160, 240, 36);
      _display->setColor(COLOR_WHITE);
      drawStr(70, 173, "SEARCH FOR ROOM SERVERS");
    }
  }
}

void TapUI::drawChannelEditor() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);
  _display->setTextSize(1);

  // Header banner: Y in 46..68
  _display->setColor(COLOR_NAVY);
  _display->fillRect(8, 46, 304, 22);
  _display->setColor(COLOR_WHITE);
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "CONFIGURE CHANNEL #%d", _editing_chan_idx);
  drawStr(16, 53, hdr);

  // Row 1: Channel Name: Y in 72..96
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(8, 72, 222, 24);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(8, 72, 222, 24);
  _display->setColor(COLOR_BLACK);
  char nameLine[38];
  snprintf(nameLine, sizeof(nameLine), "Name: %s", _edit_chan_draft.name[0] ? _edit_chan_draft.name : "(empty)");
  drawStr(14, 80, nameLine);

  // [EDIT NAME] button: X in 234..312, Y in 72..96
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(234, 72, 78, 24);
  _display->setColor(COLOR_WHITE);
  drawStr(244, 80, "EDIT NAME");

  // Row 2: Key / PSK display: Y in 100..136
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(8, 100, 222, 36);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(8, 100, 222, 36);

  unsigned char b64[50];
  memset(b64, 0, sizeof(b64));
  encode_base64(_edit_chan_draft.channel.secret, _edit_chan_key_len, b64);
  _display->setColor(COLOR_NAVY);
  drawStr(14, 106, "PSK (Base64 / 128-bit):");
  _display->setColor(COLOR_DARK_GRAY);
  char b64Line[36];
  snprintf(b64Line, sizeof(b64Line), "%.32s", (const char*)b64);
  drawStr(14, 120, b64Line);

  // [EDIT KEY] button: X in 234..312, Y in 100..136
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(234, 100, 78, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(247, 114, "EDIT KEY");

  // Row 3: Quick Key Generators: Y in 142..168
  // [#HASHTAG KEY]: X in 8..112
  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(8, 142, 104, 26);
  _display->setColor(COLOR_WHITE);
  drawStr(16, 151, "#HASHTAG KEY");

  // [PUBLIC PSK]: X in 116..212
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(116, 142, 96, 26);
  _display->setColor(COLOR_BLACK);
  drawStr(128, 151, "PUBLIC PSK");

  // [RANDOM KEY]: X in 216..312
  _display->setColor(COLOR_ORANGE_BTN);
  _display->fillRect(216, 142, 96, 26);
  _display->setColor(COLOR_BLACK);
  drawStr(228, 151, "RANDOM KEY");

  // Hex preview row: Y in 174..190
  char hexStr[36];
  mesh::Utils::toHex(hexStr, _edit_chan_draft.channel.secret, 16);
  char hexLine[48];
  snprintf(hexLine, sizeof(hexLine), "Hex: %.32s", hexStr);
  _display->setColor(COLOR_DARK_GRAY);
  drawStr(10, 176, hexLine);

  // Row 4: Bottom Actions: Y in 196..234
  // [SAVE & USE]: X in 8..122
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(8, 196, 114, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(8, 196, 114, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(26, 210, "SAVE & USE");

  // [CLEAR CH]: X in 128..220
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(128, 196, 92, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(128, 196, 92, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(144, 210, "CLEAR CH");

  // [BACK]: X in 226..312
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(226, 196, 86, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(226, 196, 86, 36);
  _display->setColor(COLOR_BLACK);
  drawStr(248, 210, "CANCEL");
}

void TapUI::handleChannelEditorTouch(uint16_t tx, uint16_t ty) {
  // [EDIT NAME]: X in 234..312, Y in 72..96
  if (tx >= 234 && tx <= 312 && ty >= 72 && ty <= 96) {
    startKeyboardEdit(EDIT_CHAN_NAME, _edit_chan_draft.name, KB_LOWER, TAB_CHANS);
    return;
  }

  // [EDIT KEY]: X in 234..312, Y in 100..136
  if (tx >= 234 && tx <= 312 && ty >= 100 && ty <= 136) {
    char hexKey[36];
    mesh::Utils::toHex(hexKey, _edit_chan_draft.channel.secret, 16);
    startKeyboardEdit(EDIT_CHAN_KEY, hexKey, KB_SYMBOLS, TAB_CHANS);
    return;
  }

  // Quick Key Generators: Y in 142..168
  if (ty >= 142 && ty <= 168) {
    // [#HASHTAG KEY]: X in 8..112
    if (tx >= 8 && tx <= 112) {
      if (_edit_chan_draft.name[0] == 0) {
        snprintf(_edit_chan_draft.name, sizeof(_edit_chan_draft.name), "#chan%d", _editing_chan_idx);
      } else if (_edit_chan_draft.name[0] != '#') {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "#%s", _edit_chan_draft.name);
        strncpy(_edit_chan_draft.name, tmp, sizeof(_edit_chan_draft.name) - 1);
        _edit_chan_draft.name[sizeof(_edit_chan_draft.name) - 1] = 0;
      }
      memset(_edit_chan_draft.channel.secret, 0, sizeof(_edit_chan_draft.channel.secret));
      mesh::Utils::sha256(_edit_chan_draft.channel.secret, 16,
                          (const uint8_t*)_edit_chan_draft.name, strlen(_edit_chan_draft.name));
      _edit_chan_key_len = 16;
      _needs_full_redraw = true;
      return;
    }
    // [PUBLIC PSK]: X in 116..212
    if (tx >= 116 && tx <= 212) {
      strcpy(_edit_chan_draft.name, "Public");
      memset(_edit_chan_draft.channel.secret, 0, sizeof(_edit_chan_draft.channel.secret));
      decode_base64((const unsigned char*)"izOH6cXN6mrJ5e26oRXNcg==", 24, _edit_chan_draft.channel.secret);
      _edit_chan_key_len = 16;
      _needs_full_redraw = true;
      return;
    }
    // [RANDOM KEY]: X in 216..312
    if (tx >= 216 && tx <= 312) {
      if (_edit_chan_draft.name[0] == 0) {
        snprintf(_edit_chan_draft.name, sizeof(_edit_chan_draft.name), "Group%d", _editing_chan_idx);
      }
      memset(_edit_chan_draft.channel.secret, 0, sizeof(_edit_chan_draft.channel.secret));
      the_mesh.getRNG()->random(_edit_chan_draft.channel.secret, 16);
      _edit_chan_key_len = 16;
      _needs_full_redraw = true;
      return;
    }
  }

  // Bottom Actions: Y in 196..234
  if (ty >= 196 && ty <= 234) {
    // [SAVE & USE]: X in 8..122
    if (tx >= 8 && tx <= 122) {
      if (_edit_chan_draft.name[0] == 0) {
        snprintf(_edit_chan_draft.name, sizeof(_edit_chan_draft.name), "Chan%d", _editing_chan_idx);
      }
      if (isChannelSecretZero(_edit_chan_draft.channel.secret, 32)) {
        mesh::Utils::sha256(_edit_chan_draft.channel.secret, 16,
                            (const uint8_t*)_edit_chan_draft.name, strlen(_edit_chan_draft.name));
      }
      the_mesh.setChannel(_editing_chan_idx, _edit_chan_draft);
      persistChannels();
      _active_channel = _editing_chan_idx;
      _active_contact_idx = -1;
      _editing_chan_idx = -1;
      _needs_full_redraw = true;
      return;
    }
    // [CLEAR CH]: X in 128..220
    if (tx >= 128 && tx <= 220) {
      clearChannel(_editing_chan_idx);
      _editing_chan_idx = -1;
      _needs_full_redraw = true;
      return;
    }
    // [CANCEL]: X in 226..312
    if (tx >= 226 && tx <= 312) {
      _editing_chan_idx = -1;
      _needs_full_redraw = true;
      return;
    }
  }
}

void TapUI::handleChansTouch(uint16_t tx, uint16_t ty) {
  if (_editing_chan_idx >= 0 && _editing_chan_idx < MAX_GROUP_CHANNELS) {
    handleChannelEditorTouch(tx, ty);
    return;
  }

  if (ty >= 46 && ty <= 68) {
    if (tx >= 8 && tx <= 156) {
      _show_rooms_subview = false;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 164 && tx <= 312) {
      _show_rooms_subview = true;
      _needs_full_redraw = true;
      return;
    }
  }

  if (!_show_rooms_subview) {
    int itemsPerPage = 3;
    int maxPages = (MAX_GROUP_CHANNELS + itemsPerPage - 1) / itemsPerPage;
    int start = _chans_page * itemsPerPage;
    int end = start + itemsPerPage;
    if (end > MAX_GROUP_CHANNELS) end = MAX_GROUP_CHANNELS;

    // Paging buttons at bottom: Y in 218..238
    if (ty >= 218 && ty <= 238) {
      if (tx >= 8 && tx <= 73 && _chans_page > 0) {
        _chans_page--;
        _needs_full_redraw = true;
        return;
      }
      if (tx >= 247 && tx <= 312 && _chans_page + 1 < maxPages) {
        _chans_page++;
        _needs_full_redraw = true;
        return;
      }
      return;
    }

    int y = 72;
    for (int i = start; i < end; i++) {
      if (ty >= y && ty <= y + 44) {
        if (tx >= 254 && tx <= 312) {
          // Open Channel Config Editor for channel i
          _editing_chan_idx = i;
          memset(&_edit_chan_draft, 0, sizeof(_edit_chan_draft));
          the_mesh.getChannel(i, _edit_chan_draft);
          _edit_chan_key_len = isChannelSecretZero(&_edit_chan_draft.channel.secret[16], 16) ? 16 : 32;
          _needs_full_redraw = true;
          return;
        } else {
          // Select channel i for chat
          ChannelDetails ch;
          memset(&ch, 0, sizeof(ch));
          the_mesh.getChannel(i, ch);
          if (i > 0 && ch.name[0] == 0 && isChannelSecretZero(ch.channel.secret, 16)) {
            // Unconfigured channel -> open config editor directly
            _editing_chan_idx = i;
            memset(&_edit_chan_draft, 0, sizeof(_edit_chan_draft));
            snprintf(_edit_chan_draft.name, sizeof(_edit_chan_draft.name), "#chan%d", i);
            mesh::Utils::sha256(_edit_chan_draft.channel.secret, 16,
                                (const uint8_t*)_edit_chan_draft.name, strlen(_edit_chan_draft.name));
            _edit_chan_key_len = 16;
            _needs_full_redraw = true;
            return;
          }
          _active_channel = i;
          _active_contact_idx = -1;
          _current_tab = TAB_CHAT;
          _needs_full_redraw = true;
          return;
        }
      }
      y += 48;
    }
  } else {
    // Room servers view
    if (tx >= 40 && tx <= 280 && ty >= 160 && ty <= 196) {
      the_mesh.advert();
      _needs_full_redraw = true;
      return;
    }
    int y = 76;
    int roomCount = 0;
    int total_slots = the_mesh.getTotalContactSlots();
    for (int i = 8; i < total_slots && roomCount < 3; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.type == 3) {
        if (ty >= y && ty <= y + 38) {
          _active_contact_idx = i;
          _current_tab = TAB_CHAT;
          _needs_full_redraw = true;
          return;
        }
        y += 44;
        roomCount++;
      }
    }
  }
}

// -------------------------------------------------------------
// GPS SCREEN (Baud Rate Settings, Time Sync & Real-Time Diagnostics)
// -------------------------------------------------------------
void TapUI::drawGPSScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);

  LocationProvider* loc = sensors.getLocationProvider();
  bool hasLoc = (loc != nullptr && loc->isEnabled());
  bool fixValid = (hasLoc && loc->isValid());

  // Status Banner: Y in 48..74
  _display->setColor(fixValid ? COLOR_GREEN_BTN : (hasLoc ? COLOR_ORANGE_BTN : COLOR_LIGHT_GRAY));
  _display->fillRect(10, 48, 300, 26);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 48, 300, 26);

  _display->setTextSize(1);
  _display->setColor(fixValid ? COLOR_WHITE : (hasLoc ? COLOR_BLACK : COLOR_DARK_GRAY));
  if (fixValid) {
    drawStr(75, 56, "[ 3D GPS FIX ACQUIRED ]");
  } else if (hasLoc) {
    drawStr(65, 56, "[ SEARCHING SATELLITES... ]");
  } else {
    drawStr(70, 56, "[ GPS RECEIVER STANDBY ]");
  }

  // Telemetry Card: Y in 78..184
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(10, 78, 300, 106);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(10, 78, 300, 106);

  _display->setTextSize(1);
  _display->setColor(COLOR_NAVY);

  char buf[70];
  long sats = (loc && loc->isEnabled()) ? loc->satellitesCount() : 0;
  snprintf(buf, sizeof(buf), "Satellites Tracked: %ld in view", sats);
  drawStr(18, 84, buf);

  double lat = 0.0, lon = 0.0;
  long alt = 0;
  if (loc && loc->isEnabled()) {
    lat = (double)loc->getLatitude() / 1e6;
    lon = (double)loc->getLongitude() / 1e6;
    alt = loc->getAltitude();
  }

  _display->setColor(COLOR_BLACK);
  if (fixValid) {
    snprintf(buf, sizeof(buf), "Latitude:  %.6f deg %c", fabs(lat), lat >= 0 ? 'N' : 'S');
    drawStr(18, 98, buf);
    snprintf(buf, sizeof(buf), "Longitude: %.6f deg %c", fabs(lon), lon >= 0 ? 'E' : 'W');
    drawStr(18, 112, buf);
    snprintf(buf, sizeof(buf), "Altitude:  %ld m (%ld ft)", alt, (long)(alt * 3.28084));
    drawStr(18, 126, buf);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(18, 98, "Latitude:  Waiting for fix...");
    drawStr(18, 112, "Longitude: Waiting for fix...");
    drawStr(18, 126, "Altitude:  Waiting for fix...");
  }

  // Diagnostics row
  char diag[45];
  if (loc) loc->formatDiagnostics(diag, sizeof(diag));
  else strcpy(diag, "en:1 sat:0 fix:0");
  snprintf(buf, sizeof(buf), "Diag: %s", diag);
  _display->setColor(COLOR_MED_GRAY);
  drawStr(18, 142, buf);

  // Baud & Pin Settings row: Y in 158..178
  _display->setColor(COLOR_NAVY);
  snprintf(buf, sizeof(buf), "Baud: %lu", _gps_baud);
  drawStr(18, 163, buf);

  // [CHG BAUD] button: X in 110..185
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(110, 158, 75, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(118, 164, "CHG BAUD");

  // [PINS: RX/TX] button: X in 195..302
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(195, 158, 107, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(202, 164, _gps_pins_swapped ? "PINS: RX16/TX15" : "PINS: RX15/TX16");

  // Action Buttons: Y in 190..232
  // [ BROADCAST LOCATION ]: X in 10..200
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(10, 190, 190, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 190, 190, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(32, 204, "BROADCAST LOCATION");

  // [ GPS ON/OFF ]: X in 210..310
  _display->setColor(hasLoc ? COLOR_GREEN_BTN : COLOR_MED_GRAY);
  _display->fillRect(210, 190, 100, 38);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(210, 190, 100, 38);
  _display->setColor(COLOR_WHITE);
  drawStr(226, 204, hasLoc ? "GPS: ON" : "GPS: OFF");
}

void TapUI::handleGPSTouch(uint16_t tx, uint16_t ty) {
  if (tx >= 110 && tx <= 185 && ty >= 158 && ty <= 180) {
    int bIdx = 0;
    for (int i = 0; i < NUM_GPS_BAUDS; i++) {
      if (GPS_BAUD_OPTIONS[i] == _gps_baud) {
        bIdx = (i + 1) % NUM_GPS_BAUDS;
        break;
      }
    }
    setGPSBaud(GPS_BAUD_OPTIONS[bIdx], true);
    return;
  }

  if (tx >= 195 && tx <= 302 && ty >= 158 && ty <= 180) {
    setGPSPinsSwapped(!_gps_pins_swapped, true);
    return;
  }

  if (tx >= 10 && tx <= 200 && ty >= 190 && ty <= 232) {
    LocationProvider* loc = sensors.getLocationProvider();
    char locMsg[64];
    if (loc && loc->isEnabled() && loc->isValid()) {
      snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f, %.5f (Alt: %ldm)",
               (double)loc->getLatitude() / 1e6, (double)loc->getLongitude() / 1e6, loc->getAltitude());
    } else {
      snprintf(locMsg, sizeof(locMsg), "[LOC] Ping from WisMesh Tap");
    }
    sendMessage(locMsg);
    _current_tab = TAB_CHAT;
    _needs_full_redraw = true;
    return;
  }

  if (tx >= 210 && tx <= 310 && ty >= 190 && ty <= 232) {
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc) {
      if (loc->isEnabled()) loc->stop();
      else loc->begin();
    }
    _needs_full_redraw = true;
    return;
  }
}

// -------------------------------------------------------------
// RADIO & DEVICE SETTINGS SCREEN (2 Sub-Tabs: [RF & LORA] | [DEVICE & SYS])
// -------------------------------------------------------------
void TapUI::drawRadioScreen() {
  _display->setColor(COLOR_WHITE);
  _display->fillRect(0, 44, 320, 196);
  _display->setTextSize(1);

  // Sub-Tab Selector Bar: Y in 45..65
  _display->setColor(_radio_subview == 0 ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(8, 45, 148, 20);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(8, 45, 148, 20);
  _display->setColor(_radio_subview == 0 ? COLOR_WHITE : COLOR_BLACK);
  drawStr(36, 51, "[ RF & LORA ]");

  _display->setColor(_radio_subview == 1 ? COLOR_NAVY : COLOR_LIGHT_GRAY);
  _display->fillRect(164, 45, 148, 20);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(164, 45, 148, 20);
  _display->setColor(_radio_subview == 1 ? COLOR_WHITE : COLOR_BLACK);
  drawStr(184, 51, "[ DEVICE & SYS ]");

  if (_radio_subview == 0) {
    drawRadioRFView();
  } else {
    drawRadioDeviceView();
  }
}

void TapUI::drawRadioRFView() {
  // 1. Node Name row: Y in 68..88
  _display->setColor(COLOR_NAVY);
  char nameBuf[36];
  snprintf(nameBuf, sizeof(nameBuf), "Node: %.22s", the_mesh.getNodePrefs()->node_name);
  drawStr(10, 74, nameBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(242, 68, 70, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(256, 74, "[NAME]");

  // 2. RF Presets Cycler: Y in 91..111
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(10, 91, 302, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(10, 91, 302, 20);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(11, 92, 28, 18);
  _display->setColor(COLOR_WHITE);
  drawStr(21, 97, "<");

  _display->setColor(COLOR_NAVY);
  const char* pName = (_selected_preset_idx < NUM_RF_PRESETS) ? RF_PRESETS[_selected_preset_idx].name : "Custom";
  char pBuf[36];
  snprintf(pBuf, sizeof(pBuf), "Preset: %s", pName);
  drawStr(48, 97, pBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(283, 92, 28, 18);
  _display->setColor(COLOR_WHITE);
  drawStr(294, 97, ">");

  // 3. Frequency row: Y in 114..134
  _display->setColor(COLOR_BLACK);
  char freqBuf[32];
  snprintf(freqBuf, sizeof(freqBuf), "Freq: %.3f MHz", _radio_freq);
  drawStr(10, 120, freqBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(172, 114, 44, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(172, 114, 44, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(176, 120, "-125k");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(220, 114, 44, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(220, 114, 44, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(224, 120, "+125k");

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(268, 114, 44, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(278, 120, "TYPE");

  // 4. SF & Bandwidth row: Y in 137..157
  _display->setColor(COLOR_BLACK);
  char sfbwBuf[36];
  snprintf(sfbwBuf, sizeof(sfbwBuf), "SF: %u | BW: %.1f kHz", _radio_sf, _radio_bw);
  drawStr(10, 143, sfbwBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(195, 137, 55, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(195, 137, 55, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(204, 143, "CHG SF");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(255, 137, 57, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(255, 137, 57, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(264, 143, "CHG BW");

  // 5. CR & TX Power row: Y in 160..180
  _display->setColor(COLOR_BLACK);
  char crtxBuf[36];
  snprintf(crtxBuf, sizeof(crtxBuf), "CR: 4/%u | TX: %d dBm", _radio_cr, _radio_tx_power);
  drawStr(10, 166, crtxBuf);

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(195, 160, 55, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(195, 160, 55, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(204, 166, "CHG CR");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(255, 160, 57, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(255, 160, 57, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(264, 166, "CHG TX");

  // 6. Status notification row: Y in 182..194
  if (millis() < _radio_status_expiry) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(10, 184, _radio_status_msg);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    char idHex[18];
    mesh::Utils::toHex(idHex, the_mesh.self_id.pub_key, 6);
    char idBuf[46];
    snprintf(idBuf, sizeof(idBuf), "Node ID: !%.12s", idHex);
    drawStr(10, 184, idBuf);
  }

  // 7. Action Buttons: Y in 196..234
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(8, 196, 108, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(8, 196, 108, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(18, 210, "APPLY & SAVE");

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(122, 196, 86, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(122, 196, 86, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(142, 210, "BEACON");

  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(214, 196, 98, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(214, 196, 98, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(228, 210, "POWER OFF");
}

void TapUI::drawRadioDeviceView() {
  // Row 1: BLE Status, On/Off toggle & Unpair: Y in 68..88
  _display->setColor(COLOR_NAVY);
  char bleBuf[28];
  if (!_ble_enabled) {
    strcpy(bleBuf, "BLE: OFF");
  } else if (ui_task.hasConnection()) {
    strcpy(bleBuf, "BLE: CONNECTED");
  } else {
    strcpy(bleBuf, "BLE: ADVERTISING");
  }
  drawStr(10, 74, bleBuf);

  // [BLE ON/OFF]: X in 170..242
  _display->setColor(_ble_enabled ? COLOR_GREEN_BTN : COLOR_MED_GRAY);
  _display->fillRect(170, 68, 72, 20);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(170, 68, 72, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(180, 74, _ble_enabled ? "BLE: ON" : "BLE: OFF");

  // [UNPAIR]: X in 248..312
  _display->setColor(COLOR_ORANGE_BTN);
  _display->fillRect(248, 68, 64, 20);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(248, 68, 64, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(258, 74, "UNPAIR");

  // Row 2: BLE PIN Config: Y in 91..111
  _display->setColor(COLOR_BLACK);
  char pinBuf[32];
  snprintf(pinBuf, sizeof(pinBuf), "BLE PIN: %06lu", (unsigned long)the_mesh.getBLEPin());
  drawStr(10, 97, pinBuf);

  // [EDIT PIN]: X in 170..242
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(170, 91, 72, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(180, 97, "EDIT PIN");

  // [RAND PIN]: X in 248..312
  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(248, 91, 64, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(256, 97, "RANDOM");

  // Row 3: Screen Brightness & Sleep Timeout: Y in 114..134
  _display->setColor(COLOR_BLACK);
  char brtBuf[24];
  snprintf(brtBuf, sizeof(brtBuf), "Bright: %u%%", _brightness_pct);
  drawStr(10, 120, brtBuf);

  // [BRT]: X in 98..148
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(98, 114, 50, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(98, 114, 50, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(106, 120, "+BRT");

  // Sleep label + button: X in 158..312
  int sIdx = 1;
  for (int i = 0; i < NUM_SLEEP_OPTIONS; i++) {
    if (SLEEP_TIMEOUTS[i] == _screen_sleep_seconds) { sIdx = i; break; }
  }
  char slpBuf[24];
  snprintf(slpBuf, sizeof(slpBuf), "Sleep: %s", SLEEP_LABELS[sIdx]);
  drawStr(156, 120, slpBuf);

  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(248, 114, 64, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(248, 114, 64, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(262, 120, "SLEEP");

  // Row 4: Clock Display & Nudge: Y in 137..157
  char dtBuf[32];
  formatLocalDateTime(dtBuf, sizeof(dtBuf), true, false);
  _display->setColor(COLOR_NAVY);
  char clkLine[38];
  snprintf(clkLine, sizeof(clkLine), "UTC: %s", dtBuf);
  drawStr(10, 143, clkLine);

  // [-1h] / [+1h]: X in 236..272, 276..312
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(236, 137, 36, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(236, 137, 36, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(243, 143, "-1h");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(276, 137, 36, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(276, 137, 36, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(283, 143, "+1h");

  // Row 5: Clock Sync & Manual Set: Y in 160..180
  // [GPS SYNC]: X in 10..112
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(10, 160, 102, 20);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(10, 160, 102, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(24, 166, "GPS TIME SYNC");

  // [SET CLOCK]: X in 118..220
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(118, 160, 102, 20);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(118, 160, 102, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(136, 166, "SET CLOCK");

  // [-1m] / [+1m]: X in 226..266, 270..312
  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(226, 160, 40, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(226, 160, 40, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(235, 166, "-1m");

  _display->setColor(COLOR_LIGHT_GRAY);
  _display->fillRect(270, 160, 42, 20);
  _display->setColor(COLOR_MED_GRAY);
  _display->drawRect(270, 160, 42, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(280, 166, "+1m");

  // Row 6: Status / Feedback line: Y in 183..194
  if (millis() < _radio_status_expiry) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(10, 184, _radio_status_msg);
  } else {
    _display->setColor(COLOR_DARK_GRAY);
    drawStr(10, 184, "Tip: All settings also available via Serial/BLE CLI");
  }

  // Row 7: System Action Buttons: Y in 196..234
  // [SAVE SETTINGS]: X in 8..116
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(8, 196, 108, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(8, 196, 108, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(18, 210, "SAVE CONFIG");

  // [REBOOT]: X in 122..208
  _display->setColor(COLOR_ORANGE_BTN);
  _display->fillRect(122, 196, 86, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(122, 196, 86, 36);
  _display->setColor(COLOR_BLACK);
  drawStr(144, 210, "REBOOT");

  // [POWER OFF]: X in 214..312
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(214, 196, 98, 36);
  _display->setColor(COLOR_DARK_GRAY);
  _display->drawRect(214, 196, 98, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(228, 210, "POWER OFF");
}

void TapUI::handleRadioTouch(uint16_t tx, uint16_t ty) {
  // Sub-Tab Selector Bar: Y in 45..65
  if (ty >= 45 && ty <= 65) {
    if (tx >= 8 && tx <= 156) {
      _radio_subview = 0;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 164 && tx <= 312) {
      _radio_subview = 1;
      _needs_full_redraw = true;
      return;
    }
  }

  if (_radio_subview == 0) {
    handleRadioRFTouch(tx, ty);
  } else {
    handleRadioDeviceTouch(tx, ty);
  }
}

void TapUI::handleRadioRFTouch(uint16_t tx, uint16_t ty) {
  // [EDIT NAME]: X in 242..312, Y in 68..88
  if (tx >= 242 && tx <= 312 && ty >= 68 && ty <= 88) {
    startKeyboardEdit(EDIT_NODE_NAME, the_mesh.getNodePrefs()->node_name, KB_LOWER, TAB_RADIO);
    return;
  }

  // Preset Prev/Next: Y in 91..111
  if (ty >= 91 && ty <= 111) {
    if (tx >= 10 && tx <= 50) {
      int p = _selected_preset_idx - 1;
      if (p < 0) p = NUM_RF_PRESETS - 1;
      if (p == NUM_RF_PRESETS - 1) _selected_preset_idx = p;
      else applyRFPreset(p);
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 270 && tx <= 312) {
      int p = (_selected_preset_idx + 1) % NUM_RF_PRESETS;
      if (p == NUM_RF_PRESETS - 1) _selected_preset_idx = p;
      else applyRFPreset(p);
      _needs_full_redraw = true;
      return;
    }
  }

  // Freq Steppers & Type: Y in 114..134
  if (ty >= 114 && ty <= 134) {
    if (tx >= 172 && tx <= 216) { // -125k
      _radio_freq -= 0.125f;
      if (_radio_freq < 150.0f) _radio_freq = 150.0f;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 220 && tx <= 264) { // +125k
      _radio_freq += 0.125f;
      if (_radio_freq > 960.0f) _radio_freq = 960.0f;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 268 && tx <= 312) { // TYPE exact freq
      char fStr[16];
      snprintf(fStr, sizeof(fStr), "%.3f", _radio_freq);
      startKeyboardEdit(EDIT_RADIO_FREQ, fStr, KB_SYMBOLS, TAB_RADIO);
      return;
    }
  }

  // SF & BW toggles: Y in 137..157
  if (ty >= 137 && ty <= 157) {
    if (tx >= 195 && tx <= 250) { // CHG SF
      _radio_sf++;
      if (_radio_sf > 12) _radio_sf = 7;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 255 && tx <= 312) { // CHG BW
      if (_radio_bw >= 500.0f) _radio_bw = 62.5f;
      else if (_radio_bw < 100.0f) _radio_bw = 125.0f;
      else if (_radio_bw < 200.0f) _radio_bw = 250.0f;
      else _radio_bw = 500.0f;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
  }

  // CR & TX toggles: Y in 160..180
  if (ty >= 160 && ty <= 180) {
    if (tx >= 195 && tx <= 250) { // CHG CR
      _radio_cr++;
      if (_radio_cr > 8) _radio_cr = 5;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 255 && tx <= 312) { // CHG TX
      static const int8_t tx_steps[] = { 10, 14, 17, 20, 22 };
      int nextIdx = 0;
      for (int i = 0; i < 5; i++) {
        if (tx_steps[i] == _radio_tx_power) {
          nextIdx = (i + 1) % 5;
          break;
        }
      }
      _radio_tx_power = tx_steps[nextIdx];
      _needs_full_redraw = true;
      return;
    }
  }

  // Bottom Action Buttons: Y in 196..234
  if (ty >= 196 && ty <= 234) {
    if (tx >= 8 && tx <= 116) {
      applyAndSaveRadioParams();
      return;
    }
    if (tx >= 122 && tx <= 208) {
      the_mesh.advert();
      showStatusNotification("Advert Beacon Broadcasted!");
      return;
    }
    if (tx >= 214 && tx <= 312) {
      powerOff();
      return;
    }
  }
}

void TapUI::handleRadioDeviceTouch(uint16_t tx, uint16_t ty) {
  // Row 1: BLE ON/OFF & UNPAIR: Y in 68..88
  if (ty >= 68 && ty <= 88) {
    if (tx >= 170 && tx <= 242) {
      setBLEEnabled(!_ble_enabled, true);
      showStatusNotification(_ble_enabled ? "BLE Enabled!" : "BLE Disabled!");
      return;
    }
    if (tx >= 248 && tx <= 312) {
      clearBLEBonds();
      return;
    }
  }

  // Row 2: BLE PIN Edit & Random: Y in 91..111
  if (ty >= 91 && ty <= 111) {
    if (tx >= 170 && tx <= 242) {
      char pinStr[12];
      snprintf(pinStr, sizeof(pinStr), "%06lu", (unsigned long)the_mesh.getBLEPin());
      startKeyboardEdit(EDIT_BLE_PIN, pinStr, KB_SYMBOLS, TAB_RADIO);
      return;
    }
    if (tx >= 248 && tx <= 312) {
      uint32_t rnd = 0;
      the_mesh.getRNG()->random((uint8_t*)&rnd, sizeof(rnd));
      uint32_t newPin = 100000 + (rnd % 900000);
      setBLEPinLive(newPin);
      showStatusNotification("New Random BLE PIN Saved!");
      return;
    }
  }

  // Row 3: Brightness & Sleep Timeout: Y in 114..134
  if (ty >= 114 && ty <= 134) {
    if (tx >= 90 && tx <= 152) {
      int nextIdx = 0;
      for (int i = 0; i < NUM_BRIGHTNESS_STEPS; i++) {
        if (BRIGHTNESS_STEPS[i] >= _brightness_pct) {
          nextIdx = (i + 1) % NUM_BRIGHTNESS_STEPS;
          break;
        }
      }
      setBrightnessPct(BRIGHTNESS_STEPS[nextIdx], true);
      return;
    }
    if (tx >= 244 && tx <= 312) {
      int sIdx = 1;
      for (int i = 0; i < NUM_SLEEP_OPTIONS; i++) {
        if (SLEEP_TIMEOUTS[i] == _screen_sleep_seconds) {
          sIdx = (i + 1) % NUM_SLEEP_OPTIONS;
          break;
        }
      }
      setScreenSleepSeconds(SLEEP_TIMEOUTS[sIdx], true);
      return;
    }
  }

  // Row 4: Clock -1h / +1h: Y in 137..157
  if (ty >= 137 && ty <= 157) {
    if (tx >= 236 && tx <= 272) {
      adjustClockSeconds(-3600);
      showStatusNotification("Clock -1 Hour");
      return;
    }
    if (tx >= 276 && tx <= 312) {
      adjustClockSeconds(3600);
      showStatusNotification("Clock +1 Hour");
      return;
    }
  }

  // Row 5: Clock GPS Sync, Set Clock, -1m, +1m: Y in 160..180
  if (ty >= 160 && ty <= 180) {
    if (tx >= 10 && tx <= 112) {
      if (syncClockFromGPS()) {
        showStatusNotification("Synced Clock from GPS!");
      } else {
        showStatusNotification("GPS Fix Required for Sync");
      }
      return;
    }
    if (tx >= 118 && tx <= 220) {
      char dtStr[28];
      formatLocalDateTime(dtStr, sizeof(dtStr), true, false);
      startKeyboardEdit(EDIT_CLOCK_TIME, dtStr, KB_SYMBOLS, TAB_RADIO);
      return;
    }
    if (tx >= 226 && tx <= 266) {
      adjustClockSeconds(-60);
      showStatusNotification("Clock -1 Minute");
      return;
    }
    if (tx >= 270 && tx <= 312) {
      adjustClockSeconds(60);
      showStatusNotification("Clock +1 Minute");
      return;
    }
  }

  // Row 7: Bottom System Actions: Y in 196..234
  if (ty >= 196 && ty <= 234) {
    if (tx >= 8 && tx <= 116) {
      the_mesh.savePrefs();
      saveDeviceSettings();
      persistChannels();
      showStatusNotification("All Settings Saved!");
      return;
    }
    if (tx >= 122 && tx <= 208) {
      the_mesh.savePrefs();
      saveDeviceSettings();
      board.reboot();
      return;
    }
    if (tx >= 214 && tx <= 312) {
      powerOff();
      return;
    }
  }
}
