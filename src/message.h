#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "core.h"

/* TODO: remove this headers from here */
#include <winsock2.h>
#include <ws2tcpip.h>

typedef enum MessageType {
  MessageType_PEER_INVALID,
  MessageType_STUN_REQUEST,
  MessageType_STUN_RESPONSE,
  MessageType_KEEPALIVE,
  MessageType_PEER_CONNECTED,
  MessageType_PEERS_INFO,
  MessageType_COUNT
} MessageType;

typedef struct MessageHeader {
  MessageType type;
  struct MessageHeader *next;
  struct MessageHeader *prev;
} MessageHeader;

typedef struct MessageStunRequest {
  MessageHeader header;
} MessageStunRequest;

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

typedef struct PeerInfoNode {
  u32 address;
  u16 port;
  struct PeerInfoNode *prev;
  struct PeerInfoNode *next;
} PeerInfoNode;

typedef struct MessagePeersInfo {
  MessageHeader header;
  PeerInfoNode *first;
  PeerInfoNode *last;
  u32 peers_count;
} MessagePeersInfo;

typedef union Message {
  MessageHeader header;
  MessageStunRequest strun_request;
  MessageStunResponse stun_response;
  MessagePeerConnected peer_connected;
  MessagePeersInfo peers_info;
} Message;

typedef struct ConnState {
  SOCKET sock;
  struct sockaddr addr;
  u8 recv_buffer[kb(10)];
  u64 recv_buffer_used;
  b32 farming;
  u32 bytes_to_farm;
} ConnState;

s32 conn_state_connect(ConnState *conn, char *address, char *port);
void conn_parse_address_and_port(ConnState *conn, u32 *address, u16 *port);
u64 conn_hash(ConnState *conn);

s32 message_read(Arena *arena, ConnState *conn, Message *msg);
s32 message_write(Arena *arena, ConnState *conn, Message *msg);

s32 message_writeto(Arena *arena, SOCKET sock, struct sockaddr *to,
                    Message *msg);
s32 message_readfrom(Arena *arena, SOCKET sock, struct sockaddr *from,
                     Message *msg);

void message_deserialize(Arena *arena, u8 *buffer, u64 size, Message *msg);
void message_serialize(Message *msg, u8 *buffer, u64 *size);

#endif
