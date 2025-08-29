#define inline
#include <winsock2.h>
#include <ws2tcpip.h>
#undef inline

#include "core.h"
#include "message.h"

#define SERVER_ADDRESS "127.0.0.1"
#define SERVER_PORT "8080"
#define SERVER_STUN_PORT "8081"

typedef enum PeerState {
  PeerState_DONT_KNOW_ITSELF,
  PeerState_WAITIN_STUN_RESPONSE,
  PeerState_KNOW_ITSELF,
  PeerState_WAITIN_PEERS_INFO,
  PeerState_PEERS_INFO_RECIEVE,
  PeerState_TRYING_TO_CONNECT,
  PeerState_CONNECTED,
  PeerState_COUNT
} PeerState;

typedef struct Peer {
  Arena arena;
  SOCKET ctrl_socket;
  SOCKET stun_socket;
  struct addrinfo *ctrl_addr;
  struct addrinfo *stun_addr;
  PeerState state;
} Peer;

void peer_process_state(Peer *peer) {
  switch (peer->state) {
  case PeerState_DONT_KNOW_ITSELF: {
    u64 size, mark;
    u8 *buffer;
    Message msg;
    mark = peer->arena.used;
    msg.header.type = MessageType_STUN_REQUEST;
    message_serialize(&msg, 0, &size);
    buffer = arena_alloc(&peer->arena, size, 4);
    message_serialize(&msg, buffer, &size);
    sendto(peer->stun_socket, (char *)buffer, size, 0, peer->stun_addr->ai_addr,
           (int)peer->stun_addr->ai_addrlen);
    peer->arena.used = mark;
    peer->state = PeerState_WAITIN_STUN_RESPONSE;
  } break;
  case PeerState_WAITIN_STUN_RESPONSE: {
    fd_set readfds;
    struct timeval timeout;
    FD_ZERO(&readfds);
    FD_SET(peer->stun_socket, &readfds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    select(0, &readfds, 0, 0, &timeout);
    if (FD_ISSET(peer->stun_socket, &readfds)) {
      /* TODO: saver address and port info */
      peer->state = PeerState_KNOW_ITSELF;
    } else {
      peer->state = PeerState_DONT_KNOW_ITSELF;
    }
  } break;
  case PeerState_KNOW_ITSELF: {
    /* peer needs to send his info over tcp to the ctrl server */
    /* it also need to mantains his port mapped sending alive packet to stun
     * server */
  } break;
  case PeerState_WAITIN_PEERS_INFO: {
    /* just wait for ctrl server response */
  } break;
  case PeerState_PEERS_INFO_RECIEVE: {
    /* we got the information of the peer try to connect to all of them */
  } break;
  case PeerState_TRYING_TO_CONNECT: {
    /* we are performing hole punching if needed */
  } break;
  case PeerState_CONNECTED: {
    /* we where able to connect to all peers */
  } break;
  default: {
    assert(!"invalid code path");
  }
  }
}

int peer_ctrl_server_init(Peer *peer) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (getaddrinfo(SERVER_ADDRESS, SERVER_PORT, &hints, &peer->ctrl_addr) != 0) {
    printf("failed to get addess info for server: %s:%s\n", SERVER_ADDRESS,
           SERVER_PORT);
    return 1;
  }
  peer->ctrl_socket =
      socket(peer->ctrl_addr->ai_family, peer->ctrl_addr->ai_socktype,
             peer->ctrl_addr->ai_protocol);
  if (peer->ctrl_socket == INVALID_SOCKET) {
    printf("failed to create ctrl socket\n");
    return 1;
  }
  if (connect(peer->ctrl_socket, peer->ctrl_addr->ai_addr,
              (int)peer->ctrl_addr->ai_addrlen) == SOCKET_ERROR) {
    printf("failed to connect to server server: %s:%s\n", SERVER_ADDRESS,
           SERVER_PORT);
    return 1;
  }
  return 0;
}

int peer_stun_server_init(Peer *peer) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  if (getaddrinfo(SERVER_ADDRESS, SERVER_STUN_PORT, &hints, &peer->stun_addr) !=
      0) {
    printf("failed to get addess info for stun server: %s:%s\n", SERVER_ADDRESS,
           SERVER_STUN_PORT);
    return 1;
  }
  peer->stun_socket =
      socket(peer->stun_addr->ai_family, peer->stun_addr->ai_socktype,
             peer->stun_addr->ai_protocol);
  if (peer->stun_socket == INVALID_SOCKET) {
    printf("failed to create stun socket\n");
    return 1;
  }
  return 0;
}

int peer_init(Peer *peer) {
  u64 memory_size;
  u8 *memory;
  memory_size = mb(8);
  memory = (u8 *)malloc(memory_size);
  arena_init(&peer->arena, memory, memory_size);
  if (peer_ctrl_server_init(peer) != 0) {
    printf("failed to initialize control server\n");
    return 1;
  }
  if (peer_stun_server_init(peer) != 0) {
    printf("failed to initialize stun server\n");
    return 1;
  }
  peer->state = PeerState_DONT_KNOW_ITSELF;
  return 0;
}

int main(void) {
  static Peer static_peer;
  WSADATA wsa_data;
  Peer *peer;

  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    printf("failed to startup windows wsa\n");
    return 1;
  }

  peer = &static_peer;
  peer_init(peer);

  for (;;) {
    peer_process_state(peer);
    Sleep(1000 * 3);
  }

  return 0;
}
