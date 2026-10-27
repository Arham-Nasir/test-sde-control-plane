/*
 * Logging Implementation
 */

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static struct {
    log_level_t level;
    log_dest_t dest;
    FILE *file;
    const char *progname;
    pthread_mutex_t lock;
    bool initialized;
#ifdef LOG_BUFFER_ENABLED
    char buffer[8192];
    size_t buffer_pos;
#endif
} log_ctx = {
    .level = LOG_LEVEL_INFO,
    .dest = LOG_DEST_STDERR,
    .file = NULL,
    .progname = "daemon",
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
};

static const char *level_names[] = {
    [LOG_LEVEL_EMERG]   = "EMERG",
    [LOG_LEVEL_ALERT]   = "ALERT",
    [LOG_LEVEL_CRIT]    = "CRIT",
    [LOG_LEVEL_ERR]     = "ERROR",
    [LOG_LEVEL_WARNING] = "WARN",
    [LOG_LEVEL_NOTICE]  = "NOTICE",
    [LOG_LEVEL_INFO]    = "INFO",
    [LOG_LEVEL_DEBUG]   = "DEBUG",
};

void log_init(const char *progname, log_dest_t dest)
{
    pthread_mutex_lock(&log_ctx.lock);

    log_ctx.progname = progname ? progname : "daemon";
    log_ctx.dest = dest;
    log_ctx.initialized = true;

    if (dest & LOG_DEST_SYSLOG)
        openlog(log_ctx.progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);

    pthread_mutex_unlock(&log_ctx.lock);
}

void log_close(void)
{
    pthread_mutex_lock(&log_ctx.lock);

    if (log_ctx.dest & LOG_DEST_SYSLOG)
        closelog();

    if (log_ctx.file) {
        fclose(log_ctx.file);
        log_ctx.file = NULL;
    }

    log_ctx.initialized = false;

    pthread_mutex_unlock(&log_ctx.lock);
}

void log_set_level(log_level_t level)
{
    log_ctx.level = level;
}

log_level_t log_get_level(void)
{
    return log_ctx.level;
}

bool log_set_file(const char *filename)
{
    FILE *f;

    pthread_mutex_lock(&log_ctx.lock);

    if (log_ctx.file) {
        fclose(log_ctx.file);
        log_ctx.file = NULL;
    }

    f = fopen(filename, "a");
    if (f == NULL) {
        pthread_mutex_unlock(&log_ctx.lock);
        return false;
    }

    log_ctx.file = f;
    log_ctx.dest |= LOG_DEST_FILE;

    pthread_mutex_unlock(&log_ctx.lock);
    return true;
}

void vzlog(log_level_t level, const char *tag, const char *format, va_list ap)
{
    char timestamp[64];
    char message[1024];
    time_t now;
    struct tm *tm_info;

    if (level > log_ctx.level)
        return;

    /* Format message */
    vsnprintf(message, sizeof(message), format, ap);

    /* Get timestamp */
    now = time(NULL);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    pthread_mutex_lock(&log_ctx.lock);

    /* Output to destinations */
    if (log_ctx.dest & LOG_DEST_STDOUT) {
        if (tag && tag[0])
            fprintf(stdout, "%s [%s] %s: %s\n",
                    timestamp, level_names[level], tag, message);
        else
            fprintf(stdout, "%s [%s] %s\n",
                    timestamp, level_names[level], message);
        fflush(stdout);
    }

    if (log_ctx.dest & LOG_DEST_STDERR) {
        if (tag && tag[0])
            fprintf(stderr, "%s [%s] %s: %s\n",
                    timestamp, level_names[level], tag, message);
        else
            fprintf(stderr, "%s [%s] %s\n",
                    timestamp, level_names[level], message);
        fflush(stderr);
    }

    if ((log_ctx.dest & LOG_DEST_FILE) && log_ctx.file) {
        if (tag && tag[0])
            fprintf(log_ctx.file, "%s [%s] %s: %s\n",
                    timestamp, level_names[level], tag, message);
        else
            fprintf(log_ctx.file, "%s [%s] %s\n",
                    timestamp, level_names[level], message);
        fflush(log_ctx.file);
    }

    if (log_ctx.dest & LOG_DEST_SYSLOG) {
        if (tag && tag[0])
            syslog(level, "[%s] %s", tag, message);
        else
            syslog(level, "%s", message);
    }

#ifdef LOG_BUFFER_ENABLED
    if (log_ctx.dest & LOG_DEST_BUFFER) {
        int len;
        if (tag && tag[0])
            len = snprintf(log_ctx.buffer + log_ctx.buffer_pos,
                          sizeof(log_ctx.buffer) - log_ctx.buffer_pos,
                          "%s [%s] %s: %s\n",
                          timestamp, level_names[level], tag, message);
        else
            len = snprintf(log_ctx.buffer + log_ctx.buffer_pos,
                          sizeof(log_ctx.buffer) - log_ctx.buffer_pos,
                          "%s [%s] %s\n",
                          timestamp, level_names[level], message);
        if (len > 0 && log_ctx.buffer_pos + len < sizeof(log_ctx.buffer))
            log_ctx.buffer_pos += len;
    }
#endif

    pthread_mutex_unlock(&log_ctx.lock);
}

void zlog(log_level_t level, const char *tag, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vzlog(level, tag, format, ap);
    va_end(ap);
}

#ifdef LOG_BUFFER_ENABLED
const char *log_buffer_get(void)
{
    return log_ctx.buffer;
}

void log_buffer_clear(void)
{
    pthread_mutex_lock(&log_ctx.lock);
    log_ctx.buffer[0] = '\0';
    log_ctx.buffer_pos = 0;
    pthread_mutex_unlock(&log_ctx.lock);
}
#endif
