#pragma once

#include <Arduino.h>
#include <Wire.h>

#define FT6336_DEFAULT_ADDR 0x38

// Register definitions
#define FT6336_REG_DEV_MODE    0x00
#define FT6336_REG_GEST_ID     0x01
#define FT6336_REG_TD_STATUS   0x02
#define FT6336_REG_TOUCH1_XH   0x03
#define FT6336_REG_TOUCH1_XL   0x04
#define FT6336_REG_TOUCH1_YH   0x05
#define FT6336_REG_TOUCH1_YL   0x06
#define FT6336_REG_CHIP_ID     0xA6

struct TouchPoint {
  uint16_t x;
  uint16_t y;
  bool isPressed;
  bool justPressed;
  bool justReleased;
};

class FT6336Touch {
private:
  TwoWire* _wire;
  uint8_t _i2c_addr;
  int _int_pin;
  int _rst_pin;
  uint8_t _rotation; // 0=0 deg, 1=90 deg, 2=180 deg, 3=270 deg
  uint16_t _width;
  uint16_t _height;

  bool _last_touched;
  uint16_t _last_x;
  uint16_t _last_y;
  unsigned long _last_poll_ms;

  uint8_t readRegister(uint8_t reg);
  bool readRegisters(uint8_t reg, uint8_t* dest, size_t len);

public:
  FT6336Touch(TwoWire* wire = &Wire, uint8_t addr = FT6336_DEFAULT_ADDR, int intPin = -1, int rstPin = -1);

  bool begin();
  void setRotation(uint8_t rotation);
  void setDimensions(uint16_t width, uint16_t height);

  // Polls touch controller; updates point state
  bool poll(TouchPoint& pt);

  uint8_t getChipId();
};
