#pragma once

#include <Arduino.h>
#include <helpers/BaseSerialInterface.h>

#if defined(NRF52_PLATFORM)
  #include <FreeRTOS.h>
  #include <queue.h>
  #include <task.h>
#elif defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <freertos/task.h>
#endif

#ifndef MAILBOX_QUEUE_DEPTH
  #define MAILBOX_QUEUE_DEPTH 16
#endif

struct MailboxFrame {
  uint16_t len;
  uint8_t data[MAX_FRAME_SIZE];
};

/**
 * InterTaskMailboxInterface
 * 
 * Provides an in-memory, thread-safe bidirectional frame queue between the
 * MeshCore protocol engine (running on Core 0 or high-priority FreeRTOS task)
 * and the Standalone UI client (running on Core 1 or separate FreeRTOS task).
 * 
 * Implements BaseSerialInterface so MeshCore treats the UI as an internal client.
 */
class InterTaskMailboxInterface : public BaseSerialInterface {
private:
  QueueHandle_t _q_to_ui;    // Frames sent from MeshCore -> UI Client
  QueueHandle_t _q_from_ui;  // Frames sent from UI Client -> MeshCore
  bool _enabled;
  uint32_t _dropped_to_ui;
  uint32_t _dropped_from_ui;

public:
  InterTaskMailboxInterface();
  ~InterTaskMailboxInterface();

  // Initialization (allocates FreeRTOS queues)
  bool begin(size_t queueDepth = MAILBOX_QUEUE_DEPTH);

  // --- BaseSerialInterface Methods (used by MeshCore engine) ---
  void enable() override { _enabled = true; }
  void disable() override { _enabled = false; }
  bool isEnabled() const override { return _enabled; }
  bool isConnected() const override { return _enabled && (_q_to_ui != nullptr); }
  void loop() override {}

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  // --- Client-Side Methods (used by UI Client task) ---
  bool clientSendFrame(const uint8_t src[], size_t len, uint32_t timeoutMs = 20);
  size_t clientCheckRecvFrame(uint8_t dest[], uint32_t timeoutMs = 0);
  bool clientHasIncoming() const;

  // Diagnostics
  uint32_t getDroppedToUiCount() const { return _dropped_to_ui; }
  uint32_t getDroppedFromUiCount() const { return _dropped_from_ui; }
};

// Global shared mailbox instance
extern InterTaskMailboxInterface mailbox_interface;
