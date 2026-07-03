#include <stdint.h>
#include <stdio.h>

#include "common.h"

// for printing
const char *tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

uint64_t fnv1a_hash(const void *str, size_t n) {
    uint64_t hash = FNV_BASIS;

    for (size_t i = 0; i < n; i++) {
        hash ^= ((uint8_t*)str)[i];
        hash *= FNV_PRIME;
    }

    return hash;
}

void ht_resize(HashTable *ht) {
    size_t new_capacity = ht->capacity * 2;
    if (ht->capacity == 0) new_capacity = 256;
    Key *new_keys = (Key *)calloc(new_capacity, sizeof(Key));
    void **new_items = (void **)calloc(new_capacity, sizeof(void *));

    for (size_t i = 0; i < ht->capacity; i++) {
        if (ht->keys[i].data == NULL) continue;
        Key key = ht->keys[i];
        void *item = ht->items[i];

        uint64_t hash = fnv1a_hash(key.data, key.size);
        size_t index = hash % new_capacity;
        while (new_keys[index].data != NULL) {
            index = (index + 1) % new_capacity;
        }

        new_keys[index] = key;
        new_items[index] = item;
    }

    ht->keys = new_keys;
    ht->items = new_items;
    ht->capacity = new_capacity;
}


void ht_insert(HashTable *ht, const char *str, void *item) {
    ht_ninsert(ht, str, strlen(str), item);
}

void ht_ninsert(HashTable *ht, const void *key, size_t key_size, void* item) {
    if (ht->count * 2 >= ht->capacity) ht_resize(ht);

    uint64_t hash = fnv1a_hash(key, key_size);
    size_t index = hash % ht->capacity;
    while (ht->keys[index].data != NULL) {
        if (ht->keys[index].size == key_size && memcmp(ht->keys[index].data, key, key_size) == 0) {
            ht->items[index] = item;
            return;
        }

        index = (index + 1) % ht->capacity;
    }

    ht->keys[index].size = key_size;
    ht->keys[index].data = malloc(key_size);
    memcpy(ht->keys[index].data, key, key_size);

    ht->items[index] = item;
    ht->count++;
}


void *ht_search(HashTable *ht, const char *str) {
    return ht_nsearch(ht, str, strlen(str));
}


void *ht_nsearch(HashTable *ht, const void *key, size_t key_size) {
    if (ht->capacity == 0) return NULL;

    uint64_t hash = fnv1a_hash(key, key_size);
    size_t index = hash % ht->capacity;
    while (ht->keys[index].data != NULL) {
        if (ht->keys[index].size == key_size && memcmp(ht->keys[index].data, key, key_size) == 0) {
            return ht->items[index];
        }

        index = (index + 1) % ht->capacity;
    }

    return NULL;
}

int ht_nindex(HashTable *ht, const void *key, size_t key_size) {
    if (ht->capacity == 0) return -1;

    uint64_t hash = fnv1a_hash(key, key_size);
    size_t index = hash % ht->capacity;
    while (ht->keys[index].data != NULL) {
        if (ht->keys[index].size == key_size && memcmp(ht->keys[index].data, key, key_size) == 0) {
            return index;
        }

        index = (index + 1) % ht->capacity;
    }

    return -1;
}


void arena_create(Arena *a, size_t capacity) {
    a->capacity = capacity;
    a->size = 0;
    a->data = malloc(capacity);
    if (!a->data) {
        fprintf(stderr, "Failed to create arena of capacity %zu\n", capacity);
        exit(1);
    }
}

void *arena_alloc(Arena *a, size_t size) {
    // 8 byte alignment
    size_t alignment = sizeof(void *);
    size_t aligned_size = (size + (alignment - 1)) & ~(alignment - 1);

    if (a->size + aligned_size > a->capacity) {
        fprintf(stderr, "Arena overflow (capacity %zu)", a->capacity);
        exit(1);
    }

    void *ptr = a->data + a->size;
    a->size += aligned_size;

    // zero initialize
    memset(ptr, 0, aligned_size);
    return ptr;
}

void *arena_realloc(Arena *a, void *ptr, size_t old_size, size_t new_size) {
    uint8_t *res = (uint8_t *)arena_alloc(a, new_size);
    memcpy(res, ptr, old_size);
    return res;
}

