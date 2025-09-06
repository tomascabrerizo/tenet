#ifndef _NET_H_
#define _NET_H_

#include "core.h"

typedef u64 Conn;
struct ConnAddr;
struct ConnSet;
struct Arena;

void conn_init(void);

struct ConnAddr *conn_address_create(struct Arena *arena);
struct ConnAddr *conn_address(struct Arena *arena, char *address, u16 port);

struct ConnSet *conn_set_create(struct Arena *arena);
void conn_set_clear(struct ConnSet *set);
void conn_set_add(struct ConnSet *set, Conn conn);
b32 conn_set_has(struct ConnSet *set, Conn conn);

s32 conn_select(struct ConnSet *read, struct ConnSet *write, s32 ms);

Conn conn_tcp(void);
Conn conn_udp(void);

b32 conn_bind(Conn conn, struct ConnAddr *addr);
b32 conn_listen(Conn conn);
b32 conn_connect(Conn conn, struct ConnAddr *addr);
Conn conn_accept(Conn conn, struct ConnAddr *addr);

s32 conn_read(Conn conn, u8 *buffer, u32 size);
s32 conn_write(Conn conn, u8 *buffer, u32 size);
s32 conn_read_from(Conn conn, u8 *buffer, u32 size, struct ConnAddr *from);
s32 conn_write_to(Conn conn, u8 *buffer, u32 size, struct ConnAddr *to);

void conn_close(Conn conn);
b32 conn_valid(Conn conn);

#endif
