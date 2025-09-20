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

  Protocol proto;

  Stream ctrl;
  Dgram stun;
  ConnAddr *ctrl_addr;
  ConnAddr *stun_addr;

  ConnSet *read;
  ConnSet *write;

  Peer *peers_first;
  Peer *peers_last;

  AddrMessage *addr_messages_first;
  AddrMessage *addr_messages_last;

  Peer *peers_first_free;

  u32 timeout;
  b32 running;
} Context;

/* TODO: this piece of code is in the peer and server file twice */

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

void ctx_init(Context *ctx) {
  /* Tomi: arenas setup */
  arena_init(&ctx->arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);
  arena_init(&ctx->event_arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);

  /* Tomi: protocol setup */
  proto_init(&ctx->proto, &ctx->arena);

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
  ctx->peers_first = 0;
  ctx->peers_last = 0;
  ctx->peers_first_free = 0;
  ctx->addr_messages_first = 0;
  ctx->addr_messages_last = 0;
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
  peer->stream.conn = conn;
  peer_set_timeout(peer, CONN_TIMEOUT_INFINITY);
  dllist_push_back(ctx->peers_first, ctx->peers_last, peer);
}

void peer_disconnect(Context *ctx, Peer *peer) {
  MessageHeader *msg;
  conn_close(peer->stream.conn);
  msg = peer->messages_first;
  while (msg != 0) {
    MessageHeader *to_free;
    to_free = msg;
    msg = msg->next;
    dllist_remove(peer->messages_first, peer->messages_last, to_free);
    proto_message_free(&ctx->proto, (Message *)to_free);
  }
  dllist_remove(ctx->peers_first, ctx->peers_last, peer);
  peer->next = ctx->peers_first_free;
  ctx->peers_first_free = peer;
}
typedef struct MessageCallbackParams {
  Context *ctx;
  Peer *peer;
} MessageCallbackParams;

Message *push_ctrl_message(Context *ctx, Peer *peer) {
  MessageHeader *msg = (MessageHeader *)proto_message_alloc(&ctx->proto);
  dllist_push_back(peer->messages_first, peer->messages_last, msg);
  return (Message *)msg;
}

void push_first_peer_message(Context *ctx, Peer *peer) {
  Message *msg = push_ctrl_message(ctx, peer);
  msg->header.type = MessageType_FIRST_PEER;
}

void message_callback(Stream *stream, Message *msg, void *param) {
  MessageCallbackParams *params = (MessageCallbackParams *)param;
  switch (msg->header.type) {
  case MessageType_CONNECT: {
    push_first_peer_message(params->ctx, params->peer);
  } break;
  default: {
  } break;
  }
}

void event_loop_prepare(Context *ctx) {
  u32 now;
  Peer *peer;

  conn_set_clear(ctx->read);
  conn_set_clear(ctx->write);

  ctx->timeout = CONN_TIMEOUT_INFINITY;

  conn_set_add(ctx->read, ctx->ctrl.conn);
  now = conn_current_time_ms();
  for (peer = ctx->peers_first; peer != 0; peer = peer->next) {
    u32 remaining;
    if (peer->timeout == CONN_TIMEOUT_INFINITY) {
      remaining = CONN_TIMEOUT_INFINITY;
    } else {
      remaining = max(peer->timeout - (now - peer->last_activity), 0);
    }
    ctx->timeout = min(ctx->timeout, remaining);

    conn_set_add(ctx->read, peer->stream.conn);
    if (!dllist_empty(peer->messages_first, peer->messages_last)) {
      conn_set_add(ctx->write, peer->stream.conn);
    }
  }

  conn_set_add(ctx->read, ctx->stun.conn);
  if (!dllist_empty(ctx->addr_messages_first, ctx->addr_messages_last)) {
    conn_set_add(ctx->write, ctx->stun.conn);
  }
}

void event_loop_process(Context *ctx) {
  Peer *peer;
  u32 res;

  res = conn_select(ctx->read, ctx->write, ctx->timeout);
  assert(res != CONN_ERROR);

  if (conn_set_has(ctx->read, ctx->ctrl.conn)) {
    ConnErr other;
    other = conn_accept(ctx->ctrl.conn, 0);
    if (other.err == CONN_OK) {
      peer_connect(ctx, other.conn);
    }
  }

  for (peer = ctx->peers_first; peer != 0;) {
    Peer *next_peer = peer->next;

    if (conn_set_has(ctx->read, peer->stream.conn)) {
      u32 res;
      MessageCallbackParams params;
      params.ctx = ctx;
      params.peer = peer;
      res = stream_proccess_messages(&ctx->event_arena, &peer->stream,
                                     message_callback, &params);
      if (res == CONN_ERROR) {
        peer_disconnect(ctx, peer);
        goto next;
      }
    }

    if (conn_set_has(ctx->write, peer->stream.conn)) {
      u32 res;
      MessageHeader *msg;
      assert(peer->messages_first);
      msg = peer->messages_first;
      dllist_remove(peer->messages_first, peer->messages_last, msg);
      res = stream_message_write(&ctx->event_arena, &peer->stream,
                                 (Message *)msg);
      if (res == CONN_ERROR) {
        peer_disconnect(ctx, peer);
        goto next;
      }
    }

  next:
    peer = next_peer;
  }

  if (conn_set_has(ctx->read, ctx->stun.conn)) {
    Message *msg;
    ConnAddr *from;
    from = conn_address_create(&ctx->event_arena);
    msg = dgram_message_read_from(&ctx->event_arena, &ctx->stun, from);
    if (msg) {
      switch (msg->header.type) {
      case MessageType_STUN: {
        AddrMessage *addr_msg;
        addr_msg = proto_addr_message_alloc(&ctx->proto);
        addr_msg->msg.stun_response.header.type = MessageType_STUN_RESPONSE;
        conn_address_get_address_and_port(from,
                                          &addr_msg->msg.stun_response.addr,
                                          &addr_msg->msg.stun_response.port);
        conn_address_set(addr_msg->addr, from);
        dllist_push_back(ctx->addr_messages_first, ctx->addr_messages_last,
                         addr_msg);
      } break;
      case MessageType_KEEP_ALIVE: {
        static u8 buffer[64];
        conn_address_string(from, buffer, sizeof(buffer));
        printf("Keep alive package receive from: %s\n", buffer);
      } break;
      default: {
        /* Tomi: ignore unknow messages */
      } break;
      }
    }
  }

  if (conn_set_has(ctx->write, ctx->stun.conn)) {
    AddrMessage *addr_msg;
    assert(ctx->addr_messages_first);
    addr_msg = ctx->addr_messages_first;
    dllist_remove(ctx->addr_messages_first, ctx->addr_messages_last, addr_msg);
    dgram_message_write_to(&ctx->event_arena, &ctx->stun, &addr_msg->msg,
                           addr_msg->addr);
    proto_addr_message_free(&ctx->proto, addr_msg);
  }
}

void event_loop_cleanup(Context *ctx) { ctx->event_arena.used = 0; }

int main(void) {
  static Context _context;
  Context *ctx = &_context;

  conn_init();
  ctx_init(ctx);

  for (;;) {
    if (!ctx->running) {
      break;
    }

    event_loop_prepare(ctx);
    event_loop_process(ctx);
    event_loop_cleanup(ctx);
  }

  return 0;
}
