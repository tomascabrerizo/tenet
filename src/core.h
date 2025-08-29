#ifndef _CORE_H_
#define _CORE_H_

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef unsigned int b32;
#define false 0
#define true (!(false))

#define array_len(array) (sizeof((array)) / sizeof((array)[0]))
#define kb(value) ((value) * 1024ll)
#define mb(value) (kb(value) * 1024ll)
#define gb(value) (mb(value) * 1024ll)

#define unused(var) ((void)(var))
#define is_power_of_two(value) ((value) != 0 && ((value) & ((value) - 1)) == 0)

#define checknull(p) ((p) == 0)
#define setnull(p) ((p) = 0)

#define dllist_push_back(f, l, n)                                              \
  (checknull(l)                                                                \
       ? ((f) = (l) = (n), setnull((n)->prev), setnull((n)->next))             \
       : ((l)->next = (n), (n)->prev = (l), setnull((n)->next), (l) = (n)))

#define dllist_remove(f, l, n)                                                 \
  (((n) == (f) ? (f) = (n)->next : (0)), ((n) == (l) ? (l) = (n)->prev : (0)), \
   (checknull((n)->prev) ? (0) : ((n)->prev->next = (n)->next)),               \
   (checknull((n)->next) ? (0) : ((n)->next->prev = (n)->prev)))

#define write_u8_be(buffer, value)                                             \
  ((u8 *)(buffer))[0] = (u8)((value >> 0) & 0xff);                             \
  (buffer) = ((u8 *)(buffer)) + 1

#define write_u16_be(buffer, value)                                            \
  ((u8 *)(buffer))[0] = (u8)((value >> 8) & 0xff);                             \
  ((u8 *)(buffer))[1] = (u8)((value >> 0) & 0xff);                             \
  (buffer) = ((u8 *)(buffer)) + 2

#define write_u32_be(buffer, value)                                            \
  ((u8 *)(buffer))[0] = (u8)((value >> 24) & 0xff);                            \
  ((u8 *)(buffer))[1] = (u8)((value >> 16) & 0xff);                            \
  ((u8 *)(buffer))[2] = (u8)((value >> 8) & 0xff);                             \
  ((u8 *)(buffer))[3] = (u8)((value >> 0) & 0xff);                             \
  (buffer) = ((u8 *)(buffer)) + 4

/* TODO: maybe this macros can return the used size */
#define write_u8_be_or_count(buffer, value, size)                              \
  do {                                                                         \
    if (buffer) {                                                              \
      write_u8_be(buffer, value);                                              \
    }                                                                          \
    size += 1;                                                                 \
  } while (0)

#define write_u16_be_or_count(buffer, value, size)                             \
  do {                                                                         \
    if (buffer) {                                                              \
      write_u16_be(buffer, value);                                             \
    }                                                                          \
    size += 2;                                                                 \
  } while (0)

#define write_u32_be_or_count(buffer, value, size)                             \
  do {                                                                         \
    if (buffer) {                                                              \
      write_u32_be(buffer, value);                                             \
    }                                                                          \
    size += 4;                                                                 \
  } while (0)

#define read_u32_be(buffer)                                                    \
  (u32)(((u8 *)(buffer))[0] << 24) | (u32)(((u8 *)(buffer))[1] << 16) |        \
      (u32)(((u8 *)(buffer))[2] << 8) | (u32)(((u8 *)(buffer))[3] << 0);       \
  (buffer) += 4

#define read_u16_be(buffer)                                                    \
  (u16)((u16)(((u8 *)(buffer))[0] << 8) | (u16)(((u8 *)(buffer))[1] << 0));    \
  (buffer) += 2

#define read_u8_be(buffer)                                                     \
  (u8)(((u8 *)(buffer))[0]);                                                   \
  (buffer) += 1

typedef struct Arena {
  u8 *data;
  u64 used;
  u64 size;
} Arena;

void arena_init(Arena *arena, u8 *data, u64 size);
void *arena_alloc(Arena *arena, u64 size, u32 align);

#endif
