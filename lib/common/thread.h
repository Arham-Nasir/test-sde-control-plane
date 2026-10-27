/*
 * Event Loop / Thread System - FRR Style
 *
 * FRR uses a custom event-driven architecture rather than traditional threads.
 * Each daemon has a "thread master" that manages scheduled events, timers,
 * and I/O callbacks.
 *
 * Pattern Reference: lib/frrevent.h in FRRouting (formerly lib/thread.h)
 */

#ifndef _COMMON_THREAD_H
#define _COMMON_THREAD_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct event;
struct event_loop;

/* Thread/Event types */
typedef enum {
    EVENT_UNUSED,
    EVENT_READ,
    EVENT_WRITE,
    EVENT_TIMER,
    EVENT_EVENT,        /* One-shot scheduled event */
    EVENT_EXECUTE,      /* Immediate execution */
} event_type_t;

/* Event callback function */
typedef void (*event_func_t)(struct event *event);

/* Event structure */
struct event {
    event_type_t type;
    event_func_t func;
    void *arg;                  /* User-provided argument */

    /* For timers */
    struct timeval sched;       /* Scheduled time */

    /* For I/O */
    int fd;
    int val;                    /* Return value or flags */

    /* Linkage */
    struct event *next;
    struct event *prev;
    struct event_loop *master;

    /* Debug info */
    const char *funcname;
    const char *filename;
    int line;
};

/* Event loop structure */
struct event_loop {
    /* Event lists */
    struct event *read_head;
    struct event *write_head;
    struct event *timer_head;
    struct event *event_head;

    /* For select() */
    int maxfd;
    fd_set readfd;
    fd_set writefd;
    fd_set exceptfd;

    /* Control */
    bool running;
    bool spin;          /* Spin instead of blocking */

    /* Statistics */
    uint64_t events_scheduled;
    uint64_t events_executed;
};

/* Convenience typedef */
typedef struct event_loop thread_master;
typedef struct event thread;

/* Event loop management */
struct event_loop *event_loop_new(void);
void event_loop_free(struct event_loop *loop);

/* Event scheduling */
struct event *event_add_read(struct event_loop *loop, event_func_t func,
                             void *arg, int fd,
                             const char *funcname, const char *filename, int line);

struct event *event_add_write(struct event_loop *loop, event_func_t func,
                              void *arg, int fd,
                              const char *funcname, const char *filename, int line);

struct event *event_add_timer(struct event_loop *loop, event_func_t func,
                              void *arg, unsigned long timer_ms,
                              const char *funcname, const char *filename, int line);

struct event *event_add_timer_tv(struct event_loop *loop, event_func_t func,
                                 void *arg, struct timeval *tv,
                                 const char *funcname, const char *filename, int line);

struct event *event_add_event(struct event_loop *loop, event_func_t func,
                              void *arg, int val,
                              const char *funcname, const char *filename, int line);

struct event *event_execute(struct event_loop *loop, event_func_t func,
                            void *arg, int val,
                            const char *funcname, const char *filename, int line);

/* Convenience macros with debug info */
#define EVENT_ADD_READ(L, F, A, FD) \
    event_add_read((L), (F), (A), (FD), #F, __FILE__, __LINE__)

#define EVENT_ADD_WRITE(L, F, A, FD) \
    event_add_write((L), (F), (A), (FD), #F, __FILE__, __LINE__)

#define EVENT_ADD_TIMER(L, F, A, MS) \
    event_add_timer((L), (F), (A), (MS), #F, __FILE__, __LINE__)

#define EVENT_ADD_TIMER_TV(L, F, A, TV) \
    event_add_timer_tv((L), (F), (A), (TV), #F, __FILE__, __LINE__)

#define EVENT_ADD_EVENT(L, F, A, V) \
    event_add_event((L), (F), (A), (V), #F, __FILE__, __LINE__)

#define EVENT_EXECUTE(L, F, A, V) \
    event_execute((L), (F), (A), (V), #F, __FILE__, __LINE__)

/* Legacy macros (FRR compatibility) */
#define thread_add_read     EVENT_ADD_READ
#define thread_add_write    EVENT_ADD_WRITE
#define thread_add_timer    EVENT_ADD_TIMER
#define thread_add_event    EVENT_ADD_EVENT

/* Event cancellation */
void event_cancel(struct event **event);
void event_cancel_by_arg(struct event_loop *loop, void *arg);

/* Event loop execution */
struct event *event_fetch(struct event_loop *loop, struct event *event);
void event_call(struct event *event);
void event_loop_run(struct event_loop *loop);
void event_loop_stop(struct event_loop *loop);

/* Utility */
void *EVENT_ARG(struct event *event);
int EVENT_FD(struct event *event);
int EVENT_VAL(struct event *event);

/* Legacy compatibility */
#define THREAD_ARG  EVENT_ARG
#define THREAD_FD   EVENT_FD
#define THREAD_VAL  EVENT_VAL

/* Timer utility */
void timeval_add(struct timeval *a, const struct timeval *b);
bool timeval_cmp(const struct timeval *a, const struct timeval *b);
long timeval_elapsed_ms(const struct timeval *start, const struct timeval *end);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_THREAD_H */
