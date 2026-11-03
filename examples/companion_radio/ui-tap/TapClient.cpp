#include "TapClient.h"

TapClient::TapClient(InterTaskMailboxInterface* mailbox)
    : _mailbox(mailbox),
      _connected(false),
      _last_handshake_ms(0),
      _last_sync_ms(0),
      _message_count(0),
      _contact_count(0),
      _active_channel(0),
      _active_contact_idx(-1),
      _unread_count(0) {
  memset(_self_name, 0, sizeof(_self_name));
  strcpy(_self_name, "WisMesh Tap");
  memset(_self_pubkey, 0, sizeof(_self_pubkey));

  // Default channels
  for (uint8_t i = 0; i < MAX_UI_CHANNELS; i++) {
    _channels[i].index = i;
    _channels[i].active = (i == 0);
    if (i == 0) {
      strcpy(_channels[i].name, "Public #0");
    } else {
      snprintf(_channels[i].name, sizeof(_channels[i].name), "Channel %d", i);
    }
  }
}

void TapClient::begin() {
  requestHandshake();
}

void TapClient::requestHandshake() {
  uint8_t frame[16];
  frame[0] = PROTO_CMD_APP_START;
  memset(&frame[1], 0, 7); // reserved 7 bytes
  strcpy((char*)&frame[8], "WisMeshTap");
  size_t len = 8 + strlen("WisMeshTap");

  _mailbox->clientSendFrame(frame, len);
  _last_handshake_ms = millis();
}

void TapClient::requestContacts() {
  uint8_t frame[5];
  frame[0] = PROTO_CMD_GET_CONTACTS;
  uint32_t since = 0;
  memcpy(&frame[1], &since, 4);
  _mailbox->clientSendFrame(frame, 5);
}

void TapClient::requestChannels() {
  for (uint8_t i = 0; i < MAX_UI_CHANNELS; i++) {
    uint8_t frame[2];
    frame[0] = PROTO_CMD_GET_CHANNEL;
    frame[1] = i;
    _mailbox->clientSendFrame(frame, 2);
  }
}

void TapClient::syncNextMessage() {
  uint8_t frame[1];
  frame[0] = PROTO_CMD_SYNC_NEXT_MESSAGE;
  _mailbox->clientSendFrame(frame, 1);
}

bool TapClient::sendChannelText(uint8_t channelIdx, const char* text) {
  if (!text || strlen(text) == 0) return false;

  size_t textLen = strlen(text);
  if (textLen > MAX_MSG_TEXT_LEN - 1) textLen = MAX_MSG_TEXT_LEN - 1;

  uint8_t frame[MAX_FRAME_SIZE];
  frame[0] = PROTO_CMD_SEND_CHANNEL_TXT_MSG;
  frame[1] = channelIdx;
  memcpy(&frame[2], text, textLen);
  frame[2 + textLen] = 0;

  bool ok = _mailbox->clientSendFrame(frame, 2 + textLen);
  if (ok) {
    addLocalMessage("Me", text, true, channelIdx, false);
  }
  return ok;
}

bool TapClient::sendDirectText(const uint8_t recipientKeyPrefix[6], const char* text) {
  if (!text || strlen(text) == 0) return false;

  size_t textLen = strlen(text);
  if (textLen > MAX_MSG_TEXT_LEN - 1) textLen = MAX_MSG_TEXT_LEN - 1;

  uint8_t frame[MAX_FRAME_SIZE];
  frame[0] = PROTO_CMD_SEND_TXT_MSG;
  memcpy(&frame[1], recipientKeyPrefix, 6);
  memcpy(&frame[7], text, textLen);
  frame[7 + textLen] = 0;

  bool ok = _mailbox->clientSendFrame(frame, 7 + textLen);
  if (ok) {
    addLocalMessage("Me (DM)", text, true, 0, true);
  }
  return ok;
}

