/*
 * Hash Table - FRR Style
 *
 * Generic hash table implementation for fast route lookup and storage.
 *
 * Pattern Reference: lib/hash.h in FRRouting
 */

#ifndef _COMMON_HASH_H
#define _COMMON_HASH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct hash;
struct hash_bucket;

/* Hash function type */
typedef uint32_t (*hash_key_func)(const void *data);

/* Comparison function type (returns true if equal) */
typedef bool (*hash_cmp_func)(const void *a, const void *b);

/* Allocation function for copying data */
typedef void *(*hash_alloc_func)(void *data);

/* Free function for releasing data */
typedef void (*hash_free_func)(void *data);

/* Hash bucket */
struct hash_bucket {
    void *data;
    struct hash_bucket *next;
};

/* Hash table */
struct hash {
    struct hash_bucket **index;     /* Array of bucket pointers */
    uint32_t size;                  /* Number of buckets */
    uint32_t count;                 /* Number of entries */
    hash_key_func hash_key;         /* Hash function */
    hash_cmp_func hash_cmp;         /* Comparison function */
    const char *name;               /* Table name for debugging */
};

/* Standard hash sizes (prime numbers) */
#define HASH_SIZE_SMALL     31
#define HASH_SIZE_MEDIUM    1021
#define HASH_SIZE_LARGE     65521
#define HASH_SIZE_HUGE      1048573

/* Create a new hash table */
struct hash *hash_create(hash_key_func hash_key, hash_cmp_func hash_cmp,
                         const char *name);

struct hash *hash_create_size(uint32_t size, hash_key_func hash_key,
                              hash_cmp_func hash_cmp, const char *name);

/* Destroy a hash table */
void hash_free(struct hash *hash);
void hash_clean(struct hash *hash, hash_free_func free_func);
void hash_clean_and_free(struct hash **hash, hash_free_func free_func);

/* Get existing entry or create new one */
void *hash_get(struct hash *hash, void *data, hash_alloc_func alloc_func);

/* Lookup only (no allocation) */
void *hash_lookup(struct hash *hash, void *data);

/* Remove entry */
void *hash_release(struct hash *hash, void *data);

/* Iteration callback */
typedef void (*hash_iter_func)(struct hash_bucket *bucket, void *arg);

/* Iterate over all entries */
void hash_iterate(struct hash *hash, hash_iter_func iter_func, void *arg);

/* Get statistics */
uint32_t hash_count(struct hash *hash);

/* Debug: Print hash statistics */
void hash_stats(struct hash *hash);

/* Common hash functions */
uint32_t hash_string(const char *str);
uint32_t hash_bytes(const void *data, size_t len);
uint32_t hash_combine(uint32_t h1, uint32_t h2);

/* Jenkins hash functions */
uint32_t jhash(const void *key, uint32_t length, uint32_t initval);
uint32_t jhash2(const uint32_t *k, uint32_t length, uint32_t initval);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_HASH_H */
