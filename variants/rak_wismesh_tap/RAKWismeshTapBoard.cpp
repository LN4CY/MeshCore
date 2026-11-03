#include <Arduino.h>
#include <Wire.h>
#include "nrf_gpio.h"
#include "RAKWismeshTapBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void RAKWismeshTapBoard::initiateShutdown(uint8_t reason) {
  // Turn off backlight and slot power
  setBacklight(false);
  setSlotPower(false);

  // Disable LoRa module power before shutdown
  digitalWrite(SX126X_POWER_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif

void RAKWismeshTapBoard::begin() {
  NRF52BoardDCDC::begin();

  // Initialize battery reading
  pinMode(PIN_VBAT_READ, INPUT);

  // Turn on WisBlock 3V3_S slot power (WB_IO2 / Pin 34)
  pinMode(PIN_WISBLOCK_3V3_S, OUTPUT);
  digitalWrite(PIN_WISBLOCK_3V3_S, HIGH);

  // Turn on Backlight (WB_IO3 / Pin 21)
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  // Initialize I2C pins for Touch and Sensors
#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif
  Wire.begin();

  // Initialize LoRa SX1262 power
  pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);  // Allow SX1262 power to stabilize
}
