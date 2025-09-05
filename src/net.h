#ifndef _NET_H_
#define _NET_H_

struct Arena;
void conn_init(struct Arena *arena);

typedef void *ConnAddress;
struct ConnSet;
struct ConnTimeout;
struct ConnTCP;

ConnAddress conn_address(struct Arena *arena, char *address, u16 port);

struct ConnSet *conn_set_create(void);
void conn_set_destroy(struct ConnSet *set);
void conn_set_clear(struct ConnSet *set);
void conn_set_add(struct ConnSet *set, struct ConnTCP *conn);
b32 conn_set_has(struct ConnSet *set, struct ConnTCP *conn);

struct ConnTimeout *conn_timeout_create(void);
void conn_timeout_destroy(struct ConnTimeout *timeout);
void conn_timeout_set(struct ConnTimeout *timeout, s32 ms);

s32 conn_select(struct ConnSet *read, struct ConnSet *write,
                struct ConnTimeout *timeout);

struct ConnTCP *conn_tcp_create(void);
void conn_tcp_destroy(struct ConnTCP *conn);
b32 conn_tcp_listen(struct ConnTCP *conn, u16 port);
b32 conn_tcp_connect(struct ConnTCP *conn, ConnAddress address);
struct ConnTCP *conn_tcp_accept(struct ConnTCP *conn);

#endif
