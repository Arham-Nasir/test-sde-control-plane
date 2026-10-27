/*
 * Event Loop Implementation
 */

#include "thread.h"
#include "memory.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>

struct event_loop *event_loop_new(void)
{
    struct event_loop *loop;

    loop = XCALLOC(MTYPE_THREAD_MASTER, sizeof(struct event_loop));
    loop->maxfd = 0;
    FD_ZERO(&loop->readfd);
    FD_ZERO(&loop->writefd);
    FD_ZERO(&loop->exceptfd);

    return loop;
}

static void free_event_list(struct event *head)
{
    struct event *t, *next;

    for (t = head; t; t = next) {
        next = t->next;
        XFREE(MTYPE_THREAD, t);
    }
}

void event_loop_free(struct event_loop *loop)
{
    if (!loop)
        return;

    free_event_list(loop->read_head);
    free_event_list(loop->write_head);
    free_event_list(loop->timer_head);
    free_event_list(loop->event_head);

    XFREE(MTYPE_THREAD_MASTER, loop);
}

static struct event *event_new(struct event_loop *loop, event_type_t type,
                               event_func_t func, void *arg,
                               const char *funcname, const char *filename, int line)
{
    struct event *event;

    event = XCALLOC(MTYPE_THREAD, sizeof(struct event));
    event->type = type;
    event->func = func;
    event->arg = arg;
    event->master = loop;
    event->funcname = funcname;
    event->filename = filename;
    event->line = line;

    loop->events_scheduled++;

    return event;
}

static void event_list_add(struct event **head, struct event *event)
{
    event->next = *head;
    event->prev = NULL;

    if (*head)
        (*head)->prev = event;
    *head = event;
}

static void event_list_add_sorted(struct event **head, struct event *event)
{
    struct event *t;

    if (*head == NULL || timeval_cmp(&event->sched, &(*head)->sched)) {
        event_list_add(head, event);
        return;
    }

    for (t = *head; t->next; t = t->next) {
        if (timeval_cmp(&event->sched, &t->next->sched)) {
            event->next = t->next;
            event->prev = t;
            t->next->prev = event;
            t->next = event;
            return;
        }
    }

    /* Add at end */
    t->next = event;
    event->prev = t;
}

static void event_list_delete(struct event **head, struct event *event)
{
    if (event->prev)
        event->prev->next = event->next;
    else
        *head = event->next;

    if (event->next)
        event->next->prev = event->prev;
}

struct event *event_add_read(struct event_loop *loop, event_func_t func,
                             void *arg, int fd,
                             const char *funcname, const char *filename, int line)
{
    struct event *event;

    if (fd < 0 || fd >= FD_SETSIZE) {
        log_err("event_add_read: invalid fd %d", fd);
        return NULL;
    }

    event = event_new(loop, EVENT_READ, func, arg, funcname, filename, line);
    event->fd = fd;

    FD_SET(fd, &loop->readfd);
    if (fd > loop->maxfd)
        loop->maxfd = fd;

    event_list_add(&loop->read_head, event);

    return event;
}

struct event *event_add_write(struct event_loop *loop, event_func_t func,
                              void *arg, int fd,
                              const char *funcname, const char *filename, int line)
{
    struct event *event;

    if (fd < 0 || fd >= FD_SETSIZE) {
        log_err("event_add_write: invalid fd %d", fd);
        return NULL;
    }

    event = event_new(loop, EVENT_WRITE, func, arg, funcname, filename, line);
    event->fd = fd;

    FD_SET(fd, &loop->writefd);
    if (fd > loop->maxfd)
        loop->maxfd = fd;

    event_list_add(&loop->write_head, event);

    return event;
}

struct event *event_add_timer(struct event_loop *loop, event_func_t func,
                              void *arg, unsigned long timer_ms,
                              const char *funcname, const char *filename, int line)
{
    struct timeval tv;

    tv.tv_sec = timer_ms / 1000;
    tv.tv_usec = (timer_ms % 1000) * 1000;

    return event_add_timer_tv(loop, func, arg, &tv, funcname, filename, line);
}

struct event *event_add_timer_tv(struct event_loop *loop, event_func_t func,
                                 void *arg, struct timeval *tv,
                                 const char *funcname, const char *filename, int line)
{
    struct event *event;
    struct timeval now;

    event = event_new(loop, EVENT_TIMER, func, arg, funcname, filename, line);

    gettimeofday(&now, NULL);
    event->sched = now;
    timeval_add(&event->sched, tv);

    event_list_add_sorted(&loop->timer_head, event);

    return event;
}

struct event *event_add_event(struct event_loop *loop, event_func_t func,
                              void *arg, int val,
                              const char *funcname, const char *filename, int line)
{
    struct event *event;

    event = event_new(loop, EVENT_EVENT, func, arg, funcname, filename, line);
    event->val = val;

    event_list_add(&loop->event_head, event);

    return event;
}

struct event *event_execute(struct event_loop *loop, event_func_t func,
                            void *arg, int val,
                            const char *funcname, const char *filename, int line)
{
    struct event event;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_EXECUTE;
    event.func = func;
    event.arg = arg;
    event.val = val;
    event.master = loop;
    event.funcname = funcname;
    event.filename = filename;
    event.line = line;

    func(&event);

    return NULL;
}

