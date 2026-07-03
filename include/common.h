#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#define MAX_PARAMS 4

#define DYN_ARR(type) struct { \
    type *items; \
    size_t count, capacity; \
}

#define UNREACHABLE(err)\
    do {\
        fprintf(stderr, "UNREACHABLE reached at %s:%d in %s(): %s\n", __FILE__, __LINE__, __func__, (err));\
        abort();\
    } while (0)

/* Struct definition needed to use array macros.
 * typedef struct {
 *     T items;
 *     size_t count;
 *     size_t capacity;
 * } array;
*/

#define array_append(array, item)\
    do {\
        if ((array).count >= (array).capacity) {\
            if ((array).capacity == 0) (array).capacity = 128;\
            (array).capacity *= 2;\
            (array).items = realloc((array).items, (array).capacity * sizeof(*(array).items));\
        }\
        (array).items[(array).count++] = item;\
    } while (0)

#define array_init(array, size)\
    do {\
        (array).capacity = size;\
        (array).items = malloc((array).capacity * sizeof(*(array).items));\
        (array).count = 0;\
    } while (0)

#define array_free(array)\
    do {\
        (array).capacity = 0;\
        free((array).items);\
        (array).count = 0;\
    } while (0)

// TODO: make sure this minimum isn't too small. don't want to be too memory wasteful
// and most arena arrays will be a small number of ast children
#define ARENA_ARRAY_MINIMUM_CAP 16
#define arena_array_append(arena, array, item)\
    do {\
        if ((array).count >= (array).capacity) {\
            size_t old_capacity = (array).capacity;\
            if ((array).capacity == 0) (array).capacity = ARENA_ARRAY_MINIMUM_CAP;\
            (array).capacity *= 2;\
            (array).items = arena_realloc(arena, (array).items, old_capacity * sizeof(*(array).items), (array).capacity * sizeof(*(array).items));\
        }\
        (array).items[(array).count++] = item;\
    } while (0)

// NOTE: this hash table implementation does not support removal (yet?), as the symbol table
// does not require removal



#define FNV_PRIME 0x100000001b3
#define FNV_BASIS 0xcbf29ce484222325

uint64_t fnv1a_hash(const void *str, size_t n);

typedef struct {
    size_t size;
    void *data;
} Key;

typedef struct {
    Key *keys;
    void **items;
    size_t count;
    size_t capacity;
} HashTable;

void ht_insert(HashTable *ht, const char *str, void *item);
void ht_ninsert(HashTable *ht, const void *key, size_t key_size, void *item);
void ht_resize(HashTable *ht);
void *ht_search(HashTable *ht, const char *str);
void *ht_nsearch(HashTable *ht, const void *key, size_t key_size);
int ht_nindex(HashTable *ht, const void *key, size_t key_size);

// for printing

extern const char* tabs;

#define print_indent(extra, ...) \
    printf("%.*s" __VA_ARGS__, indent + (extra), tabs);

// currently only a fixed sized arena
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} Arena;

typedef struct {
    Arena *persistent;
    Arena *scratch;
} Arenas;

typedef struct {
    size_t len;
    char *data;
} String;

void arena_create(Arena *a, size_t capacity);
void *arena_alloc(Arena *a, size_t size);
void *arena_realloc(Arena *a, void *ptr, size_t old_size, size_t new_size);
void *arena_intern(Arena *a, HashTable *intern_table, const void *value, size_t size);
void arena_clear(Arena *a);
void arena_free(Arena *a);

String arena_sprintf(Arena *a, const char *fmt, ...);
size_t arena_mark(Arena *a);
void arena_restore(Arena *a, size_t cursor);

#define arena_alloc_type(arena_ptr, type) \
    (type *)arena_alloc((arena_ptr), sizeof(type))

typedef struct {
    size_t len;
    const char *str;
} StringView;


typedef struct {
    uint64_t *buckets;
    size_t bit_count; 
    size_t word_count;   
} Set;

void set_create(Set *set, Arena *arena, size_t size);
bool set_equals(Set *a, Set *b);
void set_intersect(Set *a, Set *b);
bool set_insert(Set *set, size_t val);
void set_remove(Set *set, size_t val);
bool set_pop(Set *set, size_t *out);
bool set_empty(Set *set);
void set_add(Set *a, Set *b); // stores into a
void set_subtract(Set *a, Set *b);
bool set_has(Set *set, size_t val);
void set_complement(Set *set);
void set_clear(Set *set);
void set_copy(Set *dst, Set* src);

#define DEFINE_STACK(type) \
struct { \
    Arena *arena; \
    type *stack; \
    size_t capacity; \
    size_t cursor; \
} \

#define DEFINE_STACK_DEF(name, type) \
     static inline void name##_set_backing(name *stack, Arena *arena) {\
         stack->arena = arena;\
     }\
     static inline type name##_pop(name *stack) { \
         if (stack->cursor <= 0) return (type) { 0 };\
         return stack->stack[--stack->cursor];\
     } \
     static inline type name##_peek(name *stack, int dist) { \
        assert(stack->cursor >= (size_t)dist && "cannot peek"); \
        return stack->stack[stack->cursor - dist]; \
     } \
     static inline void name##_push(name *stack, type val) { \
        if (stack->cursor + 1 > stack->capacity) { \
            size_t old = stack->capacity; \
            size_t new = old == 0 ? 16 : old * 2; \
            stack->stack = arena_realloc(stack->arena, stack->stack, old * sizeof(type), new * sizeof(type)); \
            stack->capacity = new;\
        }\
        stack->stack[stack->cursor++] = val; \
     } \

#define sv_literal(str) (StringView){ sizeof(str) - 1, (str) }
#define sv_fmt "%.*s"
#define sv_arg(sv) (int)(sv).len, (sv).str

#endif
