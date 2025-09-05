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
  PeerState_ONLY_PEER_IN_NETWORK,
  PeerState_CONNECTED,
  PeerState_COUNT
} PeerState;

typedef enum PeerConnectionState {
  PeerConnectionState_NOT_CONNECTED,
  PeerConnectionState_HOLE_PUNCHING,
  PeerConnectionState_DIRECT_CONNECTION,
  PeerConnectionState_WAIT_SYN,
  PeerConnectionState_WAIT_ACK,
  PeerConnectionState_CONNECTED
} PeerConnectionState;

typedef struct PeerConnection {
  PeerConnectionState state;
  u32 address;
  u16 port;
  struct PeerConnection *next;
  struct PeerConnection *prev;
} PeerConnection;

typedef struct Peer {
  Arena arena;
  SOCKET stun_socket;
  struct addrinfo *stun_addr;
  ConnState ctrl_conn;

  PeerState state;
  PeerConnection *conn_first;
  PeerConnection *conn_last;

  u64 own_address;
  u16 own_port;
} Peer;

struct sockaddr_in peer_conn_get_sockaddr(PeerConnection *peer_conn) {
  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(peer_conn->port);
  address.sin_addr.S_un.S_addr = htonl(peer_conn->address);
  return address;
}

void peer_process_conections(Peer *peer, PeerConnection *peer_conn,
                             Message *msg) {
  switch (peer_conn->state) {
  case PeerConnectionState_NOT_CONNECTED: {
    /* TODO: if peer and peer_conn came from the same ip try local connections
     */
    peer_conn->state = PeerConnectionState_HOLE_PUNCHING;
  } break;
  case PeerConnectionState_HOLE_PUNCHING: {
    if (msg) {
      peer_conn->state = PeerConnectionState_WAIT_SYN;
    } else {
      struct sockaddr_in to;
      Message msg;
      msg.header.type = MessageType_HOLE_PUNCH;
      to = peer_conn_get_sockaddr(peer_conn);
      message_writeto(&peer->arena, peer->stun_socket, (struct sockaddr *)&to,
                      &msg);
    }
  } break;
  case PeerConnectionState_DIRECT_CONNECTION: {
    /* TODO: try to connect to the peer using the local ip address and port */
  } break;
  case PeerConnectionState_WAIT_SYN: {
    if (msg && msg->header.type == MessageType_SYN) {
      peer_conn->state = PeerConnectionState_WAIT_ACK;
    } else {
      struct sockaddr_in to;
      Message msg;
      msg.header.type = MessageType_SYN;
      to = peer_conn_get_sockaddr(peer_conn);
      message_writeto(&peer->arena, peer->stun_socket, (struct sockaddr *)&to,
                      &msg);
    }
  } break;
  case PeerConnectionState_WAIT_ACK: {
    if (msg && msg->header.type == MessageType_SYN_ACK) {
      peer_conn->state = PeerConnectionState_CONNECTED;
    } else {
      struct sockaddr_in to;
      Message msg;
      msg.header.type = MessageType_SYN_ACK;
      to = peer_conn_get_sockaddr(peer_conn);
      message_writeto(&peer->arena, peer->stun_socket, (struct sockaddr *)&to,
                      &msg);
    }
  } break;
  case PeerConnectionState_CONNECTED: {
    /* TODO: Wait for a new peer connection or else just comunicate with other
     * peers */
  } break;
  }
}

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
    fd_set readfds;
    struct timeval timeout;
    FD_ZERO(&readfds);
    FD_SET(peer->ctrl_conn.sock, &readfds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    count = select(0, &readfds, 0, 0, &timeout);
    if (count > 0) {
      if (FD_ISSET(peer->ctrl_conn.sock, &readfds)) {
        PeerInfoNode *info_node;
        Message msg;
        message_read(&peer->arena, &peer->ctrl_conn, &msg);
        assert(msg.header.type == MessageType_PEERS_INFO);
        for (info_node = msg.peers_info.first; info_node != 0;
             info_node = info_node->next) {
          PeerConnection *conn;
          conn = (PeerConnection *)arena_alloc(&peer->arena, sizeof(*conn), 8);
          memset(conn, 0, sizeof(*conn));
          conn->address = info_node->address;
          conn->port = info_node->port;
          conn->state = PeerConnectionState_NOT_CONNECTED;
          dllist_push_back(peer->conn_first, peer->conn_last, conn);
        }
        peer->state = PeerState_PEERS_INFO_RECIEVE;
      }
    } else {
      u64 mark;
      Message msg;
      mark = peer->arena.used;
      msg.header.type = MessageType_KEEPALIVE;
      message_writeto(&peer->arena, peer->stun_socket, peer->stun_addr->ai_addr,
                      &msg);
      peer->arena.used = mark;
    }
  } break;
  case PeerState_PEERS_INFO_RECIEVE: {
    if (!dllist_empty(peer->conn_first, peer->conn_last)) {
      PeerConnection *other;
      s32 count;
      fd_set readfds;
      struct timeval timeout;
      FD_ZERO(&readfds);
      FD_SET(peer->stun_socket, &readfds);
      timeout.tv_sec = 0;
      timeout.tv_usec = 200 * 1000;
      count = select(0, &readfds, 0, 0, &timeout);
      for (other = peer->conn_first; other != 0; other = other->next) {
        if (FD_ISSET(peer->stun_socket, &readfds)) {
          struct sockaddr from;
          Message msg;
          u32 msg_address;
          u16 msg_port;
          message_readfrom(&peer->arena, peer->stun_socket, &from, &msg);
          sockaddr_parse_address_and_port(&from, &msg_address, &msg_port);
          if (other->address == msg_address && other->port == msg_port) {
            peer_process_conections(peer, other, &msg);
          }
        }
        if (count == 0) {
          peer_process_conections(peer, other, 0);
        }
      }
    } else {
      peer->state = PeerState_ONLY_PEER_IN_NETWORK;
    }
  } break;
  case PeerState_ONLY_PEER_IN_NETWORK: {
    s32 count;
    fd_set readfds;
    struct timeval timeout;
    FD_ZERO(&readfds);
    FD_SET(peer->ctrl_conn.sock, &readfds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    count = select(0, &readfds, 0, 0, &timeout);
    if (count > 0) {
      if (FD_ISSET(peer->ctrl_conn.sock, &readfds)) {
        PeerConnection *conn;
        Message msg;
        message_read(&peer->arena, &peer->ctrl_conn, &msg);
        assert(msg.header.type == MessageType_PEER_CONNECTED);
        /* TODO: connect to new peer (hole punch if necesary) */

        unused(conn);
        /* conn = (PeerConnection *)arena_alloc(&peer->arena, sizeof(*conn), 8);
        memset(conn, 0, sizeof(*conn));
        conn->address = info_node->address;
        conn->port = info_node->port;
        conn->state = PeerConnectionState_NOT_CONNECTED;
        dllist_push_back(peer->conn_first, peer->conn_last, conn); */

        peer->state = PeerState_CONNECTED;
      }
    } else {
      u64 mark;
      Message msg;
      mark = peer->arena.used;
      msg.header.type = MessageType_KEEPALIVE;
      message_writeto(&peer->arena, peer->stun_socket, peer->stun_addr->ai_addr,
                      &msg);
      peer->arena.used = mark;
    }
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
  if (peer_stun_server_init(peer) != 0) {
    printf("failed to initialize stun server\n");
    return 1;
  }
  conn_state_connect(&peer->ctrl_conn, SERVER_ADDRESS, SERVER_PORT);
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
