#include "core.h"
#include "message.h"

#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct Peer {
  SOCKET socket;
  u8 recv_buffer[kb(10)];
  u32 recv_buffer_used;
  b32 farming;
  u32 bytes_to_farm;

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

  Peer *free_peers;
  MessageHeader *free_messages;
  void (*message_callback)(struct Server *server, Message *msg);
} Server;

int server_init(Server *server,
                void (*message_callback)(struct Server *server, Message *msg)) {
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
    FD_SET(peer->socket, &readfds);
    if (peer->messages_count) {
      FD_SET(peer->socket, &writefds);
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
    Peer *peer;
    peer = server_peer_alloc(server);
    peer->socket = accept(server->listener_socket, 0, 0);
    if (peer->socket == INVALID_SOCKET) {
      /* TODO: alloc peer after checking the socket error */
      return 1;
    }
  }

  peer = server->peers_first;
  while (peer != 0) {
    if (FD_ISSET(peer->socket, &writefds)) {
      u8 *buffer;
      u64 mark, size;
      Message *msg;
      msg = peer_messages_pop(peer);
      message_serialize(msg, 0, &size);
      mark = server->arena.used;
      buffer = arena_alloc(&server->arena, size, 4);
      message_serialize(msg, buffer, &size);
      send(peer->socket, (char *)buffer, size, 0);
      server->arena.used = mark;
    }

    if (FD_ISSET(peer->socket, &readfds)) {
      s32 size;
      size =
          recv(peer->socket, (char *)peer->recv_buffer + peer->recv_buffer_used,
               array_len(peer->recv_buffer) - peer->recv_buffer_used, 0);
      if (size == 0 || size == SOCKET_ERROR) {
        Peer *to_free;
        to_free = peer;
        peer = peer->next;
        closesocket(to_free->socket);
        server_peer_free(server, to_free);
        continue;
      }
      peer->recv_buffer_used += size;

      if (!peer->farming) {
        if (peer->recv_buffer_used >= sizeof(u32)) {
          u8 *buffer = peer->recv_buffer;
          peer->bytes_to_farm = read_u32_be(buffer);
          peer->farming = true;
        }
      }

      if (peer->farming) {
        if (peer->recv_buffer_used >= peer->bytes_to_farm) {
          u32 extra_bytes;
          u64 mark;
          Message msg;
          mark = server->scratch.used;
          message_deserialize(&server->scratch, peer->recv_buffer,
                              peer->bytes_to_farm, &msg);
          server->message_callback(server, &msg);
          server->scratch.used = mark;

          extra_bytes = peer->recv_buffer_used - peer->bytes_to_farm;
          memcpy(peer->recv_buffer, peer->recv_buffer + peer->bytes_to_farm,
                 extra_bytes);
          peer->recv_buffer_used = extra_bytes;
          peer->bytes_to_farm = 0;
          peer->farming = false;
        }
      }
    }

    peer = peer->next;
  }

  return 0;
}

void server_dump(Server *server) {
  Peer *peer;
  Peer *p;
  MessageHeader *m;
  u32 free_count;
  u32 free_msg_count;

  printf("=== Server Dump ===\n");
  printf("Listener socket: %d\n", (s32)server->listener_socket);
  printf("Peers count: %u\n", server->peers_count);

  printf("Arena used: %u\n", (u32)server->arena.used);
  printf("Arena size: %u\n", (u32)server->arena.size);
  printf("Scratch used: %u\n", (u32)server->scratch.used);
  printf("Scratch size: %u\n", (u32)server->scratch.size);

  free_count = 0;
  for (p = server->free_peers; p != 0; p = p->next) {
    free_count++;
  }
  printf("Free peers: %u\n", free_count);

  free_msg_count = 0;
  for (m = server->free_messages; m != 0; m = m->next) {
    free_msg_count++;
  }
  printf("Free messages: %u\n", free_msg_count);

  printf("--- Active Peers ---\n");
  for (peer = server->peers_first; peer != 0; peer = peer->next) {
    printf("Peer socket: %d\n", (s32)peer->socket);
    printf("  Messages in queue: %u\n", peer->messages_count);
    printf("  Recv buffer used: %u bytes\n", peer->recv_buffer_used);
    printf("  Farming: %s\n", peer->farming ? "true" : "false");
    printf("  Bytes to farm: %u\n", peer->bytes_to_farm);
  }
  printf("===================\n");
}

void message_callback(Server *server, Message *msg) {
  switch (msg->header.type) {
  case MessageType_PEER_CONNECTED: {
    printf("MessagePeerConnected receive: %d:%d\n", msg->peer_connected.address,
           msg->peer_connected.port);
  } break;
  case MessageType_PEERS_INFO: {
    printf("MessagePeersInfo receive, peers count: %d\n",
           msg->peers_info.peers_count);
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
    /* server_dump(server); */
    if (server_process_peers(server) != 0) {
      printf("failed to process peers\n");
      return 1;
    }
  }

  return 0;
}
