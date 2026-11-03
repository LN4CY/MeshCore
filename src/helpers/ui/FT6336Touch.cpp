#include "FT6336Touch.h"

FT6336Touch::FT6336Touch(TwoWire* wire, uint8_t addr, int intPin, int rstPin)
    : _wire(wire),
      _i2c_addr(addr),
      _int_pin(intPin),
      _rst_pin(rstPin),
      _rotation(1),
      _width(320),
      _height(240),
      _last_touched(false),
      _last_x(0),
      _last_y(0),
      _last_poll_ms(0) {}

uint8_t FT6336Touch::readRegister(uint8_t reg) {
  uint8_t val = 0;
  readRegisters(reg, &val, 1);
  return val;
}

bool FT6336Touch::readRegisters(uint8_t reg, uint8_t* dest, size_t len) {
  _wire->beginTransmission(_i2c_addr);
  _wire->write(reg);
  if (_wire->endTransmission() != 0) {
    return false;
  }

  uint8_t count = _wire->requestFrom(_i2c_addr, (uint8_t)len);
  if (count != len) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    dest[i] = _wire->read();
  }
  return true;
}

bool FT6336Touch::begin() {
  if (_rst_pin >= 0) {
    pinMode(_rst_pin, OUTPUT);
    digitalWrite(_rst_pin, LOW);
    delay(10);
    digitalWrite(_rst_pin, HIGH);
    delay(50);
  }

  if (_int_pin >= 0) {
    pinMode(_int_pin, INPUT_PULLUP);
  }

  // Probe I2C device
  _wire->beginTransmission(_i2c_addr);
  if (_wire->endTransmission() != 0) {
    return false;
  }

  return true;
}

void FT6336Touch::setRotation(uint8_t rotation) {
  _rotation = rotation % 4;
}

void FT6336Touch::setDimensions(uint16_t width, uint16_t height) {
  _width = width;
  _height = height;
}

uint8_t FT6336Touch::getChipId() {
  return readRegister(FT6336_REG_CHIP_ID);
}

bool FT6336Touch::poll(TouchPoint& pt) {
  pt.justPressed = false;
  pt.justReleased = false;

  // Rate limit polling to avoid saturating I2C bus (every 15ms ~ 66Hz is plenty for touch)
  unsigned long now = millis();
  if (now - _last_poll_ms < 15) {
    pt.x = _last_x;
    pt.y = _last_y;
    pt.isPressed = _last_touched;
    return _last_touched;
  }
  _last_poll_ms = now;

  // Read status and coordinates in a single 5-byte transaction
  // Reg 0x02: TD_STATUS, Reg 0x03-0x04: XH/XL, Reg 0x05-0x06: YH/YL
  uint8_t buf[5];
  if (!readRegisters(FT6336_REG_TD_STATUS, buf, 5)) {
    pt.isPressed = _last_touched;
    pt.x = _last_x;
    pt.y = _last_y;
    return false;
  }

  uint8_t touches = buf[0] & 0x0F;
  bool isPressed = (touches > 0 && touches <= 2);

  if (isPressed) {
    uint16_t raw_x = ((buf[1] & 0x0F) << 8) | buf[2];
    uint16_t raw_y = ((buf[3] & 0x0F) << 8) | buf[4];

    // Native touch panel coordinates are usually 240 (X) x 320 (Y)
    // Transform coordinates based on screen rotation
    uint16_t screen_x = 0;
    uint16_t screen_y = 0;

    switch (_rotation) {
      case 0: // Portrait (240x320)
        screen_x = raw_x;
        screen_y = raw_y;
        break;
      case 1: // Landscape (320x240) - Typical WisMesh Tap layout
        screen_x = raw_y;
        screen_y = 240 > raw_x ? (240 - raw_x) : 0;
        break;
      case 2: // Inverted portrait
        screen_x = 240 > raw_x ? (240 - raw_x) : 0;
        screen_y = 320 > raw_y ? (320 - raw_y) : 0;
        break;
      case 3: // Inverted landscape (320x240)
        screen_x = 320 > raw_y ? (320 - raw_y) : 0;
        screen_y = raw_x;
        break;
    }

    // Clamp to screen bounds
    if (screen_x >= _width) screen_x = _width - 1;
    if (screen_y >= _height) screen_y = _height - 1;

    pt.x = screen_x;
    pt.y = screen_y;
    pt.isPressed = true;
    pt.justPressed = !_last_touched;

    _last_x = screen_x;
    _last_y = screen_y;
    _last_touched = true;
    return true;
  } else {
    pt.isPressed = false;
    pt.justReleased = _last_touched;
    pt.x = _last_x;
    pt.y = _last_y;

    _last_touched = false;
    return false;
  }
}
