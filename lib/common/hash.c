/*
 * Hash Table Implementation
 */

#include "hash.h"
#include "memory.h"
#include "log.h"

#include <string.h>

struct hash *hash_create(hash_key_func hash_key, hash_cmp_func hash_cmp,
                         const char *name)
{
    return hash_create_size(HASH_SIZE_MEDIUM, hash_key, hash_cmp, name);
}

struct hash *hash_create_size(uint32_t size, hash_key_func hash_key,
                              hash_cmp_func hash_cmp, const char *name)
{
    struct hash *hash;

    hash = XCALLOC(MTYPE_HASH, sizeof(struct hash));
    hash->index = XCALLOC(MTYPE_HASH_BUCKET, sizeof(struct hash_bucket *) * size);
    hash->size = size;
    hash->count = 0;
    hash->hash_key = hash_key;
    hash->hash_cmp = hash_cmp;
    hash->name = name;

    return hash;
}

void hash_clean(struct hash *hash, hash_free_func free_func)
{
    struct hash_bucket *bucket, *next;
    uint32_t i;

    if (hash == NULL)
        return;

    for (i = 0; i < hash->size; i++) {
        for (bucket = hash->index[i]; bucket; bucket = next) {
            next = bucket->next;
            if (free_func && bucket->data)
                free_func(bucket->data);
            XFREE(MTYPE_HASH_BUCKET, bucket);
        }
        hash->index[i] = NULL;
    }
    hash->count = 0;
}

void hash_free(struct hash *hash)
{
    if (hash == NULL)
        return;

    hash_clean(hash, NULL);
    XFREE(MTYPE_HASH_BUCKET, hash->index);
    XFREE(MTYPE_HASH, hash);
}

void hash_clean_and_free(struct hash **hash, hash_free_func free_func)
{
    if (hash == NULL || *hash == NULL)
        return;

    hash_clean(*hash, free_func);
    XFREE(MTYPE_HASH_BUCKET, (*hash)->index);
    XFREE(MTYPE_HASH, *hash);
    *hash = NULL;
}

void *hash_get(struct hash *hash, void *data, hash_alloc_func alloc_func)
{
    uint32_t key;
    uint32_t index;
    struct hash_bucket *bucket;

    if (hash == NULL || data == NULL)
        return NULL;

    key = hash->hash_key(data);
    index = key % hash->size;

    /* Search for existing entry */
    for (bucket = hash->index[index]; bucket; bucket = bucket->next) {
        if (hash->hash_cmp(bucket->data, data))
            return bucket->data;
    }

    /* Not found - create new if alloc_func provided */
    if (alloc_func == NULL)
        return NULL;

    bucket = XCALLOC(MTYPE_HASH_BUCKET, sizeof(struct hash_bucket));
    bucket->data = alloc_func(data);
    bucket->next = hash->index[index];
    hash->index[index] = bucket;
    hash->count++;

    return bucket->data;
}

void *hash_lookup(struct hash *hash, void *data)
{
    return hash_get(hash, data, NULL);
}

void *hash_release(struct hash *hash, void *data)
{
    uint32_t key;
    uint32_t index;
    struct hash_bucket *bucket, *prev;
    void *ret;

    if (hash == NULL || data == NULL)
        return NULL;

    key = hash->hash_key(data);
    index = key % hash->size;

    prev = NULL;
    for (bucket = hash->index[index]; bucket; bucket = bucket->next) {
        if (hash->hash_cmp(bucket->data, data)) {
            /* Found - remove from chain */
            if (prev)
                prev->next = bucket->next;
            else
                hash->index[index] = bucket->next;

            ret = bucket->data;
            XFREE(MTYPE_HASH_BUCKET, bucket);
            hash->count--;
            return ret;
        }
        prev = bucket;
    }

    return NULL;
}

void hash_iterate(struct hash *hash, hash_iter_func iter_func, void *arg)
{
    struct hash_bucket *bucket;
    uint32_t i;

    if (hash == NULL || iter_func == NULL)
        return;

    for (i = 0; i < hash->size; i++) {
        for (bucket = hash->index[i]; bucket; bucket = bucket->next) {
            iter_func(bucket, arg);
        }
    }
}

uint32_t hash_count(struct hash *hash)
{
    return hash ? hash->count : 0;
}

