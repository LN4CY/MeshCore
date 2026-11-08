#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>

class MyMesh;

class UITask {
  mesh::MainBoard* _board;
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  const mesh::LocalIdentity* _self_id;
  SensorManager* _sensors;
  MyMesh* _mesh;
  char _version_info[32];
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;
  uint8_t _curr_page = 0;
  unsigned long _last_sensor_query = 0;

  // Cached sensor readings
  bool _has_temp = false;
  float _temp = 0.0f;
  bool _has_hum = false;
  float _hum = 0.0f;
  bool _has_press = false;
  float _press = 0.0f;
  bool _has_solar_v = false;
  float _solar_v = 0.0f;
  bool _has_solar_ma = false;
  float _solar_ma = 0.0f;
  bool _has_gps = false;
  float _gps_lat = 0.0f, _gps_lon = 0.0f;
  float _mcu_temp = NAN;

  void updateSensors();
  void renderBootScreen();
  void renderPowerOffScreen();
  void renderPageNodeInfo();
  void renderPageSensors();
  void renderPageStats();
  void renderCurrScreen();

  static uint8_t calcBatteryPercent(uint16_t mv);

public:
  UITask(mesh::MainBoard& board, DisplayDriver& display)
    : _board(&board), _display(&display), _node_prefs(nullptr), _self_id(nullptr), _sensors(nullptr), _mesh(nullptr) {
    _next_read = _next_refresh = 0;
  }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version,
             const mesh::LocalIdentity* self_id = nullptr,
             SensorManager* sensors = nullptr,
             MyMesh* mesh = nullptr);

  void loop();
};
