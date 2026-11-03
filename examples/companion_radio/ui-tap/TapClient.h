#pragma once

#include <Arduino.h>
#include <helpers/InterTaskMailboxInterface.h>

#define MAX_UI_MESSAGES   16
#define MAX_UI_CONTACTS   20
#define MAX_UI_CHANNELS   8
#define MAX_MSG_TEXT_LEN  120

// MeshCore Protocol Command Codes
#define PROTO_CMD_APP_START             1
#define PROTO_CMD_SEND_TXT_MSG          2
#define PROTO_CMD_SEND_CHANNEL_TXT_MSG  3
#define PROTO_CMD_GET_CONTACTS          4
#define PROTO_CMD_GET_DEVICE_TIME       5
#define PROTO_CMD_SET_DEVICE_TIME       6
#define PROTO_CMD_SYNC_NEXT_MESSAGE     10
#define PROTO_CMD_DEVICE_QUERY          22
#define PROTO_CMD_GET_CHANNEL           31

// MeshCore Protocol Response & Push Codes
#define PROTO_RESP_CODE_OK              0
#define PROTO_RESP_CODE_ERR             1
#define PROTO_RESP_CODE_CONTACTS_START  2
#define PROTO_RESP_CODE_CONTACT         3
#define PROTO_RESP_CODE_END_OF_CONTACTS 4
#define PROTO_RESP_CODE_SELF_INFO       5
#define PROTO_RESP_CODE_SENT            6
#define PROTO_RESP_CODE_CONTACT_MSG_V3  16
#define PROTO_RESP_CODE_CHANNEL_MSG_V3  17
#define PROTO_RESP_CODE_CHANNEL_INFO    18
#define PROTO_RESP_CODE_NO_MORE_MESSAGES 10

#define PROTO_PUSH_CODE_ADVERT          0x80
#define PROTO_PUSH_CODE_SEND_CONFIRMED  0x82
#define PROTO_PUSH_CODE_MSG_WAITING     0x83
#define PROTO_PUSH_CODE_NEW_ADVERT      0x8A

struct TapChatMessage {
  char sender[24];
  char text[MAX_MSG_TEXT_LEN];
  uint32_t timestamp;
  uint32_t msg_id;
  uint8_t channel_idx;
  bool is_outgoing;
  bool is_delivered;
  bool is_direct;
};

struct TapContact {
  char name[24];
  uint8_t pubkey_prefix[6];
  uint32_t lastmod;
  float snr;
  uint8_t hops;
  bool valid;
};

struct TapChannel {
  char name[16];
  uint8_t index;
  bool active;
};

class TapClient {
private:
  InterTaskMailboxInterface* _mailbox;

  // Local Cached State
  char _self_name[32];
  uint8_t _self_pubkey[32];
  bool _connected;
  uint32_t _last_handshake_ms;
  uint32_t _last_sync_ms;

  TapChatMessage _messages[MAX_UI_MESSAGES];
  uint8_t _message_count;

  TapContact _contacts[MAX_UI_CONTACTS];
  uint8_t _contact_count;

  TapChannel _channels[MAX_UI_CHANNELS];
  uint8_t _active_channel;
  int8_t _active_contact_idx; // -1 if in channel mode

  uint16_t _unread_count;

  void handleFrame(const uint8_t* frame, size_t len);
  void handleSelfInfo(const uint8_t* frame, size_t len);
  void handleContact(const uint8_t* frame, size_t len);
  void handleChannelInfo(const uint8_t* frame, size_t len);
  void handleIncomingMessage(const uint8_t* frame, size_t len, bool isChannel);
  void handleAdvert(const uint8_t* frame, size_t len);
  void handleSendConfirmed(const uint8_t* frame, size_t len);

public:
  TapClient(InterTaskMailboxInterface* mailbox = &mailbox_interface);

  void begin();
  void poll(); // Reads incoming frames from mailbox

  // High-level Actions
  void requestHandshake();
  void requestContacts();
  void requestChannels();
  void syncNextMessage();

  bool sendChannelText(uint8_t channelIdx, const char* text);
  bool sendDirectText(const uint8_t recipientKeyPrefix[6], const char* text);

  // Getters & UI Data Access
  bool isConnected() const { return _connected; }
  const char* getSelfName() const { return _self_name; }
  const uint8_t* getSelfPubKey() const { return _self_pubkey; }

  uint8_t getActiveChannel() const { return _active_channel; }
  void setActiveChannel(uint8_t ch) { _active_channel = ch; _active_contact_idx = -1; }

  int8_t getActiveContactIdx() const { return _active_contact_idx; }
  void setActiveContactIdx(int8_t idx) { _active_contact_idx = idx; }

  uint8_t getMessageCount() const { return _message_count; }
  const TapChatMessage& getMessage(uint8_t idx) const { return _messages[idx]; }

  uint8_t getContactCount() const { return _contact_count; }
  const TapContact& getContact(uint8_t idx) const { return _contacts[idx]; }

  const TapChannel& getChannel(uint8_t idx) const { return _channels[idx]; }

  uint16_t getUnreadCount() const { return _unread_count; }
  void clearUnread() { _unread_count = 0; }

  void addLocalMessage(const char* sender, const char* text, bool isOutgoing, uint8_t chIdx, bool isDirect);
};
