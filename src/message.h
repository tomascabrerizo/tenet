#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "core.h"

typedef enum MessageType {
  MessageType_PEER_INVALID,
  MessageType_STUN_RESPONSE,
  MessageType_PEER_CONNECTED,
  MessageType_PEERS_INFO,
  MessageType_COUNT
} MessageType;

typedef struct MessageHeader {
  MessageType type;
  struct MessageHeader *next;
  struct MessageHeader *prev;
} MessageHeader;

typedef struct MessageStunResponse {
  MessageHeader header;
  u32 address;
  u16 port;
} MessageStunResponse;

typedef struct MessagePeerConnected {
  MessageHeader header;
  u32 address;
  u16 port;
} MessagePeerConnected;

typedef struct PeersInfoNode {
  u32 address;
  u16 port;
  struct PeersInfoNode *prev;
  struct PeersInfoNode *next;
} PeersInfoNode;

typedef struct MessagePeersInfo {
  MessageHeader header;
  PeersInfoNode *first;
  PeersInfoNode *last;
  u32 peers_count;
} MessagePeersInfo;

typedef union Message {
  MessageHeader header;
  MessageStunResponse stun_response;
  MessagePeerConnected peer_connected;
  MessagePeersInfo peers_info;
} Message;

void message_deserialize(Arena *arena, u8 *buffer, u64 size, Message *msg);
void message_serialize(Message *msg, u8 *buffer, u64 *size);
void message_dump(Message *msg);

#endif
