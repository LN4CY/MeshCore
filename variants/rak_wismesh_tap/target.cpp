#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <RTClib.h>
#include <math.h>

RAKWismeshTapBoard board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 3000, true, true);
#endif

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// ---------------------------------------------------------------------------
// TapLocationProvider implementation
// ---------------------------------------------------------------------------
static double calcDistMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = (lat2 - lat1) * (M_PI / 180.0);
  double dLon = (lon2 - lon1) * (M_PI / 180.0);
  double a = sin(dLat * 0.5) * sin(dLat * 0.5) +
             cos(lat1 * (M_PI / 180.0)) * cos(lat2 * (M_PI / 180.0)) *
             sin(dLon * 0.5) * sin(dLon * 0.5);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return R * c;
}

static double calcBearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double phi1 = lat1 * (M_PI / 180.0);
  double phi2 = lat2 * (M_PI / 180.0);
  double dLon = (lon2 - lon1) * (M_PI / 180.0);
  double y = sin(dLon) * cos(phi2);
  double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);
  double brng = atan2(y, x) * (180.0 / M_PI);
  return fmod(brng + 360.0, 360.0);
}

TapLocationProvider::TapLocationProvider(Stream& ser, mesh::RTCClock* clock)
  : _nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _clock(clock), _gps_serial(&ser) {}

void TapLocationProvider::begin() {
  _begin_calls++;
  _enabled = true;
}

void TapLocationProvider::reset() {
  _nmea.clear();
}

void TapLocationProvider::stop() {
  _stop_calls++;
  _enabled = false;
}

long TapLocationProvider::getTimestamp() {
  DateTime dt(_nmea.getYear(), _nmea.getMonth(), _nmea.getDay(),
              _nmea.getHour(), _nmea.getMinute(), _nmea.getSecond());
  return dt.unixtime();
}

void TapLocationProvider::sendSentence(const char* sentence) {
  _nmea.sendSentence(*_gps_serial, sentence);
}

void TapLocationProvider::loop() {
  if (!_enabled) return;

  while (_gps_serial->available()) {
    char c = _gps_serial->read();
    _uart_bytes++;
    _last_uart_ms = millis();
    _uart_seen = true;

    bool parsed = _nmea.process(c);

    if ((c == '\0' || c == '\r' || c == '\n') && _nmea.getSentence()[0] != '\0') {
      if (MicroNMEA::testChecksum(_nmea.getSentence())) {
        _nmea_checksum_ok++;
        _last_nmea_ms = millis();
        _nmea_seen = true;

        const char* message_id = _nmea.getMessageID();
        if (parsed && isValid() &&
            (strcmp(message_id, "GGA") == 0 || strcmp(message_id, "RMC") == 0)) {
          unsigned long now_ms = millis();
          _last_fix_ms = now_ms;
          _fix_seen = true;

          // Update speed & course from RMC sentence when present
          long spd = _nmea.getSpeed();
          if (spd >= 0) {
            _speed_knots_milli = spd;
          }
          long crs = _nmea.getCourse();
          if (crs >= 0 && crs <= 360000L) {
            _course_deg_milli = crs;
            _has_course = true;
          }

          // Fallback speed/course calculation from consecutive fixes
          double cur_lat = (double)_nmea.getLatitude() / 1000000.0;
          double cur_lon = (double)_nmea.getLongitude() / 1000000.0;
          if (_prev_pos_ms != 0 && (now_ms > _prev_pos_ms)) {
            double dt_s = (double)(now_ms - _prev_pos_ms) / 1000.0;
            double dist_m = calcDistMeters(_prev_lat, _prev_lon, cur_lat, cur_lon);
            if (dist_m >= 3.0 && dt_s >= 0.5 && dt_s <= 60.0) {
              double brg = calcBearingDeg(_prev_lat, _prev_lon, cur_lat, cur_lon);
              if (crs < 0) {
                _course_deg_milli = (long)(brg * 1000.0);
                _has_course = true;
              }
              if (spd < 0) {
                double mps = dist_m / dt_s;
                _speed_knots_milli = (long)(mps * 1943.844);
              }
              _prev_lat = cur_lat;
              _prev_lon = cur_lon;
              _prev_pos_ms = now_ms;
            } else if (dt_s >= 8.0 && dist_m < 2.0 && spd < 0) {
              _speed_knots_milli = 0;
              _prev_lat = cur_lat;
              _prev_lon = cur_lon;
              _prev_pos_ms = now_ms;
            }
          } else {
            _prev_lat = cur_lat;
            _prev_lon = cur_lon;
            _prev_pos_ms = now_ms;
          }
        }
      } else {
        _nmea_checksum_bad++;
      }
    }
  }

  if (!isValid()) _time_valid = 0;

  if ((long)(millis() - _next_check) > 0) {
    _next_check = millis() + 1000;
    if (!_time_sync_needed && _clock != NULL && (millis() - _last_time_sync) > TIME_SYNC_INTERVAL) {
      _time_sync_needed = true;
    }
    if (_time_sync_needed && _time_valid > 2) {
      if (_clock != NULL && _nmea.getYear() >= 2024) {
        _clock->setCurrentTime(getTimestamp());
        _time_sync_needed = false;
        _last_time_sync = millis();
      }
    }
    if (isValid()) {
      _time_valid++;
    }
  }
}

