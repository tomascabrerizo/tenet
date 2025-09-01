#include "core.h"
#include "message.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#define PEERS_MAP_SIZE 1024
typedef struct PeerInfoBucket {
  b32 used;
  u64 hash;
  u32 address;
  u16 port;
  struct PeerInfoBucket *next;
} PeerInfoBucket;

typedef struct PeerInfoHashMap {
  PeerInfoBucket buckets[PEERS_MAP_SIZE];
  PeerInfoBucket *free_first;
} PeerInfoHashMap;

b32 peer_info_hashmap_has(PeerInfoHashMap *map, u64 hash) {
  u64 index;
  PeerInfoBucket *bucket;
  index = (hash % (u64)PEERS_MAP_SIZE);
  assert(index < PEERS_MAP_SIZE);
  bucket = &map->buckets[index];
  if (bucket->used) {
    return false;
  }
  while (bucket && bucket->hash != hash) {
    bucket = bucket->next;
  }
  return bucket != 0;
}

void peer_info_hashmap_insert(Arena *arena, PeerInfoHashMap *map, u64 hash,
                              u32 address, u32 port) {
  u64 index;
  PeerInfoBucket *bucket;

  if (peer_info_hashmap_has(map, hash)) {
    return;
  }

  index = (hash % (u64)PEERS_MAP_SIZE);
  assert(index < PEERS_MAP_SIZE);
  bucket = &map->buckets[index];
  if (bucket->used) {
    PeerInfoBucket *new_bucket;
    if (map->free_first) {
      new_bucket = map->free_first;
      map->free_first = map->free_first->next;
      memset(new_bucket, 0, sizeof(*new_bucket));
    } else {
      new_bucket = (PeerInfoBucket *)arena_alloc(arena, sizeof(*new_bucket), 4);
      memset(new_bucket, 0, sizeof(*new_bucket));
    }
    new_bucket->next = bucket->next;
    bucket->next = new_bucket;
    bucket = new_bucket;
  }
  bucket->used = true;
  bucket->hash = hash;
  bucket->address = address;
  bucket->port = port;
}

void peer_info_hashmap_remove(PeerInfoHashMap *map, u64 hash) {
  u64 index;
  PeerInfoBucket *bucket, *prev_bucket;
  index = (hash % (u64)PEERS_MAP_SIZE);
  assert(index < PEERS_MAP_SIZE);
  bucket = &map->buckets[index];
  assert(bucket->used);
  prev_bucket = 0;
  while (bucket && bucket->hash != hash) {
    prev_bucket = bucket;
    bucket = bucket->next;
  }
  assert(bucket);
  if (!prev_bucket) {
    bucket->used = false;
  } else {
    prev_bucket->next = bucket->next;
    bucket->next = map->free_first;
    map->free_first = bucket;
  }
}

void peer_info_hashmap_get(PeerInfoHashMap *map, u64 hash, u32 *address,
                           u32 *port) {
  u64 index;
  PeerInfoBucket *bucket;
  index = (hash % (u64)PEERS_MAP_SIZE);
  assert(index < PEERS_MAP_SIZE);
  bucket = &map->buckets[index];
  assert(bucket->used);
  while (bucket && bucket->hash != hash) {
    bucket = bucket->next;
  }
  assert(bucket);
  *address = bucket->address;
  *port = bucket->port;
}

PeerInfoBucket *peer_info_hashmap_get_bucket(PeerInfoHashMap *map, u64 hash) {
  u64 index;
  PeerInfoBucket *bucket;
  index = (hash % (u64)PEERS_MAP_SIZE);
  assert(index < PEERS_MAP_SIZE);
  bucket = &map->buckets[index];
  if (bucket->used) {
    return 0;
  }
  while (bucket && bucket->hash != hash) {
    bucket = bucket->next;
  }
  return bucket;
}

typedef struct Peer {
  ConnState conn;

  MessageHeader *messages_first;
  MessageHeader *messages_last;
  u32 messages_count;

  struct Peer *next;
  struct Peer *prev;
} Peer;

Message *peer_messages_pop(Peer *peer) {
  MessageHeader *msg;
  msg = peer->messages_first;
  if (msg) {
    dllist_remove(peer->messages_first, peer->messages_last, msg);
    --peer->messages_count;
    msg->next = 0;
    msg->prev = 0;
    return (Message *)msg;
  }
  return 0;
}

#define SERVER_LISTENER_PORT 8080
#define SERVER_STUN_PORT 8081

typedef struct Server {
  Arena arena;
  Arena scratch;

  struct sockaddr_in stun_addr;
  SOCKET stun_socket;

  struct sockaddr_in listener_addr;
  SOCKET listener_socket;

  Peer *peers_first;
  Peer *peers_last;
  u32 peers_count;
  PeerInfoHashMap peer_info_map;

  Peer *free_peers;
  MessageHeader *free_messages;
  void (*message_callback)(struct Server *server, Peer *peer, Message *msg);
} Server;

