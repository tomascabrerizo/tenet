#include "proto.h"

typedef struct Peer {
  Stream stream;
  MessageHeader *messages_first;
  MessageHeader *messages_last;
  u32 timeout;
  u32 last_activity;

  struct Peer *next;
  struct Peer *prev;
} Peer;

typedef struct Context {
  Arena arena;
  Arena event_arena;

  Stream ctrl;
  Dgram stun;

  ConnAddr *ctrl_addr;
  ConnAddr *stun_addr;

  ConnSet *read;
  ConnSet *write;

  Peer *peers_fist;
  Peer *peers_last;

  Peer *peers_first_free;
  MessageHeader *messages_first_free;

  u32 timeout;
  b32 running;
} Context;

b32 ctrl_server_init(Stream *ctrl, ConnAddr *addr) {
  ConnErr tcp;
  tcp = conn_tcp();
  if (tcp.err == CONN_ERROR) {
    return false;
  }
  ctrl->conn = tcp.conn;
  ctrl->recv_buffer_used = 0;
  ctrl->bytes_to_farm = 0;
  ctrl->farming = false;
  if (conn_bind(ctrl->conn, addr) == CONN_ERROR) {
    return false;
  }
  if (conn_listen(ctrl->conn) == CONN_ERROR) {
    return false;
  }
  return true;
}

b32 stun_server_init(Dgram *stun, ConnAddr *addr) {
  ConnErr udp;
  udp = conn_udp();
  if (udp.err == CONN_ERROR) {
    return false;
  }
  stun->conn = udp.conn;
  if (conn_bind(stun->conn, addr) == CONN_ERROR) {
    return false;
  }
  return true;
}

#define SERVER_ADDRESS "192.168.100.197"
#define CTRL_PORT 8080
#define STUN_PORT 8081

#define DEFAULT_ARENAS_SIZE mb(10)

void context_init(Context *ctx) {
  /* Tomi: arenas setup */
  arena_init(&ctx->arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);
  arena_init(&ctx->event_arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);

  /* Tomi: ctrl server setup */
  ctx->ctrl_addr = conn_address(&ctx->arena, SERVER_ADDRESS, CTRL_PORT);
  assert(ctrl_server_init(&ctx->ctrl, ctx->ctrl_addr));
  /* Tomi: stun server setup */
  ctx->stun_addr = conn_address(&ctx->arena, SERVER_ADDRESS, STUN_PORT);
  assert(stun_server_init(&ctx->stun, ctx->stun_addr));
  /* Tomi: select sets setup */
  ctx->read = conn_set_create(&ctx->arena);
  ctx->write = conn_set_create(&ctx->arena);
  ctx->timeout = CONN_TIMEOUT_INFINITY;
  ctx->running = true;

  /* Tomi: link list setup */
  ctx->peers_fist = 0;
  ctx->peers_last = 0;
  ctx->peers_first_free = 0;
  ctx->messages_first_free = 0;
}

void peer_set_timeout(Peer *peer, u32 timeout) {
  peer->timeout = timeout;
  peer->last_activity = conn_current_time_ms();
}

void peer_connect(Context *ctx, Conn conn) {
  Peer *peer;
  if (ctx->peers_first_free) {
    peer = ctx->peers_first_free;
    ctx->peers_first_free = ctx->peers_first_free->next;
  } else {
    peer = arena_push(&ctx->arena, sizeof(*peer), 8);
  }
  assert(peer);
  memset(peer, 0, sizeof(*peer));
  peer_set_timeout(peer, CONN_TIMEOUT_INFINITY);
  dllist_push_back(ctx->peers_fist, ctx->peers_last, peer);
}

void peer_disconect(Context *ctx, Peer *peer) {
  MessageHeader *msg;
  conn_close(peer->stream.conn);
  msg = peer->messages_first;
  while (msg != 0) {
    MessageHeader *to_free;
    to_free = msg;
    msg = msg->next;
    dllist_remove(peer->messages_first, peer->messages_last, to_free);
    to_free->next = ctx->messages_first_free;
    ctx->messages_first_free = to_free;
  }
  dllist_remove(ctx->peers_fist, ctx->peers_last, peer);
  peer->next = ctx->peers_first_free;
  ctx->peers_first_free = peer;
}

int main(void) {
  static Context _context;
  Context *ctx = &_context;

  conn_init();
  context_init(ctx);

  for (;;) {
    u32 now;
    Peer *peer;
    if (!ctx->running) {
      break;
    }

    conn_set_clear(ctx->read);
    conn_set_clear(ctx->write);
    conn_set_add(ctx->read, ctx->ctrl.conn);
    conn_set_add(ctx->read, ctx->stun.conn);
    ctx->timeout = CONN_TIMEOUT_INFINITY;

    now = conn_current_time_ms();
    for (peer = ctx->peers_fist; peer != 0; peer = peer->next) {
      u32 remaining;
      remaining = max(peer->timeout - (now - peer->last_activity), 0);
      ctx->timeout = min(ctx->timeout, remaining);

      conn_set_add(ctx->read, peer->stream.conn);
      if (!dllist_empty(peer->messages_first, peer->messages_last)) {
        conn_set_add(ctx->write, peer->stream.conn);
      }
    }

    conn_select(ctx->read, ctx->write, ctx->timeout);

    if (conn_set_has(ctx->read, ctx->ctrl.conn)) {
      ConnErr other;
      other = conn_accept(ctx->ctrl.conn, 0);
      if (other.err == CONN_OK) {
        peer_connect(ctx, other.conn);
      }
    }

    for (peer = ctx->peers_fist; peer != 0; peer = peer->next) {
      if (conn_set_has(ctx->read, peer->stream.conn)) {
      }
    }

    ctx->event_arena.used = 0;
  }

  return 0;
}