static void formatAgeHelper(char* out, size_t out_size, bool seen, unsigned long timestamp) {
  if (!seen) {
    snprintf(out, out_size, "never");
  } else {
    snprintf(out, out_size, "%lu", (unsigned long)(millis() - timestamp));
  }
}

void TapLocationProvider::formatDiagnostics(char* out, size_t out_size) {
  if (out_size == 0) return;
  char uart_age[11];
  char nmea_age[11];
  char fix_age[11];
  formatAgeHelper(uart_age, sizeof(uart_age), _uart_seen, _last_uart_ms);
  formatAgeHelper(nmea_age, sizeof(nmea_age), _nmea_seen, _last_nmea_ms);
  formatAgeHelper(fix_age, sizeof(fix_age), _fix_seen, _last_fix_ms);

  snprintf(out, out_size,
           "en:%u ub:%lu ua:%s ok:%lu bad:%lu na:%s sat:%ld fix:%u fa:%s",
           isEnabled() ? 1U : 0U,
           (unsigned long)_uart_bytes,
           uart_age,
           (unsigned long)_nmea_checksum_ok,
           (unsigned long)_nmea_checksum_bad,
           nmea_age,
           satellitesCount(),
           isValid() ? 1U : 0U,
           fix_age);
}

// ---------------------------------------------------------------------------
// TapSensorManager implementation
// ---------------------------------------------------------------------------
bool TapSensorManager::begin() {
  bool res = EnvironmentSensorManager::begin();
  // EnvironmentSensorManager::rakGPSInit() disables GPS on RAK_WISMESH_TAP
  // because WB_IO2..WB_IO5 are display pins. Restore external Serial1 GPS state!
  gps_detected = true;
  gps_active = true;
  if (_location) {
    _location->begin();
  }
  return res;
}

void TapSensorManager::loop() {
  EnvironmentSensorManager::loop();
  if (gps_active && _location && _location->isValid()) {
    double lat = ((double)_location->getLatitude()) / 1000000.0;
    double lon = ((double)_location->getLongitude()) / 1000000.0;
    if (lat != 0.0 || lon != 0.0) {
      node_lat = lat;
      node_lon = lon;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
    }
  }
}

bool TapSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    gps_detected = true;
    if (strcmp(value, "0") == 0) {
      setGpsActive(false);
      return true;
    } else if (strcmp(value, "1") == 0) {
      setGpsActive(true);
      return true;
    }
    return false;
  }
  return EnvironmentSensorManager::setSettingValue(name, value);
}

void TapSensorManager::setGpsActive(bool active) {
  gps_detected = true;
  gps_active = active;
  if (_location) {
    if (active) _location->begin();
    else _location->stop();
  }
}

TapLocationProvider tap_gps(Serial1, &rtc_clock);
TapSensorManager sensors(tap_gps);

bool radio_init() {
  Serial.println("[BOOT] Probing RTC clock...");
  rtc_clock.begin(Wire);
  Serial.println("[BOOT] Initializing SX1262 LoRa radio via SPI...");
  bool res = radio.std_init(&SPI);
  if (!res) {
    Serial.println("[ERROR] SX1262 radio initialization failed!");
  } else {
    Serial.println("[BOOT] SX1262 LoRa radio initialized successfully!");
  }
  return res;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
