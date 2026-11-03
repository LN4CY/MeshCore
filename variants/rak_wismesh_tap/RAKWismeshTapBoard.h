#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#define PIN_VBAT_READ       5
#define ADC_MULTIPLIER      (3 * 1.73 * 1.187 * 1000)
#define BATTERY_SAMPLES     8

// WisBlock IO Slot & Peripheral Control Pins for WisMesh Tap
#define PIN_WISBLOCK_3V3_S  34  // WB_IO2 (P1.02) - Powers sensor & IO slot modules
#define PIN_TFT_BL          21  // WB_IO3 (P0.21) - Backlight control
#define PIN_TFT_DC          4   // WB_IO4 (P0.04) - Display Data/Command
#define PIN_TFT_RST         9   // WB_IO5 (P0.09) - Display Reset
#define PIN_TFT_CS          26  // WB_SPI_CS (P0.26) - Display Chip Select
#define PIN_TOUCH_INT       10  // WB_IO6 (P0.10) - Touch Interrupt
#define PIN_TOUCH_RST       17  // WB_IO1 (P0.17) - Touch Reset
#ifndef PIN_USER_BTN
#define PIN_USER_BTN        31  // WB_A1 / AIN1 (P0.31) - User Button
#endif

class RAKWismeshTapBoard : public NRF52BoardDCDC {
private:
  uint32_t _last_heartbeat = 0;
  uint32_t _heartbeat_off_time = 0;

protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  RAKWismeshTapBoard() : NRF52Board("WisMeshTap_OTA") {}

  void begin();
  void loop() override;
  void sleep(uint32_t secs) override;
  void onBeforeTransmit() override;
  void onAfterTransmit() override;
  void onBootComplete() override;
  void powerOff() override;

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  void setBacklight(bool on) {
    digitalWrite(PIN_TFT_BL, on ? HIGH : LOW);
  }

  void setSlotPower(bool on) {
    digitalWrite(PIN_WISBLOCK_3V3_S, on ? HIGH : LOW);
  }

  void setLedGreen(bool on) {
    digitalWrite(LED_GREEN, on ? HIGH : LOW);
  }

  void setLedBlue(bool on) {
    digitalWrite(LED_BLUE, on ? HIGH : LOW);
  }

  const char* getManufacturerName() const override {
    return "RAK WisMesh Tap";
  }
};