int server_init(Server *server,
                void (*message_callback)(struct Server *server, Peer *peer,
                                         Message *msg)) {
  u64 memory_size, scratch_size;
  u8 *memory, *scratch;

  memset(server, 0, sizeof(*server));
  memory_size = mb(64);
  memory = (u8 *)malloc(memory_size);
  arena_init(&server->arena, memory, memory_size);

  scratch_size = mb(8);
  scratch = (u8 *)malloc(scratch_size);
  arena_init(&server->scratch, scratch, scratch_size);

  server->message_callback = message_callback;

  {
    server->listener_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listener_socket == INVALID_SOCKET) {
      printf("failed to create listener socket\n");
      return 1;
    }

    server->listener_addr.sin_family = AF_INET;
    server->listener_addr.sin_addr.s_addr = INADDR_ANY;
    server->listener_addr.sin_port = htons(SERVER_LISTENER_PORT);

    if (bind(server->listener_socket, (struct sockaddr *)&server->listener_addr,
             sizeof(server->listener_addr)) == SOCKET_ERROR) {
      printf("failed to bind listener socketn\n");
      return 1;
    }

    if (listen(server->listener_socket, SOMAXCONN) == SOCKET_ERROR) {
      printf("failed to listen for connections\n");
      return 1;
    }
  }

  {
    server->stun_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server->stun_socket == INVALID_SOCKET) {
      printf("failed to create stun socket\n");
      return 1;
    }

    server->stun_addr.sin_family = AF_INET;
    server->stun_addr.sin_addr.s_addr = INADDR_ANY;
    server->stun_addr.sin_port = htons(SERVER_STUN_PORT);

    if (bind(server->stun_socket, (struct sockaddr *)&server->stun_addr,
             sizeof(server->stun_addr)) == SOCKET_ERROR) {
      printf("failed to bind stun socketn\n");
      return 1;
    }
  }

  return 0;
}

Message *server_message_alloc(Server *server) {
  MessageHeader *msg;
  if (server->free_messages) {
    msg = server->free_messages;
    server->free_messages = server->free_messages->next;
  } else {
    msg = (MessageHeader *)arena_alloc(&server->arena, sizeof(Message), 4);
  }
  memset(msg, 0, sizeof(Message));
  return (Message *)msg;
}

void server_message_free(Server *server, Message *msg) {
  msg->header.next = server->free_messages;
  msg->header.prev = 0;
  server->free_messages = &msg->header;
}

Peer *server_peer_alloc(Server *server) {
  Peer *peer;
  if (server->free_peers) {
    peer = server->free_peers;
    server->free_peers = server->free_peers->next;
  } else {
    peer = arena_alloc(&server->arena, sizeof(*peer), 4);
  }
  memset(peer, 0, sizeof(*peer));

  dllist_push_back(server->peers_first, server->peers_last, peer);
  server->peers_count++;

  return peer;
}

void server_peer_free(Server *server, Peer *peer) {
  MessageHeader *msg;
  assert(server->peers_count > 0);

  msg = peer->messages_first;
  while (msg != 0) {
    MessageHeader *to_free;
    to_free = msg;
    msg = msg->next;
    server_message_free(server, (Message *)to_free);
  }

  dllist_remove(server->peers_first, server->peers_last, peer);
  server->peers_count--;

  peer->next = server->free_peers;
  peer->prev = 0;
  server->free_peers = peer;
}

