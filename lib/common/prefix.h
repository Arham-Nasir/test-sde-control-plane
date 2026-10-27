/*
 * IP Prefix Handling - FRR Style
 *
 * Data structures and functions for handling IPv4/IPv6 prefixes.
 * These are fundamental to any routing protocol implementation.
 *
 * Pattern Reference: lib/prefix.h in FRRouting
 */

#ifndef _COMMON_PREFIX_H
#define _COMMON_PREFIX_H

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Address family constants */
#define AFI_IP      1
#define AFI_IP6     2
#define AFI_MAX     3

/* Subsequent Address Family Identifier */
#define SAFI_UNICAST    1
#define SAFI_MULTICAST  2
#define SAFI_MPLS_VPN   128
#define SAFI_EVPN       70
#define SAFI_MAX        129

/* Maximum prefix lengths */
#define IPV4_MAX_BITLEN     32
#define IPV6_MAX_BITLEN     128
#define IPV4_MAX_BYTELEN    4
#define IPV6_MAX_BYTELEN    16

/* Prefix string buffer sizes */
#define PREFIX_STRLEN       64
#define INET_ADDRSTRLEN     16
#define INET6_ADDRSTRLEN    46

/*
 * Generic IP prefix structure
 * Can hold either IPv4 or IPv6 prefix
 */
struct prefix {
    uint8_t family;         /* AF_INET or AF_INET6 */
    uint8_t prefixlen;      /* Prefix length */
    union {
        struct in_addr prefix4;
        struct in6_addr prefix6;
        uint8_t val[16];
    } u;
};

/* Convenience typedefs */
typedef struct prefix prefix_t;

/* IPv4-specific prefix */
struct prefix_ipv4 {
    uint8_t family;
    uint8_t prefixlen;
    struct in_addr prefix;
};

/* IPv6-specific prefix */
struct prefix_ipv6 {
    uint8_t family;
    uint8_t prefixlen;
    struct in6_addr prefix;
};

/* Route Distinguisher for VPN prefixes */
struct rd {
    uint8_t val[8];
};

/* Prefix initialization */
void prefix_init(struct prefix *p);
void prefix_ipv4_init(struct prefix_ipv4 *p);
void prefix_ipv6_init(struct prefix_ipv6 *p);

/* Prefix copy */
void prefix_copy(struct prefix *dst, const struct prefix *src);

/* Prefix comparison */
int prefix_cmp(const struct prefix *p1, const struct prefix *p2);
bool prefix_same(const struct prefix *p1, const struct prefix *p2);

/* Check if p1 contains p2 (p2 is more specific or equal) */
bool prefix_match(const struct prefix *p1, const struct prefix *p2);

/* Apply netmask to prefix */
void apply_mask(struct prefix *p);
void apply_mask_ipv4(struct prefix_ipv4 *p);
void apply_mask_ipv6(struct prefix_ipv6 *p);

/* String conversion */
const char *prefix2str(const struct prefix *p, char *buf, size_t size);
int str2prefix(const char *str, struct prefix *p);
int str2prefix_ipv4(const char *str, struct prefix_ipv4 *p);
int str2prefix_ipv6(const char *str, struct prefix_ipv6 *p);

/* Address-only string conversion */
const char *inet_ntop_prefix(const struct prefix *p, char *buf, size_t size);

/* Utility functions */
uint8_t prefix_blen(const struct prefix *p);
bool prefix_is_host(const struct prefix *p);
bool prefix_is_default(const struct prefix *p);

/* Common prefix calculation (longest common prefix) */
void prefix_common(const struct prefix *p1, const struct prefix *p2,
                   struct prefix *result);

/* Hash function for prefix (for use in hash tables) */
uint32_t prefix_hash(const struct prefix *p);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_PREFIX_H */