void TapClient::addLocalMessage(const char* sender, const char* text, bool isOutgoing, uint8_t chIdx, bool isDirect) {
  if (_message_count < MAX_UI_MESSAGES) {
    _message_count++;
  } else {
    // Shift messages up
    for (uint8_t i = 0; i < MAX_UI_MESSAGES - 1; i++) {
      _messages[i] = _messages[i + 1];
    }
  }

  uint8_t idx = _message_count - 1;
  strncpy(_messages[idx].sender, sender, sizeof(_messages[idx].sender) - 1);
  _messages[idx].sender[sizeof(_messages[idx].sender) - 1] = 0;

  strncpy(_messages[idx].text, text, sizeof(_messages[idx].text) - 1);
  _messages[idx].text[sizeof(_messages[idx].text) - 1] = 0;

  _messages[idx].timestamp = millis() / 1000;
  _messages[idx].msg_id = 0;
  _messages[idx].channel_idx = chIdx;
  _messages[idx].is_outgoing = isOutgoing;
  _messages[idx].is_delivered = false;
  _messages[idx].is_direct = isDirect;

  if (!isOutgoing) {
    _unread_count++;
  }
}

void TapClient::poll() {
  // Periodically re-attempt handshake if not connected
  if (!_connected && (millis() - _last_handshake_ms > 3000)) {
    requestHandshake();
  }

  uint8_t frame[MAX_FRAME_SIZE];
  size_t len = 0;

  while ((len = _mailbox->clientCheckRecvFrame(frame)) > 0) {
    handleFrame(frame, len);
  }
}

void TapClient::handleFrame(const uint8_t* frame, size_t len) {
  if (len == 0) return;

  uint8_t code = frame[0];

  switch (code) {
    case PROTO_RESP_CODE_SELF_INFO:
      handleSelfInfo(frame, len);
      break;

    case PROTO_RESP_CODE_CONTACT:
      handleContact(frame, len);
      break;

    case PROTO_RESP_CODE_CHANNEL_INFO:
      handleChannelInfo(frame, len);
      break;

    case PROTO_RESP_CODE_CHANNEL_MSG_V3:
      handleIncomingMessage(frame, len, true);
      break;

    case PROTO_RESP_CODE_CONTACT_MSG_V3:
      handleIncomingMessage(frame, len, false);
      break;

    case PROTO_PUSH_CODE_MSG_WAITING:
      syncNextMessage();
      break;

    case PROTO_PUSH_CODE_SEND_CONFIRMED:
      handleSendConfirmed(frame, len);
      break;

    case PROTO_PUSH_CODE_ADVERT:
    case PROTO_PUSH_CODE_NEW_ADVERT:
      handleAdvert(frame, len);
      break;

    case PROTO_RESP_CODE_SENT:
      if (len >= 6 && _message_count > 0) {
        uint32_t msgId;
        memcpy(&msgId, &frame[2], 4);
        _messages[_message_count - 1].msg_id = msgId;
      }
      break;

    default:
      break;
  }
}

void TapClient::handleSelfInfo(const uint8_t* frame, size_t len) {
  _connected = true;

  if (len >= 36) {
    memcpy(_self_pubkey, &frame[4], 32);
  }

  // Node name starts after lat/lon (byte 44)
  if (len > 44) {
    size_t nameLen = len - 44;
    if (nameLen >= sizeof(_self_name)) nameLen = sizeof(_self_name) - 1;
    memcpy(_self_name, &frame[44], nameLen);
    _self_name[nameLen] = 0;
  }

  // After handshake, fetch contacts & channels
  requestContacts();
  requestChannels();
  syncNextMessage();
}

void TapClient::handleContact(const uint8_t* frame, size_t len) {
  if (len < 13) return;

  uint8_t prefix[6];
  memcpy(prefix, &frame[1], 6);

  // Check if contact already in cache
  int matchIdx = -1;
  for (uint8_t i = 0; i < _contact_count; i++) {
    if (memcmp(_contacts[i].pubkey_prefix, prefix, 6) == 0) {
      matchIdx = i;
      break;
    }
  }

  if (matchIdx == -1) {
    if (_contact_count < MAX_UI_CONTACTS) {
      matchIdx = _contact_count++;
    } else {
      matchIdx = MAX_UI_CONTACTS - 1; // overwrite oldest
    }
  }

  memcpy(_contacts[matchIdx].pubkey_prefix, prefix, 6);
  memcpy(&_contacts[matchIdx].lastmod, &frame[9], 4);
  _contacts[matchIdx].valid = true;

  size_t nameLen = len - 13;
  if (nameLen >= sizeof(_contacts[matchIdx].name)) nameLen = sizeof(_contacts[matchIdx].name) - 1;
  memcpy(_contacts[matchIdx].name, &frame[13], nameLen);
  _contacts[matchIdx].name[nameLen] = 0;
}

