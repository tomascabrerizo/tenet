#include "message.h"

typedef struct ConnState {
  SOCKET sock;
  u8 recv_buffer[kb(10)];
  u64 recv_buffer_used;
  b32 farming;
  u32 bytes_to_farm;
} ConnState;

s32 message_read(Arena *arena, ConnState *conn, Message *msg) {
  s32 size;
  size = recv(conn->sock, (char *)conn->recv_buffer + conn->recv_buffer_used,
              array_len(conn->recv_buffer) - conn->recv_buffer_used, 0);
  if (size == 0 || size == SOCKET_ERROR) {
    return 1;
  }
  conn->recv_buffer_used += size;
  if (!conn->farming) {
    if (conn->recv_buffer_used >= sizeof(u32)) {
      u8 *buffer = conn->recv_buffer;
      conn->bytes_to_farm = read_u32_be(buffer);
      conn->farming = true;
    }
  }
  if (conn->farming) {
    if (conn->recv_buffer_used >= conn->bytes_to_farm) {
      u32 extra_bytes;
      message_deserialize(arena, conn->recv_buffer, conn->bytes_to_farm, msg);
      extra_bytes = conn->recv_buffer_used - conn->bytes_to_farm;
      memcpy(conn->recv_buffer, conn->recv_buffer + conn->bytes_to_farm,
             extra_bytes);
      conn->recv_buffer_used = extra_bytes;
      conn->bytes_to_farm = 0;
      conn->farming = false;
    }
  }
  return 0;
}

s32 message_writeto(Arena *arena, SOCKET sock, struct sockaddr *to,
                    Message *msg) {
  u64 size, mark;
  s32 res, to_size;
  u8 *buffer;
  to_size = sizeof(*to);
  mark = arena->used;
  message_serialize(msg, 0, &size);
  buffer = arena_alloc(arena, size, 4);
  message_serialize(msg, buffer, &size);
  res = sendto(sock, (char *)buffer, size, 0, to, to_size);
  arena->used = mark;
  return res;
}

s32 message_readfrom(Arena *arena, SOCKET sock, struct sockaddr *from,
                     Message *msg) {
  s32 size, from_size;
  u8 buffer[1024];
  from_size = sizeof(*from);
  size = recvfrom(sock, (char *)buffer, sizeof(buffer), 0, from, &from_size);
  message_deserialize(arena, buffer, size, msg);
  return size;
}

void message_deserialize(Arena *arena, u8 *buffer, u64 size, Message *msg) {
  u32 message_size;
  message_size = read_u32_be(buffer);
  assert(message_size == size);
  msg->header.type = (MessageType)read_u8_be(buffer);
  switch (msg->header.type) {
  case MessageType_KEEPALIVE:
  case MessageType_STUN_REQUEST: {
    /* empty body*/
  } break;
  case MessageType_STUN_RESPONSE: {
    MessageStunResponse *stun_response = &msg->stun_response;
    stun_response->address = read_u32_be(buffer);
    stun_response->port = read_u16_be(buffer);
  } break;
  case MessageType_PEER_CONNECTED: {
    MessagePeerConnected *peer_connected = &msg->peer_connected;
    peer_connected->address = read_u32_be(buffer);
    peer_connected->port = read_u16_be(buffer);
  } break;
  case MessageType_PEERS_INFO: {
    int i;
    MessagePeersInfo *peers_info = &msg->peers_info;
    peers_info->first = 0;
    peers_info->last = 0;
    peers_info->peers_count = read_u32_be(buffer);
    for (i = 0; i < peers_info->peers_count; ++i) {
      PeerInfoNode *node = (PeerInfoNode *)arena_alloc(arena, sizeof(*node), 4);
      memset(node, 0, sizeof(PeerInfoNode));
      node->address = read_u32_be(buffer);
      node->port = read_u16_be(buffer);
      dllist_push_back(peers_info->first, peers_info->last, node);
    }
  } break;
  case MessageType_PEER_INVALID:
  case MessageType_COUNT: {
    assert(!"invalid code path");
  }
  }
}

void message_serialize(Message *msg, u8 *buffer, u64 *size) {
  u32 total_size;
  total_size = 0;
  write_u32_be_or_count(buffer, *size, total_size);
  write_u8_be_or_count(buffer, (u8)msg->header.type, total_size);
  switch (msg->header.type) {
  case MessageType_KEEPALIVE:
  case MessageType_STUN_REQUEST: {
    /* empty body*/
  } break;
  case MessageType_STUN_RESPONSE: {
    MessageStunResponse *stun_response = &msg->stun_response;
    write_u32_be_or_count(buffer, stun_response->address, total_size);
    write_u16_be_or_count(buffer, stun_response->port, total_size);
  } break;
  case MessageType_PEER_CONNECTED: {
    MessagePeerConnected *peer_connected = &msg->peer_connected;
    write_u32_be_or_count(buffer, peer_connected->address, total_size);
    write_u16_be_or_count(buffer, peer_connected->port, total_size);
  } break;
  case MessageType_PEERS_INFO: {
    PeerInfoNode *node;
    MessagePeersInfo *peers_info = &msg->peers_info;
    write_u32_be_or_count(buffer, peers_info->peers_count, total_size);
    for (node = peers_info->first; node != 0; node = node->next) {
      write_u32_be_or_count(buffer, node->address, total_size);
      write_u16_be_or_count(buffer, node->port, total_size);
    }
  } break;
  case MessageType_PEER_INVALID:
  case MessageType_COUNT: {
    assert(!"invalid code path");
  }
  }
  *size = total_size;
}

void message_dump(Message *msg) {
  switch (msg->header.type) {
  case MessageType_PEER_INVALID:
    printf("Message: INVALID\n");
    break;

  case MessageType_STUN_RESPONSE:
    printf("Message: STUN_RESPONSE\n");
    printf("  Address: %u.%u.%u.%u\n",
           (msg->stun_response.address >> 24) & 0xFF,
           (msg->stun_response.address >> 16) & 0xFF,
           (msg->stun_response.address >> 8) & 0xFF,
           msg->stun_response.address & 0xFF);
    printf("  Port: %u\n", msg->stun_response.port);
    break;

  case MessageType_PEER_CONNECTED:
    printf("Message: PEER_CONNECTED\n");
    printf("  Address: %u.%u.%u.%u\n",
           (msg->peer_connected.address >> 24) & 0xFF,
           (msg->peer_connected.address >> 16) & 0xFF,
           (msg->peer_connected.address >> 8) & 0xFF,
           msg->peer_connected.address & 0xFF);
    printf("  Port: %u\n", msg->peer_connected.port);
    break;

  case MessageType_PEERS_INFO: {
    PeerInfoNode *node;
    printf("Message: PEERS_INFO\n");
    printf("  Peers count: %u\n", msg->peers_info.peers_count);
    node = msg->peers_info.first;
    while (node) {
      printf("  Peer: %u.%u.%u.%u:%u\n", (node->address >> 24) & 0xFF,
             (node->address >> 16) & 0xFF, (node->address >> 8) & 0xFF,
             node->address & 0xFF, node->port);
      node = node->next;
    }
  } break;

  case MessageType_COUNT:
    printf("Message: COUNT (invalid marker)\n");
    break;

  default:
    printf("Message: UNKNOWN TYPE (%d)\n", (int)msg->header.type);
    break;
  }
}
