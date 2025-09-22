#include "proto.h"

typedef struct PeerList {
  struct Peer *first;
  struct Peer *last;
} PeerList;

typedef struct Peer {
  Stream stream;
  MessageHeader *messages_first;
  MessageHeader *messages_last;

  u32 raw_addr;
  u16 raw_port;
  u32 raw_local_addr;
  u16 raw_local_port;

  struct Peer *next;
  struct Peer *prev;
} Peer;

typedef struct Context {
  Arena arena;
  Arena event_arena;

  MessageAllocator message_allocator;
  AddrMessageAllocator addr_message_allocator;

  Stream ctrl;
  ConnAddr *ctrl_addr;

  Dgram stun;
  ConnAddr *stun_addr;

  ConnSet *read;
  ConnSet *write;

  Peer *peers_first;
  Peer *peers_last;
  Peer *peers_first_free;

  AddrMessage *addr_messages_first;
  AddrMessage *addr_messages_last;

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

void ctx_init(Context *ctx) {
  /* Tomi: arenas setup */
  arena_init(&ctx->arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);
  arena_init(&ctx->event_arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);

  /* Tomi: allocators setup */
  message_allocator_init(&ctx->message_allocator, &ctx->arena);
  addr_message_allocator_init(&ctx->addr_message_allocator, &ctx->arena);

  /* Tomi: ctrl server setup */
  ctx->ctrl_addr = conn_address(&ctx->arena, SERVER_ADDRESS, CTRL_PORT);
  assert(ctrl_server_init(&ctx->ctrl, ctx->ctrl_addr));
  /* Tomi: stun server setup */
  ctx->stun_addr = conn_address(&ctx->arena, SERVER_ADDRESS, STUN_PORT);
  assert(stun_server_init(&ctx->stun, ctx->stun_addr));
  /* Tomi: select sets setup */
  ctx->read = conn_set_create(&ctx->arena);
  ctx->write = conn_set_create(&ctx->arena);
  ctx->running = true;

  /* Tomi: link list setup */
  ctx->peers_first = 0;
  ctx->peers_last = 0;
  ctx->peers_first_free = 0;
  ctx->addr_messages_first = 0;
  ctx->addr_messages_last = 0;
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
    message_free(&ctx->message_allocator, (Message *)to_free);
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
  MessageHeader *msg = (MessageHeader *)message_alloc(&ctx->message_allocator);
  dllist_push_back(peer->messages_first, peer->messages_last, msg);
  return (Message *)msg;
}

Peer *peer_copy(Arena *arena, Peer *peer) {
  Peer *copy = arena_push(arena, sizeof(*copy), 8);
  memcpy(copy, peer, sizeof(Peer));
  return copy;
}

#if 0 
PeerList get_others_peers_list(Arena *arena, Peer *first, Peer *last,
                               Peer *peer) {
  PeerList res;
  Peer *other;
  memset(&res, 0, sizeof(res));
  for (other = first; other != 0; other = other->next) {
    if (other == peer) {
      continue;
    }
    Peer *copy = peer_copy(arena, other);
    dllist_push_back(res.first, res.last, copy);
  }

  return res;
}
#endif

PeerConnected *allocate_peer_connected_node(Arena *arena, Peer *peer) {
  PeerConnected *node = arena_push(arena, sizeof(*node), 8);
  node->addr = peer->raw_addr;
  node->port = peer->raw_port;
  node->local_addr = peer->raw_local_addr;
  node->local_port = peer->raw_local_port;
  node->next = 0;
  node->prev = 0;
  return node;
}

MessagePeersToConnect *calculate_current_peer_connected_message(
    Arena *arena, MessageAllocator *allocator, Peer *peer) {
  PeerConnected *node;
  MessagePeersToConnect *msg;
  msg = (MessagePeersToConnect *)message_alloc(allocator);
  msg->header.type = MessageType_PEERS_TO_CONNECT;
  msg->count = 1;
  node = allocate_peer_connected_node(arena, peer);
  dllist_push_back(msg->first, msg->last, node);
  return msg;
}

MessagePeersToConnect *
calculate_others_peers_connected_message(Arena *arena,
                                         MessageAllocator *allocator,
                                         Peer *first, Peer *last, Peer *peer) {

  MessagePeersToConnect *msg;
  msg = (MessagePeersToConnect *)message_alloc(allocator);
  msg->count = 0;
  Peer *other;
  for (other = first; other != 0; other = other->next) {
    PeerConnected *node;
    if (other == peer) {
      continue;
    }
    msg->count++;
    node = allocate_peer_connected_node(arena, other);
    dllist_push_back(msg->first, msg->last, node);
  }
  return msg;
}

void message_callback(Stream *stream, Message *msg, void *param) {
  Context *ctx;
  Peer *peer;
  MessageCallbackParams *params = (MessageCallbackParams *)param;
  ctx = params->ctx;
  peer = params->peer;

  switch (msg->header.type) {
  case MessageType_CONNECT: {
    Peer *other;
    MessageHeader *header;

    peer->raw_addr = msg->connect.addr;
    peer->raw_port = msg->connect.port;
    peer->raw_local_addr = msg->connect.local_addr;
    peer->raw_local_port = msg->connect.local_port;

    header = (MessageHeader *)calculate_others_peers_connected_message(
        &ctx->event_arena, &ctx->message_allocator, ctx->peers_first,
        ctx->peers_last, peer);
    dllist_push_back(peer->messages_first, peer->messages_last, header);

    header = (MessageHeader *)calculate_current_peer_connected_message(
        &ctx->event_arena, &ctx->message_allocator, peer);
    for (other = ctx->peers_first; other != 0; other = other->next) {
      if (other == peer) {
        continue;
      }
      dllist_push_back(other->messages_first, other->messages_last, header);
    }
  } break;
  default: {
  } break;
  }
}

void event_loop_prepare(Context *ctx) {
  Peer *peer;

  conn_set_clear(ctx->read);
  conn_set_clear(ctx->write);

  conn_set_add(ctx->read, ctx->ctrl.conn);
  for (peer = ctx->peers_first; peer != 0; peer = peer->next) {
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

  res = conn_select(ctx->read, ctx->write, CONN_TIMEOUT_INFINITY);
  assert(res != CONN_ERROR);

  /* NOTE: ctrl socket  */

  if (conn_set_has(ctx->read, ctx->ctrl.conn)) {
    ConnErr other;
    other = conn_accept(ctx->ctrl.conn, 0);
    if (other.err == CONN_OK) {
      peer_connect(ctx, other.conn);
    }
  }

  /* NOTE: peers sockets  */

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

  /* NOTE: stun socket  */

  if (conn_set_has(ctx->read, ctx->stun.conn)) {
    Message *msg;
    ConnAddr *from;
    from = conn_address_create(&ctx->event_arena);
    msg = dgram_message_read_from(&ctx->event_arena, &ctx->stun, from);
    if (msg) {
      switch (msg->header.type) {
      case MessageType_STUN: {
        AddrMessage *addr_msg;
        addr_msg = addr_message_alloc(&ctx->addr_message_allocator);
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
    addr_message_free(&ctx->addr_message_allocator, addr_msg);
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
