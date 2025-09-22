#ifndef _PROTO_H_
#define _PROTO_H_

#include "net.h"

#define write_u8_be(buffer, value)                                             \
  ((u8 *)(buffer))[0] = (u8)((value >> 0) & 0xff);                             \
  (buffer) = ((u8 *)(buffer)) + 1

#define write_u16_be(buffer, value)                                            \
  ((u8 *)(buffer))[0] = (u8)((value >> 8) & 0xff);                             \
  ((u8 *)(buffer))[1] = (u8)((value >> 0) & 0xff);                             \
  (buffer) = ((u8 *)(buffer)) + 2

#define write_u32_be(buffer, value)                                            \
  ((u8 *)(buffer))[0] = (u8)((value >> 24) & 0xff);                            \
  ((u8 *)(buffer))[1] = (u8)((value >> 16) & 0xff);                            \
  ((u8 *)(buffer))[2] = (u8)((value >> 8) & 0xff);                             \
  ((u8 *)(buffer))[3] = (u8)((value >> 0) & 0xff);                             \
  (buffer) = ((u8 *)(buffer)) + 4

#define write_u8_be_or_count(buffer, value, size)                              \
  do {                                                                         \
    if (buffer) {                                                              \
      write_u8_be(buffer, value);                                              \
    }                                                                          \
    size += 1;                                                                 \
  } while (0)

#define write_u16_be_or_count(buffer, value, size)                             \
  do {                                                                         \
    if (buffer) {                                                              \
      write_u16_be(buffer, value);                                             \
    }                                                                          \
    size += 2;                                                                 \
  } while (0)

#define write_u32_be_or_count(buffer, value, size)                             \
  do {                                                                         \
    if (buffer) {                                                              \
      write_u32_be(buffer, value);                                             \
    }                                                                          \
    size += 4;                                                                 \
  } while (0)

#define peek_u32_be(buffer)                                                    \
  (u32)(((u8 *)(buffer))[0] << 24) | (u32)(((u8 *)(buffer))[1] << 16) |        \
      (u32)(((u8 *)(buffer))[2] << 8) | (u32)(((u8 *)(buffer))[3] << 0)

#define read_u32_be(buffer)                                                    \
  (u32)(((u8 *)(buffer))[0] << 24) | (u32)(((u8 *)(buffer))[1] << 16) |        \
      (u32)(((u8 *)(buffer))[2] << 8) | (u32)(((u8 *)(buffer))[3] << 0);       \
  (buffer) += 4

#define read_u16_be(buffer)                                                    \
  (u16)((u16)(((u8 *)(buffer))[0] << 8) | (u16)(((u8 *)(buffer))[1] << 0));    \
  (buffer) += 2

#define read_u8_be(buffer)                                                     \
  (u8)(((u8 *)(buffer))[0]);                                                   \
  (buffer) += 1

#define PROTO_MAGIC                                                            \
  (((u32)'T' << 24) | ((u32)'E' << 16) | ((u32)'N' << 8) | ((u32)'T' << 0))

#define valid_proto(buffer) (peek_u32_be(buffer) == PROTO_MAGIC)

typedef enum MessageType {
  MessageType_INVALID,
  MessageType_STUN,
  MessageType_STUN_RESPONSE,
  MessageType_KEEP_ALIVE,
  MessageType_CONNECT,
  MessageType_PEERS_TO_CONNECT,
  MessageType_COUNT
} MessageType;

typedef struct MessageHeader {
  MessageType type;
  struct MessageHeader *next;
  struct MessageHeader *prev;
} MessageHeader;

typedef struct MessageStunResponse {
  MessageHeader header;
  u32 addr;
  u16 port;
} MessageStunResponse;

typedef struct MessageConnect {
  MessageHeader header;
  u32 addr;
  u16 port;
  u32 local_addr;
  u16 local_port;
} MessageConnect;

typedef struct PeerConnected {
  u32 addr;
  u16 port;
  u32 local_addr;
  u16 local_port;
  struct PeerConnected *next;
  struct PeerConnected *prev;
} PeerConnected;

typedef struct MessagePeersToConnect {
  MessageHeader header;
  PeerConnected *first;
  PeerConnected *last;
  u32 count;
} MessagePeersToConnect;

typedef union Message {
  MessageHeader header;
  MessageStunResponse stun_response;
  MessageConnect connect;
  MessagePeersToConnect peers_to_connect;
} Message;

Message *message_deserialize(Arena *arena, u8 *buffer, u64 size);
u8 *message_serialize(Arena *arena, Message *msg, u64 *size);

/* TODO: This is not a stream protocol, is a message protocol, consider change
 * this name */
typedef struct Stream {
  Conn conn;
  u8 recv_buffer[kb(10)];
  u64 recv_buffer_used;
  b32 farming;
  u32 bytes_to_farm;
} Stream;

typedef void (*MessageCallback)(Stream *stream, Message *msg, void *param);
u32 stream_proccess_messages(Arena *arena, Stream *stream,
                             MessageCallback callback, void *param);
u32 stream_message_write(Arena *arena, Stream *stream, Message *msg);

typedef struct Dgram {
  Conn conn;
} Dgram;

u32 dgram_message_write_to(Arena *arena, Dgram *dgram, Message *msg,
                           struct ConnAddr *to);
Message *dgram_message_read_from(Arena *arena, Dgram *dgram,
                                 struct ConnAddr *from);

typedef struct AddrMessage {
  Message msg;
  ConnAddr *addr;
  struct AddrMessage *next;
  struct AddrMessage *prev;
} AddrMessage;

typedef struct MessageAllocator {
  Arena *arena;
  MessageHeader *first_free;
} MessageAllocator;

void message_allocator_init(MessageAllocator *allocator, Arena *arena);
Message *message_alloc(MessageAllocator *allocator);
void message_free(MessageAllocator *allocator, Message *msg);

typedef struct AddrMessageAllocator {
  Arena *arena;
  AddrMessage *first_free;
} AddrMessageAllocator;

void addr_message_allocator_init(AddrMessageAllocator *allocator, Arena *arena);
AddrMessage *addr_message_alloc(AddrMessageAllocator *allocator);
void addr_message_free(AddrMessageAllocator *allocator, AddrMessage *msg);

#endif
