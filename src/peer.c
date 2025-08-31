#include "core.h"
#include "message.h"

#include <winsock2.h>
#include <ws2tcpip.h>

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
  SOCKET stun_socket;
  struct addrinfo *stun_addr;
  ConnState ctrl_conn;

  PeerState state;
  PeerInfoNode *peer_node_fisrt;
  PeerInfoNode *peer_node_last;

  u64 own_address;
  u16 own_port;
} Peer;

void peer_process_state(Peer *peer) {
  switch (peer->state) {
  case PeerState_DONT_KNOW_ITSELF: {
    u64 mark;
    Message msg;
    mark = peer->arena.used;
    msg.header.type = MessageType_STUN_REQUEST;
    message_writeto(&peer->arena, peer->stun_socket, peer->stun_addr->ai_addr,
                    &msg);
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
      u64 mark;
      Message msg;
      mark = peer->arena.used;
      message_readfrom(&peer->arena, peer->stun_socket, 0, &msg);
      assert(msg.header.type == MessageType_STUN_RESPONSE);
      peer->own_address = msg.stun_response.address;
      peer->own_port = msg.stun_response.port;
      peer->state = PeerState_KNOW_ITSELF;
      peer->arena.used = mark;
    } else {
      peer->state = PeerState_DONT_KNOW_ITSELF;
    }
  } break;
  case PeerState_KNOW_ITSELF: {
    Message msg;
    msg.header.type = MessageType_PEER_CONNECTED;
    msg.peer_connected.address = peer->own_address;
    msg.peer_connected.port = peer->own_port;
    message_write(&peer->arena, &peer->ctrl_conn, &msg);
    peer->state = PeerState_WAITIN_PEERS_INFO;
  } break;
  case PeerState_WAITIN_PEERS_INFO: {
    s32 count;
    fd_set readfds, writefds;
    struct timeval timeout;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_SET(peer->ctrl_conn.sock, &readfds);
    FD_SET(peer->stun_socket, &writefds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    count = select(0, &readfds, 0, 0, &timeout);
    if (count > 0) {
      if (FD_ISSET(peer->ctrl_conn.sock, &readfds)) {
        Message msg;
        message_read(&peer->arena, &peer->ctrl_conn, &msg);
        assert(msg.header.type == MessageType_PEERS_INFO);
        assert(peer->peer_node_fisrt == 0 && peer->peer_node_last == 0);
        peer->peer_node_fisrt = msg.peers_info.first;
        peer->peer_node_last = msg.peers_info.last;
        peer->state = PeerState_PEERS_INFO_RECIEVE;
      }
    } else {
      if (FD_ISSET(peer->stun_socket, &writefds)) {
        u64 mark;
        Message msg;
        mark = peer->arena.used;
        msg.header.type = MessageType_KEEPALIVE;
        message_writeto(&peer->arena, peer->stun_socket,
                        peer->stun_addr->ai_addr, &msg);
        peer->arena.used = mark;
      }
    }
  } break;
  case PeerState_PEERS_INFO_RECIEVE: {
    /* TODO: we got the information of the peer try to connect to all of them */
  } break;
  case PeerState_TRYING_TO_CONNECT: {
    /* TODO: we are performing hole punching if needed */
  } break;
  case PeerState_CONNECTED: {
    /* TODO: we where able to connect to all peers */
  } break;
  default: {
    assert(!"invalid code path");
  }
  }
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
  memset(peer, 0, sizeof(*peer));
  memory_size = mb(8);
  memory = (u8 *)malloc(memory_size);
  arena_init(&peer->arena, memory, memory_size);
  conn_state_init(&peer->ctrl_conn, SERVER_ADDRESS, SERVER_PORT);
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
  }

  return 0;
}
