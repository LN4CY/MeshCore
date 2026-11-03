#include <Arduino.h>
#include <Wire.h>
#include <InternalFileSystem.h>
#include <bluefruit.h>
#include <time.h>

extern unsigned int encode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);
extern unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);
#include "nrf_gpio.h"
#include "RAKWismeshTapBoard.h"
#include "target.h"

#ifdef DISPLAY_CLASS
#include "../../examples/companion_radio/MyMesh.h"
#include "../../examples/companion_radio/DataStore.h"
#include "../../examples/companion_radio/ui-tap/UITask.h"
extern MyMesh the_mesh;
extern DataStore store;
extern UITask ui_task;
#endif

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

void RAKWismeshTapBoard::setBacklight(bool on) {
  _backlight_on = on;
  pinMode(PIN_TFT_BL, OUTPUT);
  if (!on || _brightness_pct == 0) {
    analogWrite(PIN_TFT_BL, 0);
    digitalWrite(PIN_TFT_BL, LOW);
  } else if (_brightness_pct >= 100) {
    analogWrite(PIN_TFT_BL, 255);
  } else {
    uint32_t pwm = ((uint32_t)_brightness_pct * 255UL) / 100UL;
    if (pwm < 8) pwm = 8;
    analogWrite(PIN_TFT_BL, (uint8_t)pwm);
  }
}

void RAKWismeshTapBoard::setBacklightBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  _brightness_pct = pct;
  if (_backlight_on) {
    setBacklight(true);
  }
}

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

#if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

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
  Serial.println("[CLI] Type 'help' for all MeshCore & WisMesh Tap commands.");
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

void RAKWismeshTapBoard::powerOff() {
  Serial.println("[POWER] User power off requested");
  analogWrite(PIN_TFT_BL, 0);
  HwPWM0.stop();
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);

#if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
  // Wait for the user button to be released (active LOW), otherwise the
  // wake-on-LOW sense would immediately wake us back up.
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(PIN_USER_BTN) == LOW && millis() - t0 < 10000) {
    delay(10);
  }
  delay(50); // debounce
#endif

  setSlotPower(false);

  NRF52Board::shutdownPeripherals();
  digitalWrite(SX126X_POWER_EN, LOW);

#if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
  // Arm wake from SYSTEMOFF on user button press
  nrf_gpio_cfg_sense_input(PIN_USER_BTN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_system_off();
  } else {
    NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
  }
  while (1) { }
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

