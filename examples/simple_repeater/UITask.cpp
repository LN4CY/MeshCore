#include "UITask.h"
#include "target.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>
#include <CayenneLPP.h>
#include "MyMesh.h"

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds
#define SENSOR_QUERY_INTERVAL 4000  // refresh sensors every 4s when on sensor screen

#define POWEROFF_DELAY 3000

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

uint8_t UITask::calcBatteryPercent(uint16_t mv) {
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

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version,
                   const mesh::LocalIdentity* self_id,
                   SensorManager* sensors,
                   MyMesh* mesh) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _started_at = millis();
  _node_prefs = node_prefs;
  _self_id = self_id;
  _sensors = sensors;
  _mesh = mesh;
  _curr_page = 0;
  _display->turnOn();

#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA) && defined(DISPLAY_CLASS)
  analog_btn.begin();
#endif

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
  free(version);

  updateSensors();
}

void UITask::updateSensors() {
  _mcu_temp = _board ? _board->getMCUTemperature() : NAN;

  if (_sensors == nullptr) return;

  // Query sensors via CayenneLPP
  CayenneLPP telemetry(128);
  telemetry.reset();
  _sensors->querySensors(TELEM_PERM_ENVIRONMENT | TELEM_PERM_LOCATION, telemetry);

  uint8_t* buf = telemetry.getBuffer();
  uint8_t size = telemetry.getSize();
  uint8_t i = 0;

  while (i + 2 <= size) {
    uint8_t ch = buf[i++];
    uint8_t type = buf[i++];
    switch (type) {
      case LPP_DIGITAL_INPUT:
      case LPP_DIGITAL_OUTPUT:
      case LPP_PRESENCE:
      case LPP_SWITCH:
        i += 1;
        break;
      case LPP_RELATIVE_HUMIDITY:
        if (i + 1 <= size) {
          _has_hum = true;
          _hum = buf[i] * 0.5f;
          i += 1;
        }
        break;
      case LPP_TEMPERATURE:
        if (i + 2 <= size) {
          int16_t raw = (int16_t)((buf[i] << 8) | buf[i + 1]);
          _has_temp = true;
          _temp = raw * 0.1f;
          i += 2;
        }
        break;
      case LPP_BAROMETRIC_PRESSURE:
        if (i + 2 <= size) {
          uint16_t raw = (uint16_t)((buf[i] << 8) | buf[i + 1]);
          _has_press = true;
          _press = raw * 0.1f;
          i += 2;
        }
        break;
      case LPP_VOLTAGE:
        if (i + 2 <= size) {
          uint16_t raw = (uint16_t)((buf[i] << 8) | buf[i + 1]);
          if (ch != TELEM_CHANNEL_SELF) {
            _has_solar_v = true;
            _solar_v = raw * 0.01f;
          }
          i += 2;
        }
        break;
      case LPP_CURRENT:
        if (i + 2 <= size) {
          uint16_t raw = (uint16_t)((buf[i] << 8) | buf[i + 1]);
          _has_solar_ma = true;
          _solar_ma = raw * 1.0f;
          i += 2;
        }
        break;
      case LPP_GPS:
        if (i + 9 <= size) {
          int32_t lat_raw = ((int32_t)(int8_t)buf[i] << 16) | ((uint32_t)buf[i+1] << 8) | buf[i+2];
          int32_t lon_raw = ((int32_t)(int8_t)buf[i+3] << 16) | ((uint32_t)buf[i+4] << 8) | buf[i+5];
          _has_gps = true;
          _gps_lat = lat_raw * 0.0001f;
          _gps_lon = lon_raw * 0.0001f;
          i += 9;
        }
        break;
      case LPP_ANALOG_INPUT:
      case LPP_ANALOG_OUTPUT:
      case LPP_LUMINOSITY:
      case LPP_POWER:
      case LPP_ALTITUDE:
      case LPP_CONCENTRATION:
        i += 2;
        break;
      case LPP_GENERIC_SENSOR:
      case LPP_FREQUENCY:
      case LPP_DISTANCE:
      case LPP_ENERGY:
      case LPP_UNIXTIME:
        i += 4;
        break;
      case LPP_ACCELEROMETER:
      case LPP_GYROMETER:
        i += 6;
        break;
      case LPP_COLOUR:
        i += 3;
        break;
      default:
        i = size;
        break;
    }
  }

  // Also check if GPS location is directly on SensorManager
  if (!_has_gps && _sensors->isGPSDetected() && (_sensors->node_lat != 0.0 || _sensors->node_lon != 0.0)) {
    _has_gps = true;
    _gps_lat = (float)_sensors->node_lat;
    _gps_lon = (float)_sensors->node_lon;
  }
}

