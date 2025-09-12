#include "proto.h"

Message *message_deserialize(Arena *arena, u8 *buffer, u64 size) {
  Message *msg;
  u32 proto, message_size;
  proto = read_u32_be(buffer);
  if (proto != PROTO_MAGIC) {
    return 0;
  }
  message_size = read_u32_be(buffer);
  assert(message_size == size);
  msg = arena_push(arena, sizeof(*msg), 8);
  msg->header.type = (MessageType)read_u8_be(buffer);
  switch (msg->header.type) {
  case MessageType_STUN_RESPONSE: {
    msg->stun_response.address = read_u32_be(buffer);
    msg->stun_response.port = read_u16_be(buffer);
  } break;
  case MessageType_STUN: {
    /* Tomi: empty body*/
  } break;
  case MessageType_INVALID:
  case MessageType_COUNT: {
    assert(!"invalid code path");
  }
  }
  return 0;
}

void message_serialize_internal(Message *msg, u8 *buffer, u64 *size);
u8 *message_serialize(Arena *arena, Message *msg, u64 *size) {
  u8 *buffer;
  message_serialize_internal(msg, 0, size);
  buffer = arena_push(arena, *size, 8);
  message_serialize_internal(msg, buffer, size);
  return buffer;
}

void message_serialize_internal(Message *msg, u8 *buffer, u64 *size) {
  u32 total_size;
  total_size = 0;
  write_u32_be_or_count(buffer, PROTO_MAGIC, total_size);
  write_u32_be_or_count(buffer, (u32)(*size), total_size);
  write_u8_be_or_count(buffer, (u8)msg->header.type, total_size);
  switch (msg->header.type) {
  case MessageType_STUN_RESPONSE: {
    write_u32_be_or_count(buffer, msg->stun_response.address, total_size);
    write_u16_be_or_count(buffer, msg->stun_response.port, total_size);
  } break;
  case MessageType_STUN: {
    /* Tomi: empty body*/
  } break;
  case MessageType_INVALID:
  case MessageType_COUNT: {
    assert(!"invalid code path");
  }
  }
  *size = total_size;
}

u32 stream_message_write(Arena *arena, Stream *stream, Message *msg) {
  u64 size;
  s32 total_sent;
  u8 *buffer;
  buffer = message_serialize(arena, msg, &size);
  assert(buffer);
  total_sent = 0;
  while (total_sent < (u32)size) {
    s32 sent =
        conn_write(stream->conn, buffer + total_sent, (s32)size - total_sent);
    if (sent == CONN_ERROR) {
      return CONN_ERROR;
    }
    total_sent += sent;
  }
  return CONN_OK;
}

u32 stream_proccess_messages(Arena *arena, Stream *stream,
                             MessageCallback callback, void *param) {
  s32 size;
  u8 *recv_buffer_pos;
  u32 recv_buffer_size;

  recv_buffer_pos = stream->recv_buffer + stream->recv_buffer_used;
  recv_buffer_size = array_len(stream->recv_buffer) - stream->recv_buffer_used;
  size = conn_read(stream->conn, recv_buffer_pos, recv_buffer_size);
  if (size == CONN_ERROR) {
    return CONN_ERROR;
  }
  stream->recv_buffer_used += size;

  while (stream->recv_buffer_used >= 8) {
    while (!stream->farming) {
      u32 proto;
      u8 *buffer = stream->recv_buffer;
      proto = read_u32_be(buffer);
      if (proto == PROTO_MAGIC) {
        stream->bytes_to_farm = read_u32_be(buffer);
        stream->farming = true;
      } else {
        stream->recv_buffer_used -= 1;
        memcpy(stream->recv_buffer, stream->recv_buffer + 1,
               stream->recv_buffer_used);
      }
    }

    if (stream->farming && stream->recv_buffer_used >= stream->bytes_to_farm) {
      Message *msg;
      u32 extra_bytes;
      msg = message_deserialize(arena, stream->recv_buffer,
                                stream->bytes_to_farm);
      if (callback) {
        callback(stream, msg, param);
      }
      extra_bytes = stream->recv_buffer_used - stream->bytes_to_farm;
      memcpy(stream->recv_buffer, stream->recv_buffer + stream->bytes_to_farm,
             extra_bytes);
      stream->recv_buffer_used = extra_bytes;
      stream->bytes_to_farm = 0;
      stream->farming = false;
    }
  }
  return CONN_OK;
}

u32 dgram_message_write_to(Arena *arena, Dgram *dgram, Message *msg,
                           ConnAddr *to) {
  u64 size;
  s32 sent;
  u8 *buffer;
  buffer = message_serialize(arena, msg, &size);
  assert(size <= sizeof(buffer));
  sent = conn_write_to(dgram->conn, buffer, size, to);
  if (sent == CONN_ERROR) {
    return CONN_ERROR;
  }
  return CONN_OK;
}

Message *dgram_message_read_from(Arena *arena, Dgram *dgram, ConnAddr *from) {
  s32 size;
  u8 buffer[1024];
  Message *msg;
  size = conn_read_from(dgram->conn, buffer, sizeof(buffer), from);
  if (size == CONN_ERROR) {
    return 0;
  }
  assert(size <= sizeof(buffer));
  if (!valid_proto(buffer)) {
    return 0;
  }
  msg = message_deserialize(arena, buffer, size);
  return msg;
}