String arena_sprintf(Arena *a, const char *fmt, ...) {
    String str = { 0 };
    va_list args, args2;

    va_start(args, fmt);

    va_copy(args2, args);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (len >= 0) {
        str.data = arena_alloc(a, (len + 1) * sizeof(char));
        if (str.data != NULL)
            vsnprintf(str.data, len + 1, fmt, args2);
        str.len = len;          // length excludes the NUL terminator
    } else {
        str.len = -1;
        str.data = NULL;
    }

    va_end(args2);
    return str;
}

void *arena_intern(Arena *a, HashTable *intern, const void *value, size_t size) {
    void *interned = ht_nsearch(intern, value, size);
    if (interned != NULL) {
        return interned;
    }
    interned = arena_alloc(a, size);
    memcpy(interned, value, size);
    ht_ninsert(intern, value, size, interned);
    return interned;
}

void arena_reset(Arena *a) {
    a->size = 0;
}

size_t arena_mark(Arena *a) {
    return a->size;
}

void arena_restore(Arena *a, size_t mark) {
    a->size = mark;
}

void arena_free(Arena *a) {
    free(a->data);
    a->data = NULL;
    a->capacity = 0;
    a->size = 0;
}


void set_create(Set *set, Arena *arena, size_t size) { // bit_count is in universe
    set->bit_count = size;
    set->word_count = (size + 63) / 64;
    set->buckets = arena_alloc(arena, set->word_count * sizeof(uint64_t)); // for bitset
}

void set_intersect(Set *a, Set *b) {
    assert(a->bit_count == b->bit_count);

    for (size_t i = 0; i < b->word_count; i++) {
        a->buckets[i] &= b->buckets[i];
    }
}

void set_add(Set *a, Set *b) {
    assert(a->bit_count == b->bit_count);

    for (size_t i = 0; i < b->word_count; i++) {
        a->buckets[i] |= b->buckets[i];
    }
}

void set_subtract(Set *a, Set *b) {
    assert(a->bit_count == b->bit_count);

    for (size_t i = 0; i < b->word_count; i++)
        a->buckets[i] &= ~b->buckets[i];
}

bool set_insert(Set *set, size_t val) {
    assert(val < set->bit_count);
    uint64_t mask = 1ULL << (val & 63);
    uint64_t *bucket = &set->buckets[val >> 6];
    if ((*bucket & mask) != 0) return false;
    *bucket |= mask;
    return true;
}

bool set_pop(Set *set, size_t *out) {
    for (size_t i = set->word_count; i-- > 0;) {
        if (set->buckets[i]) {
            uint64_t w = set->buckets[i];
            size_t bit = 63 - (size_t)__builtin_clzll(w);
            set->buckets[i] &= ~(1ULL << bit);
            *out = i * 64 + bit;
            assert(*out < set->bit_count);
            return true;
        }
    }
    return false;
}

bool set_has(Set *set, size_t val) {
    assert(val < set->bit_count);
    return (set->buckets[val >> 6] & (1ULL << (val & 63))) != 0;
}

void set_remove(Set *set, size_t val) {
    assert(val < set->bit_count);
    set->buckets[val >> 6] &= ~(1ULL << (val & 63));
}

void set_clear(Set *set) {
    memset(set->buckets, 0ULL, set->word_count * sizeof(uint64_t));
}

void set_copy(Set *dst, Set *src) {
    assert(dst->bit_count == src->bit_count);

    memcpy(dst->buckets, src->buckets, src->word_count * sizeof(uint64_t));
}

bool set_equals(Set *a, Set *b) {
    assert(a->bit_count == b->bit_count);
    return memcmp(a->buckets, b->buckets, a->word_count * sizeof(uint64_t)) == 0;
}

bool set_empty(Set *set) {
    for (size_t i = 0; i < set->word_count; i++) {
        if (set->buckets[i]) return false;
    }
    return true;
}

void set_complement(Set *set) {
    for (size_t i = 0; i < set->word_count; i++) {
        set->buckets[i] = ~set->buckets[i];
    }
    if (set->bit_count & 63)
        set->buckets[set->word_count - 1] &= (1ULL << (set->bit_count & 63)) - 1;
}
