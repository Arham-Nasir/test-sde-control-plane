/*
 * Stream Buffer Implementation
 */

#include "stream.h"
#include "memory.h"
#include "prefix.h"
#include "log.h"

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

struct stream *stream_new(size_t size)
{
    struct stream *s;

    s = XCALLOC(MTYPE_STREAM, sizeof(struct stream));
    s->data = XMALLOC(MTYPE_STREAM, size);
    s->size = size;

    return s;
}

void stream_free(struct stream *s)
{
    if (s == NULL)
        return;

    XFREE(MTYPE_STREAM, s->data);
    XFREE(MTYPE_STREAM, s);
}

struct stream *stream_dup(struct stream *s)
{
    struct stream *new;

    if (s == NULL)
        return NULL;

    new = stream_new(s->size);
    new->getp = s->getp;
    new->endp = s->endp;
    memcpy(new->data, s->data, s->endp);

    return new;
}

struct stream *stream_copy(struct stream *dst, struct stream *src)
{
    if (dst == NULL || src == NULL)
        return NULL;

    dst->getp = src->getp;
    dst->endp = src->endp;

    if (dst->size < src->endp) {
        XFREE(MTYPE_STREAM, dst->data);
        dst->data = XMALLOC(MTYPE_STREAM, src->endp);
        dst->size = src->endp;
    }

    memcpy(dst->data, src->data, src->endp);

    return dst;
}

void stream_reset(struct stream *s)
{
    s->getp = 0;
    s->endp = 0;
}

void stream_rewind_getp(struct stream *s)
{
    s->getp = 0;
}

void stream_rewind_endp(struct stream *s)
{
    s->getp = 0;
    s->endp = 0;
}

size_t stream_get_getp(struct stream *s)
{
    return s->getp;
}

size_t stream_get_endp(struct stream *s)
{
    return s->endp;
}

size_t stream_get_size(struct stream *s)
{
    return s->size;
}

size_t STREAM_READABLE(struct stream *s)
{
    return s->endp - s->getp;
}

size_t STREAM_WRITEABLE(struct stream *s)
{
    return s->size - s->endp;
}

uint8_t *STREAM_DATA(struct stream *s)
{
    return s->data;
}

uint8_t *STREAM_PNT(struct stream *s)
{
    return s->data + s->getp;
}

void stream_set_getp(struct stream *s, size_t pos)
{
    if (pos <= s->endp)
        s->getp = pos;
}

void stream_set_endp(struct stream *s, size_t pos)
{
    if (pos <= s->size) {
        s->endp = pos;
        if (s->getp > s->endp)
            s->getp = s->endp;
    }
}

void stream_forward_getp(struct stream *s, size_t size)
{
    if (s->getp + size <= s->endp)
        s->getp += size;
}

void stream_forward_endp(struct stream *s, size_t size)
{
    if (s->endp + size <= s->size)
        s->endp += size;
}

void stream_putc(struct stream *s, uint8_t c)
{
    if (STREAM_WRITEABLE(s) < 1)
        return;

    s->data[s->endp++] = c;
}

void stream_putw(struct stream *s, uint16_t w)
{
    if (STREAM_WRITEABLE(s) < 2)
        return;

    s->data[s->endp++] = (w >> 8) & 0xff;
    s->data[s->endp++] = w & 0xff;
}

void stream_put3(struct stream *s, uint32_t l)
{
    if (STREAM_WRITEABLE(s) < 3)
        return;

    s->data[s->endp++] = (l >> 16) & 0xff;
    s->data[s->endp++] = (l >> 8) & 0xff;
    s->data[s->endp++] = l & 0xff;
}

void stream_putl(struct stream *s, uint32_t l)
{
    if (STREAM_WRITEABLE(s) < 4)
        return;

    s->data[s->endp++] = (l >> 24) & 0xff;
    s->data[s->endp++] = (l >> 16) & 0xff;
    s->data[s->endp++] = (l >> 8) & 0xff;
    s->data[s->endp++] = l & 0xff;
}

void stream_putq(struct stream *s, uint64_t q)
{
    if (STREAM_WRITEABLE(s) < 8)
        return;

    s->data[s->endp++] = (q >> 56) & 0xff;
    s->data[s->endp++] = (q >> 48) & 0xff;
    s->data[s->endp++] = (q >> 40) & 0xff;
    s->data[s->endp++] = (q >> 32) & 0xff;
    s->data[s->endp++] = (q >> 24) & 0xff;
    s->data[s->endp++] = (q >> 16) & 0xff;
    s->data[s->endp++] = (q >> 8) & 0xff;
    s->data[s->endp++] = q & 0xff;
}

