/*
 * Memory Management Implementation
 */

#include "memory.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Memory type names for debugging */
static const char *mtype_names[] = {
    [MTYPE_TMP]             = "TMP",
    [MTYPE_BGP]             = "BGP",
    [MTYPE_BGP_PEER]        = "BGP_PEER",
    [MTYPE_BGP_PEER_HOST]   = "BGP_PEER_HOST",
    [MTYPE_BGP_ATTR]        = "BGP_ATTR",
    [MTYPE_BGP_ROUTE]       = "BGP_ROUTE",
    [MTYPE_BGP_PATH]        = "BGP_PATH",
    [MTYPE_BGP_PACKET]      = "BGP_PACKET",
    [MTYPE_BGP_GR]          = "BGP_GR",
    [MTYPE_BGP_RR]          = "BGP_RR",
    [MTYPE_ROUTE_TABLE]     = "ROUTE_TABLE",
    [MTYPE_ROUTE_NODE]      = "ROUTE_NODE",
    [MTYPE_PREFIX]          = "PREFIX",
    [MTYPE_STREAM]          = "STREAM",
    [MTYPE_HASH]            = "HASH",
    [MTYPE_HASH_BUCKET]     = "HASH_BUCKET",
    [MTYPE_THREAD]          = "THREAD",
    [MTYPE_THREAD_MASTER]   = "THREAD_MASTER",
};

/* Per-type statistics */
static mem_stats_t type_stats[MTYPE_MAX];
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

const char *mtype_name(memtype_t type)
{
    if (type >= MTYPE_MAX)
        return "UNKNOWN";
    return mtype_names[type];
}

static void update_stats_alloc(memtype_t type, size_t size)
{
    pthread_mutex_lock(&stats_lock);
    type_stats[type].alloc_count++;
    type_stats[type].current_bytes += size;
    if (type_stats[type].current_bytes > type_stats[type].peak_bytes)
        type_stats[type].peak_bytes = type_stats[type].current_bytes;
    pthread_mutex_unlock(&stats_lock);
}

static void update_stats_free(memtype_t type, size_t size)
{
    pthread_mutex_lock(&stats_lock);
    type_stats[type].free_count++;
    if (type_stats[type].current_bytes >= size)
        type_stats[type].current_bytes -= size;
    pthread_mutex_unlock(&stats_lock);
}

void *qmalloc(memtype_t type, size_t size)
{
    void *ptr;

    if (size == 0)
        size = 1;

    ptr = malloc(size);
    if (ptr == NULL) {
        log_err("Memory allocation failed: type=%s size=%zu",
                mtype_name(type), size);
        abort();
    }

    update_stats_alloc(type, size);
    return ptr;
}

void *qcalloc(memtype_t type, size_t size)
{
    void *ptr;

    if (size == 0)
        size = 1;

    ptr = calloc(1, size);
    if (ptr == NULL) {
        log_err("Memory allocation failed: type=%s size=%zu",
                mtype_name(type), size);
        abort();
    }

    update_stats_alloc(type, size);
    return ptr;
}

void *qrealloc(memtype_t type, void *ptr, size_t size)
{
    void *new_ptr;

    if (size == 0) {
        qfree(type, ptr);
        return NULL;
    }

    new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        log_err("Memory reallocation failed: type=%s size=%zu",
                mtype_name(type), size);
        abort();
    }

    /* Note: In real FRR, we'd track the size delta properly */
    update_stats_alloc(type, size);
    return new_ptr;
}

void qfree(memtype_t type, void *ptr)
{
    if (ptr == NULL)
        return;

    /* Note: Real FRR tracks allocation sizes; we approximate here */
    update_stats_free(type, 0);
    free(ptr);
}

char *qstrdup(memtype_t type, const char *str)
{
    char *dup;
    size_t len;

    if (str == NULL)
        return NULL;

    len = strlen(str) + 1;
    dup = qmalloc(type, len);
    memcpy(dup, str, len);

    return dup;
}

void mem_stats_get(memtype_t type, mem_stats_t *stats)
{
    if (type >= MTYPE_MAX || stats == NULL)
        return;

    pthread_mutex_lock(&stats_lock);
    *stats = type_stats[type];
    pthread_mutex_unlock(&stats_lock);
}

void mem_stats_dump(void)
{
    log_info("Memory Statistics:");
    log_info("%-20s %10s %10s %12s %12s",
             "Type", "Allocs", "Frees", "Current", "Peak");

    pthread_mutex_lock(&stats_lock);
    for (int i = 0; i < MTYPE_MAX; i++) {
        if (type_stats[i].alloc_count > 0) {
            log_info("%-20s %10zu %10zu %12zu %12zu",
                     mtype_names[i],
                     type_stats[i].alloc_count,
                     type_stats[i].free_count,
                     type_stats[i].current_bytes,
                     type_stats[i].peak_bytes);
        }
    }
    pthread_mutex_unlock(&stats_lock);
}

void mem_stats_reset(void)
{
    pthread_mutex_lock(&stats_lock);
    memset(type_stats, 0, sizeof(type_stats));
    pthread_mutex_unlock(&stats_lock);
}
