#include "core.h"

void arena_init(Arena *arena, u8 *data, u64 size) {
  arena->data = data;
  arena->size = size;
  arena->used = 0;
}

void *arena_push(Arena *arena, u64 size, u32 align) {
  u64 address, align_address;
  u64 a, total_size;

  assert(is_power_of_two(align));
  a = (align - 1);
  address = (u64)arena->data + arena->used;
  align_address = (address + a) & ~a;
  total_size = (align_address - address) + size;

  assert(arena->used + total_size <= arena->size);
  arena->used += total_size;
  return (void *)align_address;
}