void UITask::renderBootScreen() {
  _display->setColor(UIColor::corp_blue);
  int logoWidth = 128;
  _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

  const char* website = "https://meshcore.io";
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(1);
  _display->drawTextCentered(_display->width() / 2, 22, website);
  _display->drawTextCentered(_display->width() / 2, 35, _version_info);
  _display->drawTextCentered(_display->width() / 2, 48, "< Repeater >");
}

void UITask::renderPowerOffScreen() {
  _display->setColor(UIColor::corp_blue);
  int logoWidth = 128;
  _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

  const char* website = "https://meshcore.io";
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(1);
  _display->drawTextCentered(_display->width() / 2, 22, website);

  const char* poweroff_string = "Turning OFF";
  _display->drawTextCentered(_display->width() / 2, 48, poweroff_string);
}

void UITask::renderPageNodeInfo() {
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(1);

  // Line 0: Node Name
  char title[32];
  if (_node_prefs && _node_prefs->node_name[0]) {
    snprintf(title, sizeof(title), "%.21s", _node_prefs->node_name);
  } else {
    snprintf(title, sizeof(title), "MeshCore Repeater");
  }
  _display->setCursor(0, 0);
  _display->print(title);

  // Line 1: Node ID
  char id_line[32];
  if (_self_id != nullptr) {
    char id_hex[16];
    mesh::Utils::toHex(id_hex, _self_id->pub_key, 6);
    snprintf(id_line, sizeof(id_line), "ID: !%s", id_hex);
  } else {
    snprintf(id_line, sizeof(id_line), "ID: Unknown");
  }
  _display->setCursor(0, 11);
  _display->print(id_line);

  // Line 2: Battery SOC & Voltage
  char bat_line[32];
  uint16_t mv = _board ? _board->getBattMilliVolts() : 0;
  uint8_t pct = calcBatteryPercent(mv);
  bool ext = _board && _board->isExternalPowered();
  if (ext) {
    snprintf(bat_line, sizeof(bat_line), "BAT: %u%% %1.2fV [USB]", pct, (float)mv / 1000.0f);
  } else {
    snprintf(bat_line, sizeof(bat_line), "BAT: %u%% (%1.2fV)", pct, (float)mv / 1000.0f);
  }
  _display->setCursor(0, 22);
  _display->print(bat_line);

  // Line 3: RF Settings (Freq, SF, CR)
  char rf_line[32];
  if (_node_prefs) {
    snprintf(rf_line, sizeof(rf_line), "RF: %06.3f SF%d CR%d", _node_prefs->freq, _node_prefs->sf, _node_prefs->cr);
  } else {
    snprintf(rf_line, sizeof(rf_line), "RF: LoRa");
  }
  _display->setCursor(0, 33);
  _display->print(rf_line);

  // Line 4: Bandwidth & TX Power
  char bw_line[32];
  if (_node_prefs) {
    snprintf(bw_line, sizeof(bw_line), "BW:%03.1f TX:%ddBm", _node_prefs->bw, _node_prefs->tx_power_dbm);
  } else {
    snprintf(bw_line, sizeof(bw_line), "Repeater Active");
  }
  _display->setCursor(0, 44);
  _display->print(bw_line);

  // Line 5: Navigation hint
  _display->setCursor(0, 55);
  _display->print("[1/3] Click -> Sensors");
}

void UITask::renderPageSensors() {
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(1);

  _display->setCursor(0, 0);
  _display->print("-- Sensors [2/3] --");

  int y = 11;
  char s_line[32];
  bool any_sensor = false;

  if (_has_temp || _has_hum) {
    if (_has_temp && _has_hum) {
      snprintf(s_line, sizeof(s_line), "T:%1.1fC  H:%1.1f%%", _temp, _hum);
    } else if (_has_temp) {
      snprintf(s_line, sizeof(s_line), "Temp: %1.1f C", _temp);
    } else {
      snprintf(s_line, sizeof(s_line), "Hum: %1.1f %%", _hum);
    }
    _display->setCursor(0, y);
    _display->print(s_line);
    y += 11;
    any_sensor = true;
  }

  if (_has_press && y <= 44) {
    snprintf(s_line, sizeof(s_line), "Press: %1.1f hPa", _press);
    _display->setCursor(0, y);
    _display->print(s_line);
    y += 11;
    any_sensor = true;
  }

  if ((_has_solar_v || _has_solar_ma) && y <= 44) {
    snprintf(s_line, sizeof(s_line), "Solar: %1.2fV %1.0fmA", _solar_v, _solar_ma);
    _display->setCursor(0, y);
    _display->print(s_line);
    y += 11;
    any_sensor = true;
  }

  if (_has_gps && y <= 44) {
    snprintf(s_line, sizeof(s_line), "GPS: %1.3f,%1.3f", _gps_lat, _gps_lon);
    _display->setCursor(0, y);
    _display->print(s_line);
    y += 11;
    any_sensor = true;
  }

  if (!isnan(_mcu_temp) && y <= 44) {
    snprintf(s_line, sizeof(s_line), "MCU Temp: %1.1f C", _mcu_temp);
    _display->setCursor(0, y);
    _display->print(s_line);
    y += 11;
    any_sensor = true;
  }

  if (!any_sensor) {
    _display->setCursor(0, 20);
    _display->print("No external sensors");
    _display->setCursor(0, 32);
    _display->print("detected on I2C bus");
  }

  _display->setCursor(0, 55);
  _display->print("[2/3] Click -> Stats");
}

