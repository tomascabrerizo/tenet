#include "core.h"
#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#define TCP_CONN_DEFAULT_BUFFER_SIZE (kb(10))

typedef struct ConnSet {
  fd_set fds;
  struct ConnSet *next;
} ConnSet;

typedef struct ConnTimeout {
  struct timeval *val_ptr;
  struct timeval val;
  struct ConnTimeout *next;
} ConnTimeout;

typedef struct ConnTCP {
  SOCKET sock;
  struct sockaddr_in addr;
  u8 recv_buffer[TCP_CONN_DEFAULT_BUFFER_SIZE];
  u64 recv_buffer_used;
  b32 farming;
  u32 bytes_to_farm;
  struct ConnTCP *next;
} ConnTCP;

typedef struct ConnContext {
  Arena *arena;
  ConnTCP *tcp_first_free;
  ConnSet *set_first_free;
  ConnTimeout *timeout_first_free;
} ConnContext;

static ConnContext context;

ConnTCP *conn_tcp_alloc(void) {
  ConnTCP *conn;
  if (context.tcp_first_free) {
    conn = context.tcp_first_free;
    context.tcp_first_free = context.tcp_first_free->next;
  } else {
    conn = arena_alloc(context.arena, sizeof(*conn), 8);
  }
  assert(conn);
  memset(conn, 0, sizeof(*conn));
  conn->sock = INVALID_SOCKET;
  return conn;
}

void conn_tcp_free(ConnTCP *conn) {
  assert(conn);
  conn->next = context.tcp_first_free;
  context.tcp_first_free = conn;
}

void conn_init(Arena *arena) {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);
  memset(&context, 0, sizeof(context));
  context.arena = arena;
}

ConnAddress conn_address(Arena *arena, char *address, u16 port) {
  struct sockaddr_in *addr;
  addr = arena_alloc(arena, sizeof(struct sockaddr_in), 8);
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  inet_pton(addr->sin_family, address, &addr->sin_addr);
  return (ConnAddress)addr;
}

ConnSet *conn_set_create(void) {
  ConnSet *set;
  if (context.set_first_free) {
    set = context.set_first_free;
    context.set_first_free = context.set_first_free->next;
  } else {
    set = arena_alloc(context.arena, sizeof(*set), 8);
  }
  assert(set);
  memset(set, 0, sizeof(*set));
  return set;
}

void conn_set_destroy(ConnSet *set) {
  assert(set);
  set->next = context.set_first_free;
  context.set_first_free = set;
}

void conn_set_clear(ConnSet *set) { FD_ZERO(&set->fds); }

void conn_set_add(ConnSet *set, struct ConnTCP *conn) {
  FD_SET(conn->sock, &set->fds);
}

b32 conn_set_has(ConnSet *set, ConnTCP *conn) {
  return FD_ISSET(conn->sock, &set->fds);
}

ConnTimeout *conn_timeout_create(void) {
  ConnTimeout *timeout;
  if (context.timeout_first_free) {
    timeout = context.timeout_first_free;
    context.timeout_first_free = context.timeout_first_free->next;
  } else {
    timeout = arena_alloc(context.arena, sizeof(*timeout), 8);
  }
  assert(timeout);
  memset(timeout, 0, sizeof(*timeout));
  return timeout;
}

void conn_timeout_destroy(ConnTimeout *timeout) {
  assert(timeout);
  timeout->next = context.timeout_first_free;
  context.timeout_first_free = timeout;
}

void conn_timeout_set(struct ConnTimeout *timeout, s32 ms) {
  timeout->val_ptr = 0;
  if (ms > -1) {
    timeout->val.tv_sec = ms / 1000;
    timeout->val.tv_usec = (ms % 1000) * 1000;
    timeout->val_ptr = &timeout->val;
  }
}

s32 conn_select(ConnSet *read, ConnSet *write, ConnTimeout *timeout) {
  fd_set *fd_read, *fd_write;
  fd_read = fd_write = 0;
  if (read) {
    fd_read = &read->fds;
  }
  if (write) {
    fd_write = &write->fds;
  }
  return select(0, fd_read, fd_write, 0, timeout->val_ptr);
}

ConnTCP *conn_tcp_create(void) {
  ConnTCP *conn;
  SOCKET sock;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    return 0;
  }
  conn = conn_tcp_alloc();
  conn->sock = sock;
  return conn;
}

void conn_tcp_destroy(ConnTCP *conn) {
  assert(conn);
  closesocket(conn->sock);
  conn_tcp_free(conn);
}

b32 conn_tcp_listen(ConnTCP *conn, u16 port) {
  struct sockaddr_in addr;
  assert(conn->sock != INVALID_SOCKET);

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(conn->sock, (struct sockaddr *)&addr, sizeof(addr)) ==
      SOCKET_ERROR) {
    return false;
  }
  if (listen(conn->sock, SOMAXCONN) == SOCKET_ERROR) {
    return false;
  }

  conn->addr = addr;
  return true;
}

b32 conn_tcp_connect(ConnTCP *conn, ConnAddress address) {
  assert(conn->sock != INVALID_SOCKET);
  if (connect(conn->sock, (struct sockaddr *)address,
              (int)sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
    return false;
  }
  return true;
}

struct ConnTCP *conn_tcp_accept(struct ConnTCP *conn) {
  SOCKET sock;
  struct sockaddr_in addr;
  s32 addr_len;
  ConnTCP *other;
  addr_len = sizeof(addr);
  sock = accept(conn->sock, (struct sockaddr *)&addr, &addr_len);
  if (sock == INVALID_SOCKET) {
    return 0;
  }
  other = conn_tcp_alloc();
  other->sock = sock;
  other->addr = addr;
  return other;
}