void TapClient::handleChannelInfo(const uint8_t* frame, size_t len) {
  if (len < 2) return;
  uint8_t ch = frame[1];
  if (ch < MAX_UI_CHANNELS) {
    _channels[ch].index = ch;
    _channels[ch].active = true;
    if (len > 2) {
      size_t nameLen = len - 2;
      if (nameLen >= sizeof(_channels[ch].name)) nameLen = sizeof(_channels[ch].name) - 1;
      memcpy(_channels[ch].name, &frame[2], nameLen);
      _channels[ch].name[nameLen] = 0;
    }
  }
}

void TapClient::handleIncomingMessage(const uint8_t* frame, size_t len, bool isChannel) {
  char senderName[24] = "Unknown";
  char textBuf[MAX_MSG_TEXT_LEN] = {0};
  uint8_t chIdx = 0;

  if (isChannel && len >= 12) {
    chIdx = frame[1];
    uint8_t prefix[6];
    memcpy(prefix, &frame[2], 6);

    // Look up sender name
    for (uint8_t i = 0; i < _contact_count; i++) {
      if (memcmp(_contacts[i].pubkey_prefix, prefix, 6) == 0) {
        strncpy(senderName, _contacts[i].name, sizeof(senderName) - 1);
        break;
      }
    }
    if (strcmp(senderName, "Unknown") == 0) {
      snprintf(senderName, sizeof(senderName), "%02X%02X", prefix[0], prefix[1]);
    }

    size_t textLen = len - 12;
    if (textLen >= sizeof(textBuf)) textLen = sizeof(textBuf) - 1;
    memcpy(textBuf, &frame[12], textLen);
    textBuf[textLen] = 0;

    addLocalMessage(senderName, textBuf, false, chIdx, false);
    syncNextMessage(); // check if more messages are in queue
  } else if (!isChannel && len >= 11) {
    uint8_t prefix[6];
    memcpy(prefix, &frame[1], 6);

    for (uint8_t i = 0; i < _contact_count; i++) {
      if (memcmp(_contacts[i].pubkey_prefix, prefix, 6) == 0) {
        strncpy(senderName, _contacts[i].name, sizeof(senderName) - 1);
        break;
      }
    }
    if (strcmp(senderName, "Unknown") == 0) {
      snprintf(senderName, sizeof(senderName), "%02X%02X", prefix[0], prefix[1]);
    }

    size_t textLen = len - 11;
    if (textLen >= sizeof(textBuf)) textLen = sizeof(textBuf) - 1;
    memcpy(textBuf, &frame[11], textLen);
    textBuf[textLen] = 0;

    addLocalMessage(senderName, textBuf, false, 0, true);
    syncNextMessage();
  }
}

void TapClient::handleAdvert(const uint8_t* frame, size_t len) {
  if (len < 8) return;
  // Advert frames carry public key prefix, SNR, hops
  // Update matching contact
  uint8_t prefix[6];
  memcpy(prefix, &frame[1], 6);

  for (uint8_t i = 0; i < _contact_count; i++) {
    if (memcmp(_contacts[i].pubkey_prefix, prefix, 6) == 0) {
      _contacts[i].lastmod = millis() / 1000;
      break;
    }
  }
}

void TapClient::handleSendConfirmed(const uint8_t* frame, size_t len) {
  if (len < 5) return;
  uint32_t msgId;
  memcpy(&msgId, &frame[1], 4);

  for (uint8_t i = 0; i < _message_count; i++) {
    if (_messages[i].is_outgoing && _messages[i].msg_id == msgId) {
      _messages[i].is_delivered = true;
      break;
    }
  }
}
