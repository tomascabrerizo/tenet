#include "message.h"

void sockaddr_parse_address_and_port(struct sockaddr_in *a, u32 *address,
                                     u16 *port) {
  *address = ntohl(a->sin_addr.S_un.S_addr);
  *port = ntohs(a->sin_port);
}

u64 conn_hash(ConnState *conn) {
  u32 address;
  u16 port;
  u64 hash;
  sockaddr_parse_address_and_port(&conn->addr, &address, &port);
  hash = (((u64)address) << 32) | (u64)port;
  return hash;
}

s32 conn_state_connect(ConnState *conn, char *address, char *port) {
  struct addrinfo hints, *info, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (getaddrinfo(address, port, &hints, &info) != 0) {
    printf("failed to get addess info for conn: %s:%s\n", address, port);
    return 1;
  }

  for (res = info; res != 0; res = res->ai_next) {
    if (res->ai_family == AF_INET) {
      break;
    }
  }

  if (!res) {
    printf("failed to get ipv4 address to connect\n");
    freeaddrinfo(info);
    return 1;
  }

  conn->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (conn->sock == INVALID_SOCKET) {
    printf("failed to create conn socket\n");
    freeaddrinfo(info);
    return 1;
  }
  if (connect(conn->sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
    printf("failed to connect to connect with conn: %s:%s\n", address, port);
    freeaddrinfo(info);
    return 1;
  }

  conn->recv_buffer_used = 0;
  conn->farming = 0;
  conn->bytes_to_farm = 0;

  conn->addr = *((struct sockaddr_in *)res->ai_addr);
  freeaddrinfo(info);
  return 0;
}

s32 message_write(Arena *arena, ConnState *conn, Message *msg) {
  u64 size, mark;
  s32 res;
  u8 *buffer;
  mark = arena->used;
  message_serialize(msg, 0, &size);
  buffer = arena_alloc(arena, size, 8);
  message_serialize(msg, buffer, &size);
  res = send(conn->sock, (char *)buffer, (s32)size, 0);
  arena->used = mark;
  return res;
}

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
  buffer = arena_alloc(arena, size, 8);
  message_serialize(msg, buffer, &size);
  res = sendto(sock, (char *)buffer, size, 0, (struct sockaddr *)to, to_size);
  arena->used = mark;
  return res;
}

s32 message_readfrom(Arena *arena, SOCKET sock, struct sockaddr *from,
                     Message *msg) {
  s32 size;
  u8 buffer[1024];
  if (from) {
    s32 from_size;
    from_size = sizeof(*from);
    size = recvfrom(sock, (char *)buffer, sizeof(buffer), 0,
                    (struct sockaddr *)from, &from_size);
  } else {
    size = recvfrom(sock, (char *)buffer, sizeof(buffer), 0, 0, 0);
  }
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
  case MessageType_HOLE_PUNCH:
  case MessageType_SYN:
  case MessageType_SYN_ACK:
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
      PeerInfoNode *node = (PeerInfoNode *)arena_alloc(arena, sizeof(*node), 8);
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
  case MessageType_HOLE_PUNCH:
  case MessageType_SYN:
  case MessageType_SYN_ACK:
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
