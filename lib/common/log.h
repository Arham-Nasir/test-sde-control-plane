/*
 * Logging System - FRR Style
 *
 * Provides structured logging with severity levels, destinations,
 * and protocol-specific tagging.
 *
 * Pattern Reference: lib/log.h in FRRouting
 */

#ifndef _COMMON_LOG_H
#define _COMMON_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels (compatible with syslog) */
typedef enum {
    LOG_LEVEL_EMERG   = LOG_EMERG,      /* System is unusable */
    LOG_LEVEL_ALERT   = LOG_ALERT,      /* Action must be taken immediately */
    LOG_LEVEL_CRIT    = LOG_CRIT,       /* Critical conditions */
    LOG_LEVEL_ERR     = LOG_ERR,        /* Error conditions */
    LOG_LEVEL_WARNING = LOG_WARNING,    /* Warning conditions */
    LOG_LEVEL_NOTICE  = LOG_NOTICE,     /* Normal but significant */
    LOG_LEVEL_INFO    = LOG_INFO,       /* Informational */
    LOG_LEVEL_DEBUG   = LOG_DEBUG,      /* Debug-level messages */
} log_level_t;

/* Log destinations */
typedef enum {
    LOG_DEST_STDOUT   = (1 << 0),
    LOG_DEST_STDERR   = (1 << 1),
    LOG_DEST_SYSLOG   = (1 << 2),
    LOG_DEST_FILE     = (1 << 3),
    LOG_DEST_BUFFER   = (1 << 4),   /* For testing */
} log_dest_t;

/* Protocol tags */
#define LOG_TAG_NONE    ""
#define LOG_TAG_BGP     "BGP"
#define LOG_TAG_OSPF    "OSPF"
#define LOG_TAG_ISIS    "ISIS"
#define LOG_TAG_RIB     "RIB"
#define LOG_TAG_ZEBRA   "ZEBRA"

/* Initialize logging */
void log_init(const char *progname, log_dest_t dest);
void log_close(void);

/* Set log level threshold */
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

/* Set log file */
bool log_set_file(const char *filename);

/* Core logging functions */
void zlog(log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

void vzlog(log_level_t level, const char *tag, const char *format, va_list ap);

/* Convenience macros with automatic tag */
#ifndef LOG_TAG
#define LOG_TAG LOG_TAG_NONE
#endif

#define log_emerg(fmt, ...)   zlog(LOG_LEVEL_EMERG, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_alert(fmt, ...)   zlog(LOG_LEVEL_ALERT, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_crit(fmt, ...)    zlog(LOG_LEVEL_CRIT, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_err(fmt, ...)     zlog(LOG_LEVEL_ERR, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)    zlog(LOG_LEVEL_WARNING, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_notice(fmt, ...)  zlog(LOG_LEVEL_NOTICE, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)    zlog(LOG_LEVEL_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...)   zlog(LOG_LEVEL_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)

/* FRR-style debug macros */
#ifdef DEBUG_ENABLED
#define DEBUGD(tag, fmt, ...) zlog(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define DEBUGD(tag, fmt, ...) do { } while(0)
#endif

/* Protocol-specific logging */
#define zlog_bgp(level, fmt, ...)   zlog(level, LOG_TAG_BGP, fmt, ##__VA_ARGS__)
#define zlog_ospf(level, fmt, ...)  zlog(level, LOG_TAG_OSPF, fmt, ##__VA_ARGS__)
#define zlog_isis(level, fmt, ...)  zlog(level, LOG_TAG_ISIS, fmt, ##__VA_ARGS__)
#define zlog_rib(level, fmt, ...)   zlog(level, LOG_TAG_RIB, fmt, ##__VA_ARGS__)
#define zlog_zebra(level, fmt, ...) zlog(level, LOG_TAG_ZEBRA, fmt, ##__VA_ARGS__)

/* Assert with logging */
#define LOG_ASSERT(cond) do { \
    if (!(cond)) { \
        log_crit("Assertion failed: %s at %s:%d", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

/* Buffer access for testing */
#ifdef LOG_BUFFER_ENABLED
const char *log_buffer_get(void);
void log_buffer_clear(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_LOG_H */
