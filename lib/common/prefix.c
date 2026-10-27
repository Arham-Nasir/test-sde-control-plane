/*
 * IP Prefix Handling Implementation
 */

#include "prefix.h"
#include "memory.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void prefix_init(struct prefix *p)
{
    memset(p, 0, sizeof(struct prefix));
}

void prefix_ipv4_init(struct prefix_ipv4 *p)
{
    memset(p, 0, sizeof(struct prefix_ipv4));
    p->family = AF_INET;
}

void prefix_ipv6_init(struct prefix_ipv6 *p)
{
    memset(p, 0, sizeof(struct prefix_ipv6));
    p->family = AF_INET6;
}

void prefix_copy(struct prefix *dst, const struct prefix *src)
{
    memcpy(dst, src, sizeof(struct prefix));
}

int prefix_cmp(const struct prefix *p1, const struct prefix *p2)
{
    int offset;
    int shift;

    /* Compare address family */
    if (p1->family != p2->family)
        return (int)p1->family - (int)p2->family;

    /* Compare prefix length */
    if (p1->prefixlen != p2->prefixlen)
        return (int)p1->prefixlen - (int)p2->prefixlen;

    /* Compare prefix bytes */
    offset = p1->prefixlen / 8;
    shift = p1->prefixlen % 8;

    if (offset) {
        int ret = memcmp(&p1->u.val, &p2->u.val, offset);
        if (ret)
            return ret;
    }

    if (shift) {
        uint8_t mask = (0xff << (8 - shift)) & 0xff;
        return (p1->u.val[offset] & mask) - (p2->u.val[offset] & mask);
    }

    return 0;
}

bool prefix_same(const struct prefix *p1, const struct prefix *p2)
{
    if (p1 == NULL && p2 == NULL)
        return true;
    if (p1 == NULL || p2 == NULL)
        return false;

    return prefix_cmp(p1, p2) == 0;
}

bool prefix_match(const struct prefix *p1, const struct prefix *p2)
{
    int offset;
    int shift;

    if (p1->family != p2->family)
        return false;

    /* p1 must be less specific or equal */
    if (p1->prefixlen > p2->prefixlen)
        return false;

    offset = p1->prefixlen / 8;
    shift = p1->prefixlen % 8;

    if (offset && memcmp(&p1->u.val, &p2->u.val, offset) != 0)
        return false;

    if (shift) {
        uint8_t mask = (0xff << (8 - shift)) & 0xff;
        if ((p1->u.val[offset] & mask) != (p2->u.val[offset] & mask))
            return false;
    }

    return true;
}

static void masklen2ip(int masklen, struct in_addr *addr)
{
    if (masklen == 0)
        addr->s_addr = 0;
    else
        addr->s_addr = htonl(~((1 << (32 - masklen)) - 1));
}

static void masklen2ip6(int masklen, struct in6_addr *addr)
{
    memset(addr, 0, sizeof(struct in6_addr));

    for (int i = 0; i < masklen / 8; i++)
        addr->s6_addr[i] = 0xff;

    if (masklen % 8)
        addr->s6_addr[masklen / 8] = (0xff << (8 - (masklen % 8))) & 0xff;
}

void apply_mask(struct prefix *p)
{
    if (p->family == AF_INET)
        apply_mask_ipv4((struct prefix_ipv4 *)p);
    else if (p->family == AF_INET6)
        apply_mask_ipv6((struct prefix_ipv6 *)p);
}

void apply_mask_ipv4(struct prefix_ipv4 *p)
{
    struct in_addr mask;

    masklen2ip(p->prefixlen, &mask);
    p->prefix.s_addr &= mask.s_addr;
}

void apply_mask_ipv6(struct prefix_ipv6 *p)
{
    struct in6_addr mask;

    masklen2ip6(p->prefixlen, &mask);
    for (int i = 0; i < 16; i++)
        p->prefix.s6_addr[i] &= mask.s6_addr[i];
}

const char *prefix2str(const struct prefix *p, char *buf, size_t size)
{
    char addr[INET6_ADDRSTRLEN];

    if (p->family == AF_INET) {
        inet_ntop(AF_INET, &p->u.prefix4, addr, sizeof(addr));
    } else if (p->family == AF_INET6) {
        inet_ntop(AF_INET6, &p->u.prefix6, addr, sizeof(addr));
    } else {
        snprintf(buf, size, "unknown");
        return buf;
    }

    snprintf(buf, size, "%s/%u", addr, p->prefixlen);
    return buf;
}

