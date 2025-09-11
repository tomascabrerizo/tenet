#ifndef _NET_H_
#define _NET_H_

#include "core.h"

typedef u64 Conn;
typedef struct ConnAddr ConnAddr;
typedef struct ConnSet ConnSet;

#define CONN_INFINITY ((u32) - 1)

#define CONN_INVALID ((u32) - 1)
#define CONN_ERROR ((u32)1)
#define CONN_OK ((u32)0)

typedef struct ConnErr {
  Conn conn;
  u32 err;
} ConnErr;

void conn_init(void);

struct ConnAddr *conn_address_create(struct Arena *arena);
struct ConnAddr *conn_address(struct Arena *arena, char *address, u16 port);

struct ConnSet *conn_set_create(struct Arena *arena);
void conn_set_clear(struct ConnSet *set);
void conn_set_add(struct ConnSet *set, Conn conn);
b32 conn_set_has(struct ConnSet *set, Conn conn);

s32 conn_select(struct ConnSet *read, struct ConnSet *write, u32 ms);

ConnErr conn_tcp(void);
ConnErr conn_udp(void);

u32 conn_bind(Conn conn, struct ConnAddr *addr);
u32 conn_listen(Conn conn);
u32 conn_connect(Conn conn, struct ConnAddr *addr);
ConnErr conn_accept(Conn conn, struct ConnAddr *addr);

u32 conn_read(Conn conn, u8 *buffer, u32 size);
u32 conn_write(Conn conn, u8 *buffer, u32 size);
u32 conn_read_from(Conn conn, u8 *buffer, u32 size, struct ConnAddr *from);
u32 conn_write_to(Conn conn, u8 *buffer, u32 size, struct ConnAddr *to);

void conn_close(Conn conn);

#endif
