#include "InterTaskMailboxInterface.h"

InterTaskMailboxInterface mailbox_interface;

InterTaskMailboxInterface::InterTaskMailboxInterface()
    : _q_to_ui(nullptr),
      _q_from_ui(nullptr),
      _enabled(false),
      _dropped_to_ui(0),
      _dropped_from_ui(0) {}

InterTaskMailboxInterface::~InterTaskMailboxInterface() {
  if (_q_to_ui) {
    vQueueDelete(_q_to_ui);
    _q_to_ui = nullptr;
  }
  if (_q_from_ui) {
    vQueueDelete(_q_from_ui);
    _q_from_ui = nullptr;
  }
}

bool InterTaskMailboxInterface::begin(size_t queueDepth) {
  if (!_q_to_ui) {
    _q_to_ui = xQueueCreate(queueDepth, sizeof(MailboxFrame));
  }
  if (!_q_from_ui) {
    _q_from_ui = xQueueCreate(queueDepth, sizeof(MailboxFrame));
  }

  _enabled = (_q_to_ui != nullptr && _q_from_ui != nullptr);
  return _enabled;
}

bool InterTaskMailboxInterface::isWriteBusy() const {
  if (!_enabled || !_q_to_ui) return false;
  return uxQueueSpacesAvailable(_q_to_ui) == 0;
}

size_t InterTaskMailboxInterface::writeFrame(const uint8_t src[], size_t len) {
  if (!_enabled || !_q_to_ui || len == 0 || len > MAX_FRAME_SIZE) {
    return 0;
  }

  MailboxFrame frame;
  frame.len = (uint16_t)len;
  memcpy(frame.data, src, len);

  // Non-blocking send from MeshCore to UI
  BaseType_t res = xQueueSend(_q_to_ui, &frame, 0);
  if (res == pdTRUE) {
    return len;
  } else {
    _dropped_to_ui++;
    return 0;
  }
}

size_t InterTaskMailboxInterface::checkRecvFrame(uint8_t dest[]) {
  if (!_enabled || !_q_from_ui) {
    return 0;
  }

  MailboxFrame frame;
  // Non-blocking receive for MeshCore
  if (xQueueReceive(_q_from_ui, &frame, 0) == pdTRUE) {
    size_t copyLen = frame.len > MAX_FRAME_SIZE ? MAX_FRAME_SIZE : frame.len;
    memcpy(dest, frame.data, copyLen);
    return copyLen;
  }

  return 0;
}

bool InterTaskMailboxInterface::clientSendFrame(const uint8_t src[], size_t len, uint32_t timeoutMs) {
  if (!_enabled || !_q_from_ui || len == 0 || len > MAX_FRAME_SIZE) {
    return false;
  }

  MailboxFrame frame;
  frame.len = (uint16_t)len;
  memcpy(frame.data, src, len);

  TickType_t ticks = timeoutMs == 0 ? 0 : pdMS_TO_TICKS(timeoutMs);
  BaseType_t res = xQueueSend(_q_from_ui, &frame, ticks);
  if (res == pdTRUE) {
    return true;
  } else {
    _dropped_from_ui++;
    return false;
  }
}

size_t InterTaskMailboxInterface::clientCheckRecvFrame(uint8_t dest[], uint32_t timeoutMs) {
  if (!_enabled || !_q_to_ui) {
    return 0;
  }

  MailboxFrame frame;
  TickType_t ticks = timeoutMs == 0 ? 0 : pdMS_TO_TICKS(timeoutMs);
  if (xQueueReceive(_q_to_ui, &frame, ticks) == pdTRUE) {
    size_t copyLen = frame.len > MAX_FRAME_SIZE ? MAX_FRAME_SIZE : frame.len;
    memcpy(dest, frame.data, copyLen);
    return copyLen;
  }

  return 0;
}

bool InterTaskMailboxInterface::clientHasIncoming() const {
  if (!_enabled || !_q_to_ui) return false;
  return uxQueueMessagesWaiting(_q_to_ui) > 0;
}
