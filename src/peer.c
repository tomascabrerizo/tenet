#include "proto.h"

typedef enum State { State_DONT_KNOW_IT_SELF, State_KNOW_IT_SELF } State;

struct Context;
typedef void (*EventCallback)(struct Context *ctx);
typedef struct Context {
  Arena arena;
  Arena event_arena;

  Stream ctrl;
  Dgram stun;
  ConnAddr *ctrl_addr;
  ConnAddr *stun_addr;

  ConnSet *read;
  ConnSet *write;

  State state;

  MessageHeader *messages_first;
  MessageHeader *messages_last;

  MessageHeader *stun_messages_first;
  MessageHeader *stun_messages_last;

  MessageHeader *messages_first_free;

  EventCallback stun_on_timeout;
  EventCallback stun_on_read;
  EventCallback stun_on_write;

  EventCallback ctrl_on_timeout;
  EventCallback ctrl_on_read;
  EventCallback ctrl_on_write;

  u32 timeout;
  b32 running;
} Context;

/* TODO: this piece of code is in the peer and server file twice */
Message *message_alloc(Context *ctx) {
  Message *msg;
  if (ctx->messages_first_free) {
    msg = (Message *)ctx->messages_first_free;
    ctx->messages_first_free = ctx->messages_first_free->next;
  } else {
    msg = (Message *)arena_push(&ctx->arena, sizeof(*msg), 8);
  }
  memset(msg, 0, sizeof(*msg));
  return msg;
}

void message_free(Context *ctx, Message *msg) {
  msg->header.next = ctx->messages_first_free;
  msg->header.prev = 0;
  ctx->messages_first_free = &msg->header;
}

void ctx_stun_message_push(Context *ctx, Message *msg) {
  MessageHeader *header;
  header = &msg->header;
  dllist_push_back(ctx->stun_messages_first, ctx->stun_messages_last, header);
}

Message *ctx_stun_message_pop(Context *ctx) {
  MessageHeader *header;
  assert(!dllist_empty(ctx->stun_messages_first, ctx->stun_messages_last));
  header = ctx->stun_messages_first;
  dllist_remove(ctx->stun_messages_first, ctx->stun_messages_last, header);
  return (Message *)header;
}

#define SERVER_ADDRESS "192.168.100.197"
#define CTRL_PORT 8080
#define STUN_PORT 8081

#define DEFAULT_ARENAS_SIZE mb(10)

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

b32 stun_init(Dgram *stun) {
  ConnErr udp;
  udp = conn_udp();
  if (udp.err == CONN_ERROR) {
    return false;
  }
  stun->conn = udp.conn;
  return true;
}

void ctx_init(Context *ctx, EventCallback stun_on_timeout,
              EventCallback stun_on_read, EventCallback stun_on_write,
              EventCallback ctrl_on_timeout, EventCallback ctrl_on_read,
              EventCallback ctrl_on_write) {
  memset(ctx, 0, sizeof(*ctx));

  /* Tomi: arenas setup */
  arena_init(&ctx->arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);
  arena_init(&ctx->event_arena, (u8 *)malloc(DEFAULT_ARENAS_SIZE),
             DEFAULT_ARENAS_SIZE);

  /* Tomi: ctrl setup */
  ctx->ctrl_addr = conn_address(&ctx->arena, SERVER_ADDRESS, CTRL_PORT);
  assert(ctrl_init(&ctx->ctrl, ctx->ctrl_addr));
  /* Tomi: stun setup */
  ctx->stun_addr = conn_address(&ctx->arena, SERVER_ADDRESS, STUN_PORT);
  assert(stun_init(&ctx->stun));

  /* Tomi: select sets setup */
  ctx->read = conn_set_create(&ctx->arena);
  ctx->write = conn_set_create(&ctx->arena);
  ctx->timeout = 0;
  ctx->running = true;

  ctx->stun_on_timeout = stun_on_timeout;
  ctx->stun_on_read = stun_on_read;
  ctx->stun_on_write = stun_on_write;

  ctx->ctrl_on_timeout = ctrl_on_timeout;
  ctx->ctrl_on_read = ctrl_on_read;
  ctx->ctrl_on_write = ctrl_on_write;

  ctx->state = State_DONT_KNOW_IT_SELF;
}

void event_loop_prepare(Context *ctx) {
  conn_set_clear(ctx->read);
  conn_set_clear(ctx->write);

  conn_set_add(ctx->read, ctx->ctrl.conn);
  if (!dllist_empty(ctx->messages_first, ctx->messages_last)) {
    conn_set_add(ctx->write, ctx->ctrl.conn);
  }

  conn_set_add(ctx->read, ctx->stun.conn);
  if (!dllist_empty(ctx->stun_messages_first, ctx->stun_messages_last)) {
    conn_set_add(ctx->write, ctx->stun.conn);
  }
}

void event_loop_process(Context *ctx) {
  u32 res;

  res = conn_select(ctx->read, ctx->write, ctx->timeout);
  assert(res != CONN_ERROR);

  if (res == 0) {
    if (ctx->stun_on_timeout) {
      ctx->stun_on_timeout(ctx);
    }
    if (ctx->ctrl_on_timeout) {
      ctx->ctrl_on_timeout(ctx);
    }
  }
  if (conn_set_has(ctx->read, ctx->ctrl.conn)) {
    if (ctx->ctrl_on_read) {
      ctx->ctrl_on_read(ctx);
    }
  }
  if (conn_set_has(ctx->write, ctx->ctrl.conn)) {
    if (ctx->ctrl_on_write) {
      ctx->ctrl_on_write(ctx);
    }
  }
  if (conn_set_has(ctx->read, ctx->stun.conn)) {
    if (ctx->stun_on_read) {
      ctx->stun_on_read(ctx);
    }
  }
  if (conn_set_has(ctx->write, ctx->stun.conn)) {
    if (ctx->stun_on_write) {
      ctx->stun_on_write(ctx);
    }
  }
}

void event_loop_cleanup(Context *ctx) { ctx->event_arena.used = 0; }

void stun_on_timeout(Context *ctx) {
  switch (ctx->state) {
  case State_DONT_KNOW_IT_SELF: {
    Message *msg = message_alloc(ctx);
    msg->header.type = MessageType_STUN;
    ctx_stun_message_push(ctx, msg);
    ctx->timeout = 200;
  } break;
  case State_KNOW_IT_SELF: {
  } break;
  }
}

void stun_on_read(Context *ctx) {
  switch (ctx->state) {
  case State_DONT_KNOW_IT_SELF: {
    Message *msg;
    ConnAddr *from;
    from = conn_address_create(&ctx->event_arena);
    msg = dgram_message_read_from(&ctx->event_arena, &ctx->stun, from);
    if (msg->header.type == MessageType_STUN_RESPONSE) {
      /* TODO: save own public address and port */
      ctx->state = State_KNOW_IT_SELF;
      ctx->timeout = CONN_TIMEOUT_INFINITY;
    }
  } break;
  case State_KNOW_IT_SELF: {
  } break;
  }
}

void stun_on_write(Context *ctx) {
  switch (ctx->state) {
  case State_DONT_KNOW_IT_SELF: {
    Message *msg;
    msg = ctx_stun_message_pop(ctx);
    dgram_message_write_to(&ctx->event_arena, &ctx->stun, msg, ctx->stun_addr);
    message_free(ctx, msg);
  } break;
  case State_KNOW_IT_SELF: {
  } break;
  }
}

int main(void) {
  static Context _context;
  Context *ctx = &_context;

  conn_init();
  ctx_init(ctx, stun_on_timeout, stun_on_read, stun_on_write, 0, 0, 0);

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