bool RAKWismeshTapBoard::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
#ifdef DISPLAY_CLASS
  while (*command == ' ') command++;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  TapUI& ui = ui_task.getUI();

  // ---------------- HELP / LIST COMMANDS ----------------
  if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    Serial.println("\r\n=== MeshCore & WisMesh Tap CLI Commands ===");
    Serial.println("System:   ver | board | status | reboot | poweroff | start ota | erase | save | apply");
    Serial.println("Stats:    stats-core | stats-radio | stats-packets | clear stats");
    Serial.println("Clock:    clock | clock sync | clock gps | clock set <YYYY-MM-DD HH:MM:SS> | clock adjust <sec> | time <epoch>");
    Serial.println("Mesh/RF:  advert | advert.zerohop | presets | preset <0-26|name> | hash [1b|2b|3b]");
    Serial.println("Channels: channels | channel get <0-7> | channel set <0-7> <name> [hex/b64_key]");
    Serial.println("          channel hashtag <0-7> <#tag> | channel public <0-7> | channel random <0-7> [name]");
    Serial.println("          channel clear <0-7> | channel use <0-7>");
    Serial.println("Contacts: contacts | contact fav <name|idx> [on|off] | contact remove <idx>");
    Serial.println("Messages: messages [all|unread] | send chan <0-7> <msg> | send dm <name|idx> <msg>");
    Serial.println("Device:   ble [on|off|status|unpair|pin <6digits>] | brightness [5-100] | sleep [0|30|60|120|300] | theme [dark|light]");
    Serial.println("GPS/Sens: gps [on|off|sync|setloc <lat> <lon>|baud <rate>|swap] | sensor list | sensor get <k> | sensor set <k> <v>");
    Serial.println("Get/Set:  get/set name|freq|sf|bw|cr|tx|hash|radio|lat|lon|ble.pin|ble|brightness|sleep|theme|gps.baud|gps.swap");
    Serial.println("          get/set af|int.thresh|agc.reset.interval|multi.acks|rxdelay|txdelay|direct.txdelay|dutycycle");
    Serial.println("          get/set cad|path.hash.mode|adc.multiplier|prv.key|pub.key|manual.add.contacts|client.repeat");
    strcpy(reply, "OK - full command reference printed to Serial. Key cmds: status, channels, contacts, ble, clock, brightness, sleep, get/set, apply");
    return true;
  }

  // ---------------- SYSTEM & STATS COMMANDS ----------------
  if (memcmp(command, "reboot", 6) == 0) {
    the_mesh.savePrefs();
    ui.saveDeviceSettings();
    board.reboot();
    return true;
  }
  if (memcmp(command, "poweroff", 8) == 0 || memcmp(command, "shutdown", 8) == 0) {
    ui.powerOff();
    return true;
  }
  if (memcmp(command, "ver", 3) == 0) {
    snprintf(reply, 160, "%s (Build: %s) - RAK WisMesh Tap Standalone", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
    return true;
  }
  if (memcmp(command, "board", 5) == 0) {
    snprintf(reply, 160, "%s (nRF52840 + SX1262 + ST7789 + FT6336U)", getManufacturerName());
    return true;
  }
  if (memcmp(command, "start ota", 9) == 0 || strcmp(command, "dfu") == 0) {
    if (startOTAUpdate(prefs->node_name, reply)) {
      return true;
    }
    strcpy(reply, "Error starting OTA/DFU");
    return true;
  }
  if (memcmp(command, "erase", 5) == 0) {
    bool s = InternalFS.format();
    snprintf(reply, 80, "File system erase: %s", s ? "OK" : "Err");
    return true;
  }
  if (strcmp(command, "save") == 0) {
    the_mesh.savePrefs();
    ui.saveDeviceSettings();
    ui.persistChannels();
    strcpy(reply, "OK - all prefs, device settings, and channels saved");
    return true;
  }
  if (strcmp(command, "apply") == 0) {
    the_mesh.savePrefs();
    radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
    radio_driver.setTxPower(prefs->tx_power_dbm);
    ui.syncRadioParamsFromPrefs();
    ui.markNeedsRedraw();
    snprintf(reply, 140, "OK - Applied radio %.3f MHz, BW %.1f kHz, SF%u, CR 4/%u, TX %u dBm",
             prefs->freq, prefs->bw, prefs->sf, prefs->cr, prefs->tx_power_dbm);
    return true;
  }
  if (strcmp(command, "status") == 0) {
    char dtStr[32];
    ui.formatLocalDateTime(dtStr, sizeof(dtStr), true, false);
    uint16_t mv = getBattMilliVolts();
    uint8_t bpct = TapUI::calcBatteryPercent(mv);
    snprintf(reply, 160, "Node:%s | %.3fMHz SF%u BW%.1f TX%udBm %uB | Batt:%u%%(%umV) | BLE:%s(PIN:%06lu) | Theme:%s | %s UTC",
             prefs->node_name, prefs->freq, prefs->sf, prefs->bw, prefs->tx_power_dbm, prefs->path_hash_mode + 1,
             bpct, mv, ui.isBLEEnabled() ? "ON" : "OFF", (unsigned long)the_mesh.getBLEPin(),
             ui.isDarkMode() ? "DARK" : "LIGHT", dtStr);
    return true;
  }
  if (memcmp(command, "stats-core", 10) == 0) {
    snprintf(reply, 160, "battery_mv=%u uptime_secs=%lu",
             getBattMilliVolts(), (unsigned long)(millis() / 1000));
    return true;
  }
  if (memcmp(command, "stats-radio", 11) == 0) {
    snprintf(reply, 160, "noise_floor=%d last_rssi=%d last_snr=%.2f tx_air_secs=%lu rx_air_secs=%lu",
             (int16_t)radio_driver.getNoiseFloor(), (int16_t)radio_driver.getLastRSSI(),
             radio_driver.getLastSNR(),
             (unsigned long)(the_mesh.getTotalAirTime() / 1000),
             (unsigned long)(the_mesh.getReceiveAirTime() / 1000));
    return true;
  }
  if (memcmp(command, "stats-packets", 13) == 0) {
    snprintf(reply, 160, "recv=%lu sent=%lu flood_tx=%lu direct_tx=%lu flood_rx=%lu direct_rx=%lu recv_errors=%lu",
             (unsigned long)radio_driver.getPacketsRecv(), (unsigned long)radio_driver.getPacketsSent(),
             (unsigned long)the_mesh.getNumSentFlood(), (unsigned long)the_mesh.getNumSentDirect(),
             (unsigned long)the_mesh.getNumRecvFlood(), (unsigned long)the_mesh.getNumRecvDirect(),
             (unsigned long)radio_driver.getPacketsRecvErrors());
    return true;
  }
  if (memcmp(command, "clear stats", 11) == 0) {
    the_mesh.resetStats();
    radio_driver.resetStats();
    strcpy(reply, "(stats cleared)");
    return true;
  }

  // ---------------- ADVERT & PRESETS ----------------
  if (strcmp(command, "advert") == 0) {
    the_mesh.advert();
    strcpy(reply, "OK - Advert broadcasted");
    return true;
  }
  if (strcmp(command, "advert.zerohop") == 0) {
    uint8_t cmd_bytes[6] = { 0x02, 0xFF, 0, 0, 0, 0 };
    the_mesh.getRNG()->random(&cmd_bytes[2], 4);
    mesh::Packet* req = the_mesh.createControlData(cmd_bytes, sizeof(cmd_bytes));
    if (req) the_mesh.sendZeroHop(req);
    strcpy(reply, "OK - Zero-hop discovery probe sent");
    return true;
  }
  if (strcmp(command, "presets") == 0) {
    Serial.println("\r\n=== Official MeshCore RF Presets (api.meshcore.nz) ===");
    for (int i = 0; i < TapUI::getNumPresets() - 1; i++) {
      const RFPreset& p = TapUI::getPreset(i);
      uint8_t hb = p.path_hash_size ? p.path_hash_size : 1;
      Serial.printf(" [%2d] %-24s : %.3f MHz, SF%u, BW %.1f kHz, CR 4/%u (%uB hash)\r\n",
                    i, p.name, p.freq, p.sf, p.bw, p.cr, hb);
    }
    strcpy(reply, "27 official MeshCore presets (0..26) printed to Serial. E.g. 'preset 23' (USA), 'preset 6' (Canada 3B), 'preset 8' (EU/UK Narrow)");
    return true;
  }
  if (memcmp(command, "preset ", 7) == 0) {
    const char* arg = command + 7;
    while (*arg == ' ') arg++;
    int idx = -1;
    if (*arg >= '0' && *arg <= '9') {
      idx = atoi(arg);
    } else {
      // Exact case-insensitive match first, then substring match
      for (int i = 0; i < TapUI::getNumPresets() - 1; i++) {
        if (strcasecmp(arg, TapUI::getPreset(i).name) == 0) {
          idx = i;
          break;
        }
      }
      if (idx < 0) {
        for (int i = 0; i < TapUI::getNumPresets() - 1; i++) {
          if (strncasecmp(arg, TapUI::getPreset(i).name, strlen(arg)) == 0) {
            idx = i;
            break;
          }
        }
      }
    }
    if (idx >= 0 && idx < TapUI::getNumPresets() - 1) {
      ui.applyRFPreset(idx);
      ui.applyAndSaveRadioParams();
      const RFPreset& p = TapUI::getPreset(idx);
      uint8_t hb = p.path_hash_size ? p.path_hash_size : 1;
      snprintf(reply, 150, "OK - Preset [%d] %s applied (%.3f MHz, SF%u, BW %.1f kHz, CR 4/%u, %uB hash)",
               idx, p.name, p.freq, p.sf, p.bw, p.cr, hb);
    } else {
      strcpy(reply, "Err - specify preset index 0..26 or name (type 'presets' for list)");
    }
    return true;
  }

  // ---------------- CLOCK & TIME COMMANDS ----------------
  if (memcmp(command, "time ", 5) == 0) {
    uint32_t secs = (uint32_t)atol(&command[5]);
    uint32_t curr = the_mesh.getRTCClock()->getCurrentTime();
    if (secs > curr) {
      the_mesh.getRTCClock()->setCurrentTime(secs);
      ui.markNeedsRedraw();
      strcpy(reply, "(time set)");
    } else {
      strcpy(reply, "ERR: clock cannot go backwards (use 'clock set YYYY-MM-DD HH:MM:SS' to override)");
    }
    return true;
  }
  if (memcmp(command, "clock", 5) == 0) {
    const char* sub = command + 5;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      char dtStr[36];
      ui.formatLocalDateTime(dtStr, sizeof(dtStr), true, true);
      snprintf(reply, 120, "%s (epoch: %lu)", dtStr, (unsigned long)the_mesh.getRTCClock()->getCurrentTime());
      return true;
    }
    if (memcmp(sub, "sync", 4) == 0) {
      uint32_t curr = the_mesh.getRTCClock()->getCurrentTime();
      if (sender_timestamp > curr) {
        the_mesh.getRTCClock()->setCurrentTime(sender_timestamp + 1);
        ui.markNeedsRedraw();
        strcpy(reply, "OK - clock synced from sender timestamp");
      } else if (ui.syncClockFromGPS()) {
        strcpy(reply, "OK - clock synced from GPS");
      } else {
        strcpy(reply, "ERR: no newer timestamp or valid GPS fix available");
      }
      return true;
    }
    if (memcmp(sub, "gps", 3) == 0) {
      if (ui.syncClockFromGPS()) {
        char dtStr[36];
        ui.formatLocalDateTime(dtStr, sizeof(dtStr), true, true);
        snprintf(reply, 120, "OK - synced from GPS: %s", dtStr);
      } else {
        strcpy(reply, "ERR: GPS does not have a valid time fix yet");
      }
      return true;
    }
    if (memcmp(sub, "set ", 4) == 0) {
      const char* dt = sub + 4;
      while (*dt == ' ') dt++;
      if (*dt >= '1' && *dt <= '9' && strchr(dt, '-') == nullptr && strchr(dt, '/') == nullptr) {
        uint32_t ep = (uint32_t)strtoul(dt, nullptr, 10);
        if (ep > 1500000000UL) {
          the_mesh.getRTCClock()->setCurrentTime(ep);
          ui.markNeedsRedraw();
          strcpy(reply, "OK - epoch time set");
          return true;
        }
      }
      if (ui.parseAndSetLocalDateTime(dt)) {
        char dtStr[36];
        ui.formatLocalDateTime(dtStr, sizeof(dtStr), true, true);
        snprintf(reply, 120, "OK - clock set to %s", dtStr);
      } else {
        strcpy(reply, "ERR: format must be YYYY-MM-DD HH:MM:SS or unix epoch");
      }
      return true;
    }
    if (memcmp(sub, "adjust ", 7) == 0) {
      int32_t delta = atol(sub + 7);
      ui.adjustClockSeconds(delta);
      char dtStr[36];
      ui.formatLocalDateTime(dtStr, sizeof(dtStr), true, true);
      snprintf(reply, 120, "OK - adjusted by %ld s -> %s", (long)delta, dtStr);
      return true;
    }
  }

  // ---------------- CHANNELS COMMANDS ----------------
  if (strcmp(command, "channels") == 0 || strcmp(command, "channel list") == 0) {
    Serial.println("\r\n=== Configured Group Channels (0..7) ===");
    int pos = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      memset(&ch, 0, sizeof(ch));
      the_mesh.getChannel(i, ch);
      unsigned char b64[50];
      memset(b64, 0, sizeof(b64));
      encode_base64(ch.channel.secret, 16, b64);
      char hexKey[36];
      mesh::Utils::toHex(hexKey, ch.channel.secret, 16);
      bool active = (ui.getActiveChannel() == i);
      Serial.printf(" Ch #%d %s: Name='%s' | PSK(b64)=%s | Hex=%s\r\n",
                    i, active ? "[ACTIVE]" : "        ",
                    ch.name[0] ? ch.name : "(empty)", (const char*)b64, hexKey);
      if (ch.name[0] != 0 && pos < 130) {
        pos += snprintf(reply + pos, 155 - pos, "%s#%d:%s", (pos > 0) ? ", " : "", i, ch.name);
      }
    }
    if (pos == 0) strcpy(reply, "No channels named");
    return true;
  }
  if (memcmp(command, "channel ", 8) == 0) {
    const char* sub = command + 8;
    while (*sub == ' ') sub++;

    if (memcmp(sub, "get ", 4) == 0) {
      int idx = atoi(sub + 4);
      if (idx < 0 || idx >= MAX_GROUP_CHANNELS) {
        strcpy(reply, "ERR: channel index must be 0..7");
        return true;
      }
      ChannelDetails ch;
      memset(&ch, 0, sizeof(ch));
      the_mesh.getChannel(idx, ch);
      unsigned char b64[50];
      memset(b64, 0, sizeof(b64));
      encode_base64(ch.channel.secret, 16, b64);
      char hexKey[36];
      mesh::Utils::toHex(hexKey, ch.channel.secret, 16);
      snprintf(reply, 160, "Ch #%d: name='%s' b64=%s hex=%s",
               idx, ch.name[0] ? ch.name : "(empty)", (const char*)b64, hexKey);
      return true;
    }
    if (memcmp(sub, "use ", 4) == 0) {
      int idx = atoi(sub + 4);
      if (idx >= 0 && idx < MAX_GROUP_CHANNELS) {
        ui.setActiveChannel((uint8_t)idx);
        ChannelDetails ch;
        the_mesh.getChannel(idx, ch);
        snprintf(reply, 100, "OK - Active UI channel set to #%d (%s)", idx, ch.name[0] ? ch.name : "Public");
      } else {
        strcpy(reply, "ERR: channel index must be 0..7");
      }
      return true;
    }
    if (memcmp(sub, "hashtag ", 8) == 0) {
      const char* p = sub + 8;
      while (*p == ' ') p++;
      int idx = atoi(p);
      const char* tag = strchr(p, ' ');
      if (tag) {
        while (*tag == ' ') tag++;
        if (ui.setChannelHashtag((uint8_t)idx, tag)) {
          ChannelDetails ch;
          the_mesh.getChannel(idx, ch);
          char hexKey[36];
          mesh::Utils::toHex(hexKey, ch.channel.secret, 16);
          snprintf(reply, 150, "OK - Ch #%d set to %s (SHA256 key: %s)", idx, ch.name, hexKey);
          return true;
        }
      }
      strcpy(reply, "Usage: channel hashtag <0-7> <#name>");
      return true;
    }
    if (memcmp(sub, "public ", 7) == 0) {
      int idx = atoi(sub + 7);
      if (ui.setChannelPublic((uint8_t)idx)) {
        snprintf(reply, 100, "OK - Ch #%d set to Public (default MeshCore PSK)", idx);
      } else {
        strcpy(reply, "ERR: channel index must be 0..7");
      }
      return true;
    }
    if (memcmp(sub, "random ", 7) == 0) {
      const char* p = sub + 7;
      while (*p == ' ') p++;
      int idx = atoi(p);
      const char* nm = strchr(p, ' ');
      if (nm) { while (*nm == ' ') nm++; }
      if (ui.setChannelRandom((uint8_t)idx, nm)) {
        ChannelDetails ch;
        the_mesh.getChannel(idx, ch);
        unsigned char b64[50];
        memset(b64, 0, sizeof(b64));
        encode_base64(ch.channel.secret, 16, b64);
        snprintf(reply, 150, "OK - Ch #%d '%s' generated random PSK: %s", idx, ch.name, (const char*)b64);
      } else {
        strcpy(reply, "ERR: channel index must be 0..7");
      }
      return true;
    }
    if (memcmp(sub, "clear ", 6) == 0 || memcmp(sub, "remove ", 7) == 0) {
      const char* p = strchr(sub, ' ');
      int idx = p ? atoi(p + 1) : -1;
      if (idx >= 0 && idx < MAX_GROUP_CHANNELS && ui.clearChannel((uint8_t)idx)) {
        snprintf(reply, 100, "OK - Ch #%d cleared", idx);
      } else {
        strcpy(reply, "ERR: channel index must be 0..7");
      }
      return true;
    }
    if (memcmp(sub, "set ", 4) == 0) {
      const char* p = sub + 4;
      while (*p == ' ') p++;
      int idx = atoi(p);
      const char* nmStart = strchr(p, ' ');
      if (!nmStart || idx < 0 || idx >= MAX_GROUP_CHANNELS) {
        strcpy(reply, "Usage: channel set <0-7> <name> [32hex_or_base64_key]");
        return true;
      }
      while (*nmStart == ' ') nmStart++;
      char nameBuf[32];
      const char* keyStart = strchr(nmStart, ' ');
      if (!keyStart) {
        // Only name given -> if starts with #, compute hashtag SHA256 key; if "Public", set public
        if (strcasecmp(nmStart, "Public") == 0) {
          ui.setChannelPublic((uint8_t)idx);
          snprintf(reply, 120, "OK - Ch #%d set to Public", idx);
          return true;
        }
        if (ui.setChannelHashtag((uint8_t)idx, nmStart)) {
          ChannelDetails ch;
          the_mesh.getChannel(idx, ch);
          snprintf(reply, 120, "OK - Ch #%d set to hashtag %s", idx, ch.name);
          return true;
        }
        strcpy(reply, "ERR: failed to set channel");
        return true;
      }
      size_t nlen = (size_t)(keyStart - nmStart);
      if (nlen >= sizeof(nameBuf)) nlen = sizeof(nameBuf) - 1;
      memcpy(nameBuf, nmStart, nlen);
      nameBuf[nlen] = 0;
      while (*keyStart == ' ') keyStart++;

      if (ui.setChannelCustomKey((uint8_t)idx, nameBuf, keyStart)) {
        snprintf(reply, 120, "OK - Ch #%d '%s' updated & saved", idx, nameBuf);
      } else {
        strcpy(reply, "ERR: invalid key (provide 32/64-char hex or Base64 PSK)");
      }
      return true;
    }
  }

  // ---------------- CONTACTS, FAVORITES & MESSAGING COMMANDS ----------------
  if (strcmp(command, "contacts") == 0) {
    int total = the_mesh.getTotalContactSlots();
    int count = 0;
    int favCount = 0;
    Serial.println("\r\n=== Discovered Mesh Contacts (Favorites First) ===");
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 8; i < total; i++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(i, c) || c.type == 0) continue;
        bool fav = c.isFav();
        if ((pass == 0 && !fav) || (pass == 1 && fav)) continue;
        if (fav) favCount++;
        char idHex[12];
        mesh::Utils::toHex(idHex, c.id.pub_key, 4);
        Serial.printf(" [%d] %sName='%s' | ID=!%s | Type=%u | Flags=0x%02X | Lat=%.4f Lon=%.4f\r\n",
                      i, fav ? "[*FAV] " : "       ", c.name, idHex, c.type, c.flags,
                      (double)c.gps_lat / 1e6, (double)c.gps_lon / 1e6);
        count++;
      }
    }
    snprintf(reply, 140, "%d contacts (%d favorites) listed on Serial", count, favCount);
    return true;
  }
  if (memcmp(command, "contact fav ", 12) == 0 || memcmp(command, "fav ", 4) == 0) {
    const char* p = (memcmp(command, "fav ", 4) == 0) ? (command + 4) : (command + 12);
    while (*p == ' ') p++;
    if (*p == 0) {
      strcpy(reply, "Usage: contact fav <name_or_idx> [on|off]");
      return true;
    }
    char target[32];
    const char* opt = strchr(p, ' ');
    if (opt) {
      size_t tlen = (size_t)(opt - p);
      if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
      memcpy(target, p, tlen);
      target[tlen] = 0;
      while (*opt == ' ') opt++;
    } else {
      strncpy(target, p, sizeof(target) - 1);
      target[sizeof(target) - 1] = 0;
    }

    int total = the_mesh.getTotalContactSlots();
    int foundIdx = -1;
    if (target[0] >= '0' && target[0] <= '9') {
      foundIdx = atoi(target);
    } else {
      for (int i = 8; i < total; i++) {
        ContactInfo c;
        if (the_mesh.getContactByIdx(i, c) && c.type != 0 && strcasecmp(c.name, target) == 0) {
          foundIdx = i;
          break;
        }
      }
    }

    ContactInfo c;
    if (foundIdx >= 8 && the_mesh.getContactByIdx(foundIdx, c) && c.type != 0) {
      bool newFav = !c.isFav();
      if (opt && *opt) {
        newFav = (strcasecmp(opt, "on") == 0 || strcmp(opt, "1") == 0 || strcasecmp(opt, "true") == 0);
      }
      ui.setContactFavorite(foundIdx, newFav);
      snprintf(reply, 120, "OK - Contact [%d] '%s' favorite set to %s",
               foundIdx, c.name, newFav ? "ON (*FAV)" : "OFF");
    } else {
      strcpy(reply, "ERR: contact not found (use 'contacts' to see list)");
    }
    return true;
  }
  if (memcmp(command, "messages", 8) == 0 || strcmp(command, "inbox") == 0) {
    const char* sub = (memcmp(command, "messages", 8) == 0) ? (command + 8) : "";
    while (*sub == ' ') sub++;
    bool onlyUnread = (strcasecmp(sub, "unread") == 0 || strcasecmp(sub, "new") == 0);
    uint8_t total = ui.getMessageCount();
    int unread = ui.getTotalUnreadCount();
    Serial.printf("\r\n=== Messages (%u stored, %d unread) ===\r\n", total, unread);
    for (uint8_t i = 0; i < total; i++) {
      const UIMessage& m = ui.getMessage(i);
      if (onlyUnread && !m.unread) continue;
      Serial.printf(" [%2u] %s %s (%s): %s\r\n",
                    i, m.unread ? "[UNREAD]" : "        ",
                    m.sender, m.is_direct ? "DM" : m.target_name, m.text);
    }
    if (strcasecmp(sub, "read") == 0 || strcasecmp(sub, "clear-unread") == 0) {
      ui.markAllMessagesRead();
      strcpy(reply, "OK - All messages marked as read");
    } else {
      snprintf(reply, 140, "%u messages in history (%d unread) - printed to Serial", total, unread);
    }
    return true;
  }
  if (memcmp(command, "contact remove ", 15) == 0) {
    int idx = atoi(command + 15);
    ContactInfo c;
    if (the_mesh.getContactByIdx(idx, c)) {
      the_mesh.removeContact(c);
      ui.markNeedsRedraw();
      snprintf(reply, 100, "OK - Contact [%d] '%s' removed", idx, c.name);
    } else {
      strcpy(reply, "ERR: contact index not found");
    }
    return true;
  }
  if (memcmp(command, "send chan ", 10) == 0) {
    const char* p = command + 10;
    while (*p == ' ') p++;
    int idx = atoi(p);
    const char* msg = strchr(p, ' ');
    if (msg && idx >= 0 && idx < MAX_GROUP_CHANNELS) {
      while (*msg == ' ') msg++;
      if (*msg) {
        ui.setActiveChannel((uint8_t)idx);
        ui.sendMessage(msg);
        snprintf(reply, 120, "OK - Sent to Ch #%d: %s", idx, msg);
        return true;
      }
    }
    strcpy(reply, "Usage: send chan <0-7> <message>");
    return true;
  }
  if (memcmp(command, "send dm ", 8) == 0) {
    const char* p = command + 8;
    while (*p == ' ') p++;
    const char* msg = strchr(p, ' ');
    if (!msg) {
      strcpy(reply, "Usage: send dm <contact_name_or_idx> <message>");
      return true;
    }
    char target[32];
    size_t tlen = (size_t)(msg - p);
    if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
    memcpy(target, p, tlen);
    target[tlen] = 0;
    while (*msg == ' ') msg++;

    int total = the_mesh.getTotalContactSlots();
    int foundIdx = -1;
    if (target[0] >= '0' && target[0] <= '9') {
      foundIdx = atoi(target);
    } else {
      for (int i = 8; i < total; i++) {
        ContactInfo c;
        if (the_mesh.getContactByIdx(i, c) && c.type != 0 && strcasecmp(c.name, target) == 0) {
          foundIdx = i;
          break;
        }
      }
    }
    ContactInfo c;
    if (foundIdx >= 0 && the_mesh.getContactByIdx(foundIdx, c) && c.type != 0) {
      uint32_t ack = 0, timeout = 0;
      the_mesh.sendMessage(c, the_mesh.getRTCClock()->getCurrentTimeUnique(), 0, msg, ack, timeout);
      snprintf(reply, 120, "OK - Direct message sent to '%s'", c.name);
    } else {
      strcpy(reply, "ERR: contact not found");
    }
    return true;
  }

  // ---------------- BLE, BRIGHTNESS & SLEEP SHORTCUTS ----------------
  if (memcmp(command, "ble", 3) == 0 && (command[3] == 0 || command[3] == ' ')) {
    const char* sub = command + 3;
    while (*sub == ' ') sub++;
    if (*sub == 0 || strcmp(sub, "status") == 0) {
      snprintf(reply, 120, "BLE: %s | Connected: %s | PIN: %06lu",
               ui.isBLEEnabled() ? "ON" : "OFF",
               ui_task.hasConnection() ? "YES" : "NO",
               (unsigned long)the_mesh.getBLEPin());
      return true;
    }
    if (strcmp(sub, "on") == 0) {
      ui.setBLEEnabled(true, true);
      strcpy(reply, "OK - BLE enabled");
      return true;
    }
    if (strcmp(sub, "off") == 0) {
      ui.setBLEEnabled(false, true);
      strcpy(reply, "OK - BLE disabled");
      return true;
    }
    if (strcmp(sub, "unpair") == 0 || strcmp(sub, "clearbonds") == 0) {
      ui.clearBLEBonds();
      strcpy(reply, "OK - BLE peripheral bonds cleared");
      return true;
    }
    if (memcmp(sub, "pin ", 4) == 0) {
      uint32_t pin = (uint32_t)atol(sub + 4);
      if (pin >= 100000 && pin <= 999999) {
        ui.setBLEPinLive(pin);
        snprintf(reply, 100, "OK - BLE PIN set to %06lu", (unsigned long)pin);
      } else {
        strcpy(reply, "ERR: BLE PIN must be a 6-digit number (100000..999999)");
      }
      return true;
    }
  }
  if (memcmp(command, "brightness", 10) == 0) {
    const char* sub = command + 10;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      snprintf(reply, 80, "> %u%%", ui.getBrightnessPct());
      return true;
    }
    int pct = atoi(sub);
    ui.setBrightnessPct((uint8_t)pct, true);
    snprintf(reply, 80, "OK - Brightness set to %u%%", ui.getBrightnessPct());
    return true;
  }
  if (memcmp(command, "sleep", 5) == 0 && (command[5] == 0 || command[5] == ' ')) {
    const char* sub = command + 5;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      snprintf(reply, 80, "> %lu seconds (0=never)", (unsigned long)ui.getScreenSleepSeconds());
      return true;
    }
    uint32_t secs = (strcasecmp(sub, "never") == 0 || strcasecmp(sub, "off") == 0) ? 0 : (uint32_t)atol(sub);
    ui.setScreenSleepSeconds(secs, true);
    snprintf(reply, 80, "OK - Screen sleep timeout set to %lu s", (unsigned long)secs);
    return true;
  }
  if (memcmp(command, "theme", 5) == 0 && (command[5] == 0 || command[5] == ' ')) {
    const char* sub = command + 5;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      snprintf(reply, 60, "> %s", ui.isDarkMode() ? "dark" : "light");
      return true;
    }
    bool dark = !(strcasecmp(sub, "light") == 0 || strcasecmp(sub, "lite") == 0 || strcmp(sub, "0") == 0);
    ui.setDarkMode(dark, true);
    snprintf(reply, 80, "OK - UI theme set to %s", dark ? "DARK" : "LIGHT");
    return true;
  }
  if (memcmp(command, "hash", 4) == 0 && (command[4] == 0 || command[4] == ' ')) {
    const char* sub = command + 4;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      snprintf(reply, 60, "> %uB (path.hash.mode=%u)", prefs->path_hash_mode + 1, prefs->path_hash_mode);
      return true;
    }
    int val = atoi(sub);
    uint8_t mode = 0xFF;
    if (strcasecmp(sub, "1b") == 0 || val == 1) mode = 0;
    else if (strcasecmp(sub, "2b") == 0 || val == 2) mode = 1;
    else if (strcasecmp(sub, "3b") == 0 || val == 3) mode = 2;
    if (mode <= 2) {
      ui.setPathHashMode(mode);
      snprintf(reply, 80, "OK - Path hash size set to %uB (mode %u)", mode + 1, mode);
    } else {
      strcpy(reply, "Usage: hash <1b|2b|3b>");
    }
    return true;
  }

  // ---------------- GPS & SENSOR COMMANDS ----------------
  if (memcmp(command, "gps", 3) == 0 && (command[3] == 0 || command[3] == ' ')) {
    LocationProvider* loc = sensors.getLocationProvider();
    const char* sub = command + 3;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      if (!loc) {
        strcpy(reply, "GPS provider unavailable");
        return true;
      }
      char diag[64];
      loc->formatDiagnostics(diag, sizeof(diag));
      snprintf(reply, 160, "%s | lat=%.6f lon=%.6f alt=%ldm | baud=%lu pins=%s",
               diag, (double)loc->getLatitude() / 1e6, (double)loc->getLongitude() / 1e6,
               loc->getAltitude(), (unsigned long)ui.getGPSBaud(),
               ui.areGPSPinsSwapped() ? "RX16/TX15" : "RX15/TX16");
      return true;
    }
    if (strcmp(sub, "on") == 0) {
      if (loc) loc->begin();
      prefs->gps_enabled = 1;
      the_mesh.savePrefs();
      ui.markNeedsRedraw();
      strcpy(reply, "OK - GPS enabled");
      return true;
    }
    if (strcmp(sub, "off") == 0) {
      if (loc) loc->stop();
      prefs->gps_enabled = 0;
      the_mesh.savePrefs();
      ui.markNeedsRedraw();
      strcpy(reply, "OK - GPS disabled");
      return true;
    }
    if (strcmp(sub, "sync") == 0) {
      if (ui.syncClockFromGPS()) strcpy(reply, "OK - Clock synced from GPS");
      else strcpy(reply, "ERR - Waiting for valid GPS time fix");
      return true;
    }
    if (memcmp(sub, "baud ", 5) == 0) {
      uint32_t b = (uint32_t)atol(sub + 5);
      if (b >= 4800 && b <= 921600) {
        ui.setGPSBaud(b, true);
        snprintf(reply, 80, "OK - GPS baud set to %lu", (unsigned long)b);
      } else {
        strcpy(reply, "ERR: invalid baud rate");
      }
      return true;
    }
    if (strcmp(sub, "swap") == 0) {
      ui.setGPSPinsSwapped(!ui.areGPSPinsSwapped(), true);
      snprintf(reply, 80, "OK - GPS pins set to %s", ui.areGPSPinsSwapped() ? "RX16/TX15" : "RX15/TX16");
      return true;
    }
    if (memcmp(sub, "setloc ", 7) == 0) {
      double lat = 0.0, lon = 0.0;
      if (sscanf(sub + 7, "%lf %lf", &lat, &lon) == 2) {
        prefs->node_lat = lat;
        prefs->node_lon = lon;
        the_mesh.savePrefs();
        snprintf(reply, 100, "OK - Node coordinates set to %.6f, %.6f", lat, lon);
      } else {
        strcpy(reply, "Usage: gps setloc <lat> <lon>");
      }
      return true;
    }
  }
  if (memcmp(command, "sensor ", 7) == 0) {
    const char* sub = command + 7;
    if (memcmp(sub, "get ", 4) == 0) {
      const char* val = sensors.getSettingByKey(sub + 4);
      if (val) snprintf(reply, 120, "> %s", val);
      else strcpy(reply, "null");
      return true;
    }
    if (memcmp(sub, "set ", 4) == 0) {
      const char* k = sub + 4;
      const char* v = strchr(k, ' ');
      if (!v) {
        strcpy(reply, "Usage: sensor set <key> <value>");
        return true;
      }
      char keyBuf[32];
      size_t klen = (size_t)(v - k);
      if (klen >= sizeof(keyBuf)) klen = sizeof(keyBuf) - 1;
      memcpy(keyBuf, k, klen);
      keyBuf[klen] = 0;
      while (*v == ' ') v++;
      if (sensors.setSettingValue(keyBuf, v)) strcpy(reply, "ok");
      else strcpy(reply, "can't find custom var");
      return true;
    }
    if (memcmp(sub, "list", 4) == 0) {
      int n = sensors.getNumSettings();
      if (n == 0) {
        strcpy(reply, "No custom sensor variables registered");
        return true;
      }
      int pos = 0;
      for (int i = 0; i < n && pos < 140; i++) {
        pos += snprintf(reply + pos, 155 - pos, "%s%s=%s", (i > 0) ? ", " : "",
                        sensors.getSettingName(i), sensors.getSettingValue(i));
      }
      return true;
    }
  }

  // ---------------- STANDARD MESHCORE GET / SET ----------------
  if (memcmp(command, "get ", 4) == 0) {
    const char* config = &command[4];
    while (*config == ' ') config++;

    if (strcmp(config, "name") == 0) {
      snprintf(reply, 80, "> %s", prefs->node_name);
      return true;
    }
    if (strcmp(config, "lat") == 0) {
      snprintf(reply, 60, "> %.6f", prefs->node_lat);
      return true;
    }
    if (strcmp(config, "lon") == 0) {
      snprintf(reply, 60, "> %.6f", prefs->node_lon);
      return true;
    }
    if (strcmp(config, "sf") == 0) {
      snprintf(reply, 40, "> %u", prefs->sf);
      return true;
    }
    if (strcmp(config, "bw") == 0) {
      snprintf(reply, 40, "> %.1f", prefs->bw);
      return true;
    }
    if (strcmp(config, "cr") == 0) {
      snprintf(reply, 40, "> %u", prefs->cr);
      return true;
    }
    if (strcmp(config, "ble.pin") == 0 || strcmp(config, "pin") == 0) {
      snprintf(reply, 40, "> %06lu", (unsigned long)the_mesh.getBLEPin());
      return true;
    }
    if (strcmp(config, "ble") == 0) {
      snprintf(reply, 40, "> %s", ui.isBLEEnabled() ? "on" : "off");
      return true;
    }
    if (strcmp(config, "brightness") == 0) {
      snprintf(reply, 40, "> %u", ui.getBrightnessPct());
      return true;
    }
    if (strcmp(config, "sleep") == 0) {
      snprintf(reply, 40, "> %lu", (unsigned long)ui.getScreenSleepSeconds());
      return true;
    }
    if (strcmp(config, "theme") == 0) {
      snprintf(reply, 40, "> %s", ui.isDarkMode() ? "dark" : "light");
      return true;
    }
    if (strcmp(config, "hash") == 0 || strcmp(config, "path.hash") == 0) {
      snprintf(reply, 60, "> %uB (mode %u)", prefs->path_hash_mode + 1, prefs->path_hash_mode);
      return true;
    }
    if (strcmp(config, "gps.baud") == 0) {
      snprintf(reply, 40, "> %lu", (unsigned long)ui.getGPSBaud());
      return true;
    }
    if (strcmp(config, "gps.swap") == 0) {
      snprintf(reply, 40, "> %s", ui.areGPSPinsSwapped() ? "on" : "off");
      return true;
    }
    if (strcmp(config, "clock") == 0) {
      char dtStr[36];
      ui.formatLocalDateTime(dtStr, sizeof(dtStr), true, true);
      snprintf(reply, 80, "> %s", dtStr);
      return true;
    }
    if (strcmp(config, "adc.multiplier") == 0) {
      snprintf(reply, 40, "> %.3f", getAdcMultiplier());
      return true;
    }
    if (strcmp(config, "pub.key") == 0) {
      strcpy(reply, "> ");
      mesh::Utils::toHex(&reply[2], the_mesh.self_id.pub_key, PUB_KEY_SIZE);
      return true;
    }
    if (strcmp(config, "prv.key") == 0) {
      uint8_t prv_key[PRV_KEY_SIZE];
      int len = the_mesh.self_id.writeTo(prv_key, PRV_KEY_SIZE);
      strcpy(reply, "> ");
      mesh::Utils::toHex(&reply[2], prv_key, len);
      return true;
    }
    if (strcmp(config, "manual.add.contacts") == 0) {
      snprintf(reply, 40, "> %s", prefs->manual_add_contacts ? "on" : "off");
      return true;
    }
    if (strcmp(config, "autoadd.config") == 0) {
      snprintf(reply, 40, "> %u", prefs->autoadd_config);
      return true;
    }
    if (strcmp(config, "autoadd.maxhops") == 0) {
      snprintf(reply, 40, "> %u", prefs->autoadd_max_hops);
      return true;
    }
    if (strcmp(config, "telemetry.mode.base") == 0) {
      snprintf(reply, 40, "> %u", prefs->telemetry_mode_base);
      return true;
    }
    if (strcmp(config, "telemetry.mode.loc") == 0) {
      snprintf(reply, 40, "> %u", prefs->telemetry_mode_loc);
      return true;
    }
    if (strcmp(config, "telemetry.mode.env") == 0) {
      snprintf(reply, 40, "> %u", prefs->telemetry_mode_env);
      return true;
    }
    if (strcmp(config, "advert.loc.policy") == 0) {
      snprintf(reply, 40, "> %u", prefs->advert_loc_policy);
      return true;
    }
    if (strcmp(config, "client.repeat") == 0 || strcmp(config, "repeat") == 0) {
      snprintf(reply, 40, "> %s", prefs->isRepeatEn() ? "on" : "off");
      return true;
    }
  }

  if (memcmp(command, "set ", 4) == 0) {
    const char* config = &command[4];
    while (*config == ' ') config++;

    if (memcmp(config, "name ", 5) == 0) {
      const char* nm = config + 5;
      while (*nm == ' ') nm++;
      strncpy(prefs->node_name, nm, sizeof(prefs->node_name) - 1);
      prefs->node_name[sizeof(prefs->node_name) - 1] = 0;
      the_mesh.savePrefs();
      ui.markNeedsRedraw();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "lat ", 4) == 0) {
      prefs->node_lat = atof(config + 4);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "lon ", 4) == 0) {
      prefs->node_lon = atof(config + 4);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "freq ", 5) == 0) {
      float f = atof(config + 5);
      if (f >= 150.0f && f <= 960.0f) {
        prefs->freq = f;
        the_mesh.savePrefs();
        radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
        ui.syncRadioParamsFromPrefs();
        ui.markNeedsRedraw();
        strcpy(reply, "OK - Frequency applied & saved");
      } else {
        strcpy(reply, "ERR: frequency must be 150..960 MHz");
      }
      return true;
    }
    if (memcmp(config, "sf ", 3) == 0) {
      int sf = atoi(config + 3);
      if (sf >= 5 && sf <= 12) {
        prefs->sf = (uint8_t)sf;
        the_mesh.savePrefs();
        radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
        ui.syncRadioParamsFromPrefs();
        ui.markNeedsRedraw();
        strcpy(reply, "OK - SF applied & saved");
      } else {
        strcpy(reply, "ERR: SF must be 5..12");
      }
      return true;
    }
    if (memcmp(config, "bw ", 3) == 0) {
      float bw = atof(config + 3);
      if (bw >= 7.8f && bw <= 500.0f) {
        prefs->bw = bw;
        the_mesh.savePrefs();
        radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
        ui.syncRadioParamsFromPrefs();
        ui.markNeedsRedraw();
        strcpy(reply, "OK - BW applied & saved");
      } else {
        strcpy(reply, "ERR: BW must be 7.8..500.0 kHz");
      }
      return true;
    }
    if (memcmp(config, "cr ", 3) == 0) {
      int cr = atoi(config + 3);
      if (cr >= 5 && cr <= 8) {
        prefs->cr = (uint8_t)cr;
        the_mesh.savePrefs();
        radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
        ui.syncRadioParamsFromPrefs();
        ui.markNeedsRedraw();
        strcpy(reply, "OK - CR applied & saved");
      } else {
        strcpy(reply, "ERR: CR must be 5..8");
      }
      return true;
    }
    if (memcmp(config, "ble.pin ", 8) == 0 || memcmp(config, "pin ", 4) == 0) {
      const char* val = strchr(config, ' ') + 1;
      uint32_t pin = (uint32_t)atol(val);
      if (pin >= 100000 && pin <= 999999) {
        ui.setBLEPinLive(pin);
        snprintf(reply, 80, "OK - BLE PIN set to %06lu", (unsigned long)pin);
      } else {
        strcpy(reply, "ERR: PIN must be 6 digits (100000..999999)");
      }
      return true;
    }
    if (memcmp(config, "ble ", 4) == 0) {
      const char* val = config + 4;
      while (*val == ' ') val++;
      bool en = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0);
      ui.setBLEEnabled(en, true);
      snprintf(reply, 80, "OK - BLE %s", en ? "enabled" : "disabled");
      return true;
    }
    if (memcmp(config, "brightness ", 11) == 0) {
      int pct = atoi(config + 11);
      ui.setBrightnessPct((uint8_t)pct, true);
      snprintf(reply, 80, "OK - Brightness %u%%", ui.getBrightnessPct());
      return true;
    }
    if (memcmp(config, "sleep ", 6) == 0) {
      const char* val = config + 6;
      while (*val == ' ') val++;
      uint32_t secs = (strcasecmp(val, "never") == 0 || strcasecmp(val, "off") == 0) ? 0 : (uint32_t)atol(val);
      ui.setScreenSleepSeconds(secs, true);
      snprintf(reply, 80, "OK - Sleep timeout %lu s", (unsigned long)secs);
      return true;
    }
    if (memcmp(config, "theme ", 6) == 0) {
      const char* val = config + 6;
      while (*val == ' ') val++;
      bool dark = !(strcasecmp(val, "light") == 0 || strcasecmp(val, "lite") == 0 || strcmp(val, "0") == 0);
      ui.setDarkMode(dark, true);
      snprintf(reply, 80, "OK - Theme set to %s", dark ? "DARK" : "LIGHT");
      return true;
    }
    if (memcmp(config, "hash ", 5) == 0 || memcmp(config, "path.hash ", 10) == 0) {
      const char* val = strchr(config, ' ') + 1;
      while (*val == ' ') val++;
      int n = atoi(val);
      uint8_t mode = 0xFF;
      if (strcasecmp(val, "1b") == 0 || n == 1) mode = 0;
      else if (strcasecmp(val, "2b") == 0 || n == 2) mode = 1;
      else if (strcasecmp(val, "3b") == 0 || n == 3) mode = 2;
      if (mode <= 2) {
        ui.setPathHashMode(mode);
        snprintf(reply, 80, "OK - Path hash size set to %uB (mode %u)", mode + 1, mode);
      } else {
        strcpy(reply, "ERR: specify 1B, 2B, or 3B");
      }
      return true;
    }
    if (memcmp(config, "gps.baud ", 9) == 0) {
      uint32_t b = (uint32_t)atol(config + 9);
      ui.setGPSBaud(b, true);
      snprintf(reply, 80, "OK - GPS baud %lu", (unsigned long)b);
      return true;
    }
    if (memcmp(config, "gps.swap ", 9) == 0) {
      const char* val = config + 9;
      while (*val == ' ') val++;
      bool sw = (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
      ui.setGPSPinsSwapped(sw, true);
      snprintf(reply, 80, "OK - GPS pins %s", sw ? "swapped (RX16/TX15)" : "normal (RX15/TX16)");
      return true;
    }
    if (memcmp(config, "clock ", 6) == 0) {
      if (ui.parseAndSetLocalDateTime(config + 6)) {
        strcpy(reply, "OK - clock updated");
      } else {
        strcpy(reply, "ERR: use YYYY-MM-DD HH:MM:SS");
      }
      return true;
    }
    if (memcmp(config, "adc.multiplier ", 15) == 0) {
      float m = atof(config + 15);
      setAdcMultiplier(m);
      snprintf(reply, 80, "OK - ADC multiplier %.3f", getAdcMultiplier());
      return true;
    }
    if (memcmp(config, "prv.key ", 8) == 0) {
      uint8_t prv_key[PRV_KEY_SIZE];
      bool success = mesh::Utils::fromHex(prv_key, PRV_KEY_SIZE, &config[8]);
      if (success && mesh::LocalIdentity::validatePrivateKey(prv_key)) {
        mesh::LocalIdentity new_id;
        new_id.readFrom(prv_key, PRV_KEY_SIZE);
        store.saveMainIdentity(new_id);
        strcpy(reply, "OK, reboot to apply! New pubkey: ");
        mesh::Utils::toHex(&reply[33], new_id.pub_key, PUB_KEY_SIZE);
      } else {
        strcpy(reply, "Error, invalid private key");
      }
      return true;
    }
    if (memcmp(config, "manual.add.contacts ", 20) == 0) {
      const char* v = config + 20;
      prefs->manual_add_contacts = (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0) ? 1 : 0;
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "autoadd.config ", 15) == 0) {
      prefs->autoadd_config = (uint8_t)atoi(config + 15);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "autoadd.maxhops ", 16) == 0) {
      prefs->autoadd_max_hops = (uint8_t)atoi(config + 16);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "telemetry.mode.base ", 20) == 0) {
      prefs->telemetry_mode_base = (uint8_t)atoi(config + 20);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "telemetry.mode.loc ", 19) == 0) {
      prefs->telemetry_mode_loc = (uint8_t)atoi(config + 19);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "telemetry.mode.env ", 19) == 0) {
      prefs->telemetry_mode_env = (uint8_t)atoi(config + 19);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "advert.loc.policy ", 18) == 0) {
      prefs->advert_loc_policy = (uint8_t)atoi(config + 18);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
    if (memcmp(config, "client.repeat ", 14) == 0 || memcmp(config, "repeat ", 7) == 0) {
      const char* v = strchr(config, ' ') + 1;
      prefs->setRepeatEn(strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0);
      the_mesh.savePrefs();
      strcpy(reply, "OK");
      return true;
    }
  }
#endif
  return false;
}
