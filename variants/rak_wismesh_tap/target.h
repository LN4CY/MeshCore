#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <RAKWismeshTapBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <MicroNMEA.h>

class TapLocationProvider : public LocationProvider {
  char _nmeaBuffer[100];
  MicroNMEA _nmea;
  mesh::RTCClock* _clock;
  Stream* _gps_serial;
  bool _enabled = true;
  unsigned long _next_check = 0;
  long _time_valid = 0;
  unsigned long _last_time_sync = 0;
  static const unsigned long TIME_SYNC_INTERVAL = 1800000UL;

  // Diagnostics
  uint32_t _uart_bytes = 0;
  uint32_t _nmea_checksum_ok = 0;
  uint32_t _nmea_checksum_bad = 0;
  uint32_t _begin_calls = 0;
  uint32_t _stop_calls = 0;
  unsigned long _last_uart_ms = 0;
  unsigned long _last_nmea_ms = 0;
  unsigned long _last_fix_ms = 0;
  bool _uart_seen = false;
  bool _nmea_seen = false;
  bool _fix_seen = false;

  // Speed & Course tracking (milli-knots & milli-degrees)
  long _speed_knots_milli = 0;
  long _course_deg_milli = 0;
  bool _has_course = false;
  double _prev_lat = 0.0;
  double _prev_lon = 0.0;
  unsigned long _prev_pos_ms = 0;

public:
  TapLocationProvider(Stream& ser, mesh::RTCClock* clock = nullptr);

  void begin() override;
  void reset() override;
  void stop() override;
  bool isEnabled() override { return _enabled; }
  void syncTime() override { _nmea.clear(); LocationProvider::syncTime(); }
  long getLatitude() override { return _nmea.getLatitude(); }
  long getLongitude() override { return _nmea.getLongitude(); }
  long getAltitude() override {
    long alt = 0;
    _nmea.getAltitude(alt);
    return alt;
  }
  long satellitesCount() override { return _nmea.getNumSatellites(); }
  bool isValid() override { return _nmea.isValid(); }
  long getTimestamp() override;
  void sendSentence(const char* sentence) override;
  void loop() override;
  void formatDiagnostics(char* out, size_t out_size) override;

  // Extended navigation telemetry (Speed, Heading/Course, HDOP)
  long getSpeedKnotsMilli() const { return _speed_knots_milli; }
  float getSpeedKnots() const { return _speed_knots_milli > 0 ? (_speed_knots_milli * 0.001f) : 0.0f; }
  float getSpeedKmh() const { return _speed_knots_milli > 0 ? (_speed_knots_milli * 0.001852f) : 0.0f; }
  float getSpeedMph() const { return _speed_knots_milli > 0 ? (_speed_knots_milli * 0.00115078f) : 0.0f; }
  bool hasCourse() const { return _has_course; }
  float getCourseDeg() const { return _has_course ? (_course_deg_milli * 0.001f) : 0.0f; }
  uint8_t getHDOP() const { return _nmea.getHDOP(); }
};

class TapSensorManager : public EnvironmentSensorManager {
public:
  TapSensorManager(LocationProvider& location) : EnvironmentSensorManager(location) {
    gps_detected = true;
    gps_active = true;
  }
  bool begin() override;
  void loop() override;
  bool setSettingValue(const char* name, const char* value) override;
  void setGpsActive(bool active);
  bool isGpsActive() const { return gps_active; }
};

#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7789LCDDisplay.h>
  extern DISPLAY_CLASS display;
  #include <helpers/ui/MomentaryButton.h>
  extern MomentaryButton user_btn;
#endif

extern RAKWismeshTapBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern TapLocationProvider tap_gps;
extern TapSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
