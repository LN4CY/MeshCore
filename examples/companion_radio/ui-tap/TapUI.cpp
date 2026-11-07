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
#define TAP_SETTINGS_MAGIC 0x54415033UL // "TAP3" (defaults to dark_mode = 1, tz_offset = -5)

#define COLOR_NAVY       0x1994
#define COLOR_DARK_HDR   0x0863
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
#define COLOR_GOLD_STAR  0xFEA0
#define COLOR_PURPLE_BTN 0x791B
#define COLOR_CYAN_BTN   0x04F7

// Light mode Node Card Colors
#define COLOR_CARD_CLIENT_BG   0xFFFF
#define COLOR_CARD_CLIENT_BD   0x2A69
#define COLOR_CARD_REPEAT_BG   0xFFFA
#define COLOR_CARD_REPEAT_BD   0xFC00
#define COLOR_CARD_ROOM_BG     0xF7BF
#define COLOR_CARD_ROOM_BD     0x791B
#define COLOR_CARD_SENSOR_BG   0xEFFE
#define COLOR_CARD_SENSOR_BD   0x04F7

// Dark mode Node Card Colors
#define COLOR_DK_CLIENT_BG     0x0842
#define COLOR_DK_REPEAT_BG     0x20E0
#define COLOR_DK_ROOM_BG       0x1863
#define COLOR_DK_SENSOR_BG     0x0123

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
  { "Australia",                915.800f, 10, 250.0f, 5, 0 },
  { "Australia (Narrow)",       916.575f,  7,  62.5f, 7, 0 },
  { "Australia (Mid)",          915.075f,  9, 125.0f, 5, 0 },
  { "Australia: SA, WA",        923.125f,  8,  62.5f, 8, 0 },
  { "Australia: QLD",           923.125f,  8,  62.5f, 5, 0 },
  { "Brazil",                   923.125f,  8,  62.5f, 8, 0 },
  { "Canada (3B)",              910.525f,  7,  62.5f, 5, 3 },
  { "Costa Rica",               910.525f, 11, 125.0f, 5, 0 },
  { "EU/UK (Narrow)",           869.618f,  8,  62.5f, 8, 0 },
  { "EU/UK (Deprecated)",       869.525f, 11, 250.0f, 5, 0 },
  { "Czech Republic (Narrow)",  869.432f,  7,  62.5f, 5, 2 },
  { "EU 433MHz (Long Range)",   433.650f, 11, 250.0f, 5, 0 },
  { "EU 433MHz (Narrow)",       433.650f,  8,  62.5f, 8, 0 },
  { "Hungary (2B)",             869.618f,  7,  62.5f, 5, 2 },
  { "Netherlands",              869.618f,  7,  62.5f, 5, 0 },
  { "Netherlands (Limburg)",    869.618f,  8,  62.5f, 8, 2 },
  { "New Zealand (Narrow)",     917.375f,  7,  62.5f, 5, 2 },
  { "New Zealand (Gisborne)",   917.375f, 11, 250.0f, 5, 1 },
  { "Portugal 433",             433.375f,  9,  62.5f, 6, 0 },
  { "Portugal 868",             869.618f,  7,  62.5f, 6, 0 },
  { "Slovakia (2B)",            869.618f,  7,  62.5f, 5, 2 },
  { "Switzerland",              869.618f,  8,  62.5f, 8, 0 },
  { "Thailand",                 921.000f,  7,  62.5f, 5, 0 },
  { "USA",                      910.525f,  7,  62.5f, 5, 0 },
  { "USA - SoCal (3B)",         927.875f,  7,  62.5f, 5, 3 },
  { "Vietnam (Narrow)",         920.250f,  8,  62.5f, 5, 0 },
  { "Vietnam (Deprecated)",     920.250f, 11, 250.0f, 5, 0 },
  { "Custom",                     0.000f,  0,   0.0f, 0, 0 }
};
static const int NUM_RF_PRESETS = 28;

static const uint32_t GPS_BAUD_OPTIONS[] = { 9600, 19200, 38400, 57600, 115200 };
static const int NUM_GPS_BAUDS = 5;

static const uint32_t SLEEP_TIMEOUTS[] = { 30, 60, 120, 300, 0 }; // 0 = never
static const char* SLEEP_LABELS[] = { "30s", "1m", "2m", "5m", "Off" };
static const int NUM_SLEEP_OPTIONS = 5;

static const uint8_t BRIGHTNESS_STEPS[] = { 10, 25, 50, 75, 100 };
static const int NUM_BRIGHTNESS_STEPS = 5;

static bool isChannelSecretZero(const uint8_t* secret, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (secret[i] != 0) return false;
  }
  return true;
}

uint8_t TapUI::calcBatteryPercent(uint16_t mv) {
  if (mv >= 4180) return 100;
  if (mv <= 3250) return 0;
  // Standard single-cell LiPo discharge curve approximation
  if (mv >= 4000) {
    return 80 + (uint8_t)(((uint32_t)(mv - 4000) * 20UL) / 180UL);
  }
  if (mv >= 3700) {
    return 25 + (uint8_t)(((uint32_t)(mv - 3700) * 55UL) / 300UL);
  }
  return (uint8_t)(((uint32_t)(mv - 3250) * 25UL) / 450UL);
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
    _needs_tabbar_redraw(false),
    _last_status_ms(0),
    _backlight_on(true),
    _brightness_pct(100),
    _ble_enabled(true),
    _dark_mode(true), // Dark theme by default for lower power consumption
    _tz_offset(-5),   // Default local timezone (UTC-5)
    _tz_configured(false),
    _screen_sleep_seconds(60),
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
    _chat_scroll_offset(0),
    _total_chat_lines(0),
    _last_rx_packet_count(0),
    _last_rx_packet_ms(0),
    _last_rx_rssi(-128.0f),
    _last_rx_snr(0.0f),
    _sig_show_snr(false),
    _last_is_charging(false),
    _text_len(0),
    _nodes_page(0),
    _gps_baud(9600),
    _gps_pins_swapped(false),
    _gps_subview(0),
    _gps_nav_target_slot(-1),
    _gps_metric_units(true),
    _gps_fix_saved_once(false),
    _last_gps_save_ms(0),
    _last_gps_ui_refresh_ms(0),
    _last_gps_card_hash(0),
    _selected_preset_idx(23), // Default USA (910.525 MHz / SF7 / BW62.5 / CR5)
    _radio_freq(910.525f),
    _radio_sf(7),
    _radio_bw(62.5f),
    _radio_cr(5),
    _radio_tx_power(22),
    _radio_path_hash_mode(0),
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
      _dark_mode = (s.dark_mode != 0);
      if (s.tz_configured && s.tz_offset >= -12 && s.tz_offset <= 14) {
        _tz_offset = s.tz_offset;
        _tz_configured = true;
      }
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
  s.dark_mode = _dark_mode ? 1 : 0;
  s.tz_offset = _tz_offset;
  s.tz_configured = _tz_configured ? 1 : 0;
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

void TapUI::setDarkMode(bool dark, bool persist) {
  _dark_mode = dark;
  if (persist) saveDeviceSettings();
  _needs_full_redraw = true;
}

void TapUI::syncRadioParamsFromPrefs() {
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    if (prefs->freq > 100.0f) _radio_freq = prefs->freq;
    if (prefs->sf >= 5 && prefs->sf <= 12) _radio_sf = prefs->sf;
    if (prefs->bw > 5.0f) _radio_bw = prefs->bw;
    if (prefs->cr >= 5 && prefs->cr <= 8) _radio_cr = prefs->cr;
    if (prefs->tx_power_dbm != 0) _radio_tx_power = prefs->tx_power_dbm;
    _radio_path_hash_mode = (prefs->path_hash_mode <= 2) ? prefs->path_hash_mode : 0;

    _selected_preset_idx = NUM_RF_PRESETS - 1; // Custom
    for (int p = NUM_RF_PRESETS - 2; p >= 0; p--) {
      uint8_t p_mode = (RF_PRESETS[p].path_hash_size > 0) ? (RF_PRESETS[p].path_hash_size - 1) : 0;
      if (fabs(_radio_freq - RF_PRESETS[p].freq) < 0.005f &&
          _radio_sf == RF_PRESETS[p].sf &&
          fabs(_radio_bw - RF_PRESETS[p].bw) < 1.0f &&
          _radio_cr == RF_PRESETS[p].cr &&
          _radio_path_hash_mode == p_mode) {
        _selected_preset_idx = p;
        if (p == 23 || p == 8) break; // Prefer USA / EU/UK (Narrow)
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

  // Enable GPS & location sharing with MeshCore companion app and adverts by default, and sync timezone
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    bool changed = false;
    if (prefs->gps_enabled == 0 && prefs->advert_loc_policy == 0 && prefs->telemetry_mode_loc == 0) {
      prefs->gps_enabled = 1;
      prefs->advert_loc_policy = ADVERT_LOC_SHARE;
      prefs->telemetry_mode_loc = TELEM_MODE_ALLOW_ALL;
      changed = true;
    }
    if (_tz_configured) {
      if (prefs->tz_offset != _tz_offset) {
        prefs->tz_offset = _tz_offset;
        changed = true;
      }
    } else if (prefs->tz_offset != 0 && prefs->tz_offset >= -12 && prefs->tz_offset <= 14) {
      _tz_offset = prefs->tz_offset;
      _tz_configured = true;
      saveDeviceSettings();
    } else {
      // Default local timezone (UTC-5) and sync to prefs
      _tz_offset = -5;
      _tz_configured = true;
      prefs->tz_offset = _tz_offset;
      changed = true;
      saveDeviceSettings();
    }
    if (changed) {
      the_mesh.savePrefs();
    }
  }

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

int8_t TapUI::getTimezoneOffset() const {
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs && prefs->tz_offset != 0 && prefs->tz_offset >= -12 && prefs->tz_offset <= 14) {
    return prefs->tz_offset;
  }
  return _tz_offset;
}

void TapUI::setTimezoneOffset(int8_t tz, bool persist) {
  if (tz < -12) tz = -12;
  if (tz > 14) tz = 14;
  _tz_offset = tz;
  _tz_configured = true;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    prefs->tz_offset = tz;
  }
  if (persist) {
    the_mesh.savePrefs();
    saveDeviceSettings();
  }
  _needs_full_redraw = true;
}

void TapUI::formatLocalDateTime(char* out, size_t max_len, bool include_date, bool include_tz) const {
  uint32_t utc = the_mesh.getRTCClock()->getCurrentTime();
  int8_t tz = getTimezoneOffset();
  time_t local_t = (time_t)((int64_t)utc + (int64_t)tz * 3600LL);
  struct tm tm_info;
  gmtime_r(&local_t, &tm_info);

  if (include_date && include_tz) {
    snprintf(out, max_len, "%04d-%02d-%02d %02d:%02d:%02d UTC%+d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, (int)tz);
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
    matched = sscanf(str, "%d/%d/%d %d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc);
  }
  if (matched < 5) return false;
  if (yr < 2020 || yr > 2099 || mo < 1 || mo > 12 || dy < 1 || dy > 31) return false;
  if (hr < 0 || hr > 23 || mn < 0 || mn > 59 || sc < 0 || sc > 59) return false;

  static const int days_before_month[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
  uint32_t days = (yr - 1970) * 365 + (yr - 1969) / 4 - (yr - 1901) / 100 + (yr - 1601) / 400;
  days += days_before_month[mo - 1];
  bool is_leap = ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0));
  if (is_leap && mo > 2) days += 1;
  days += (dy - 1);

  int64_t local_epoch = (int64_t)days * 86400LL + (int64_t)hr * 3600LL + (int64_t)mn * 60LL + (int64_t)sc;
  int64_t utc_epoch = local_epoch - (int64_t)getTimezoneOffset() * 3600LL;
  if (utc_epoch < 1500000000LL) return false;
  the_mesh.getRTCClock()->setCurrentTime((uint32_t)utc_epoch);
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
    _chat_scroll_offset = 0;
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

// -------------------------------------------------------------
// CONTACT FAVORITES & UNREAD MESSAGE HELPERS
// -------------------------------------------------------------
bool TapUI::setContactFavorite(int contact_idx, bool fav) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(contact_idx, c) || c.type == 0) return false;
  ContactInfo* live = the_mesh.lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
  if (!live) return false;
  if (fav) live->flags |= 0x01;
  else live->flags &= ~0x01;
  store.saveContacts(&the_mesh);
  _needs_full_redraw = true;
  return true;
}

bool TapUI::toggleContactFavorite(int contact_idx) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(contact_idx, c) || c.type == 0) return false;
  return setContactFavorite(contact_idx, !c.isFav());
}

int TapUI::getTotalUnreadCount() const {
  int cnt = 0;
  for (int i = 0; i < _msg_count; i++) {
    if (_messages[i].unread) cnt++;
  }
  return cnt;
}

int TapUI::getChannelUnreadCount(uint8_t channel_idx) const {
  int cnt = 0;
  for (int i = 0; i < _msg_count; i++) {
    if (_messages[i].unread && !_messages[i].is_direct && _messages[i].channel_idx == channel_idx) {
      cnt++;
    }
  }
  return cnt;
}

int TapUI::getContactUnreadCount(const uint8_t* pubkey_prefix) const {
  if (!pubkey_prefix) return 0;
  int cnt = 0;
  for (int i = 0; i < _msg_count; i++) {
    if (_messages[i].unread && _messages[i].is_direct &&
        memcmp(_messages[i].contact_pubkey, pubkey_prefix, 4) == 0) {
      cnt++;
    }
  }
  return cnt;
}

void TapUI::markAllMessagesRead() {
  for (int i = 0; i < _msg_count; i++) {
    _messages[i].unread = false;
  }
  _needs_full_redraw = true;
}

int TapUI::buildSortedContactList(DiscoveredNodeItem* items, int max_items) {
  int total_slots = the_mesh.getTotalContactSlots();
  int count = 0;
  ContactInfo c; // Single ContactInfo instance (~152 bytes)

  // Pass 0: Favorites first (pass == 0), then non-favorites (pass == 1)
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 8; i < total_slots && count < max_items; i++) {
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.type == 0) continue; // ADV_TYPE_NONE

      bool fav = c.isFav();
      if ((pass == 0 && !fav) || (pass == 1 && fav)) continue;

      if (c.name[0] != 0) {
        strncpy(items[count].name, c.name, 27);
        items[count].name[27] = 0;
      } else {
        snprintf(items[count].name, sizeof(items[count].name), "!%02X%02X%02X%02X",
                 c.id.pub_key[0], c.id.pub_key[1], c.id.pub_key[2], c.id.pub_key[3]);
      }
      mesh::Utils::toHex(items[count].id_hex, c.id.pub_key, 4);
      items[count].type = c.type;
      items[count].is_fav = fav;
      items[count].unread_dm = (uint8_t)getContactUnreadCount(c.id.pub_key);
      items[count].lat = c.gps_lat;
      items[count].lon = c.gps_lon;
      items[count].contact_idx = i;
      count++;
    }
  }
  return count;
}