void UITask::renderPageStats() {
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(1);

  _display->setCursor(0, 0);
  _display->print("-- Stats [3/3] --");

  char line[32];
  uint32_t up_secs = (uint32_t)(millis() / 1000);
  uint32_t days = up_secs / 86400;
  uint32_t hours = (up_secs % 86400) / 3600;
  uint32_t mins = (up_secs % 3600) / 60;
  if (days > 0) {
    snprintf(line, sizeof(line), "Up: %lud %luh %lum", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
  } else {
    snprintf(line, sizeof(line), "Up: %02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)mins, (unsigned long)(up_secs % 60));
  }
  _display->setCursor(0, 11);
  _display->print(line);

  // Packets sent / received
  snprintf(line, sizeof(line), "Pkts: Tx %lu / Rx %lu",
           (unsigned long)radio_driver.getPacketsSent(),
           (unsigned long)radio_driver.getPacketsRecv());
  _display->setCursor(0, 22);
  _display->print(line);

  // Airtime
  if (_mesh) {
    snprintf(line, sizeof(line), "Air: %1.1fs Rx:%1.1fs",
             (float)_mesh->getTotalAirTime() / 1000.0f,
             (float)_mesh->getReceiveAirTime() / 1000.0f);
  } else {
    snprintf(line, sizeof(line), "Air: 0.0s");
  }
  _display->setCursor(0, 33);
  _display->print(line);

  // Firmware version
  _display->setCursor(0, 44);
  _display->print(_version_info);

  _display->setCursor(0, 55);
  _display->print("[3/3] Click -> Node");
}

void UITask::renderCurrScreen() {
  if (millis() < _started_at + BOOT_SCREEN_MILLIS) {
    renderBootScreen();
  } else if (_powering_off_at > 0) {
    renderPowerOffScreen();
  } else {
    switch (_curr_page) {
      case 1:
        renderPageSensors();
        break;
      case 2:
        renderPageStats();
        break;
      case 0:
      default:
        renderPageNodeInfo();
        break;
    }
  }
}

void UITask::loop() {
#if defined(DISPLAY_CLASS)
  int ev = BUTTON_EVENT_NONE;
#if defined(PIN_USER_BTN)
  ev = user_btn.check();
#endif
#if defined(PIN_USER_BTN_ANA)
  if (ev == BUTTON_EVENT_NONE) {
    ev = analog_btn.check();
  }
#endif

  if (ev == BUTTON_EVENT_CLICK) {
    if (_display->isOn()) {
      _curr_page = (_curr_page + 1) % 3;
      if (_curr_page == 1) {
        updateSensors();
      }
    } else {
      _display->turnOn();
      _curr_page = 0;
      updateSensors();
    }
    _next_refresh = 0;                        // force immediate redraw
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    _display->turnOn();
    Serial.println("Powering Off");
    _powering_off_at = millis() + POWEROFF_DELAY;
    _next_refresh = 0;
  }
#endif

  if (_display->isOn()) {
    if (_curr_page == 1 && (millis() - _last_sensor_query >= SENSOR_QUERY_INTERVAL)) {
      updateSensors();
      _last_sensor_query = millis();
    }

    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }

  if (_powering_off_at > 0) { // power off timer armed
#if defined(LED_PIN) && defined(LED_STATE_ON)
    digitalWrite(LED_PIN, LED_STATE_ON); // switch on the led until poweroff
    delay(1000);
    digitalWrite(LED_PIN, !LED_STATE_ON); // off LED
#endif
    if (millis() > _powering_off_at) {
      _board->powerOff();  // should not return
    }
  }
}
