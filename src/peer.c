#include "proto.h"

typedef enum State {
  State_DONT_KNOW_IT_SELF,
  State_KNOW_IT_SELF,
  State_CONNECTED,
} State;

struct Context;
typedef void (*EventCallback)(struct Context *ctx, Message *msg,
                              ConnAddr *addr);

typedef struct Peer {
  ConnAddr *addr;

  u32 timeout;
  u32 last_activity;

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
  Dgram transport;
  ConnAddr *ctrl_addr;
  ConnAddr *transport_addr;
  ConnAddr *local_addr;

  ConnSet *read;
  ConnSet *write;

  State state;

  MessageHeader *messages_first;
  MessageHeader *messages_last;

  AddrMessage *addr_messages_first;
  AddrMessage *addr_messages_last;

  EventCallback transport_on_timeout;
  EventCallback transport_on_read;

  u32 timeout;
  b32 running;

  u32 own_addr;
  u16 own_port;
  u32 own_local_addr;
  u16 own_local_port;
} Context;

#define SERVER_ADDRESS "192.168.100.197"
#define SERVER_CTRL_PORT 8080
#define SERVER_STUN_PORT 8081

#define DEFAULT_ARENAS_SIZE mb(10)

void print_le_address(u32 addr) {
  u8 b0, b1, b2, b3;
  b0 = (addr >> 24) & 0xff;
  b1 = (addr >> 16) & 0xff;
  b2 = (addr >> 8) & 0xff;
  b3 = (addr >> 0) & 0xff;
  printf("%d.%d.%d.%d\n", b0, b1, b2, b3);
}

b32 ctrl_init(Stream *ctrl, ConnAddr *addr) {
  ConnErr tcp;
  tcp = conn_tcp();
  if (tcp.err == CONN_ERROR) {
    return false;
  }
  ctrl->conn = tcp.conn;
  ctrl->recv_buffer_used = 0;
  ctrl->bytes_to_farm = 0;
  ctrl->farming = false;
  if (conn_connect(ctrl->conn, addr) == CONN_ERROR) {
    return false;
  }
  return true;
}

b32 transport_init(Dgram *transport) {
  ConnErr udp;
  udp = conn_udp();
  if (udp.err == CONN_ERROR) {
    return false;
  }
  transport->conn = udp.conn;

  return true;
}

void ctx_init(Context *ctx, EventCallback transport_on_timeout,
              EventCallback transport_on_read) {

  memset(ctx, 0, sizeof(*ctx));

  /* Tomi: arenas setup */
  arena_init(&ctx->arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);
  arena_init(&ctx->event_arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);

  /* Tomi: allocators setup */
  message_allocator_init(&ctx->message_allocator, &ctx->arena);
  addr_message_allocator_init(&ctx->addr_message_allocator, &ctx->arena);

  /* Tomi: ctrl setup */
  ctx->ctrl_addr = conn_address(&ctx->arena, SERVER_ADDRESS, SERVER_CTRL_PORT);
  assert(ctrl_init(&ctx->ctrl, ctx->ctrl_addr));
  /* Tomi: transport setup */
  ctx->transport_addr =
      conn_address(&ctx->arena, SERVER_ADDRESS, SERVER_STUN_PORT);
  assert(transport_init(&ctx->transport));

  u64 mark = ctx->arena.used;
  ConnAddr *default_addr = conn_get_default_network_addapter_addr(&ctx->arena);
  assert(default_addr);
  assert(conn_bind(ctx->transport.conn, default_addr) != CONN_ERROR);
  ctx->arena.used = mark;

  ctx->local_addr = conn_get_addr(&ctx->arena, ctx->transport.conn);
  conn_address_get_address_and_port(ctx->local_addr, &ctx->own_local_addr,
                                    &ctx->own_local_port);

  conn_address_print(ctx->local_addr);

  /* Tomi: select sets setup */
  ctx->read = conn_set_create(&ctx->arena);
  ctx->write = conn_set_create(&ctx->arena);
  ctx->timeout = 0;
  ctx->running = true;

  ctx->transport_on_timeout = transport_on_timeout;
  ctx->transport_on_read = transport_on_read;

  ctx->state = State_DONT_KNOW_IT_SELF;
}

void event_loop_prepare(Context *ctx) {
  conn_set_clear(ctx->read);
  conn_set_clear(ctx->write);

  conn_set_add(ctx->read, ctx->ctrl.conn);
  if (!dllist_empty(ctx->messages_first, ctx->messages_last)) {
    conn_set_add(ctx->write, ctx->ctrl.conn);
  }

  conn_set_add(ctx->read, ctx->transport.conn);
  if (!dllist_empty(ctx->addr_messages_first, ctx->addr_messages_last)) {
    conn_set_add(ctx->write, ctx->transport.conn);
  }
}

void message_callback(Stream *stream, Message *msg, void *param);

