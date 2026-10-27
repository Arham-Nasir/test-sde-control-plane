/*
 * Stream Buffer - FRR Style
 *
 * Network packet serialization/deserialization buffers.
 * Used for encoding and decoding BGP messages.
 *
 * Pattern Reference: lib/stream.h in FRRouting
 */

#ifndef _COMMON_STREAM_H
#define _COMMON_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stream structure */
struct stream {
    size_t getp;            /* Get pointer (read position) */
    size_t endp;            /* End pointer (write position) */
    size_t size;            /* Buffer size */
    uint8_t *data;          /* Data buffer */
};

/* FIFO of streams (for packet queues) */
struct stream_fifo {
    struct stream *head;
    struct stream *tail;
    size_t count;
    size_t max;             /* Maximum streams (0 = unlimited) */
};

/* Stream creation/destruction */
struct stream *stream_new(size_t size);
void stream_free(struct stream *s);
struct stream *stream_dup(struct stream *s);
struct stream *stream_copy(struct stream *dst, struct stream *src);

/* Reset stream positions */
void stream_reset(struct stream *s);
void stream_rewind_getp(struct stream *s);
void stream_rewind_endp(struct stream *s);

/* Get stream properties */
size_t stream_get_getp(struct stream *s);
size_t stream_get_endp(struct stream *s);
size_t stream_get_size(struct stream *s);

/* Remaining space/data */
size_t STREAM_READABLE(struct stream *s);
size_t STREAM_WRITEABLE(struct stream *s);
uint8_t *STREAM_DATA(struct stream *s);
uint8_t *STREAM_PNT(struct stream *s);

/* Position manipulation */
void stream_set_getp(struct stream *s, size_t pos);
void stream_set_endp(struct stream *s, size_t pos);
void stream_forward_getp(struct stream *s, size_t size);
void stream_forward_endp(struct stream *s, size_t size);

/* Write operations (append to stream) */
void stream_putc(struct stream *s, uint8_t c);
void stream_putw(struct stream *s, uint16_t w);
void stream_put3(struct stream *s, uint32_t l);  /* 3-byte value */
void stream_putl(struct stream *s, uint32_t l);
void stream_putq(struct stream *s, uint64_t q);
void stream_put(struct stream *s, const void *src, size_t size);

/* Write at specific position (no position change) */
void stream_putc_at(struct stream *s, size_t pos, uint8_t c);
void stream_putw_at(struct stream *s, size_t pos, uint16_t w);
void stream_put3_at(struct stream *s, size_t pos, uint32_t l);
void stream_putl_at(struct stream *s, size_t pos, uint32_t l);
void stream_putq_at(struct stream *s, size_t pos, uint64_t q);

/* Read operations (read from stream) */
uint8_t stream_getc(struct stream *s);
uint8_t stream_getc_from(struct stream *s, size_t pos);
uint16_t stream_getw(struct stream *s);
uint16_t stream_getw_from(struct stream *s, size_t pos);
uint32_t stream_get3(struct stream *s);
uint32_t stream_get3_from(struct stream *s, size_t pos);
uint32_t stream_getl(struct stream *s);
uint32_t stream_getl_from(struct stream *s, size_t pos);
uint64_t stream_getq(struct stream *s);
uint64_t stream_getq_from(struct stream *s, size_t pos);
void stream_get(void *dst, struct stream *s, size_t size);
void stream_get_from(void *dst, struct stream *s, size_t pos, size_t size);

/* IP address operations */
void stream_put_ipv4(struct stream *s, struct in_addr *addr);
void stream_get_ipv4(struct stream *s, struct in_addr *addr);
void stream_put_ipv6(struct stream *s, struct in6_addr *addr);
void stream_get_ipv6(struct stream *s, struct in6_addr *addr);

/* Prefix operations */
struct prefix;
void stream_put_prefix(struct stream *s, struct prefix *p);
void stream_get_prefix(struct stream *s, struct prefix *p);

/* Stream FIFO operations */
struct stream_fifo *stream_fifo_new(void);
void stream_fifo_free(struct stream_fifo *fifo);
void stream_fifo_push(struct stream_fifo *fifo, struct stream *s);
struct stream *stream_fifo_pop(struct stream_fifo *fifo);
struct stream *stream_fifo_head(struct stream_fifo *fifo);
void stream_fifo_clean(struct stream_fifo *fifo);
size_t stream_fifo_count(struct stream_fifo *fifo);

/* Debug */
void stream_hexdump(struct stream *s);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_STREAM_H */