void TapUI::restartGPS() {
  Serial1.end();
  if (!_gps_pins_swapped) {
    Serial1.setPins(PIN_SERIAL1_RX, PIN_SERIAL1_TX);
  } else {
    Serial1.setPins(PIN_SERIAL1_TX, PIN_SERIAL1_RX);
  }
  Serial1.begin(_gps_baud);

  sensors.setGpsActive(true);
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

bool TapUI::isGPSShareEnabled() const {
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return true;
  return prefs->advert_loc_policy != ADVERT_LOC_NONE;
}

void TapUI::setGPSShareEnabled(bool share) {
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return;
  prefs->advert_loc_policy = share ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  prefs->telemetry_mode_loc = share ? TELEM_MODE_ALLOW_ALL : TELEM_MODE_DENY;
  the_mesh.savePrefs();
  _needs_full_redraw = true;
}

const char* TapUI::degToCardinal(float deg) {
  static const char* DIRS[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
  };
  float norm = fmod(deg + 360.0f, 360.0f);
  int idx = (int)((norm + 11.25f) / 22.5f) & 15;
  return DIRS[idx];
}

double TapUI::calcDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = (lat2 - lat1) * (M_PI / 180.0);
  double dLon = (lon2 - lon1) * (M_PI / 180.0);
  double a = sin(dLat * 0.5) * sin(dLat * 0.5) +
             cos(lat1 * (M_PI / 180.0)) * cos(lat2 * (M_PI / 180.0)) *
             sin(dLon * 0.5) * sin(dLon * 0.5);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return R * c;
}

double TapUI::calcBearingDegrees(double lat1, double lon1, double lat2, double lon2) {
  double phi1 = lat1 * (M_PI / 180.0);
  double phi2 = lat2 * (M_PI / 180.0);
  double dLon = (lon2 - lon1) * (M_PI / 180.0);
  double y = sin(dLon) * cos(phi2);
  double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);
  double brng = atan2(y, x) * (180.0 / M_PI);
  return fmod(brng + 360.0, 360.0);
}

void TapUI::syncGPSWithMesh() {
  LocationProvider* loc = sensors.getLocationProvider();
  if (!loc || !loc->isEnabled() || !loc->isValid()) return;

  double lat = (double)loc->getLatitude() / 1000000.0;
  double lon = (double)loc->getLongitude() / 1000000.0;
  double alt = (double)loc->getAltitude() / 1000.0;
  if (lat == 0.0 && lon == 0.0) return;

  sensors.node_lat = lat;
  sensors.node_lon = lon;
  sensors.node_altitude = alt;

  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    double prev_saved_lat = prefs->node_lat;
    double prev_saved_lon = prefs->node_lon;
    prefs->node_lat = lat;
    prefs->node_lon = lon;

    unsigned long now = millis();
    if (!_gps_fix_saved_once ||
        ((now - _last_gps_save_ms > 300000UL) &&
         calcDistanceMeters(prev_saved_lat, prev_saved_lon, lat, lon) > 50.0)) {
      _gps_fix_saved_once = true;
      _last_gps_save_ms = now;
      the_mesh.savePrefs();
    }
  }
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
  _radio_path_hash_mode = (RF_PRESETS[preset_idx].path_hash_size > 0)
                            ? (RF_PRESETS[preset_idx].path_hash_size - 1)
                            : 0;
  _needs_full_redraw = true;
}

void TapUI::setPathHashMode(uint8_t mode) {
  if (mode > 2) return;
  _radio_path_hash_mode = mode;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (prefs) {
    prefs->path_hash_mode = mode;
    the_mesh.savePrefs();
  }
  syncRadioParamsFromPrefs();
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
    prefs->path_hash_mode = _radio_path_hash_mode;
    the_mesh.savePrefs();
  }
  radio_driver.setParams(_radio_freq, _radio_bw, _radio_sf, _radio_cr);
  radio_driver.setTxPower(_radio_tx_power);
  showStatusNotification("Radio & Hash Mode Applied!");
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
  m->timestamp = the_mesh.getRTCClock()->getCurrentTime();
  if (pubkey) memcpy(m->contact_pubkey, pubkey, 4);
  else memset(m->contact_pubkey, 0, 4);
  strncpy(m->target_name, sender ? sender : "", sizeof(m->target_name) - 1);
  m->target_name[sizeof(m->target_name) - 1] = 0;

  // Check if user is currently viewing this exact DM conversation at the bottom
  bool viewing_now = (!_is_screen_sleeping && _current_tab == TAB_CHAT && _edit_mode == EDIT_NONE &&
                      _chat_scroll_offset == 0 && _active_contact_idx >= 0);
  if (viewing_now) {
    ContactInfo active_c;
    if (the_mesh.getContactByIdx(_active_contact_idx, active_c)) {
      if (pubkey && memcmp(pubkey, active_c.id.pub_key, 4) != 0) viewing_now = false;
    } else {
      viewing_now = false;
    }
  }
  m->unread = !viewing_now;

  if (_current_tab == TAB_CHAT) {
    _needs_chat_messages_redraw = true;
  } else if (_current_tab == TAB_NODES) {
    _needs_full_redraw = true;
  } else {
    _needs_tabbar_redraw = true;
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
  m->timestamp = the_mesh.getRTCClock()->getCurrentTime();
  memset(m->contact_pubkey, 0, 4);
  strncpy(m->target_name, channel_name ? channel_name : "Public", sizeof(m->target_name) - 1);
  m->target_name[sizeof(m->target_name) - 1] = 0;

  bool viewing_now = (!_is_screen_sleeping && _current_tab == TAB_CHAT && _edit_mode == EDIT_NONE &&
                      _chat_scroll_offset == 0 && _active_contact_idx == -1 && _active_channel == channel_idx);
  m->unread = !viewing_now;

  if (_current_tab == TAB_CHAT) {
    _needs_chat_messages_redraw = true;
  } else if (_current_tab == TAB_CHANS) {
    _needs_full_redraw = true;
  } else {
    _needs_tabbar_redraw = true;
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
  m->unread = false;
  m->timestamp = the_mesh.getRTCClock()->getCurrentTime();

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

  _chat_scroll_offset = 0; // Jump to latest when sending
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

  // Keep MeshCore sensors.node_lat/node_lon and prefs->node_lat/node_lon synced with live GPS fix
  syncGPSWithMesh();

  // Screen sleep inactivity timer
  if (_screen_sleep_seconds > 0 && !_is_screen_sleeping && (now - _last_activity_ms > _screen_sleep_seconds * 1000UL)) {
    _is_screen_sleeping = true;
    board.setBacklight(false);
  }

  // Live signal metrics from radio driver (refreshes status bar on incoming RF packet)
  uint32_t rx_pkts = radio_driver.getPacketsRecv();
  if (rx_pkts != _last_rx_packet_count) {
    _last_rx_packet_count = rx_pkts;
    _last_rx_packet_ms = now;
    _last_rx_rssi = radio_driver.getLastRSSI();
    _last_rx_snr = radio_driver.getLastSNR();
    if (!_is_screen_sleeping && !_needs_full_redraw) {
      drawStatusBar();
    }
  }

  // Monitor charging status (instant redraw on plug/unplug)
  bool is_chg = board.isExternalPowered();
  if (is_chg != _last_is_charging) {
    _last_is_charging = is_chg;
    if (!_is_screen_sleeping && !_needs_full_redraw) {
      drawStatusBar();
    }
  }

  // Flicker-free partial refresh when viewing the GPS tab (only redraws card interior when values change)
  if (!_is_screen_sleeping && _current_tab == TAB_GPS && !_needs_full_redraw && (now - _last_gps_ui_refresh_ms > 3000UL)) {
    _last_gps_ui_refresh_ms = now;
    refreshGPSDataOnly();
  }

  TouchPoint pt;
  _touch.poll(pt);
  _touch_is_down = pt.isPressed;
  {
    if (pt.justPressed) {
      _last_activity_ms = now;

      if (_is_screen_sleeping) {
        wakeScreen();
        return;
      }

      uint16_t tx = pt.x;
      uint16_t ty = pt.y;

      if (ty < 20) {
        return; // Ignore taps on status bar
      } else if (ty >= 20 && ty <= 42) {
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

  if (_needs_full_redraw) {
    _needs_full_redraw = false;
    _needs_chat_messages_redraw = false;
    _needs_tabbar_redraw = false;

    _display->setColor(colBg());
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
    _needs_tabbar_redraw = false;
    if (_current_tab == TAB_CHAT) {
      drawChatMessagesOnly();
      drawTabBar();
    }
  } else if (_needs_tabbar_redraw) {
    _needs_tabbar_redraw = false;
    drawTabBar();
  } else if (now - _last_status_ms > 10000) {
    drawStatusBar();
    _last_status_ms = now;
  }
}

void TapUI::drawStatusBar() {
  _display->setColor(_dark_mode ? COLOR_DARK_HDR : COLOR_NAVY);
  _display->fillRect(0, 0, 320, 20);

  // 1. Top-Left Signal Strength Indicator (Bars only, X = 4..17)
  bool sig_recent = (_last_rx_packet_count > 0) && (millis() - _last_rx_packet_ms < 180000UL);
  drawSignalBars(4, 5, _last_rx_rssi, _last_rx_snr, sig_recent);

  // 2. Top-Left Battery Icon (Icon only, X = 22..38)
  uint16_t mv = board.getBattMilliVolts();
  uint8_t pct = calcBatteryPercent(mv);
  bool is_charging = board.isExternalPowered();
  uint16_t batCol = is_charging ? COLOR_GREEN_BTN : ((pct > 40) ? COLOR_GREEN_BTN : ((pct > 18) ? COLOR_ORANGE_BTN : COLOR_RED_BTN));

  _display->setColor(COLOR_WHITE);
  _display->drawRect(22, 5, 15, 10);
  _display->fillRect(37, 8, 2, 4);
  int fillW = (pct * 11) / 100;
  if (fillW > 0) {
    _display->setColor(batCol);
    _display->fillRect(24, 7, fillW, 6);
  }

  // When charging, draw sharp lightning bolt in center of battery
  if (is_charging) {
    _display->setColor(COLOR_GOLD_STAR);
    _display->fillRect(29, 6, 2, 2); // top segment
    _display->fillRect(28, 7, 2, 2); // upper slant
    _display->fillRect(26, 9, 7, 1); // center crossbar
    _display->fillRect(29, 10, 2, 2); // lower slant
    _display->fillRect(28, 11, 2, 2); // bottom tip
  }

  _display->setTextSize(1);

  // 3. Active Target (X = 46)
  char recBuf[16];
  if (_active_contact_idx >= 0) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(_active_contact_idx, c)) {
      if (c.name[0] != 0) snprintf(recBuf, sizeof(recBuf), "@%.10s", c.name);
      else snprintf(recBuf, sizeof(recBuf), "@!%02X%02X", c.id.pub_key[0], c.id.pub_key[1]);
    } else {
      strcpy(recBuf, "@Node");
    }
  } else {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch)) {
      if (ch.name[0] == '#') snprintf(recBuf, sizeof(recBuf), "%.10s", ch.name);
      else snprintf(recBuf, sizeof(recBuf), "#%.9s", ch.name[0] ? ch.name : "Public");
    } else {
      strcpy(recBuf, "#Public");
    }
  }
  _display->setColor(COLOR_WHITE);
  drawStr(46, 5, recBuf);

  // 4. BLE Status / PIN (X = 126)
  if (!_ble_enabled) {
    _display->setColor(COLOR_MED_GRAY);
    drawStr(126, 5, "BT:OFF");
  } else if (ui_task.hasConnection()) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(126, 5, "BT:CONN");
  } else if (the_mesh.getBLEPin() != 0) {
    char pinBuf[12];
    snprintf(pinBuf, sizeof(pinBuf), "P:%06lu", (unsigned long)the_mesh.getBLEPin());
    _display->setColor(COLOR_ORANGE_BTN);
    drawStr(126, 5, pinBuf);
  } else {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(126, 5, "BT:ON");
  }

  // 5. GPS Status (X = 196)
  LocationProvider* loc = sensors.getLocationProvider();
  if (loc && loc->isEnabled()) {
    if (loc->isValid()) {
      char gpsBuf[10];
      snprintf(gpsBuf, sizeof(gpsBuf), "G:%ld", loc->satellitesCount());
      _display->setColor(COLOR_GREEN_BTN);
      drawStr(196, 5, gpsBuf);
    } else {
      _display->setColor(COLOR_ORANGE_BTN);
      drawStr(196, 5, "G:..");
    }
  } else {
    _display->setColor(COLOR_MED_GRAY);
    drawStr(196, 5, "G:--");
  }

  // 6. Clock HH:MM (anchored to right edge X = 316)
  char clkBuf[10];
  formatLocalDateTime(clkBuf, sizeof(clkBuf), false, false);
  _display->setColor(COLOR_WHITE);
  _display->drawTextRightAlign(316, 5, clkBuf);
}

void TapUI::drawSignalBars(int bx, int by, float rssi, float snr, bool recent) {
  int active_bars = 0;
  uint16_t active_col = COLOR_MED_GRAY;

  if (recent) {
    if (rssi >= -95.0f) {
      active_bars = 4;
      active_col = COLOR_GREEN_BTN;
    } else if (rssi >= -108.0f) {
      active_bars = 3;
      active_col = COLOR_GREEN_BTN;
    } else if (rssi >= -118.0f) {
      active_bars = 2;
      active_col = COLOR_ORANGE_BTN;
    } else {
      active_bars = 1;
      active_col = COLOR_RED_BTN;
    }
  }

  // 4 cellular-style signal bars
  // Base Y is by + 9 (y = 14)
  const int bar_heights[4] = {3, 5, 7, 9};
  for (int i = 0; i < 4; i++) {
    int h = bar_heights[i];
    int x = bx + i * 4;
    int y = by + 9 - h + 1;
    if (i < active_bars) {
      _display->setColor(active_col);
      _display->fillRect(x, y, 2, h);
    } else {
      _display->setColor(COLOR_MED_GRAY);
      _display->drawRect(x, y, 2, h);
    }
  }
}

