#include "core.h"
#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#define TCP_CONN_DEFAULT_BUFFER_SIZE (kb(10))

typedef struct ConnAddr {
  struct sockaddr_in addr_in;
} ConnAddr;

typedef struct ConnSet {
  fd_set fds;
} ConnSet;

void conn_init(void) {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);
}

ConnAddr *conn_address_create(Arena *arena) {
  ConnAddr *addr;
  addr = arena_alloc(arena, sizeof(*addr), 8);
  memset(addr, 0, sizeof(*addr));
  return addr;
}

ConnAddr *conn_address(Arena *arena, char *address, u16 port) {
  ConnAddr *addr = conn_address_create(arena);
  addr->addr_in.sin_family = AF_INET;
  addr->addr_in.sin_port = htons(port);
  inet_pton(addr->addr_in.sin_family, address, &addr->addr_in.sin_addr);
  return addr;
}

ConnSet *conn_set_create(Arena *arena) {
  ConnSet *set;
  set = arena_alloc(arena, sizeof(*set), 8);
  assert(set);
  memset(set, 0, sizeof(*set));
  return set;
}

void conn_set_clear(ConnSet *set) { FD_ZERO(&set->fds); }

void conn_set_add(ConnSet *set, Conn conn) {
  SOCKET sock;
  sock = (SOCKET)conn;
  FD_SET(sock, &set->fds);
}

b32 conn_set_has(ConnSet *set, Conn conn) {
  SOCKET sock;
  sock = (SOCKET)conn;
  return FD_ISSET(sock, &set->fds);
}

s32 conn_select(ConnSet *read, ConnSet *write, s32 ms) {
  struct timeval val, *val_ptr;
  fd_set *fd_read, *fd_write;
  val_ptr = 0;
  if (ms > -1) {
    val.tv_sec = ms / 1000;
    val.tv_usec = (ms % 1000) * 1000;
    val_ptr = &val;
  }
  fd_read = fd_write = 0;
  if (read) {
    fd_read = &read->fds;
  }
  if (write) {
    fd_write = &write->fds;
  }
  return select(0, fd_read, fd_write, 0, val_ptr);
}

Conn conn_tcp(void) {
  SOCKET sock;
  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  return (Conn)sock;
}

Conn conn_udp(void) {
  SOCKET sock;
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  return (Conn)sock;
}

b32 conn_bind(Conn conn, ConnAddr *addr) {
  SOCKET sock;
  sock = (SOCKET)conn;
  if (bind(sock, (struct sockaddr *)&addr->addr_in,
           (int)sizeof(addr->addr_in)) == SOCKET_ERROR) {
    return false;
  }
  return true;
}

b32 conn_listen(Conn conn) {
  SOCKET sock;
  sock = (SOCKET)conn;
  if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
    return false;
  }
  return true;
}

b32 conn_connect(Conn conn, ConnAddr *addr) {
  SOCKET sock;
  sock = (SOCKET)conn;
  assert(sock != INVALID_SOCKET);
  if (connect(sock, (struct sockaddr *)&addr->addr_in,
              (int)sizeof(addr->addr_in)) == SOCKET_ERROR) {
    return false;
  }
  return true;
}

Conn conn_accept(Conn conn, ConnAddr *addr) {
  SOCKET sock, other;
  s32 other_addr_len;
  struct sockaddr_in other_addr;
  other_addr_len = sizeof(other_addr);
  sock = (SOCKET)conn;
  other = accept(sock, (struct sockaddr *)&other_addr, &other_addr_len);
  if (addr) {
    addr->addr_in = other_addr;
  }
  return (Conn)other;
}

s32 conn_read_from(Conn conn, u8 *buffer, u32 size, ConnAddr *from) {
  s32 res, addr_size;
  SOCKET sock;
  sock = (SOCKET)conn;
  addr_size = (u32)sizeof(from->addr_in);
  res = recvfrom(sock, (char *)buffer, size, 0,
                 (struct sockaddr *)&from->addr_in, &addr_size);
  return res;
}

s32 conn_write_to(Conn conn, u8 *buffer, u32 size, ConnAddr *to) {
  s32 res, addr_size;
  SOCKET sock;
  sock = (SOCKET)conn;
  addr_size = (u32)sizeof(to->addr_in);
  res = sendto(sock, (char *)buffer, size, 0, (struct sockaddr *)&to->addr_in,
               addr_size);
  return res;
}

s32 conn_read(Conn conn, u8 *buffer, u32 size) {
  SOCKET sock;
  sock = (SOCKET)conn;
  return recv(sock, (char *)buffer, size, 0);
}

s32 conn_write(Conn conn, u8 *buffer, u32 size) {
  SOCKET sock;
  sock = (SOCKET)conn;
  return send(sock, (char *)buffer, size, 0);
}

void conn_close(Conn conn) {
  SOCKET sock;
  sock = (SOCKET)conn;
  closesocket(sock);
}

b32 conn_valid(Conn conn) {
  SOCKET sock;
  sock = (SOCKET)conn;
  return sock != INVALID_SOCKET;
}
