#ifndef _CORE_H_
#define _CORE_H_

#if defined(_MSC_VER)
#define inline __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define inline __attribute__((always_inline)) inline
#else
#error "failed to define inline"
#endif

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
#define false ((b32)0)
#define true ((b32)(!(false)))

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define array_len(array) (sizeof((array)) / sizeof((array)[0]))
#define kb(value) ((value) * 1024ll)
#define mb(value) (kb(value) * 1024ll)
#define gb(value) (mb(value) * 1024ll)

#define unused(var) ((void)(var))
#define is_power_of_two(value) ((value) != 0 && ((value) & ((value) - 1)) == 0)

#define checknull(p) ((p) == 0)
#define setnull(p) ((p) = 0)

#define dllist_empty(f, l) (checknull(f) && checknull(l))

#define dllist_push_back(f, l, n)                                              \
  (checknull(l)                                                                \
       ? ((f) = (l) = (n), setnull((n)->prev), setnull((n)->next))             \
       : ((l)->next = (n), (n)->prev = (l), setnull((n)->next), (l) = (n)))

#define dllist_remove(f, l, n)                                                 \
  (((n) == (f) ? (f) = (n)->next : (0)), ((n) == (l) ? (l) = (n)->prev : (0)), \
   (checknull((n)->prev) ? (0) : ((n)->prev->next = (n)->next)),               \
   (checknull((n)->next) ? (0) : ((n)->next->prev = (n)->prev)))

typedef struct Arena {
  u8 *data;
  u64 used;
  u64 size;
} Arena;

void arena_init(Arena *arena, u8 *data, u64 size);
void *arena_push(Arena *arena, u64 size, u32 align);

#endif