void event_cancel(struct event **event)
{
    struct event *t;

    if (event == NULL || *event == NULL)
        return;

    t = *event;

    switch (t->type) {
    case EVENT_READ:
        event_list_delete(&t->master->read_head, t);
        FD_CLR(t->fd, &t->master->readfd);
        break;
    case EVENT_WRITE:
        event_list_delete(&t->master->write_head, t);
        FD_CLR(t->fd, &t->master->writefd);
        break;
    case EVENT_TIMER:
        event_list_delete(&t->master->timer_head, t);
        break;
    case EVENT_EVENT:
        event_list_delete(&t->master->event_head, t);
        break;
    default:
        break;
    }

    XFREE(MTYPE_THREAD, t);
    *event = NULL;
}

void event_cancel_by_arg(struct event_loop *loop, void *arg)
{
    struct event *t, *next;

    /* Cancel from all lists */
    for (t = loop->read_head; t; t = next) {
        next = t->next;
        if (t->arg == arg) {
            event_list_delete(&loop->read_head, t);
            FD_CLR(t->fd, &loop->readfd);
            XFREE(MTYPE_THREAD, t);
        }
    }

    for (t = loop->write_head; t; t = next) {
        next = t->next;
        if (t->arg == arg) {
            event_list_delete(&loop->write_head, t);
            FD_CLR(t->fd, &loop->writefd);
            XFREE(MTYPE_THREAD, t);
        }
    }

    for (t = loop->timer_head; t; t = next) {
        next = t->next;
        if (t->arg == arg) {
            event_list_delete(&loop->timer_head, t);
            XFREE(MTYPE_THREAD, t);
        }
    }

    for (t = loop->event_head; t; t = next) {
        next = t->next;
        if (t->arg == arg) {
            event_list_delete(&loop->event_head, t);
            XFREE(MTYPE_THREAD, t);
        }
    }
}

static struct event *event_pop(struct event **head)
{
    struct event *t = *head;

    if (t) {
        *head = t->next;
        if (t->next)
            t->next->prev = NULL;
        t->next = t->prev = NULL;
    }

    return t;
}

struct event *event_fetch(struct event_loop *loop, struct event *fetch)
{
    struct event *event;
    struct timeval now, timeout, *wait;
    fd_set readfd, writefd;
    int num;

    while (loop->running) {
        /* Check scheduled events first */
        if ((event = event_pop(&loop->event_head))) {
            *fetch = *event;
            XFREE(MTYPE_THREAD, event);
            return fetch;
        }

        /* Check timers */
        gettimeofday(&now, NULL);
        while (loop->timer_head) {
            if (timeval_cmp(&loop->timer_head->sched, &now))
                break;

            event = event_pop(&loop->timer_head);
            *fetch = *event;
            XFREE(MTYPE_THREAD, event);
            return fetch;
        }

        /* Calculate select timeout */
        if (loop->timer_head) {
            timeout = loop->timer_head->sched;
            timeout.tv_sec -= now.tv_sec;
            timeout.tv_usec -= now.tv_usec;
            if (timeout.tv_usec < 0) {
                timeout.tv_sec--;
                timeout.tv_usec += 1000000;
            }
            if (timeout.tv_sec < 0) {
                timeout.tv_sec = 0;
                timeout.tv_usec = 0;
            }
            wait = &timeout;
        } else if (loop->spin) {
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;
            wait = &timeout;
        } else {
            wait = NULL;
        }

        /* Copy fd sets */
        readfd = loop->readfd;
        writefd = loop->writefd;

        num = select(loop->maxfd + 1, &readfd, &writefd, NULL, wait);

        if (num < 0) {
            if (errno == EINTR)
                continue;
            log_err("select error: %s", strerror(errno));
            continue;
        }

        if (num == 0)
            continue;

        /* Check read events */
        for (event = loop->read_head; event && num; event = event->next) {
            if (FD_ISSET(event->fd, &readfd)) {
                event_list_delete(&loop->read_head, event);
                FD_CLR(event->fd, &loop->readfd);
                *fetch = *event;
                XFREE(MTYPE_THREAD, event);
                return fetch;
            }
        }

        /* Check write events */
        for (event = loop->write_head; event && num; event = event->next) {
            if (FD_ISSET(event->fd, &writefd)) {
                event_list_delete(&loop->write_head, event);
                FD_CLR(event->fd, &loop->writefd);
                *fetch = *event;
                XFREE(MTYPE_THREAD, event);
                return fetch;
            }
        }
    }

    return NULL;
}

void event_call(struct event *event)
{
    if (event && event->func) {
        event->master->events_executed++;
        event->func(event);
    }
}

void event_loop_run(struct event_loop *loop)
{
    struct event event;

    loop->running = true;

    while (event_fetch(loop, &event))
        event_call(&event);
}

void event_loop_stop(struct event_loop *loop)
{
    loop->running = false;
}

void *EVENT_ARG(struct event *event)
{
    return event ? event->arg : NULL;
}

int EVENT_FD(struct event *event)
{
    return event ? event->fd : -1;
}

int EVENT_VAL(struct event *event)
{
    return event ? event->val : 0;
}

void timeval_add(struct timeval *a, const struct timeval *b)
{
    a->tv_sec += b->tv_sec;
    a->tv_usec += b->tv_usec;
    while (a->tv_usec >= 1000000) {
        a->tv_sec++;
        a->tv_usec -= 1000000;
    }
}

bool timeval_cmp(const struct timeval *a, const struct timeval *b)
{
    /* Returns true if a < b */
    if (a->tv_sec < b->tv_sec)
        return true;
    if (a->tv_sec > b->tv_sec)
        return false;
    return a->tv_usec < b->tv_usec;
}

long timeval_elapsed_ms(const struct timeval *start, const struct timeval *end)
{
    long sec_diff = end->tv_sec - start->tv_sec;
    long usec_diff = end->tv_usec - start->tv_usec;

    return sec_diff * 1000 + usec_diff / 1000;
}