void TapUI::drawTabBar() {
  _display->setColor(colPanelBg());
  _display->fillRect(0, 20, 320, 23);

  int totalUnread = getTotalUnreadCount();
  char chatTabLabel[12];
  if (totalUnread > 0) {
    snprintf(chatTabLabel, sizeof(chatTabLabel), "CHAT(%d)", totalUnread > 9 ? 9 : totalUnread);
  } else {
    strcpy(chatTabLabel, "CHAT");
  }

  const char* tabs[5] = { chatTabLabel, "NODES", "CHANS", "GPS", "RADIO" };
  for (int i = 0; i < 5; i++) {
    int x = i * 64;
    bool isActive = (i == (int)_current_tab);

    uint16_t tabBg = isActive ? (_dark_mode ? 0x2124 : COLOR_WHITE) : (_dark_mode ? 0x0841 : 0xC618);
    _display->setColor(tabBg);
    _display->fillRect(x + 1, 21, 62, 21);

    _display->setColor(isActive ? colTitle() : colBorder());
    _display->drawRect(x + 1, 21, 62, 21);

    _display->setTextSize(1);
    if (i == 0 && totalUnread > 0 && !isActive) {
      _display->setColor(COLOR_ORANGE_BTN);
    } else {
      _display->setColor(isActive ? colTitle() : colSubText());
    }
    int tw = strlen(tabs[i]) * 6;
    drawStr(x + (64 - tw) / 2, 27, tabs[i]);

    if (isActive) {
      _display->setColor(colTitle());
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
// CHAT SCREEN, SCROLLBACK & GENERALIZED KEYBOARD INPUT
// -------------------------------------------------------------
void TapUI::drawChatScreen() {
  drawChatMessagesOnly();
  drawTextInputBar();
  drawKeyboard();
}

void TapUI::drawChatMessagesOnly() {
  // Clear strictly chat history area: Y in 44..116
  _display->setColor(colBg());
  _display->fillRect(0, 44, 320, 73);

  if (_edit_mode != EDIT_NONE) {
    _display->setColor(colTitle());
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
        line1 = "Enter frequency in MHz (e.g. 910.525 or 869.618).";
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

    // [CANCEL] button: X in 245..312, Y in 46..64
    _display->setColor(COLOR_RED_BTN);
    _display->fillRect(245, 46, 67, 18);
    _display->setColor(COLOR_WHITE);
    drawStr(255, 51, "CANCEL");

    _display->setColor(colSubText());
    drawStr(8, 70, line1);
    drawStr(8, 86, line2);
    return;
  }

  // Filter messages for currently selected target
  ContactInfo active_c;
  bool hasActiveContact = false;
  ChannelDetails active_ch;
  memset(&active_ch, 0, sizeof(active_ch));
  if (_active_contact_idx >= 0) {
    hasActiveContact = the_mesh.getContactByIdx(_active_contact_idx, active_c);
  } else {
    the_mesh.getChannel(_active_channel, active_ch);
  }

  int matchCount = 0;

  for (int i = 0; i < _msg_count; i++) {
    UIMessage* m = &_messages[i];
    if (_active_contact_idx >= 0 && hasActiveContact) {
      if (m->is_direct) {
        if (memcmp(m->contact_pubkey, active_c.id.pub_key, 4) == 0 ||
            strcmp(m->sender, active_c.name) == 0 ||
            strcmp(m->target_name, active_c.name) == 0) {
          _chat_match_indices[matchCount++] = i;
        }
      }
    } else {
      if (!m->is_direct &&
          (m->channel_idx == _active_channel ||
           (active_ch.name[0] != 0 && strcmp(m->target_name, active_ch.name) == 0))) {
        _chat_match_indices[matchCount++] = i;
      }
    }
  }

  if (matchCount == 0) {
    _display->setColor(colSubText());
    _display->setTextSize(1);
    if (_active_contact_idx >= 0 && hasActiveContact) {
      char targetPrompt[64];
      snprintf(targetPrompt, sizeof(targetPrompt), "Direct Chat with @%s", active_c.name[0] ? active_c.name : "Node");
      drawStr(25, 65, targetPrompt);
      drawStr(35, 82, "Type message below to send private text");
    } else {
      ChannelDetails ch;
      the_mesh.getChannel(_active_channel, ch);
      char chPrompt[64];
      if (ch.name[0] == '#') {
        snprintf(chPrompt, sizeof(chPrompt), "Channel %s Broadcast (Ch #%u)", ch.name, _active_channel);
      } else {
        snprintf(chPrompt, sizeof(chPrompt), "Channel #%s Broadcast (Ch #%u)", ch.name[0] ? ch.name : "Public", _active_channel);
      }
      drawStr(25, 65, chPrompt);
      drawStr(35, 82, "Type message below to transmit on LoRa");
    }
    return;
  }

  // Format and wrap messages into line items
  _total_chat_lines = 0;
  int startMsg = 0;
  if (matchCount > 40) {
    startMsg = matchCount - 40;
  }

  for (int k = startMsg; k < matchCount; k++) {
    UIMessage* m = &_messages[_chat_match_indices[k]];
    bool wasUnread = m->unread;
    m->unread = false; // Mark read now that the user is viewing it

    char prefix[32];
    uint16_t lineCol;
    if (m->is_outgoing) {
      strcpy(prefix, "You: ");
      lineCol = colTitle();
    } else {
      if (wasUnread) {
        snprintf(prefix, sizeof(prefix), "*%s: ", m->sender);
        lineCol = COLOR_GOLD_STAR;
      } else {
        snprintf(prefix, sizeof(prefix), "%s: ", m->sender);
        lineCol = colText();
      }
    }
    formatAndWrapMessage(prefix, m->text, lineCol);
  }

  // Clamp _chat_scroll_offset (line-based offset)
  int maxScroll = (_total_chat_lines > VISIBLE_CHAT_LINES) ? (_total_chat_lines - VISIBLE_CHAT_LINES) : 0;
  if (_chat_scroll_offset > maxScroll) _chat_scroll_offset = maxScroll;
  if (_chat_scroll_offset < 0) _chat_scroll_offset = 0;

  int endLine = _total_chat_lines - _chat_scroll_offset;
  int startLine = endLine - VISIBLE_CHAT_LINES;
  if (startLine < 0) startLine = 0;

  int y = 46;
  for (int l = startLine; l < endLine; l++) {
    _display->setColor(_chat_lines[l].color);
    drawStr(6, y, _chat_lines[l].text);
    y += CHAT_LINE_HEIGHT;
  }

  // Right-side Scroll Bar & Controls (X in 280..318, Y in 45..115)
  _display->setColor(colCardBg());
  _display->fillRect(280, 45, 38, 22);
  _display->setColor(_chat_scroll_offset < maxScroll ? colTitle() : colBorder());
  _display->drawRect(280, 45, 38, 22);
  _display->setColor(_chat_scroll_offset < maxScroll ? colText() : colMutedText());
  drawStr(288, 52, "UP ^");

  // Middle indicator (shows e.g. "6/12" or "ALL")
  char posBuf[12];
  if (_total_chat_lines <= VISIBLE_CHAT_LINES) {
    strcpy(posBuf, "ALL");
    _display->setColor(colSubText());
    drawStr(290, 76, posBuf);
  } else {
    snprintf(posBuf, sizeof(posBuf), "%d/%d", endLine, _total_chat_lines);
    _display->setColor(colSubText());
    drawStr(282, 76, posBuf);
  }

  _display->setColor(colCardBg());
  _display->fillRect(280, 93, 38, 22);
  _display->setColor(_chat_scroll_offset > 0 ? colTitle() : colBorder());
  _display->drawRect(280, 93, 38, 22);
  _display->setColor(_chat_scroll_offset > 0 ? colText() : colMutedText());
  drawStr(288, 100, "DN v");
}

void TapUI::formatAndWrapMessage(const char* prefix, const char* text, uint16_t color) {
  if (!text) return;
  const int MAX_LINE_CHARS = 44;
  char curLine[48];
  int curLen = 0;

  // Add prefix to first line
  if (prefix) {
    while (*prefix && curLen < MAX_LINE_CHARS) {
      curLine[curLen++] = *prefix++;
    }
  }

  const char* p = text;
  while (*p && _total_chat_lines < MAX_CHAT_LINES) {
    if (*p == '\r') {
      p++;
      continue;
    }
    if (*p == '\n') {
      // End current line and start a new indented line
      curLine[curLen] = '\0';
      if (_total_chat_lines < MAX_CHAT_LINES) {
        strncpy(_chat_lines[_total_chat_lines].text, curLine, sizeof(_chat_lines[0].text) - 1);
        _chat_lines[_total_chat_lines].text[sizeof(_chat_lines[0].text) - 1] = '\0';
        _chat_lines[_total_chat_lines].color = color;
        _total_chat_lines++;
      }
      curLen = 0;
      curLine[curLen++] = ' ';
      curLine[curLen++] = ' ';
      p++;
      continue;
    }

    // Skip leading spaces if at start of line
    if (curLen == 0 || (curLen == 2 && curLine[0] == ' ' && curLine[1] == ' ')) {
      while (*p == ' ') p++;
      if (!*p) break;
    }

    // Identify next word
    const char* wordStart = p;
    while (*p && *p != ' ' && *p != '\n' && *p != '\r') {
      p++;
    }
    int wordLen = p - wordStart;
    if (wordLen == 0) continue;

    // Check if we need a space separator before this word
    int needed = wordLen + ((curLen > 0 && curLine[curLen - 1] != ' ') ? 1 : 0);

    if (curLen + needed <= MAX_LINE_CHARS) {
      if (curLen > 0 && curLine[curLen - 1] != ' ') {
        curLine[curLen++] = ' ';
      }
      memcpy(&curLine[curLen], wordStart, wordLen);
      curLen += wordLen;
    } else {
      // Word does not fit on current line: flush current line if not empty
      if (curLen > 0) {
        curLine[curLen] = '\0';
        if (_total_chat_lines < MAX_CHAT_LINES) {
          strncpy(_chat_lines[_total_chat_lines].text, curLine, sizeof(_chat_lines[0].text) - 1);
          _chat_lines[_total_chat_lines].text[sizeof(_chat_lines[0].text) - 1] = '\0';
          _chat_lines[_total_chat_lines].color = color;
          _total_chat_lines++;
        }
      }
      // Start next line with indent
      curLen = 0;
      curLine[curLen++] = ' ';
      curLine[curLen++] = ' ';

      // If word itself is longer than MAX_LINE_CHARS - 2 (42 chars), chunk it
      const char* w = wordStart;
      int rem = wordLen;
      while (rem > (MAX_LINE_CHARS - curLen) && _total_chat_lines < MAX_CHAT_LINES) {
        int chunk = MAX_LINE_CHARS - curLen;
        memcpy(&curLine[curLen], w, chunk);
        curLen += chunk;
        curLine[curLen] = '\0';
        strncpy(_chat_lines[_total_chat_lines].text, curLine, sizeof(_chat_lines[0].text) - 1);
        _chat_lines[_total_chat_lines].text[sizeof(_chat_lines[0].text) - 1] = '\0';
        _chat_lines[_total_chat_lines].color = color;
        _total_chat_lines++;

        w += chunk;
        rem -= chunk;
        curLen = 0;
        curLine[curLen++] = ' ';
        curLine[curLen++] = ' ';
      }
      if (rem > 0 && curLen + rem <= MAX_LINE_CHARS) {
        memcpy(&curLine[curLen], w, rem);
        curLen += rem;
      }
    }

    if (*p == ' ') p++;
  }

  // Flush any remaining characters on current line
  if (curLen > 0 && _total_chat_lines < MAX_CHAT_LINES) {
    curLine[curLen] = '\0';
    strncpy(_chat_lines[_total_chat_lines].text, curLine, sizeof(_chat_lines[0].text) - 1);
    _chat_lines[_total_chat_lines].text[sizeof(_chat_lines[0].text) - 1] = '\0';
    _chat_lines[_total_chat_lines].color = color;
    _total_chat_lines++;
  }
}

void TapUI::drawTextInputBar() {
  // Box: Y in 118..138
  _display->setColor(colKeyBg());
  _display->fillRect(1, 118, 318, 21);

  _display->setColor(colTitle());
  _display->drawRect(1, 118, 318, 21);

  _display->setTextSize(1);
  if (_text_len == 0) {
    _display->setColor(colMutedText());
    if (_edit_mode != EDIT_NONE) {
      drawStr(8, 124, "> Enter value...");
    } else {
      drawStr(8, 124, "> Type message here...");
    }
  } else {
    _display->setColor(colText());
    char displayLine[MAX_MSG_TEXT_LEN + 4];
    snprintf(displayLine, sizeof(displayLine), "> %s_", _text_buffer);
    drawStr(8, 124, displayLine);
  }
}

void TapUI::drawKeyboard() {
  // Keyboard area: Y in 140..239 (height 99)
  _display->setColor(colPanelBg());
  _display->fillRect(0, 140, 320, 100);

  _display->setTextSize(1);

  const char* r0_lower = "qwertyuiop";
  const char* r0_upper = "QWERTYUIOP";
  const char* r0_sym   = "1234567890";
  const char* r0 = (_kb_mode == KB_SYMBOLS) ? r0_sym : ((_kb_mode == KB_UPPER) ? r0_upper : r0_lower);

  for (int k = 0; k < 10; k++) {
    int kx = 1 + k * 32;
    _display->setColor(colKeyBg());
    _display->fillRect(kx, 142, 30, 23);
    _display->setColor(colBorder());
    _display->drawRect(kx, 142, 30, 23);

    _display->setColor(colText());
    char buf[2] = { r0[k], 0 };
    drawStr(kx + 11, 149, buf);
  }

  const char* r1_lower = "asdfghjkl";
  const char* r1_upper = "ASDFGHJKL";
  const char* r1_sym   = "@#$%&*-+/";
  const char* r1 = (_kb_mode == KB_SYMBOLS) ? r1_sym : ((_kb_mode == KB_UPPER) ? r1_upper : r1_lower);

  for (int k = 0; k < 9; k++) {
    int kx = 17 + k * 32;
    _display->setColor(colKeyBg());
    _display->fillRect(kx, 167, 30, 23);
    _display->setColor(colBorder());
    _display->drawRect(kx, 167, 30, 23);

    _display->setColor(colText());
    char buf[2] = { r1[k], 0 };
    drawStr(kx + 11, 174, buf);
  }

  // Row 2: Shift, 7 keys, DEL
  _display->setColor((_kb_mode == KB_UPPER) ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(1, 192, 36, 23);
  _display->setColor(colBorder());
  _display->drawRect(1, 192, 36, 23);
  _display->setColor((_kb_mode == KB_UPPER) ? COLOR_WHITE : colText());
  drawStr(7, 199, "CAP");

  const char* r2_lower = "zxcvbnm";
  const char* r2_upper = "ZXCVBNM";
  const char* r2_sym   = "()!?:,.";
  const char* r2 = (_kb_mode == KB_SYMBOLS) ? r2_sym : ((_kb_mode == KB_UPPER) ? r2_upper : r2_lower);

  for (int k = 0; k < 7; k++) {
    int kx = 39 + k * 32;
    _display->setColor(colKeyBg());
    _display->fillRect(kx, 192, 30, 23);
    _display->setColor(colBorder());
    _display->drawRect(kx, 192, 30, 23);

    _display->setColor(colText());
    char buf[2] = { r2[k], 0 };
    drawStr(kx + 11, 199, buf);
  }

  // DEL button: X in 264..319
  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(264, 192, 55, 23);
  _display->setColor(colBorder());
  _display->drawRect(264, 192, 55, 23);
  _display->setColor(COLOR_WHITE);
  drawStr(280, 199, "DEL");

  // Row 3: ?123, QUICK/CLEAR, SPACE, SEND/SAVE
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(1, 217, 44, 22);
  _display->setColor(colBorder());
  _display->drawRect(1, 217, 44, 22);
  _display->setColor((_kb_mode == KB_SYMBOLS) ? COLOR_WHITE : colText());
  drawStr(6, 224, (_kb_mode == KB_SYMBOLS) ? "ABC" : "?123");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(47, 217, 50, 22);
  _display->setColor(colBorder());
  _display->drawRect(47, 217, 50, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(54, 224, (_edit_mode != EDIT_NONE) ? "CLEAR" : "QUICK");

  _display->setColor(colKeyBg());
  _display->fillRect(99, 217, 140, 22);
  _display->setColor(colBorder());
  _display->drawRect(99, 217, 140, 22);
  _display->setColor(colSubText());
  drawStr(150, 224, "SPACE");

  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(242, 217, 77, 22);
  _display->setColor(colBorder());
  _display->drawRect(242, 217, 77, 22);
  _display->setColor(COLOR_WHITE);
  drawStr(264, 224, (_edit_mode != EDIT_NONE) ? "SAVE" : "SEND");
}

void TapUI::handleChatTouch(uint16_t tx, uint16_t ty) {
  if (_edit_mode != EDIT_NONE && tx >= 240 && tx <= 318 && ty >= 44 && ty <= 68) {
    cancelKeyboardEdit();
    return;
  }

  // Scroll Up / Down buttons in chat area: X in 276..319, Y in 44..116
  if (_edit_mode == EDIT_NONE && tx >= 276 && ty >= 44 && ty <= 116) {
    if (ty <= 74) {
      // UP (older lines)
      _chat_scroll_offset += 3;
      _needs_chat_messages_redraw = true;
      return;
    } else if (ty >= 88) {
      // DN (newer lines)
      _chat_scroll_offset -= 3;
      if (_chat_scroll_offset < 0) _chat_scroll_offset = 0;
      _needs_chat_messages_redraw = true;
      return;
    }
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

  if (ty >= 142 && ty <= 165) {
    for (int k = 0; k < 10; k++) {
      int kx = 1 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r0[k]);
        return;
      }
    }
  }

  if (ty >= 167 && ty <= 190) {
    for (int k = 0; k < 9; k++) {
      int kx = 17 + k * 32;
      if (tx >= kx && tx <= kx + 30) {
        appendChar(r1[k]);
        return;
      }
    }
  }

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
// NODES SCREEN (Favorites First + Favorite Toggle Button + Unread Badge)
// -------------------------------------------------------------
void TapUI::drawNodesScreen() {
  _display->setColor(colBg());
  _display->fillRect(0, 44, 320, 196);

  int count = buildSortedContactList(_sorted_nodes, MAX_SORTED_NODES);

  // Header row
  _display->setColor(colTitle());
  _display->setTextSize(1);
  char titleBuf[36];
  snprintf(titleBuf, sizeof(titleBuf), "Nodes (%d) - Favorites First", count);
  drawStr(8, 50, titleBuf);

  // Discover button at top right
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(214, 47, 98, 19);
  _display->setColor(COLOR_WHITE);
  drawStr(225, 53, "[DISCOVER]");

  if (count == 0) {
    _display->setColor(colSubText());
    drawStr(20, 85, "No mesh contacts discovered yet.");
    drawStr(20, 105, "Make sure other nodes are powered and in range.");
    drawStr(20, 125, "Tap [DISCOVER] above to broadcast a discovery probe.");

    _display->setColor(COLOR_GREEN_BTN);
    _display->fillRect(40, 155, 240, 36);
    _display->setColor(colBorder());
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
    DiscoveredNodeItem* item = &_sorted_nodes[i];

    uint16_t boxBg = _dark_mode ? COLOR_DK_CLIENT_BG : COLOR_CARD_CLIENT_BG;
    uint16_t boxBd = COLOR_CARD_CLIENT_BD;

    if (item->type == 2) {
      boxBg = _dark_mode ? COLOR_DK_REPEAT_BG : COLOR_CARD_REPEAT_BG;
      boxBd = COLOR_CARD_REPEAT_BD;
    } else if (item->type == 3) {
      boxBg = _dark_mode ? COLOR_DK_ROOM_BG : COLOR_CARD_ROOM_BG;
      boxBd = COLOR_CARD_ROOM_BD;
    } else if (item->type == 4) {
      boxBg = _dark_mode ? COLOR_DK_SENSOR_BG : COLOR_CARD_SENSOR_BG;
      boxBd = COLOR_CARD_SENSOR_BD;
    }

    if (item->is_fav) {
      boxBd = COLOR_GOLD_STAR;
    }

    _display->setColor(boxBg);
    _display->fillRect(8, y, 304, 46);
    _display->setColor(boxBd);
    _display->drawRect(8, y, 304, 46);
    _display->fillRect(8, y, 6, 46);

    // Node name + Favorite indicator + Unread indicator
    _display->setColor(item->is_fav ? COLOR_GOLD_STAR : colTitle());
    _display->setTextSize(1);
    char nameLine[34];
    if (item->unread_dm > 0) {
      snprintf(nameLine, sizeof(nameLine), "%s%.18s (%u NEW)",
               item->is_fav ? "* " : "", item->name, item->unread_dm);
    } else {
      snprintf(nameLine, sizeof(nameLine), "%s%.24s",
               item->is_fav ? "* " : "", item->name);
    }
    drawStr(18, y + 6, nameLine);

    _display->setColor(colSubText());
    char sub[36];
    if (item->lat != 0) {
      snprintf(sub, sizeof(sub), "!%s %.2f,%.2f", item->id_hex, (float)item->lat / 1e6f, (float)item->lon / 1e6f);
    } else {
      const char* roleStr = (item->type == 2) ? "Repeater" : ((item->type == 3) ? "Room" : "Client");
      snprintf(sub, sizeof(sub), "ID:!%s [%s]", item->id_hex, roleStr);
    }
    drawStr(18, y + 26, sub);

    // [FAV] Toggle Button: X in 198..246, Y in y+8..y+38
    _display->setColor(item->is_fav ? COLOR_ORANGE_BTN : colCardBg());
    _display->fillRect(198, y + 8, 48, 30);
    _display->setColor(item->is_fav ? COLOR_GOLD_STAR : colBorder());
    _display->drawRect(198, y + 8, 48, 30);
    _display->setColor(item->is_fav ? COLOR_BLACK : colText());
    drawStr(204, y + 19, item->is_fav ? "*FAV" : "+FAV");

    // [DM] Button: X in 252..304, Y in y+8..y+38
    _display->setColor(COLOR_GREEN_BTN);
    _display->fillRect(252, y + 8, 52, 30);
    _display->setColor(colBorder());
    _display->drawRect(252, y + 8, 52, 30);
    _display->setColor(COLOR_WHITE);
    drawStr(270, y + 19, "DM");

    y += 50;
  }

  // Paging controls at bottom: Y in 218..238
  _display->setColor(colCardBg());
  _display->fillRect(8, 218, 60, 20);
  _display->setColor(colBorder());
  _display->drawRect(8, 218, 60, 20);
  _display->setColor(colText());
  drawStr(18, 224, "< PREV");

  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", _nodes_page + 1, maxPages);
  _display->setColor(colSubText());
  drawStr(130, 224, pageBuf);

  _display->setColor(colCardBg());
  _display->fillRect(252, 218, 60, 20);
  _display->setColor(colBorder());
  _display->drawRect(252, 218, 60, 20);
  _display->setColor(colText());
  drawStr(264, 224, "NEXT >");
}

void TapUI::handleNodesTouch(uint16_t tx, uint16_t ty) {
  if ((tx >= 210 && tx <= 312 && ty >= 47 && ty <= 66) ||
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

  int count = buildSortedContactList(_sorted_nodes, MAX_SORTED_NODES);
  int itemsPerPage = 3;
  int maxPages = (count + itemsPerPage - 1) / itemsPerPage;

  if (ty >= 218 && ty <= 238) {
    if (tx >= 8 && tx <= 68 && _nodes_page > 0) {
      _nodes_page--;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 252 && tx <= 312 && _nodes_page + 1 < maxPages) {
      _nodes_page++;
      _needs_full_redraw = true;
      return;
    }
    return;
  }

  int start = _nodes_page * itemsPerPage;
  int end = start + itemsPerPage;
  if (end > count) end = count;

  int y = 70;
  for (int i = start; i < end; i++) {
    if (ty >= y && ty <= y + 46) {
      if (tx >= 196 && tx <= 248) {
        // Toggle favorite flag!
        toggleContactFavorite(_sorted_nodes[i].contact_idx);
        return;
      } else {
        // Open Direct Chat with this contact
        _active_contact_idx = _sorted_nodes[i].contact_idx;
        _chat_scroll_offset = 0;
        _current_tab = TAB_CHAT;
        _needs_full_redraw = true;
        return;
      }
    }
    y += 50;
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

  _display->setColor(colBg());
  _display->fillRect(0, 44, 320, 196);

  // Sub-tab selector: CHANNELS (0-7) | ROOM SERVERS (BBS)
  _display->setColor(!_show_rooms_subview ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(8, 46, 148, 22);
  _display->setColor(colBorder());
  _display->drawRect(8, 46, 148, 22);
  _display->setColor(!_show_rooms_subview ? COLOR_WHITE : colText());
  _display->setTextSize(1);
  drawStr(36, 53, "CHANNELS (0-7)");

  _display->setColor(_show_rooms_subview ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(164, 46, 148, 22);
  _display->setColor(colBorder());
  _display->drawRect(164, 46, 148, 22);
  _display->setColor(_show_rooms_subview ? COLOR_WHITE : colText());
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
      int unreadCh = getChannelUnreadCount((uint8_t)i);

      _display->setColor(colCardBg());
      _display->fillRect(8, y, 304, 44);
      _display->setColor(isActive ? COLOR_GREEN_BTN : (unreadCh > 0 ? COLOR_GOLD_STAR : colBorder()));
      _display->drawRect(8, y, 304, 44);

      _display->setColor(unreadCh > 0 ? COLOR_GOLD_STAR : colTitle());
      _display->setTextSize(1);
      char chTitle[34];
      if (isEmpty) {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: (Unconfigured)", i);
      } else if (unreadCh > 0) {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: %s (%d NEW)", i, ch.name[0] ? ch.name : "Public", unreadCh);
      } else if (ch.name[0] == '#') {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: %s", i, ch.name);
      } else {
        snprintf(chTitle, sizeof(chTitle), "Ch #%d: #%s", i, ch.name[0] ? ch.name : "Public");
      }
      drawStr(14, y + 7, chTitle);

      _display->setColor(colSubText());
      char keyInfo[34];
      if (isEmpty) {
        strcpy(keyInfo, "Tap [CONFIG] to set #tag or PSK");
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
      _display->setColor(colBorder());
      _display->drawRect(194, y + 8, 58, 28);
      _display->setColor(COLOR_WHITE);
      drawStr(205, y + 18, isActive ? "ACTIVE" : "SELECT");

      // [CONFIG] button: X in 256..306, Y in y+8..y+36
      _display->setColor(COLOR_PURPLE_BTN);
      _display->fillRect(256, y + 8, 50, 28);
      _display->setColor(colBorder());
      _display->drawRect(256, y + 8, 50, 28);
      _display->setColor(COLOR_WHITE);
      drawStr(264, y + 18, "CONFIG");

      y += 48;
    }

    // Paging bar at bottom: Y in 218..238
    _display->setColor(colCardBg());
    _display->fillRect(8, 218, 65, 20);
    _display->setColor(colBorder());
    _display->drawRect(8, 218, 65, 20);
    _display->setColor(colText());
    drawStr(18, 224, "< PREV");

    char pBuf[28];
    snprintf(pBuf, sizeof(pBuf), "Ch %d-%d (Page %d/%d)", start, end - 1, _chans_page + 1, maxPages);
    _display->setColor(colSubText());
    drawStr(105, 224, pBuf);

    _display->setColor(colCardBg());
    _display->fillRect(247, 218, 65, 20);
    _display->setColor(colBorder());
    _display->drawRect(247, 218, 65, 20);
    _display->setColor(colText());
    drawStr(259, 224, "NEXT >");
  } else {
    int y = 76;
    int roomCount = 0;
    int total_slots = the_mesh.getTotalContactSlots();

    for (int i = 8; i < total_slots && roomCount < 3; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c)) continue;
      if (c.type == 3) {
        _display->setColor(_dark_mode ? COLOR_DK_ROOM_BG : COLOR_CARD_ROOM_BG);
        _display->fillRect(10, y, 300, 38);
        _display->setColor(COLOR_CARD_ROOM_BD);
        _display->drawRect(10, y, 300, 38);
        _display->fillRect(10, y, 6, 38);

        _display->setColor(colTitle());
        drawStr(22, y + 8, c.name[0] ? c.name : "BBS Room Server");

        _display->setColor(colSubText());
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
      _display->setColor(colSubText());
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
  _display->setColor(colBg());
  _display->fillRect(0, 44, 320, 196);
  _display->setTextSize(1);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(8, 46, 304, 22);
  _display->setColor(COLOR_WHITE);
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "CONFIGURE CHANNEL #%d", _editing_chan_idx);
  drawStr(16, 53, hdr);

  _display->setColor(colCardBg());
  _display->fillRect(8, 72, 222, 24);
  _display->setColor(colBorder());
  _display->drawRect(8, 72, 222, 24);
  _display->setColor(colText());
  char nameLine[38];
  snprintf(nameLine, sizeof(nameLine), "Name: %s", _edit_chan_draft.name[0] ? _edit_chan_draft.name : "(empty)");
  drawStr(14, 80, nameLine);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(234, 72, 78, 24);
  _display->setColor(COLOR_WHITE);
  drawStr(244, 80, "EDIT NAME");

  _display->setColor(colCardBg());
  _display->fillRect(8, 100, 222, 36);
  _display->setColor(colBorder());
  _display->drawRect(8, 100, 222, 36);

  unsigned char b64[50];
  memset(b64, 0, sizeof(b64));
  encode_base64(_edit_chan_draft.channel.secret, _edit_chan_key_len, b64);
  _display->setColor(colTitle());
  drawStr(14, 106, "PSK (Base64 / 128-bit):");
  _display->setColor(colSubText());
  char b64Line[36];
  snprintf(b64Line, sizeof(b64Line), "%.32s", (const char*)b64);
  drawStr(14, 120, b64Line);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(234, 100, 78, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(247, 114, "EDIT KEY");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(8, 142, 104, 26);
  _display->setColor(COLOR_WHITE);
  drawStr(16, 151, "#HASHTAG KEY");

  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(116, 142, 96, 26);
  _display->setColor(COLOR_BLACK);
  drawStr(128, 151, "PUBLIC PSK");

  _display->setColor(COLOR_ORANGE_BTN);
  _display->fillRect(216, 142, 96, 26);
  _display->setColor(COLOR_BLACK);
  drawStr(228, 151, "RANDOM KEY");

  char hexStr[36];
  mesh::Utils::toHex(hexStr, _edit_chan_draft.channel.secret, 16);
  char hexLine[48];
  snprintf(hexLine, sizeof(hexLine), "Hex: %.32s", hexStr);
  _display->setColor(colSubText());
  drawStr(10, 176, hexLine);

  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(8, 196, 114, 36);
  _display->setColor(colBorder());
  _display->drawRect(8, 196, 114, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(26, 210, "SAVE & USE");

  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(128, 196, 92, 36);
  _display->setColor(colBorder());
  _display->drawRect(128, 196, 92, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(144, 210, "CLEAR CH");

  _display->setColor(colCardBg());
  _display->fillRect(226, 196, 86, 36);
  _display->setColor(colBorder());
  _display->drawRect(226, 196, 86, 36);
  _display->setColor(colText());
  drawStr(248, 210, "CANCEL");
}

void TapUI::handleChannelEditorTouch(uint16_t tx, uint16_t ty) {
  if (tx >= 234 && tx <= 312 && ty >= 72 && ty <= 96) {
    startKeyboardEdit(EDIT_CHAN_NAME, _edit_chan_draft.name, KB_LOWER, TAB_CHANS);
    return;
  }

  if (tx >= 234 && tx <= 312 && ty >= 100 && ty <= 136) {
    char hexKey[36];
    mesh::Utils::toHex(hexKey, _edit_chan_draft.channel.secret, 16);
    startKeyboardEdit(EDIT_CHAN_KEY, hexKey, KB_SYMBOLS, TAB_CHANS);
    return;
  }

  if (ty >= 142 && ty <= 168) {
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
    if (tx >= 116 && tx <= 212) {
      strcpy(_edit_chan_draft.name, "Public");
      memset(_edit_chan_draft.channel.secret, 0, sizeof(_edit_chan_draft.channel.secret));
      decode_base64((const unsigned char*)"izOH6cXN6mrJ5e26oRXNcg==", 24, _edit_chan_draft.channel.secret);
      _edit_chan_key_len = 16;
      _needs_full_redraw = true;
      return;
    }
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

  if (ty >= 196 && ty <= 234) {
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
    if (tx >= 128 && tx <= 220) {
      clearChannel(_editing_chan_idx);
      _editing_chan_idx = -1;
      _needs_full_redraw = true;
      return;
    }
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
          _editing_chan_idx = i;
          memset(&_edit_chan_draft, 0, sizeof(_edit_chan_draft));
          the_mesh.getChannel(i, _edit_chan_draft);
          _edit_chan_key_len = isChannelSecretZero(&_edit_chan_draft.channel.secret[16], 16) ? 16 : 32;
          _needs_full_redraw = true;
          return;
        } else {
          ChannelDetails ch;
          memset(&ch, 0, sizeof(ch));
          the_mesh.getChannel(i, ch);
          if (i > 0 && ch.name[0] == 0 && isChannelSecretZero(ch.channel.secret, 16)) {
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
          _chat_scroll_offset = 0;
          _current_tab = TAB_CHAT;
          _needs_full_redraw = true;
          return;
        }
      }
      y += 48;
    }
  } else {
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
          _chat_scroll_offset = 0;
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
// GPS SCREEN (2 Sub-Tabs: [GPS TELEMETRY] | [HEADING & NAV])
// -------------------------------------------------------------
void TapUI::drawGPSScreen() {
  _last_gps_card_hash = 0;
  _display->setColor(colBg());
  _display->fillRect(0, 44, 320, 196);
  _display->setTextSize(1);

  // Top sub-tab bar: [ GPS TELEMETRY ] (X=8..156, Y=45..65) | [ HEADING & NAV ] (X=164..312, Y=45..65)
  _display->setColor(_gps_subview == 0 ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(8, 45, 148, 20);
  _display->setColor(colBorder());
  _display->drawRect(8, 45, 148, 20);
  _display->setColor(_gps_subview == 0 ? COLOR_WHITE : colText());
  drawStr(32, 51, "[ GPS TELEMETRY ]");

  _display->setColor(_gps_subview == 1 ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(164, 45, 148, 20);
  _display->setColor(colBorder());
  _display->drawRect(164, 45, 148, 20);
  _display->setColor(_gps_subview == 1 ? COLOR_WHITE : colText());
  drawStr(188, 51, "[ HEADING & NAV ]");

  if (_gps_subview == 0) {
    drawGPSStatusView();
  } else {
    drawGPSNavView();
  }
}

void TapUI::drawGPSStatusView() {
  LocationProvider* loc = sensors.getLocationProvider();
  bool hasLoc = (loc != nullptr && loc->isEnabled());
  bool fixValid = (hasLoc && loc->isValid());
  long sats = hasLoc ? loc->satellitesCount() : 0;
  bool shareOn = isGPSShareEnabled();

  // Row 1 (Y = 69..89): Fix Status Banner (X=8..204) + [SHARE:ON/OFF] Toggle (X=208..312)
  _display->setColor(fixValid ? COLOR_GREEN_BTN : (hasLoc ? COLOR_ORANGE_BTN : colCardBg()));
  _display->fillRect(8, 69, 196, 20);
  _display->setColor(colBorder());
  _display->drawRect(8, 69, 196, 20);
  _display->setColor(fixValid ? COLOR_WHITE : (hasLoc ? COLOR_BLACK : colSubText()));

  char statusBuf[34];
  if (fixValid) {
    snprintf(statusBuf, sizeof(statusBuf), "3D FIX LOCKED (%ld SAT)", sats);
  } else if (hasLoc) {
    snprintf(statusBuf, sizeof(statusBuf), "SEARCHING SATS (%ld IN VIEW)", sats);
  } else {
    strcpy(statusBuf, "GPS RECEIVER OFF");
  }
  drawStr(16, 75, statusBuf);

  _display->setColor(shareOn ? COLOR_GREEN_BTN : colCardBg());
  _display->fillRect(208, 69, 104, 20);
  _display->setColor(colBorder());
  _display->drawRect(208, 69, 104, 20);
  _display->setColor(shareOn ? COLOR_WHITE : colText());
  drawStr(214, 75, shareOn ? "APP SHARE:ON" : "APP SHARE:OFF");

  // Row 2 (Y = 92..160): Coordinates, Altitude, Speed/Heading preview & Diagnostics Card
  _display->setColor(colCardBg());
  _display->fillRect(8, 92, 304, 68);
  _display->setColor(colBorder());
  _display->drawRect(8, 92, 304, 68);

  char buf[68];
  if (fixValid) {
    double lat = (double)loc->getLatitude() / 1e6;
    double lon = (double)loc->getLongitude() / 1e6;
    long alt_m = loc->getAltitude() / 1000L;

    _display->setColor(colText());
    snprintf(buf, sizeof(buf), "Lat: %.6f %c   Lon: %.6f %c",
             fabs(lat), lat >= 0 ? 'N' : 'S',
             fabs(lon), lon >= 0 ? 'E' : 'W');
    drawStr(14, 97, buf);

    snprintf(buf, sizeof(buf), "Alt: %ld m (%ld ft)   HDOP: %.1f",
             alt_m, (long)(alt_m * 3.28084), (float)tap_gps.getHDOP() / 10.0f);
    drawStr(14, 111, buf);

    _display->setColor(colTitle());
    if (tap_gps.hasCourse()) {
      float crs = tap_gps.getCourseDeg();
      snprintf(buf, sizeof(buf), "Spd: %.1f km/h (%.1f mph)  Hdg: %03d deg %s",
               tap_gps.getSpeedKmh(), tap_gps.getSpeedMph(),
               (int)(crs + 0.5f) % 360, degToCardinal(crs));
    } else {
      snprintf(buf, sizeof(buf), "Spd: %.1f km/h (%.1f mph)  Hdg: --- (Static)",
               tap_gps.getSpeedKmh(), tap_gps.getSpeedMph());
    }
    drawStr(14, 125, buf);
  } else {
    _display->setColor(colSubText());
    if (sensors.node_lat != 0.0 || sensors.node_lon != 0.0) {
      snprintf(buf, sizeof(buf), "Saved Pos: %.5f, %.5f", sensors.node_lat, sensors.node_lon);
      drawStr(14, 97, buf);
    } else {
      drawStr(14, 97, "Coordinates: Waiting for satellite fix...");
    }
    drawStr(14, 111, "Altitude:    Waiting for satellite fix...");
    drawStr(14, 125, "Tap [HEADING & NAV] for Speed & Node Dist");
  }

  char diag[48];
  if (loc) loc->formatDiagnostics(diag, sizeof(diag));
  else strcpy(diag, "en:1 sat:0 fix:0");
  snprintf(buf, sizeof(buf), "Diag: %.42s", diag);
  _display->setColor(colMutedText());
  drawStr(14, 143, buf);

  // Row 3 (Y = 164..186): UART Baud & RX/TX Pin Swap
  _display->setColor(colTitle());
  snprintf(buf, sizeof(buf), "Baud:%lu", _gps_baud);
  drawStr(10, 171, buf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(104, 165, 76, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(114, 171, "CHG BAUD");

  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(186, 165, 126, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(194, 171, _gps_pins_swapped ? "PINS: RX16/TX15" : "PINS: RX15/TX16");

  // Row 4 (Y = 190..232): [BROADCAST LOC] | [HEADING / NAV] | [GPS: ON/OFF]
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(8, 190, 114, 40);
  _display->setColor(colBorder());
  _display->drawRect(8, 190, 114, 40);
  _display->setColor(COLOR_WHITE);
  drawStr(16, 206, "BROADCAST LOC");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(126, 190, 102, 40);
  _display->setColor(colBorder());
  _display->drawRect(126, 190, 102, 40);
  _display->setColor(COLOR_WHITE);
  drawStr(136, 206, "HEADING / NAV");

  _display->setColor(hasLoc ? COLOR_GREEN_BTN : colCardBg());
  _display->fillRect(232, 190, 80, 40);
  _display->setColor(colBorder());
  _display->drawRect(232, 190, 80, 40);
  _display->setColor(hasLoc ? COLOR_WHITE : colText());
  drawStr(248, 206, hasLoc ? "GPS: ON" : "GPS: OFF");
}

void TapUI::drawLine(int x0, int y0, int x1, int y1, int thickness) {
  if (!_display) return;
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  int off = (thickness > 1) ? -(thickness / 2) : 0;
  int maxSteps = dx - dy + 2;
  if (maxSteps > 600) maxSteps = 600;

  while (maxSteps-- > 0) {
    _display->fillRect(x0 + off, y0 + off, thickness, thickness);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void TapUI::drawCircleOutline(int cx, int cy, int r) {
  if (!_display || r <= 0 || r > 200) return;
  int x = 0;
  int y = r;
  int d = 3 - 2 * r;
  while (y >= x) {
    _display->fillRect(cx + x, cy + y, 1, 1);
    _display->fillRect(cx - x, cy + y, 1, 1);
    _display->fillRect(cx + x, cy - y, 1, 1);
    _display->fillRect(cx - x, cy - y, 1, 1);
    _display->fillRect(cx + y, cy + x, 1, 1);
    _display->fillRect(cx - y, cy + x, 1, 1);
    _display->fillRect(cx + y, cy - x, 1, 1);
    _display->fillRect(cx - y, cy - x, 1, 1);
    x++;
    if (d > 0) {
      y--;
      d = d + 4 * (x - y) + 10;
    } else {
      d = d + 4 * x + 6;
    }
  }
}

void TapUI::drawCompassPointer(int cx, int cy, float angle_deg, int r_tip, int r_tail, int half_w, uint16_t color) {
  if (!_display || isnan(angle_deg) || isinf(angle_deg)) return;
  float rad = angle_deg * 0.0174532925f;
  float sinA = sinf(rad);
  float cosA = cosf(rad);

  int tipX = cx + (int)roundf((float)r_tip * sinA);
  int tipY = cy - (int)roundf((float)r_tip * cosA);
  int tailX = cx - (int)roundf((float)r_tail * sinA);
  int tailY = cy + (int)roundf((float)r_tail * cosA);

  int wingDist = r_tip - 9;
  if (wingDist < 4) wingDist = 4;
  int wingBaseX = cx + (int)roundf((float)wingDist * sinA);
  int wingBaseY = cy - (int)roundf((float)wingDist * cosA);

  int leftX = wingBaseX - (int)roundf((float)half_w * cosA);
  int leftY = wingBaseY - (int)roundf((float)half_w * sinA);
  int rightX = wingBaseX + (int)roundf((float)half_w * cosA);
  int rightY = wingBaseY + (int)roundf((float)half_w * sinA);

  _display->setColor(color);
  // Shaft from tail to tip
  drawLine(tailX, tailY, tipX, tipY, 2);
  // Filled arrowhead (fan from left wing to right wing to tip)
  drawLine(leftX, leftY, tipX, tipY, 2);
  drawLine(rightX, rightY, tipX, tipY, 2);
  drawLine(leftX, leftY, rightX, rightY, 2);
  int midLeftX = (leftX + wingBaseX) / 2;
  int midLeftY = (leftY + wingBaseY) / 2;
  int midRightX = (rightX + wingBaseX) / 2;
  int midRightY = (rightY + wingBaseY) / 2;
  drawLine(midLeftX, midLeftY, tipX, tipY, 2);
  drawLine(midRightX, midRightY, tipX, tipY, 2);
}

void TapUI::drawGPSNavView() {
  // ---------------------------------------------------------
  // 1. Top Node Selector Bar (Y = 67..87)
  // ---------------------------------------------------------
  _display->setColor(colCardBg());
  _display->fillRect(8, 67, 304, 20);
  _display->setColor(colBorder());
  _display->drawRect(8, 67, 304, 20);

  int count = buildSortedContactList(_sorted_nodes, MAX_SORTED_NODES);

  if (count > 0) {
    if (_gps_nav_target_slot < 0 && _active_contact_idx >= 0) {
      _gps_nav_target_slot = _active_contact_idx;
    }
    int selIdx = 0;
    for (int i = 0; i < count; i++) {
      if (_sorted_nodes[i].contact_idx == _gps_nav_target_slot) {
        selIdx = i;
        break;
      }
    }
    _gps_nav_target_slot = _sorted_nodes[selIdx].contact_idx;
    DiscoveredNodeItem* target = &_sorted_nodes[selIdx];

    _display->setColor(COLOR_BLUE_KEY);
    _display->fillRect(9, 68, 48, 18);
    _display->setColor(COLOR_WHITE);
    drawStr(14, 73, "< NODE");

    char targetHdr[32];
    snprintf(targetHdr, sizeof(targetHdr), "%s%.15s (%d/%d)",
             target->is_fav ? "*" : "", target->name, selIdx + 1, count);
    _display->setColor(target->is_fav ? COLOR_GOLD_STAR : colTitle());
    drawStr(64, 73, targetHdr);

    _display->setColor(COLOR_BLUE_KEY);
    _display->fillRect(263, 68, 48, 18);
    _display->setColor(COLOR_WHITE);
    drawStr(268, 73, "NODE >");
  } else {
    _display->setColor(colSubText());
    drawStr(18, 73, "Target Node: None discovered yet");
  }

  // ---------------------------------------------------------
  // 2. Main Compass & Navigation Card Frame (Y = 90..204)
  // ---------------------------------------------------------
  _display->setColor(colCardBg());
  _display->fillRect(8, 90, 304, 114);
  _display->setColor(colBorder());
  _display->drawRect(8, 90, 304, 114);

  drawGPSNavInterior();

  // ---------------------------------------------------------
  // 3. Bottom Action Buttons (Y = 207..231)
  // ---------------------------------------------------------
  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(8, 207, 102, 24);
  _display->setColor(colBorder());
  _display->drawRect(8, 207, 102, 24);
  _display->setColor(COLOR_WHITE);
  drawStr(16, 215, "REQ NODE LOC");

  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(114, 207, 102, 24);
  _display->setColor(colBorder());
  _display->drawRect(114, 207, 102, 24);
  _display->setColor(COLOR_WHITE);
  drawStr(124, 215, "SEND MY LOC");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(220, 207, 92, 24);
  _display->setColor(colBorder());
  _display->drawRect(220, 207, 92, 24);
  _display->setColor(COLOR_WHITE);
  drawStr(242, 215, "DM NODE");
}

void TapUI::drawGPSNavInterior() {
  LocationProvider* loc = sensors.getLocationProvider();
  bool hasLoc = (loc != nullptr && loc->isEnabled());
  bool fixValid = (hasLoc && loc->isValid());

  double myLat = 0.0, myLon = 0.0;
  bool myHasPos = false;
  if (fixValid) {
    myLat = (double)loc->getLatitude() / 1e6;
    myLon = (double)loc->getLongitude() / 1e6;
    myHasPos = (myLat != 0.0 || myLon != 0.0);
  } else if (sensors.node_lat != 0.0 || sensors.node_lon != 0.0) {
    myLat = sensors.node_lat;
    myLon = sensors.node_lon;
    myHasPos = true;
  }

  int count = buildSortedContactList(_sorted_nodes, MAX_SORTED_NODES);
  DiscoveredNodeItem* target = nullptr;
  if (count > 0) {
    int selIdx = 0;
    for (int i = 0; i < count; i++) {
      if (_sorted_nodes[i].contact_idx == _gps_nav_target_slot) {
        selIdx = i;
        break;
      }
    }
    target = &_sorted_nodes[selIdx];
  }
  bool targetHasPos = (target != nullptr && (target->lat != 0 || target->lon != 0));
  double brg = 0.0;
  double dist_m = 0.0;
  if (myHasPos && targetHasPos) {
    double tLat = (double)target->lat / 1e6;
    double tLon = (double)target->lon / 1e6;
    dist_m = calcDistanceMeters(myLat, myLon, tLat, tLon);
    brg = calcBearingDegrees(myLat, myLon, tLat, tLon);
  }

  // Clear interior of the Compass & Navigation Card
  _display->setColor(colCardBg());
  _display->fillRect(9, 91, 302, 112);

  // ---------------------------------------------------------
  // Left Side: Graphical Compass Rose (Center X=62, Y=147, R=40)
  // ---------------------------------------------------------
  const int cx = 62;
  const int cy = 147;
  const int r = 39;

  // Outer dial ring + inner crosshair ring
  _display->setColor(colBorder());
  drawCircleOutline(cx, cy, r);
  drawCircleOutline(cx, cy, r - 1);

  // Tick marks every 45 degrees (N, NE, E, SE, S, SW, W, NW)
  for (int a = 0; a < 360; a += 45) {
    float rad = (float)a * 0.0174532925f;
    int r_in = (a % 90 == 0) ? (r - 6) : (r - 4);
    int x0 = cx + (int)roundf((float)r_in * sinf(rad));
    int y0 = cy - (int)roundf((float)r_in * cosf(rad));
    int x1 = cx + (int)roundf((float)r * sinf(rad));
    int y1 = cy - (int)roundf((float)r * cosf(rad));
    _display->setColor((a == 0) ? COLOR_RED_BTN : colMutedText());
    drawLine(x0, y0, x1, y1, 1);
  }

  // Cardinal Labels (N, E, S, W) around the compass dial
  _display->setTextSize(1);
  _display->setColor(COLOR_RED_BTN);
  drawStr(cx - 2, cy - r - 8, "N");
  _display->setColor(colSubText());
  drawStr(cx - 2, cy + r + 2, "S");
  drawStr(cx + r + 3, cy - 3, "E");
  drawStr(cx - r - 7, cy - 3, "W");

  // Subtle center crosshair
  _display->setColor(colBorder());
  _display->fillRect(cx - 4, cy, 9, 1);
  _display->fillRect(cx, cy - 4, 1, 9);

  // 1) Draw Target Node Bearing Pointer (GOLD arrow)
  if (myHasPos && targetHasPos) {
    drawCompassPointer(cx, cy, (float)brg, r - 6, 8, 5, COLOR_GOLD_STAR);
  }

  // 2) Draw Our GPS Heading Pointer (GREEN arrow)
  if (tap_gps.hasCourse()) {
    drawCompassPointer(cx, cy, tap_gps.getCourseDeg(), r - 9, 10, 4, COLOR_GREEN_BTN);
  }

  // Center hub dot
  _display->setColor(COLOR_WHITE);
  _display->fillRect(cx - 2, cy - 2, 5, 5);

  // Vertical divider between Compass and Telemetry Readouts
  _display->setColor(colBorder());
  _display->fillRect(114, 94, 1, 106);

  // ---------------------------------------------------------
  // Right Side (X = 120..308): All Telemetry & Node Info
  // ---------------------------------------------------------
  // Unit toggle button [KM/H] / [MPH] at top-right: X=248..306, Y=93..109
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(248, 93, 58, 16);
  _display->setColor(COLOR_BLACK);
  drawStr(254, 97, _gps_metric_units ? "KM/H" : "MPH");

  // Row 1 (Y = 96): Our GPS Speed & Heading (Green indicator matches Green Needle)
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(120, 97, 5, 5);
  char spdLine[32];
  if (_gps_metric_units) {
    snprintf(spdLine, sizeof(spdLine), "SPD: %.1f km/h", tap_gps.getSpeedKmh());
  } else {
    snprintf(spdLine, sizeof(spdLine), "SPD: %.1f mph", tap_gps.getSpeedMph());
  }
  drawStr(128, 96, spdLine);

  // Row 2 (Y = 110): Our GPS Course / Heading
  char hdgLine[36];
  if (tap_gps.hasCourse()) {
    float crs = tap_gps.getCourseDeg();
    snprintf(hdgLine, sizeof(hdgLine), "HDG: %03d deg (%s)",
             (int)(crs + 0.5f) % 360, degToCardinal(crs));
    _display->setColor(COLOR_GREEN_BTN);
  } else {
    snprintf(hdgLine, sizeof(hdgLine), "HDG: --- (%.1f kts)", tap_gps.getSpeedKnots());
    _display->setColor(colSubText());
  }
  drawStr(120, 110, hdgLine);

  // Horizontal separator
  _display->setColor(colBorder());
  _display->fillRect(120, 122, 186, 1);

  // Row 3..5: Target Node Bearing, Relative Turn, Distance & Coordinates
  if (myHasPos && targetHasPos) {
    const char* card = degToCardinal((float)brg);

    // Gold indicator matches Gold Target Needle
    _display->setColor(COLOR_GOLD_STAR);
    _display->fillRect(120, 128, 5, 5);
    char brgLine[36];
    snprintf(brgLine, sizeof(brgLine), "NODE BRG: %03d deg (%s)",
             (int)(brg + 0.5) % 360, card);
    drawStr(128, 127, brgLine);

    // Distance line (Y = 141)
    char distLine[36];
    if (_gps_metric_units) {
      if (dist_m < 1000.0) {
        snprintf(distLine, sizeof(distLine), "DIST: %ld m (%.0f ft)",
                 (long)(dist_m + 0.5), dist_m * 3.28084);
      } else {
        snprintf(distLine, sizeof(distLine), "DIST: %.2f km (%.2f mi)",
                 dist_m / 1000.0, dist_m / 1609.344);
      }
    } else {
      double dist_ft = dist_m * 3.28084;
      if (dist_ft < 5280.0) {
        snprintf(distLine, sizeof(distLine), "DIST: %ld ft (%ld m)",
                 (long)(dist_ft + 0.5), (long)(dist_m + 0.5));
      } else {
        snprintf(distLine, sizeof(distLine), "DIST: %.2f mi (%.2f km)",
                 dist_m / 1609.344, dist_m / 1000.0);
      }
    }
    _display->setColor(colText());
    drawStr(120, 141, distLine);

    // Relative Turn / Direction line (Y = 155)
    char relLine[36];
    if (tap_gps.hasCourse()) {
      float rel = fmod((float)brg - tap_gps.getCourseDeg() + 540.0f, 360.0f) - 180.0f;
      const char* relWord = (fabs(rel) <= 15.0f) ? "AHEAD" :
                            (fabs(rel) >= 165.0f) ? "BEHIND" :
                            (rel > 0.0f ? "RIGHT" : "LEFT");
      snprintf(relLine, sizeof(relLine), "TURN: %+d deg (%s)",
               (int)roundf(rel), relWord);
      _display->setColor(colTitle());
    } else {
      snprintf(relLine, sizeof(relLine), "DIR: Walk %s (%03d deg)",
               card, (int)(brg + 0.5) % 360);
      _display->setColor(colTitle());
    }
    drawStr(120, 155, relLine);

    // Target Node coordinates (Y = 171)
    char tPosLine[36];
    snprintf(tPosLine, sizeof(tPosLine), "Tgt: %.4f, %.4f",
             (double)target->lat / 1e6, (double)target->lon / 1e6);
    _display->setColor(colSubText());
    drawStr(120, 171, tPosLine);
  } else {
    _display->setColor(colSubText());
    if (count == 0) {
      drawStr(120, 130, "No nodes discovered.");
      drawStr(120, 146, "Check NODES tab first.");
    } else if (!targetHasPos) {
      drawStr(120, 130, "Node has no GPS pos.");
      drawStr(120, 146, "Tap [REQ NODE LOC]");
      drawStr(120, 160, "below to query it.");
    } else {
      drawStr(120, 130, "Waiting for our GPS...");
      char tPosLine[36];
      snprintf(tPosLine, sizeof(tPosLine), "Tgt: %.4f, %.4f",
               (double)target->lat / 1e6, (double)target->lon / 1e6);
      drawStr(120, 146, tPosLine);
    }
  }

  // Our own GPS position at bottom-right (Y = 186)
  char myPosLine[36];
  if (myHasPos) {
    snprintf(myPosLine, sizeof(myPosLine), "Me:  %.4f, %.4f %s",
             myLat, myLon, fixValid ? "3D" : " Sav");
  } else {
    strcpy(myPosLine, "Me:  Waiting for fix...");
  }
  _display->setColor(colMutedText());
  drawStr(120, 186, myPosLine);
}

void TapUI::refreshGPSDataOnly() {
  if (!_display || _current_tab != TAB_GPS) return;

  LocationProvider* loc = sensors.getLocationProvider();
  bool hasLoc = (loc != nullptr && loc->isEnabled());
  bool fixValid = (hasLoc && loc->isValid());
  long sats = hasLoc ? loc->satellitesCount() : 0;
  long lat_e6 = fixValid ? loc->getLatitude() : (long)(sensors.node_lat * 1e6);
  long lon_e6 = fixValid ? loc->getLongitude() : (long)(sensors.node_lon * 1e6);
  long alt_m = fixValid ? (loc->getAltitude() / 1000L) : 0;
  int spd_tenths = (int)(tap_gps.getSpeedKmh() * 10.0f + 0.5f);
  int crs_deg = tap_gps.hasCourse() ? ((int)(tap_gps.getCourseDeg() + 0.5f) % 360) : -1;
  uint32_t rx_kb = fixValid ? 0 : (tap_gps.getUartBytes() / 512UL);

  int count = (_gps_subview == 1) ? buildSortedContactList(_sorted_nodes, MAX_SORTED_NODES) : 0;
  int selIdx = 0;
  int32_t tLat_e6 = 0, tLon_e6 = 0;
  if (_gps_subview == 1 && count > 0) {
    for (int i = 0; i < count; i++) {
      if (_sorted_nodes[i].contact_idx == _gps_nav_target_slot) {
        selIdx = i;
        break;
      }
    }
    tLat_e6 = _sorted_nodes[selIdx].lat;
    tLon_e6 = _sorted_nodes[selIdx].lon;
  }

  // Compute FNV-1a hash of the displayed values
  uint32_t h = 2166136261UL;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619UL; };
  mix(_gps_subview);
  mix(hasLoc ? 1 : 0);
  mix(fixValid ? 1 : 0);
  mix((uint32_t)sats);
  mix((uint32_t)(lat_e6 / 10)); // 5 decimal places (~1.1m)
  mix((uint32_t)(lon_e6 / 10));
  mix((uint32_t)alt_m);
  mix((uint32_t)spd_tenths);
  mix((uint32_t)(crs_deg + 1));
  if (_gps_subview == 0) {
    mix((uint32_t)tap_gps.getHDOP());
    mix(rx_kb);
  } else {
    mix(_gps_metric_units ? 1 : 0);
    mix((uint32_t)count);
    mix((uint32_t)selIdx);
    mix((uint32_t)tLat_e6);
    mix((uint32_t)tLon_e6);
  }

  if (h == _last_gps_card_hash) {
    return; // Nothing changed on screen -> zero SPI writes, zero flicker!
  }
  _last_gps_card_hash = h;

  if (_gps_subview == 0) {
    // Only redraw the status badge interior (X=9..202, Y=70..87) and Card interior (X=9..310, Y=93..158)
    _display->setTextSize(1);
    _display->setColor(fixValid ? COLOR_GREEN_BTN : (hasLoc ? COLOR_ORANGE_BTN : colCardBg()));
    _display->fillRect(9, 70, 194, 18);
    _display->setColor(fixValid ? COLOR_WHITE : (hasLoc ? COLOR_BLACK : colSubText()));
    char statusBuf[34];
    if (fixValid) {
      snprintf(statusBuf, sizeof(statusBuf), "3D FIX LOCKED (%ld SAT)", sats);
    } else if (hasLoc) {
      snprintf(statusBuf, sizeof(statusBuf), "SEARCHING SATS (%ld IN VIEW)", sats);
    } else {
      strcpy(statusBuf, "GPS RECEIVER OFF");
    }
    drawStr(16, 75, statusBuf);

    _display->setColor(colCardBg());
    _display->fillRect(9, 93, 302, 66);

    char buf[68];
    if (fixValid) {
      double lat = (double)loc->getLatitude() / 1e6;
      double lon = (double)loc->getLongitude() / 1e6;

      _display->setColor(colText());
      snprintf(buf, sizeof(buf), "Lat: %.6f %c   Lon: %.6f %c",
               fabs(lat), lat >= 0 ? 'N' : 'S',
               fabs(lon), lon >= 0 ? 'E' : 'W');
      drawStr(14, 97, buf);

      snprintf(buf, sizeof(buf), "Alt: %ld m (%ld ft)   HDOP: %.1f",
               alt_m, (long)(alt_m * 3.28084), (float)tap_gps.getHDOP() / 10.0f);
      drawStr(14, 111, buf);

      _display->setColor(colTitle());
      if (tap_gps.hasCourse()) {
        float crs = tap_gps.getCourseDeg();
        snprintf(buf, sizeof(buf), "Spd: %.1f km/h (%.1f mph)  Hdg: %03d deg %s",
                 tap_gps.getSpeedKmh(), tap_gps.getSpeedMph(),
                 (int)(crs + 0.5f) % 360, degToCardinal(crs));
      } else {
        snprintf(buf, sizeof(buf), "Spd: %.1f km/h (%.1f mph)  Hdg: --- (Static)",
                 tap_gps.getSpeedKmh(), tap_gps.getSpeedMph());
      }
      drawStr(14, 125, buf);
    } else {
      _display->setColor(colSubText());
      if (sensors.node_lat != 0.0 || sensors.node_lon != 0.0) {
        snprintf(buf, sizeof(buf), "Saved Pos: %.5f, %.5f", sensors.node_lat, sensors.node_lon);
        drawStr(14, 97, buf);
      } else {
        drawStr(14, 97, "Coordinates: Waiting for satellite fix...");
      }
      drawStr(14, 111, "Altitude:    Waiting for satellite fix...");
      drawStr(14, 125, "Tap [HEADING & NAV] for Speed & Node Dist");
    }

    char diag[48];
    if (loc) loc->formatDiagnostics(diag, sizeof(diag));
    else strcpy(diag, "en:1 sat:0 fix:0");
    snprintf(buf, sizeof(buf), "Diag: %.42s", diag);
    _display->setColor(colMutedText());
    drawStr(14, 143, buf);
  } else {
    // Subview 1: Flicker-free update of the Compass & Navigation Card interior only
    drawGPSNavInterior();
  }
}

void TapUI::handleGPSTouch(uint16_t tx, uint16_t ty) {
  // Top sub-tab bar: [ GPS TELEMETRY ] (X=8..156) | [ HEADING & NAV ] (X=164..312)
  if (ty >= 44 && ty <= 66) {
    if (tx >= 8 && tx <= 156) {
      _gps_subview = 0;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 164 && tx <= 312) {
      _gps_subview = 1;
      _needs_full_redraw = true;
      return;
    }
  }

  if (_gps_subview == 0) {
    handleGPSStatusTouch(tx, ty);
  } else {
    handleGPSNavTouch(tx, ty);
  }
}

void TapUI::handleGPSStatusTouch(uint16_t tx, uint16_t ty) {
  // [APP SHARE:ON/OFF] toggle (X=208..312, Y=69..90)
  if (tx >= 208 && tx <= 312 && ty >= 68 && ty <= 91) {
    setGPSShareEnabled(!isGPSShareEnabled());
    return;
  }

  // [CHG BAUD] (X=104..180, Y=164..186)
  if (tx >= 104 && tx <= 182 && ty >= 163 && ty <= 187) {
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

  // [PINS: RX15/TX16] (X=186..312, Y=164..186)
  if (tx >= 186 && tx <= 312 && ty >= 163 && ty <= 187) {
    setGPSPinsSwapped(!_gps_pins_swapped, true);
    return;
  }

  // Bottom Row (Y=190..234):
  if (ty >= 188 && ty <= 236) {
    // [BROADCAST LOC] (X=8..122)
    if (tx >= 8 && tx <= 122) {
      syncGPSWithMesh();
      the_mesh.advert();
      LocationProvider* loc = sensors.getLocationProvider();
      char locMsg[64];
      if (loc && loc->isEnabled() && loc->isValid()) {
        snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f, %.5f (Alt: %ldm)",
                 (double)loc->getLatitude() / 1e6,
                 (double)loc->getLongitude() / 1e6,
                 loc->getAltitude() / 1000L);
      } else if (sensors.node_lat != 0.0 || sensors.node_lon != 0.0) {
        snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f, %.5f", sensors.node_lat, sensors.node_lon);
      } else {
        snprintf(locMsg, sizeof(locMsg), "[LOC] Ping from WisMesh Tap");
      }
      sendMessage(locMsg);
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
      return;
    }

    // [HEADING / NAV] (X=126..228)
    if (tx >= 126 && tx <= 228) {
      _gps_subview = 1;
      _needs_full_redraw = true;
      return;
    }

    // [GPS: ON/OFF] (X=232..312)
    if (tx >= 232 && tx <= 312) {
      bool newState = !sensors.isGpsActive();
      sensors.setGpsActive(newState);
      NodePrefs* prefs = the_mesh.getNodePrefs();
      if (prefs) {
        prefs->gps_enabled = newState ? 1 : 0;
        the_mesh.savePrefs();
      }
      _needs_full_redraw = true;
      return;
    }
  }
}

void TapUI::handleGPSNavTouch(uint16_t tx, uint16_t ty) {
  // Unit toggle [KM/H] / [MPH] inside the compass card top-right (X=240..312, Y=90..112)
  if (tx >= 240 && tx <= 312 && ty >= 90 && ty <= 112) {
    _gps_metric_units = !_gps_metric_units;
    _last_gps_card_hash = 0;
    drawGPSNavInterior();
    return;
  }

  int count = buildSortedContactList(_sorted_nodes, MAX_SORTED_NODES);
  if (count == 0) return;

  int selIdx = 0;
  for (int i = 0; i < count; i++) {
    if (_sorted_nodes[i].contact_idx == _gps_nav_target_slot) {
      selIdx = i;
      break;
    }
  }

  // Top Contact Selector Bar [< NODE] (X=8..64, Y=66..88) / [NODE >] (X=256..312, Y=66..88)
  if (ty >= 66 && ty <= 88) {
    if (tx >= 8 && tx <= 64) {
      selIdx = (selIdx - 1 + count) % count;
      _gps_nav_target_slot = _sorted_nodes[selIdx].contact_idx;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 256 && tx <= 312) {
      selIdx = (selIdx + 1) % count;
      _gps_nav_target_slot = _sorted_nodes[selIdx].contact_idx;
      _needs_full_redraw = true;
      return;
    }
  }

  // Action buttons at bottom (Y=204..234)
  if (ty >= 204 && ty <= 234) {
    ContactInfo c;
    bool validContact = the_mesh.getContactByIdx(_sorted_nodes[selIdx].contact_idx, c);

    // [REQ NODE LOC] (X=8..112) -> Request telemetry + send self-advert
    if (tx >= 8 && tx <= 112 && validContact) {
      uint32_t tag = 0, est_timeout = 0;
      the_mesh.sendRequest(c, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      the_mesh.advert();
      _needs_full_redraw = true;
      return;
    }

    // [SEND MY LOC] (X=114..218) -> Send our coords/speed/heading to selected contact & advert
    if (tx >= 114 && tx <= 218 && validContact) {
      syncGPSWithMesh();
      the_mesh.advert();
      _active_contact_idx = _sorted_nodes[selIdx].contact_idx;
      LocationProvider* loc = sensors.getLocationProvider();
      char locMsg[64];
      if (loc && loc->isEnabled() && loc->isValid()) {
        if (tap_gps.hasCourse()) {
          snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f,%.5f Spd:%.1fkm/h Hdg:%03d%s",
                   (double)loc->getLatitude() / 1e6,
                   (double)loc->getLongitude() / 1e6,
                   tap_gps.getSpeedKmh(),
                   (int)(tap_gps.getCourseDeg() + 0.5f) % 360,
                   degToCardinal(tap_gps.getCourseDeg()));
        } else {
          snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f, %.5f (Alt:%ldm)",
                   (double)loc->getLatitude() / 1e6,
                   (double)loc->getLongitude() / 1e6,
                   loc->getAltitude() / 1000L);
        }
      } else if (sensors.node_lat != 0.0 || sensors.node_lon != 0.0) {
        snprintf(locMsg, sizeof(locMsg), "[LOC] %.5f, %.5f", sensors.node_lat, sensors.node_lon);
      } else {
        snprintf(locMsg, sizeof(locMsg), "[LOC] Ping from WisMesh Tap");
      }
      sendMessage(locMsg);
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
      return;
    }

    // [DM NODE] (X=220..312) -> Open DM chat with selected contact
    if (tx >= 220 && tx <= 312 && validContact) {
      _active_contact_idx = _sorted_nodes[selIdx].contact_idx;
      _chat_scroll_offset = 0;
      _current_tab = TAB_CHAT;
      _needs_full_redraw = true;
      return;
    }
  }
}

// -------------------------------------------------------------
// RADIO & DEVICE SETTINGS SCREEN (2 Sub-Tabs: [RF & LORA] | [DEVICE & SYS])
// -------------------------------------------------------------
void TapUI::drawRadioScreen() {
  _display->setColor(colBg());
  _display->fillRect(0, 44, 320, 196);
  _display->setTextSize(1);

  _display->setColor(_radio_subview == 0 ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(8, 45, 148, 20);
  _display->setColor(colBorder());
  _display->drawRect(8, 45, 148, 20);
  _display->setColor(_radio_subview == 0 ? COLOR_WHITE : colText());
  drawStr(36, 51, "[ RF & LORA ]");

  _display->setColor(_radio_subview == 1 ? COLOR_BLUE_KEY : colCardBg());
  _display->fillRect(164, 45, 148, 20);
  _display->setColor(colBorder());
  _display->drawRect(164, 45, 148, 20);
  _display->setColor(_radio_subview == 1 ? COLOR_WHITE : colText());
  drawStr(184, 51, "[ DEVICE & SYS ]");

  if (_radio_subview == 0) {
    drawRadioRFView();
  } else {
    drawRadioDeviceView();
  }
}

void TapUI::drawRadioRFView() {
  // 1. Node Name row: Y in 68..88
  _display->setColor(colTitle());
  char nameBuf[36];
  snprintf(nameBuf, sizeof(nameBuf), "Node: %.22s", the_mesh.getNodePrefs()->node_name);
  drawStr(10, 74, nameBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(242, 68, 70, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(256, 74, "[NAME]");

  // 2. RF Presets Cycler: Y in 91..111
  _display->setColor(colCardBg());
  _display->fillRect(10, 91, 302, 20);
  _display->setColor(colBorder());
  _display->drawRect(10, 91, 302, 20);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(11, 92, 28, 18);
  _display->setColor(COLOR_WHITE);
  drawStr(21, 97, "<");

  _display->setColor(colTitle());
  const char* pName = (_selected_preset_idx < NUM_RF_PRESETS) ? RF_PRESETS[_selected_preset_idx].name : "Custom";
  char pBuf[38];
  snprintf(pBuf, sizeof(pBuf), "Preset: %s", pName);
  drawStr(46, 97, pBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(283, 92, 28, 18);
  _display->setColor(COLOR_WHITE);
  drawStr(294, 97, ">");

  // 3. Frequency row: Y in 114..134
  _display->setColor(colText());
  char freqBuf[32];
  snprintf(freqBuf, sizeof(freqBuf), "Freq: %.3f MHz", _radio_freq);
  drawStr(10, 120, freqBuf);

  _display->setColor(colCardBg());
  _display->fillRect(172, 114, 44, 20);
  _display->setColor(colBorder());
  _display->drawRect(172, 114, 44, 20);
  _display->setColor(colText());
  drawStr(176, 120, "-125k");

  _display->setColor(colCardBg());
  _display->fillRect(220, 114, 44, 20);
  _display->setColor(colBorder());
  _display->drawRect(220, 114, 44, 20);
  _display->setColor(colText());
  drawStr(224, 120, "+125k");

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(268, 114, 44, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(278, 120, "TYPE");

  // 4. SF & Bandwidth row: Y in 137..157
  _display->setColor(colText());
  char sfbwBuf[36];
  snprintf(sfbwBuf, sizeof(sfbwBuf), "SF: %u | BW: %.1f kHz", _radio_sf, _radio_bw);
  drawStr(10, 143, sfbwBuf);

  _display->setColor(colCardBg());
  _display->fillRect(195, 137, 55, 20);
  _display->setColor(colBorder());
  _display->drawRect(195, 137, 55, 20);
  _display->setColor(colText());
  drawStr(204, 143, "CHG SF");

  _display->setColor(colCardBg());
  _display->fillRect(255, 137, 57, 20);
  _display->setColor(colBorder());
  _display->drawRect(255, 137, 57, 20);
  _display->setColor(colText());
  drawStr(264, 143, "CHG BW");

  // 5. CR, TX Power & Path Hash Size (1B/2B/3B) row: Y in 160..180
  _display->setColor(colText());
  char crtxBuf[36];
  snprintf(crtxBuf, sizeof(crtxBuf), "CR:4/%u TX:%ddB H:%uB", _radio_cr, _radio_tx_power, _radio_path_hash_mode + 1);
  drawStr(10, 166, crtxBuf);

  // [CR] button: X in 164..206
  _display->setColor(colCardBg());
  _display->fillRect(164, 160, 42, 20);
  _display->setColor(colBorder());
  _display->drawRect(164, 160, 42, 20);
  _display->setColor(colText());
  drawStr(178, 166, "CR");

  // [TX] button: X in 210..252
  _display->setColor(colCardBg());
  _display->fillRect(210, 160, 42, 20);
  _display->setColor(colBorder());
  _display->drawRect(210, 160, 42, 20);
  _display->setColor(colText());
  drawStr(224, 166, "TX");

  // [HASH: 1B/2B/3B] button: X in 256..312
  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(256, 160, 56, 20);
  _display->setColor(colBorder());
  _display->drawRect(256, 160, 56, 20);
  _display->setColor(COLOR_WHITE);
  char hBtn[12];
  snprintf(hBtn, sizeof(hBtn), "HASH:%uB", _radio_path_hash_mode + 1);
  drawStr(263, 166, hBtn);

  // 6. Status notification row: Y in 182..194
  if (millis() < _radio_status_expiry) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(10, 184, _radio_status_msg);
  } else {
    _display->setColor(colSubText());
    char idHex[18];
    mesh::Utils::toHex(idHex, the_mesh.self_id.pub_key, 6);
    char idBuf[46];
    snprintf(idBuf, sizeof(idBuf), "ID:!%.12s | PathHash:%uB", idHex, _radio_path_hash_mode + 1);
    drawStr(10, 184, idBuf);
  }

  // 7. Action Buttons: Y in 196..234
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(8, 196, 108, 36);
  _display->setColor(colBorder());
  _display->drawRect(8, 196, 108, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(18, 210, "APPLY & SAVE");

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(122, 196, 86, 36);
  _display->setColor(colBorder());
  _display->drawRect(122, 196, 86, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(142, 210, "BEACON");

  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(214, 196, 98, 36);
  _display->setColor(colBorder());
  _display->drawRect(214, 196, 98, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(228, 210, "POWER OFF");
}

void TapUI::drawRadioDeviceView() {
  // Row 1: BLE Status, On/Off toggle & Unpair: Y in 68..88
  _display->setColor(colTitle());
  char bleBuf[28];
  if (!_ble_enabled) {
    strcpy(bleBuf, "BLE: OFF");
  } else if (ui_task.hasConnection()) {
    strcpy(bleBuf, "BLE: CONNECTED");
  } else {
    strcpy(bleBuf, "BLE: ADVERTISING");
  }
  drawStr(10, 74, bleBuf);

  _display->setColor(_ble_enabled ? COLOR_GREEN_BTN : colCardBg());
  _display->fillRect(170, 68, 72, 20);
  _display->setColor(colBorder());
  _display->drawRect(170, 68, 72, 20);
  _display->setColor(_ble_enabled ? COLOR_WHITE : colText());
  drawStr(180, 74, _ble_enabled ? "BLE: ON" : "BLE: OFF");

  _display->setColor(COLOR_ORANGE_BTN);
  _display->fillRect(248, 68, 64, 20);
  _display->setColor(colBorder());
  _display->drawRect(248, 68, 64, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(258, 74, "UNPAIR");

  // Row 2: BLE PIN Config: Y in 91..111
  _display->setColor(colText());
  char pinBuf[32];
  snprintf(pinBuf, sizeof(pinBuf), "BLE PIN: %06lu", (unsigned long)the_mesh.getBLEPin());
  drawStr(10, 97, pinBuf);

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(170, 91, 72, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(180, 97, "EDIT PIN");

  _display->setColor(COLOR_PURPLE_BTN);
  _display->fillRect(248, 91, 64, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(256, 97, "RANDOM");

  // Row 3: Screen Brightness, Sleep Timeout & Dark/Light Theme: Y in 114..134
  _display->setColor(colText());
  char brtBuf[18];
  snprintf(brtBuf, sizeof(brtBuf), "Brt:%u%%", _brightness_pct);
  drawStr(10, 120, brtBuf);

  // [+BRT]: X in 68..112
  _display->setColor(COLOR_CYAN_BTN);
  _display->fillRect(68, 114, 44, 20);
  _display->setColor(colBorder());
  _display->drawRect(68, 114, 44, 20);
  _display->setColor(COLOR_BLACK);
  drawStr(76, 120, "+BRT");

  // [SLEEP]: X in 118..224
  int sIdx = 1;
  for (int i = 0; i < NUM_SLEEP_OPTIONS; i++) {
    if (SLEEP_TIMEOUTS[i] == _screen_sleep_seconds) { sIdx = i; break; }
  }
  _display->setColor(colCardBg());
  _display->fillRect(118, 114, 106, 20);
  _display->setColor(colBorder());
  _display->drawRect(118, 114, 106, 20);
  _display->setColor(colText());
  char slpBuf[22];
  snprintf(slpBuf, sizeof(slpBuf), "Sleep: %s", SLEEP_LABELS[sIdx]);
  drawStr(128, 120, slpBuf);

  // [THEME: DARK/LIGHT]: X in 230..312
  _display->setColor(_dark_mode ? COLOR_PURPLE_BTN : COLOR_ORANGE_BTN);
  _display->fillRect(230, 114, 82, 20);
  _display->setColor(colBorder());
  _display->drawRect(230, 114, 82, 20);
  _display->setColor(_dark_mode ? COLOR_WHITE : COLOR_BLACK);
  drawStr(240, 120, _dark_mode ? "THEME:DARK" : "THEME:LITE");

  // Row 4: Local Timezone & Clock Display: Y in 137..157
  char dtBuf[32];
  formatLocalDateTime(dtBuf, sizeof(dtBuf), true, false);
  _display->setColor(colTitle());
  char clkLine[42];
  snprintf(clkLine, sizeof(clkLine), "UTC%+d: %s", (int)getTimezoneOffset(), dtBuf);
  drawStr(10, 143, clkLine);

  _display->setColor(colCardBg());
  _display->fillRect(236, 137, 36, 20);
  _display->setColor(colBorder());
  _display->drawRect(236, 137, 36, 20);
  _display->setColor(colText());
  drawStr(243, 143, "-1h");

  _display->setColor(colCardBg());
  _display->fillRect(276, 137, 36, 20);
  _display->setColor(colBorder());
  _display->drawRect(276, 137, 36, 20);
  _display->setColor(colText());
  drawStr(283, 143, "+1h");

  // Row 5: Clock Sync & Manual Set: Y in 160..180
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(10, 160, 102, 20);
  _display->setColor(colBorder());
  _display->drawRect(10, 160, 102, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(24, 166, "GPS TIME SYNC");

  _display->setColor(COLOR_BLUE_KEY);
  _display->fillRect(118, 160, 102, 20);
  _display->setColor(colBorder());
  _display->drawRect(118, 160, 102, 20);
  _display->setColor(COLOR_WHITE);
  drawStr(136, 166, "SET CLOCK");

  _display->setColor(colCardBg());
  _display->fillRect(226, 160, 40, 20);
  _display->setColor(colBorder());
  _display->drawRect(226, 160, 40, 20);
  _display->setColor(colText());
  drawStr(235, 166, "-1m");

  _display->setColor(colCardBg());
  _display->fillRect(270, 160, 42, 20);
  _display->setColor(colBorder());
  _display->drawRect(270, 160, 42, 20);
  _display->setColor(colText());
  drawStr(280, 166, "+1m");

  // Row 6: Status / Feedback line: Y in 183..194
  if (millis() < _radio_status_expiry) {
    _display->setColor(COLOR_GREEN_BTN);
    drawStr(10, 184, _radio_status_msg);
  } else {
    _display->setColor(colSubText());
    drawStr(10, 184, "Tip: Dark theme saves battery power");
  }

  // Row 7: System Action Buttons: Y in 196..234
  _display->setColor(COLOR_GREEN_BTN);
  _display->fillRect(8, 196, 108, 36);
  _display->setColor(colBorder());
  _display->drawRect(8, 196, 108, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(18, 210, "SAVE CONFIG");

  _display->setColor(COLOR_ORANGE_BTN);
  _display->fillRect(122, 196, 86, 36);
  _display->setColor(colBorder());
  _display->drawRect(122, 196, 86, 36);
  _display->setColor(COLOR_BLACK);
  drawStr(144, 210, "REBOOT");

  _display->setColor(COLOR_RED_BTN);
  _display->fillRect(214, 196, 98, 36);
  _display->setColor(colBorder());
  _display->drawRect(214, 196, 98, 36);
  _display->setColor(COLOR_WHITE);
  drawStr(228, 210, "POWER OFF");
}

void TapUI::handleRadioTouch(uint16_t tx, uint16_t ty) {
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
  if (tx >= 242 && tx <= 312 && ty >= 68 && ty <= 88) {
    startKeyboardEdit(EDIT_NODE_NAME, the_mesh.getNodePrefs()->node_name, KB_LOWER, TAB_RADIO);
    return;
  }

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

  if (ty >= 114 && ty <= 134) {
    if (tx >= 172 && tx <= 216) {
      _radio_freq -= 0.125f;
      if (_radio_freq < 150.0f) _radio_freq = 150.0f;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 220 && tx <= 264) {
      _radio_freq += 0.125f;
      if (_radio_freq > 960.0f) _radio_freq = 960.0f;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 268 && tx <= 312) {
      char fStr[16];
      snprintf(fStr, sizeof(fStr), "%.3f", _radio_freq);
      startKeyboardEdit(EDIT_RADIO_FREQ, fStr, KB_SYMBOLS, TAB_RADIO);
      return;
    }
  }

  if (ty >= 137 && ty <= 157) {
    if (tx >= 195 && tx <= 250) {
      _radio_sf++;
      if (_radio_sf > 12) _radio_sf = 7;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 255 && tx <= 312) {
      if (_radio_bw >= 500.0f) _radio_bw = 62.5f;
      else if (_radio_bw < 100.0f) _radio_bw = 125.0f;
      else if (_radio_bw < 200.0f) _radio_bw = 250.0f;
      else _radio_bw = 500.0f;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
  }

  // CR, TX & Path Hash Size (1B/2B/3B) toggles: Y in 160..180
  if (ty >= 160 && ty <= 180) {
    if (tx >= 164 && tx <= 206) { // CR
      _radio_cr++;
      if (_radio_cr > 8) _radio_cr = 5;
      _selected_preset_idx = NUM_RF_PRESETS - 1;
      _needs_full_redraw = true;
      return;
    }
    if (tx >= 210 && tx <= 252) { // TX
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
    if (tx >= 256 && tx <= 312) { // HASH: 1B -> 2B -> 3B
      _radio_path_hash_mode = (_radio_path_hash_mode + 1) % 3;
      NodePrefs* prefs = the_mesh.getNodePrefs();
      if (prefs) {
        prefs->path_hash_mode = _radio_path_hash_mode;
      }
      char msg[32];
      snprintf(msg, sizeof(msg), "Path Hash set to %uB (Tap APPLY)", _radio_path_hash_mode + 1);
      showStatusNotification(msg);
      return;
    }
  }

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

  // Row 3: Brightness, Sleep Timeout, Dark/Light Theme: Y in 114..134
  if (ty >= 114 && ty <= 134) {
    if (tx >= 64 && tx <= 114) {
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
    if (tx >= 118 && tx <= 224) {
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
    if (tx >= 230 && tx <= 312) {
      setDarkMode(!_dark_mode, true);
      showStatusNotification(_dark_mode ? "Dark Theme Enabled" : "Light Theme Enabled");
      return;
    }
  }

  if (ty >= 137 && ty <= 157) {
    if (tx >= 236 && tx <= 272) {
      int8_t newTz = getTimezoneOffset() - 1;
      if (newTz < -12) newTz = 14;
      setTimezoneOffset(newTz, true);
      char msg[32];
      snprintf(msg, sizeof(msg), "Timezone set to UTC%+d", (int)getTimezoneOffset());
      showStatusNotification(msg);
      return;
    }
    if (tx >= 276 && tx <= 312) {
      int8_t newTz = getTimezoneOffset() + 1;
      if (newTz > 14) newTz = -12;
      setTimezoneOffset(newTz, true);
      char msg[32];
      snprintf(msg, sizeof(msg), "Timezone set to UTC%+d", (int)getTimezoneOffset());
      showStatusNotification(msg);
      return;
    }
  }

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
