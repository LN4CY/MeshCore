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

  // Initialize LEDs
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_GREEN, HIGH); // Green ON during boot
  digitalWrite(LED_BLUE, LOW);

  // Optional USB CDC wait so early boot messages aren't lost if terminal connects
  Serial.begin(115200);
  uint32_t cdc_start = millis();
  while (!Serial && (millis() - cdc_start < 1500));

  Serial.println("\r\n==========================================");
  Serial.println(" RAK WisMesh Tap V1 - Standalone Firmware");
  Serial.println("==========================================");
  Serial.printf("[BOOT] Board starting up (System Clock: %lu MHz)\r\n", (unsigned long)(SystemCoreClock / 1000000));

  // Initialize battery reading
  pinMode(PIN_VBAT_READ, INPUT);

  // Turn on WisBlock 3V3_S slot power (WB_IO2 / Pin 34)
  pinMode(PIN_WISBLOCK_3V3_S, OUTPUT);
  digitalWrite(PIN_WISBLOCK_3V3_S, HIGH);
  delay(30); // Allow WisBlock IO slot 3.3V rail to stabilize

  // Keep Touch controller (FT6336U) out of reset (WB_IO1 / Pin 17)
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, HIGH);

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
  delay(15);  // Allow SX1262 power rail to stabilize
  Serial.println("[BOOT] WisBlock peripherals & SX1262 power rail enabled.");
}

void RAKWismeshTapBoard::loop() {
  // Periodic non-blocking heartbeat pulse on Green LED (40ms every 2 seconds)
  uint32_t now = millis();
  if (now - _last_heartbeat >= 2000) {
    _last_heartbeat = now;
    digitalWrite(LED_GREEN, HIGH);
    _heartbeat_off_time = now + 40;
  } else if (_heartbeat_off_time != 0 && now >= _heartbeat_off_time) {
    digitalWrite(LED_GREEN, LOW);
    _heartbeat_off_time = 0;
  }
}

void RAKWismeshTapBoard::sleep(uint32_t secs) {
  // Yield 1ms to keep FreeRTOS scheduler, UI rendering, touch polling, and mailbox IPC responsive
  delay(1);
}

void RAKWismeshTapBoard::onBeforeTransmit() {
  digitalWrite(LED_BLUE, HIGH); // Blue LED ON during LoRa transmit
}

void RAKWismeshTapBoard::onAfterTransmit() {
  digitalWrite(LED_BLUE, LOW);  // Blue LED OFF after LoRa transmit
}

void RAKWismeshTapBoard::onBootComplete() {
  Serial.println("[BOOT] MeshCore boot sequence completed successfully! UI ready.");
  // Visual signal: 2 rapid green blinks
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_GREEN, HIGH);
    delay(70);
    digitalWrite(LED_GREEN, LOW);
    delay(70);
  }
}