int str2prefix(const char *str, struct prefix *p)
{
    char *cp;
    char *pnt;
    int ret;
    char buf[PREFIX_STRLEN];

    if (str == NULL || p == NULL)
        return 0;

    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Find the slash */
    pnt = strchr(buf, '/');
    if (pnt == NULL) {
        /* No prefix length - try as host route */
        ret = inet_pton(AF_INET, buf, &p->u.prefix4);
        if (ret == 1) {
            p->family = AF_INET;
            p->prefixlen = IPV4_MAX_BITLEN;
            return 1;
        }

        ret = inet_pton(AF_INET6, buf, &p->u.prefix6);
        if (ret == 1) {
            p->family = AF_INET6;
            p->prefixlen = IPV6_MAX_BITLEN;
            return 1;
        }
        return 0;
    }

    *pnt++ = '\0';

    /* Parse prefix length */
    p->prefixlen = (uint8_t)strtoul(pnt, &cp, 10);
    if (*cp != '\0')
        return 0;

    /* Parse address */
    ret = inet_pton(AF_INET, buf, &p->u.prefix4);
    if (ret == 1) {
        p->family = AF_INET;
        if (p->prefixlen > IPV4_MAX_BITLEN)
            return 0;
        apply_mask(p);
        return 1;
    }

    ret = inet_pton(AF_INET6, buf, &p->u.prefix6);
    if (ret == 1) {
        p->family = AF_INET6;
        if (p->prefixlen > IPV6_MAX_BITLEN)
            return 0;
        apply_mask(p);
        return 1;
    }

    return 0;
}

int str2prefix_ipv4(const char *str, struct prefix_ipv4 *p)
{
    struct prefix tmp;

    if (str2prefix(str, &tmp) && tmp.family == AF_INET) {
        p->family = AF_INET;
        p->prefixlen = tmp.prefixlen;
        p->prefix = tmp.u.prefix4;
        return 1;
    }
    return 0;
}

int str2prefix_ipv6(const char *str, struct prefix_ipv6 *p)
{
    struct prefix tmp;

    if (str2prefix(str, &tmp) && tmp.family == AF_INET6) {
        p->family = AF_INET6;
        p->prefixlen = tmp.prefixlen;
        p->prefix = tmp.u.prefix6;
        return 1;
    }
    return 0;
}

const char *inet_ntop_prefix(const struct prefix *p, char *buf, size_t size)
{
    if (p->family == AF_INET)
        return inet_ntop(AF_INET, &p->u.prefix4, buf, size);
    else if (p->family == AF_INET6)
        return inet_ntop(AF_INET6, &p->u.prefix6, buf, size);
    return NULL;
}

uint8_t prefix_blen(const struct prefix *p)
{
    if (p->family == AF_INET)
        return IPV4_MAX_BYTELEN;
    else if (p->family == AF_INET6)
        return IPV6_MAX_BYTELEN;
    return 0;
}

bool prefix_is_host(const struct prefix *p)
{
    if (p->family == AF_INET)
        return p->prefixlen == IPV4_MAX_BITLEN;
    else if (p->family == AF_INET6)
        return p->prefixlen == IPV6_MAX_BITLEN;
    return false;
}

bool prefix_is_default(const struct prefix *p)
{
    if (p->prefixlen != 0)
        return false;

    if (p->family == AF_INET)
        return p->u.prefix4.s_addr == 0;
    else if (p->family == AF_INET6)
        return IN6_IS_ADDR_UNSPECIFIED(&p->u.prefix6);

    return false;
}

void prefix_common(const struct prefix *p1, const struct prefix *p2,
                   struct prefix *result)
{
    int i;
    uint8_t diff;
    uint8_t mask;

    if (p1->family != p2->family) {
        prefix_init(result);
        return;
    }

    result->family = p1->family;
    result->prefixlen = 0;

    int bytelen = prefix_blen(p1);
    for (i = 0; i < bytelen; i++) {
        diff = p1->u.val[i] ^ p2->u.val[i];
        if (diff == 0) {
            result->u.val[i] = p1->u.val[i];
            result->prefixlen += 8;
        } else {
            /* Find first differing bit */
            for (mask = 0x80; mask; mask >>= 1) {
                if (diff & mask)
                    break;
                result->prefixlen++;
            }
            result->u.val[i] = p1->u.val[i] & ~((mask << 1) - 1);
            break;
        }
    }
}

uint32_t prefix_hash(const struct prefix *p)
{
    uint32_t hash = 5381;
    int len = p->prefixlen / 8 + ((p->prefixlen % 8) ? 1 : 0);

    hash = ((hash << 5) + hash) + p->family;
    hash = ((hash << 5) + hash) + p->prefixlen;

    for (int i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + p->u.val[i];

    return hash;
}