void stream_put(struct stream *s, const void *src, size_t size)
{
    if (STREAM_WRITEABLE(s) < size)
        return;

    memcpy(s->data + s->endp, src, size);
    s->endp += size;
}

void stream_putc_at(struct stream *s, size_t pos, uint8_t c)
{
    if (pos < s->size)
        s->data[pos] = c;
}

void stream_putw_at(struct stream *s, size_t pos, uint16_t w)
{
    if (pos + 2 <= s->size) {
        s->data[pos] = (w >> 8) & 0xff;
        s->data[pos + 1] = w & 0xff;
    }
}

void stream_put3_at(struct stream *s, size_t pos, uint32_t l)
{
    if (pos + 3 <= s->size) {
        s->data[pos] = (l >> 16) & 0xff;
        s->data[pos + 1] = (l >> 8) & 0xff;
        s->data[pos + 2] = l & 0xff;
    }
}

void stream_putl_at(struct stream *s, size_t pos, uint32_t l)
{
    if (pos + 4 <= s->size) {
        s->data[pos] = (l >> 24) & 0xff;
        s->data[pos + 1] = (l >> 16) & 0xff;
        s->data[pos + 2] = (l >> 8) & 0xff;
        s->data[pos + 3] = l & 0xff;
    }
}

void stream_putq_at(struct stream *s, size_t pos, uint64_t q)
{
    if (pos + 8 <= s->size) {
        s->data[pos] = (q >> 56) & 0xff;
        s->data[pos + 1] = (q >> 48) & 0xff;
        s->data[pos + 2] = (q >> 40) & 0xff;
        s->data[pos + 3] = (q >> 32) & 0xff;
        s->data[pos + 4] = (q >> 24) & 0xff;
        s->data[pos + 5] = (q >> 16) & 0xff;
        s->data[pos + 6] = (q >> 8) & 0xff;
        s->data[pos + 7] = q & 0xff;
    }
}

uint8_t stream_getc(struct stream *s)
{
    if (STREAM_READABLE(s) < 1)
        return 0;

    return s->data[s->getp++];
}

uint8_t stream_getc_from(struct stream *s, size_t pos)
{
    if (pos >= s->endp)
        return 0;

    return s->data[pos];
}

uint16_t stream_getw(struct stream *s)
{
    uint16_t w;

    if (STREAM_READABLE(s) < 2)
        return 0;

    w = (s->data[s->getp] << 8) | s->data[s->getp + 1];
    s->getp += 2;
    return w;
}

uint16_t stream_getw_from(struct stream *s, size_t pos)
{
    if (pos + 2 > s->endp)
        return 0;

    return (s->data[pos] << 8) | s->data[pos + 1];
}

uint32_t stream_get3(struct stream *s)
{
    uint32_t l;

    if (STREAM_READABLE(s) < 3)
        return 0;

    l = ((uint32_t)s->data[s->getp] << 16) |
        ((uint32_t)s->data[s->getp + 1] << 8) |
        s->data[s->getp + 2];
    s->getp += 3;
    return l;
}

uint32_t stream_get3_from(struct stream *s, size_t pos)
{
    if (pos + 3 > s->endp)
        return 0;

    return ((uint32_t)s->data[pos] << 16) |
           ((uint32_t)s->data[pos + 1] << 8) |
           s->data[pos + 2];
}

uint32_t stream_getl(struct stream *s)
{
    uint32_t l;

    if (STREAM_READABLE(s) < 4)
        return 0;

    l = ((uint32_t)s->data[s->getp] << 24) |
        ((uint32_t)s->data[s->getp + 1] << 16) |
        ((uint32_t)s->data[s->getp + 2] << 8) |
        s->data[s->getp + 3];
    s->getp += 4;
    return l;
}

uint32_t stream_getl_from(struct stream *s, size_t pos)
{
    if (pos + 4 > s->endp)
        return 0;

    return ((uint32_t)s->data[pos] << 24) |
           ((uint32_t)s->data[pos + 1] << 16) |
           ((uint32_t)s->data[pos + 2] << 8) |
           s->data[pos + 3];
}

uint64_t stream_getq(struct stream *s)
{
    uint64_t q;

    if (STREAM_READABLE(s) < 8)
        return 0;

    q = ((uint64_t)s->data[s->getp] << 56) |
        ((uint64_t)s->data[s->getp + 1] << 48) |
        ((uint64_t)s->data[s->getp + 2] << 40) |
        ((uint64_t)s->data[s->getp + 3] << 32) |
        ((uint64_t)s->data[s->getp + 4] << 24) |
        ((uint64_t)s->data[s->getp + 5] << 16) |
        ((uint64_t)s->data[s->getp + 6] << 8) |
        s->data[s->getp + 7];
    s->getp += 8;
    return q;
}