void event_loop_process(Context *ctx) {
  u32 res;

  res = conn_select(ctx->read, ctx->write, ctx->timeout);
  assert(res != CONN_ERROR);

  if (res == 0) {
    if (ctx->transport_on_timeout) {
      ctx->transport_on_timeout(ctx, 0, 0);
    }
  }
  if (conn_set_has(ctx->read, ctx->ctrl.conn)) {
    stream_proccess_messages(&ctx->event_arena, &ctx->ctrl, message_callback,
                             0);
  }
  if (conn_set_has(ctx->write, ctx->ctrl.conn)) {
    MessageHeader *msg;
    assert(ctx->messages_first);
    msg = ctx->messages_first;
    dllist_remove(ctx->messages_first, ctx->messages_last, msg);
    stream_message_write(&ctx->event_arena, &ctx->ctrl, (Message *)msg);
  }
  if (conn_set_has(ctx->read, ctx->transport.conn)) {
    Message *msg;
    ConnAddr *from;
    from = conn_address_create(&ctx->event_arena);
    msg = dgram_message_read_from(&ctx->event_arena, &ctx->transport, from);
    if (ctx->transport_on_read) {
      ctx->transport_on_read(ctx, msg, from);
    }
  }
  if (conn_set_has(ctx->write, ctx->transport.conn)) {
    AddrMessage *addr_msg;
    assert(!dllist_empty(ctx->addr_messages_first, ctx->addr_messages_last));
    addr_msg = ctx->addr_messages_first;
    dllist_remove(ctx->addr_messages_first, ctx->addr_messages_last, addr_msg);
    dgram_message_write_to(&ctx->event_arena, &ctx->transport, &addr_msg->msg,
                           addr_msg->addr);
    addr_message_free(&ctx->addr_message_allocator, addr_msg);
  }
}

void event_loop_cleanup(Context *ctx) { ctx->event_arena.used = 0; }

Message *push_ctrl_message(Context *ctx) {
  MessageHeader *msg = (MessageHeader *)message_alloc(&ctx->message_allocator);
  dllist_push_back(ctx->messages_first, ctx->messages_last, msg);
  return (Message *)msg;
}

AddrMessage *push_transport_message(Context *ctx) {
  AddrMessage *addr_msg = addr_message_alloc(&ctx->addr_message_allocator);
  conn_address_set(addr_msg->addr, ctx->transport_addr);
  dllist_push_back(ctx->addr_messages_first, ctx->addr_messages_last, addr_msg);
  return addr_msg;
}

void push_connect_message(Context *ctx) {
  Message *msg = push_ctrl_message(ctx);
  msg->header.type = MessageType_CONNECT;
  msg->connect.addr = ctx->own_addr;
  msg->connect.port = ctx->own_port;
  msg->connect.local_addr = ctx->own_local_addr;
  msg->connect.local_port = ctx->own_local_port;
}

void transport_on_timeout(Context *ctx, Message *msg, ConnAddr *addr) {
  switch (ctx->state) {
  case State_DONT_KNOW_IT_SELF: {
    AddrMessage *addr_msg = push_transport_message(ctx);
    addr_msg->msg.header.type = MessageType_STUN;
    ctx->timeout = 200;
  } break;
  case State_KNOW_IT_SELF: {
    AddrMessage *addr_msg = push_transport_message(ctx);
    addr_msg->msg.header.type = MessageType_KEEP_ALIVE;
  } break;
  case State_CONNECTED: {
  } break;
  }
}

void transport_on_read(Context *ctx, Message *msg, ConnAddr *addr) {
  switch (ctx->state) {
  case State_DONT_KNOW_IT_SELF: {
    if (conn_address_equals(ctx->transport_addr, addr)) {
      if (msg->header.type == MessageType_STUN_RESPONSE) {
        ctx->own_addr = msg->stun_response.addr;
        ctx->own_port = msg->stun_response.port;
        ctx->state = State_KNOW_IT_SELF;
        push_connect_message(ctx);
        ctx->timeout = 5000;
      }
    }
  } break;

  case State_CONNECTED:
  case State_KNOW_IT_SELF: {
  } break;
  }
}

void transport_on_write(Context *ctx) {
  switch (ctx->state) {
  case State_CONNECTED:
  case State_KNOW_IT_SELF:
  case State_DONT_KNOW_IT_SELF: {
    AddrMessage *addr_msg;
    assert(!dllist_empty(ctx->addr_messages_first, ctx->addr_messages_last));
    addr_msg = ctx->addr_messages_first;
    dllist_remove(ctx->addr_messages_first, ctx->addr_messages_last, addr_msg);
    dgram_message_write_to(&ctx->event_arena, &ctx->transport, &addr_msg->msg,
                           addr_msg->addr);
    addr_message_free(&ctx->addr_message_allocator, addr_msg);
  } break;
  }
}

void message_callback(Stream *stream, Message *msg, void *param) {
  switch (msg->header.type) {
  default: {
  } break;
  }
}

int main(void) {
  static Context _context;
  Context *ctx = &_context;

  conn_init();
  ctx_init(ctx, transport_on_timeout, transport_on_read);

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