void hash_stats(struct hash *hash)
{
    uint32_t i;
    uint32_t empty = 0;
    uint32_t max_depth = 0;

    if (hash == NULL)
        return;

    for (i = 0; i < hash->size; i++) {
        struct hash_bucket *bucket;
        uint32_t depth = 0;

        for (bucket = hash->index[i]; bucket; bucket = bucket->next)
            depth++;

        if (depth == 0)
            empty++;
        if (depth > max_depth)
            max_depth = depth;
    }

    log_info("Hash '%s': size=%u count=%u empty=%u max_depth=%u",
             hash->name ? hash->name : "unnamed",
             hash->size, hash->count, empty, max_depth);
}

/* Common hash functions */
uint32_t hash_string(const char *str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;

    return hash;
}

uint32_t hash_bytes(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t hash = 5381;
    size_t i;

    for (i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + p[i];

    return hash;
}

uint32_t hash_combine(uint32_t h1, uint32_t h2)
{
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

/* Jenkins hash */
#define rot(x, k) (((x) << (k)) | ((x) >> (32 - (k))))

#define mix(a, b, c) \
do { \
    a -= c; a ^= rot(c, 4);  c += b; \
    b -= a; b ^= rot(a, 6);  a += c; \
    c -= b; c ^= rot(b, 8);  b += a; \
    a -= c; a ^= rot(c, 16); c += b; \
    b -= a; b ^= rot(a, 19); a += c; \
    c -= b; c ^= rot(b, 4);  b += a; \
} while (0)

#define final(a, b, c) \
do { \
    c ^= b; c -= rot(b, 14); \
    a ^= c; a -= rot(c, 11); \
    b ^= a; b -= rot(a, 25); \
    c ^= b; c -= rot(b, 16); \
    a ^= c; a -= rot(c, 4);  \
    b ^= a; b -= rot(a, 14); \
    c ^= b; c -= rot(b, 24); \
} while (0)

uint32_t jhash(const void *key, uint32_t length, uint32_t initval)
{
    const uint8_t *k = key;
    uint32_t a, b, c;

    a = b = c = 0xdeadbeef + length + initval;

    while (length > 12) {
        a += k[0] + ((uint32_t)k[1] << 8) + ((uint32_t)k[2] << 16) + ((uint32_t)k[3] << 24);
        b += k[4] + ((uint32_t)k[5] << 8) + ((uint32_t)k[6] << 16) + ((uint32_t)k[7] << 24);
        c += k[8] + ((uint32_t)k[9] << 8) + ((uint32_t)k[10] << 16) + ((uint32_t)k[11] << 24);
        mix(a, b, c);
        length -= 12;
        k += 12;
    }

    switch (length) {
    case 12: c += ((uint32_t)k[11]) << 24; /* fallthrough */
    case 11: c += ((uint32_t)k[10]) << 16; /* fallthrough */
    case 10: c += ((uint32_t)k[9]) << 8;   /* fallthrough */
    case 9:  c += k[8];                     /* fallthrough */
    case 8:  b += ((uint32_t)k[7]) << 24;  /* fallthrough */
    case 7:  b += ((uint32_t)k[6]) << 16;  /* fallthrough */
    case 6:  b += ((uint32_t)k[5]) << 8;   /* fallthrough */
    case 5:  b += k[4];                     /* fallthrough */
    case 4:  a += ((uint32_t)k[3]) << 24;  /* fallthrough */
    case 3:  a += ((uint32_t)k[2]) << 16;  /* fallthrough */
    case 2:  a += ((uint32_t)k[1]) << 8;   /* fallthrough */
    case 1:  a += k[0]; break;
    case 0:  return c;
    }

    final(a, b, c);
    return c;
}

uint32_t jhash2(const uint32_t *k, uint32_t length, uint32_t initval)
{
    uint32_t a, b, c;

    a = b = c = 0xdeadbeef + (length << 2) + initval;

    while (length > 3) {
        a += k[0];
        b += k[1];
        c += k[2];
        mix(a, b, c);
        length -= 3;
        k += 3;
    }

    switch (length) {
    case 3: c += k[2]; /* fallthrough */
    case 2: b += k[1]; /* fallthrough */
    case 1: a += k[0];
        final(a, b, c);
        break;
    case 0:
        break;
    }

    return c;
}
