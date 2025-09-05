#include "core.h"
#include "message.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#define SERVER_ADDRESS "127.0.0.1"
#define SERVER_PORT "8080"
#define SERVER_STUN_PORT "8081"

typedef struct StunState {
  SOCKET sock;
  struct sockaddr_in addr;
} StunState;

typedef struct Connection {
  ConnState ctrl;
  StunState stun;
  u32 timeout_ms;
} Connection;

typedef struct Peer {
  struct Peer *next;
  struct Peer *prev;
} Peer;

typedef struct PeerList {
  struct Peer *first;
  struct Peer *last;
} PeerList;

typedef struct MessageList {
  union Message *first;
  union Message *last;
} MessageList;

typedef struct Context {
  Arena arena;
  Arena scratch;
  Connection conn;
  PeerList pending;
  PeerList connected;
  MessageList messages;
} Context;

int stun_state_connect(StunState *stun, char *address, char *port) {
  struct addrinfo hints, *info, *res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  if (getaddrinfo(address, port, &hints, &info) != 0) {
    printf("failed to get addess info for stun server: %s:%s\n", address, port);
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

  stun->sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (stun->sock == INVALID_SOCKET) {
    printf("failed to create stun socket\n");
    freeaddrinfo(info);
    return 1;
  }
  stun->addr = *((struct sockaddr_in *)res->ai_addr);
  freeaddrinfo(info);
  return 0;
}

void conn_sate_init(Connection *conn) {
  conn_state_connect(&conn->ctrl, SERVER_ADDRESS, SERVER_PORT);
  stun_state_connect(&conn->stun, SERVER_ADDRESS, SERVER_STUN_PORT);
}

void context_init(Context *ctx) {
  u8 *memory, *scratch;
  u64 memory_size, scratch_size;

  memset(ctx, 0, sizeof(*ctx));
  memory_size = mb(10);
  memory = (u8 *)malloc(memory_size);
  arena_init(&ctx->arena, memory, memory_size);
  scratch_size = mb(10);
  scratch = (u8 *)malloc(scratch_size);
  arena_init(&ctx->scratch, scratch, scratch_size);

  conn_sate_init(&ctx->conn);
}

void context_begin(Context *ctx) {
  Connection *conn;
  struct timeval timeout, *timeout_ptr;
  fd_set readfds, writefds;
  int nfds;

  conn = &ctx->conn;
  FD_ZERO(&writefds);
  FD_ZERO(&readfds);

  FD_SET(conn->ctrl.sock, &readfds);
  FD_SET(conn->stun.sock, &readfds);

  if (!dllist_empty(ctx->messages.first, ctx->messages.last)) {
    FD_SET(conn->stun.sock, &writefds);
  }

  timeout_ptr = 0;
  if (conn->timeout_ms > 0) {
    timeout.tv_sec = conn->timeout_ms / 1000;
    timeout.tv_usec = (conn->timeout_ms % 1000) * 1000;
    timeout_ptr = &timeout;
  }

  nfds = max(ctx->conn.ctrl.sock, ctx->conn.stun.sock) + 1;
  select(nfds, &readfds, &writefds, 0, timeout_ptr);
}

void context_set_next_timeout(Context *ctx, u32 ms) {
  ctx->conn.timeout_ms = ms;
}

void context_end(Context *ctx) { ctx->scratch.used = 0; }

void context_update(Context *ctx) { /* TODO: ... */ }

int main(void) {
  static Context ctx;
  WSADATA wsa_data;

  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    printf("failed to startup windows wsa\n");
    return 1;
  }

  context_init(&ctx);

  for (;;) {
    context_begin(&ctx);
    context_update(&ctx);
    context_end(&ctx);
  }

  return 0;
}
