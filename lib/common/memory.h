/*
 * Memory Management - FRR Style
 *
 * This module provides memory allocation wrappers with tracking and debugging
 * capabilities. In production FRR, this integrates with the memory type system
 * for leak detection and usage statistics.
 *
 * Pattern Reference: lib/memory.h in FRRouting
 */

#ifndef _COMMON_MEMORY_H
#define _COMMON_MEMORY_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory type identifiers for tracking allocations */
typedef enum {
    MTYPE_TMP,              /* Temporary allocations */
    MTYPE_BGP,              /* BGP general */
    MTYPE_BGP_PEER,         /* BGP peer structures */
    MTYPE_BGP_PEER_HOST,    /* BGP peer hostname */
    MTYPE_BGP_ATTR,         /* BGP attributes */
    MTYPE_BGP_ROUTE,        /* BGP route entries */
    MTYPE_BGP_PATH,         /* BGP path info */
    MTYPE_BGP_PACKET,       /* BGP packet buffers */
    MTYPE_BGP_GR,           /* BGP Graceful Restart */
    MTYPE_BGP_RR,           /* BGP Route Reflector */
    MTYPE_ROUTE_TABLE,      /* Routing table */
    MTYPE_ROUTE_NODE,       /* Route node */
    MTYPE_PREFIX,           /* Prefix structures */
    MTYPE_STREAM,           /* Stream buffers */
    MTYPE_HASH,             /* Hash tables */
    MTYPE_HASH_BUCKET,      /* Hash buckets */
    MTYPE_THREAD,           /* Thread structures */
    MTYPE_THREAD_MASTER,    /* Thread master */
    MTYPE_MAX
} memtype_t;

/* Memory type name lookup */
const char *mtype_name(memtype_t type);

/*
 * Memory allocation functions
 *
 * These wrap standard malloc/calloc/realloc/free with:
 * - Type tracking for debugging
 * - Automatic NULL check (abort on OOM in debug builds)
 * - Statistics collection
 */

void *qmalloc(memtype_t type, size_t size);
void *qcalloc(memtype_t type, size_t size);
void *qrealloc(memtype_t type, void *ptr, size_t size);
void qfree(memtype_t type, void *ptr);
char *qstrdup(memtype_t type, const char *str);

/* Convenience macros - use these in production code */
#define XMALLOC(T, S)       qmalloc((T), (S))
#define XCALLOC(T, S)       qcalloc((T), (S))
#define XREALLOC(T, P, S)   qrealloc((T), (P), (S))
#define XFREE(T, P)         do { qfree((T), (P)); (P) = NULL; } while(0)
#define XSTRDUP(T, S)       qstrdup((T), (S))

/* Memory statistics */
typedef struct {
    size_t alloc_count;
    size_t free_count;
    size_t current_bytes;
    size_t peak_bytes;
} mem_stats_t;

void mem_stats_get(memtype_t type, mem_stats_t *stats);
void mem_stats_dump(void);
void mem_stats_reset(void);

/* Memory debugging */
#ifdef DEBUG_MEMORY
void mem_check_leaks(void);
bool mem_is_allocated(void *ptr);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_MEMORY_H */