int server_process_peers(Server *server) {
  fd_set readfds, writefds;
  Peer *peer;

  FD_ZERO(&writefds);
  FD_ZERO(&readfds);
  FD_SET(server->listener_socket, &readfds);
  FD_SET(server->stun_socket, &readfds);

  for (peer = server->peers_first; peer != 0; peer = peer->next) {
    FD_SET(peer->conn.sock, &readfds);
    if (peer->messages_count) {
      FD_SET(peer->conn.sock, &writefds);
    }
  }

  if (select(0, &readfds, &writefds, 0, 0) == SOCKET_ERROR) {
    return 1;
  }

  if (FD_ISSET(server->stun_socket, &readfds)) {
    s32 size, from_size;
    u32 mark;
    u8 buffer[64];
    Message msg;
    struct sockaddr_in from;
    from_size = sizeof(from);
    size = recvfrom(server->stun_socket, (char *)buffer, sizeof(buffer), 0,
                    (struct sockaddr *)&from, &from_size);
    mark = server->scratch.used;
    message_deserialize(&server->scratch, buffer, size, &msg);
    if (msg.header.type == MessageType_STUN_REQUEST) {
      if (size != 0 && size != SOCKET_ERROR) {
        u8 *response_buffer;
        u64 response_size, mark;
        Message response;
        mark = server->scratch.used;
        response.header.type = MessageType_STUN_RESPONSE;
        response.stun_response.address = ntohl(from.sin_addr.S_un.S_addr);
        response.stun_response.port = ntohs(from.sin_port);
        message_serialize(&response, 0, &response_size);
        response_buffer = (u8 *)arena_alloc(&server->scratch, response_size, 4);
        message_serialize(&response, response_buffer, &response_size);
        printf("sending stun response: %llu\n", response_size);
        if (sendto(server->stun_socket, (char *)response_buffer, response_size,
                   0, (struct sockaddr *)&from, from_size) == SOCKET_ERROR) {
          printf("failed to send stun response\n");
          return 1;
        }
        server->scratch.used = mark;
      }
    } else {
      /* TODO: ingnore keep alive messages */
      printf("Keep alive message receive\n");
    }
    server->scratch.used = mark;
  }

  if (FD_ISSET(server->listener_socket, &readfds)) {
    s32 addr_len;
    Peer *peer;
    addr_len = (s32)sizeof(peer->conn.addr);
    peer = server_peer_alloc(server);
    peer->conn.sock =
        accept(server->listener_socket, &peer->conn.addr, &addr_len);
    if (peer->conn.sock == INVALID_SOCKET) {
      /* TODO: alloc peer after checking the socket error */
      return 1;
    }
  }

  peer = server->peers_first;
  while (peer != 0) {
    if (FD_ISSET(peer->conn.sock, &writefds)) {
      u64 mark;
      Message *msg;
      msg = peer_messages_pop(peer);
      mark = server->scratch.used;
      message_write(&server->scratch, &peer->conn, msg);
      server->scratch.used = mark;
    }

    if (FD_ISSET(peer->conn.sock, &readfds)) {
      u64 mark;
      Message msg;
      mark = server->scratch.used;
      if (message_read(&server->scratch, &peer->conn, &msg)) {
        Peer *to_free;
        to_free = peer;
        peer = peer->next;
        closesocket(to_free->conn.sock);
        server_peer_free(server, to_free);
        continue;
      }
      server->message_callback(server, peer, &msg);
      server->scratch.used = mark;
    }

    peer = peer->next;
  }

  return 0;
}

void conn_parse_address_and_port(ConnState *conn, u32 *address, u16 *port) {
  *address = ntohl(((struct sockaddr_in *)&conn->addr)->sin_addr.S_un.S_addr);
  *port = ntohs(((struct sockaddr_in *)&conn->addr)->sin_port);
}

u64 conn_hash(ConnState *conn) {
  u32 address;
  u16 port;
  u64 hash;
  conn_parse_address_and_port(conn, &address, &port);
  hash = (((u64)address) << 32) | (u64)port;
  return hash;
}

void message_callback(Server *server, Peer *peer, Message *msg) {
  Peer *other;
  switch (msg->header.type) {
  case MessageType_PEER_CONNECTED: {
    u64 hash, mark;
    MessagePeerConnected *peer_connected;
    Message send_msg;
    peer_connected = &msg->peer_connected;
    hash = conn_hash(&peer->conn);
    peer_info_hashmap_insert(&server->arena, &server->peer_info_map, hash,
                             peer_connected->address, peer_connected->port);
    memset(&send_msg, 0, sizeof(send_msg));
    send_msg.header.type = MessageType_PEERS_INFO;
    mark = server->scratch.used;
    for (other = server->peers_first; other != 0; other = other->next) {
      u64 other_hash;
      if (other == peer) {
        continue;
      }
      other_hash = conn_hash(&other->conn);
      if (peer_info_hashmap_has(&server->peer_info_map, other_hash)) {
        PeerInfoNode *node;
        node = (PeerInfoNode *)arena_alloc(&server->scratch, sizeof(*node), 4);
        conn_parse_address_and_port(&other->conn, &node->address, &node->port);
        dllist_push_back(send_msg.peers_info.first, send_msg.peers_info.last,
                         node);
      }
    }
    message_write(&server->scratch, &peer->conn, &send_msg);
    server->scratch.used = mark;
  } break;
  default: {
    assert(!"invalid code path");
  }
  }
}

int main(void) {
  static Server static_server;
  WSADATA wsa_data;
  Server *server;

  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    printf("failed to startup windows wsa\n");
    return 1;
  }

  server = &static_server;
  if (server_init(server, message_callback) != 0) {
    printf("failed to initialize server\n");
    return 1;
  }

  for (;;) {
    if (server_process_peers(server) != 0) {
      printf("failed to process peers\n");
      return 1;
    }
  }

  return 0;
}