uint64_t stream_getq_from(struct stream *s, size_t pos)
{
    if (pos + 8 > s->endp)
        return 0;

    return ((uint64_t)s->data[pos] << 56) |
           ((uint64_t)s->data[pos + 1] << 48) |
           ((uint64_t)s->data[pos + 2] << 40) |
           ((uint64_t)s->data[pos + 3] << 32) |
           ((uint64_t)s->data[pos + 4] << 24) |
           ((uint64_t)s->data[pos + 5] << 16) |
           ((uint64_t)s->data[pos + 6] << 8) |
           s->data[pos + 7];
}

void stream_get(void *dst, struct stream *s, size_t size)
{
    if (STREAM_READABLE(s) < size)
        return;

    memcpy(dst, s->data + s->getp, size);
    s->getp += size;
}

void stream_get_from(void *dst, struct stream *s, size_t pos, size_t size)
{
    if (pos + size > s->endp)
        return;

    memcpy(dst, s->data + pos, size);
}

void stream_put_ipv4(struct stream *s, struct in_addr *addr)
{
    stream_putl(s, ntohl(addr->s_addr));
}

void stream_get_ipv4(struct stream *s, struct in_addr *addr)
{
    addr->s_addr = htonl(stream_getl(s));
}

void stream_put_ipv6(struct stream *s, struct in6_addr *addr)
{
    stream_put(s, addr->s6_addr, 16);
}

void stream_get_ipv6(struct stream *s, struct in6_addr *addr)
{
    stream_get(addr->s6_addr, s, 16);
}

void stream_put_prefix(struct stream *s, struct prefix *p)
{
    size_t psize;

    stream_putc(s, p->prefixlen);

    psize = (p->prefixlen + 7) / 8;
    stream_put(s, &p->u.val, psize);
}

void stream_get_prefix(struct stream *s, struct prefix *p)
{
    size_t psize;

    p->prefixlen = stream_getc(s);
    psize = (p->prefixlen + 7) / 8;
    memset(&p->u.val, 0, sizeof(p->u.val));
    stream_get(&p->u.val, s, psize);
}

/* Stream FIFO */
struct stream_fifo *stream_fifo_new(void)
{
    return XCALLOC(MTYPE_STREAM, sizeof(struct stream_fifo));
}

void stream_fifo_free(struct stream_fifo *fifo)
{
    if (fifo == NULL)
        return;

    stream_fifo_clean(fifo);
    XFREE(MTYPE_STREAM, fifo);
}

void stream_fifo_push(struct stream_fifo *fifo, struct stream *s)
{
    if (fifo->tail)
        fifo->tail = s;
    else
        fifo->head = fifo->tail = s;
    fifo->count++;
}

struct stream *stream_fifo_pop(struct stream_fifo *fifo)
{
    struct stream *s;

    if (fifo->head == NULL)
        return NULL;

    s = fifo->head;
    fifo->head = NULL;  /* Simplified - real impl uses linked list */
    if (fifo->head == NULL)
        fifo->tail = NULL;
    fifo->count--;

    return s;
}

struct stream *stream_fifo_head(struct stream_fifo *fifo)
{
    return fifo->head;
}

void stream_fifo_clean(struct stream_fifo *fifo)
{
    struct stream *s;

    while ((s = stream_fifo_pop(fifo)))
        stream_free(s);
}

size_t stream_fifo_count(struct stream_fifo *fifo)
{
    return fifo->count;
}

void stream_hexdump(struct stream *s)
{
    size_t i;

    log_debug("Stream: getp=%zu endp=%zu size=%zu",
              s->getp, s->endp, s->size);

    for (i = 0; i < s->endp; i += 16) {
        char hex[50], ascii[18];
        size_t j;

        memset(hex, 0, sizeof(hex));
        memset(ascii, 0, sizeof(ascii));

        for (j = 0; j < 16 && i + j < s->endp; j++) {
            snprintf(hex + j * 3, sizeof(hex) - j * 3, "%02x ", s->data[i + j]);
            ascii[j] = (s->data[i + j] >= 32 && s->data[i + j] < 127)
                       ? s->data[i + j] : '.';
        }

        log_debug("%04zx: %-48s %s", i, hex, ascii);
    }
}
