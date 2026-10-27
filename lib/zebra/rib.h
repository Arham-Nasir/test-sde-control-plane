/*
 * Routing Information Base (RIB) - FRR/SONiC Style
 *
 * The RIB is the central routing table that stores routes from all
 * routing protocols and selects the best routes for forwarding.
 *
 * In SONiC architecture, the RIB is managed by the fpmsyncd daemon
 * and synced to the kernel and hardware.
 *
 * Pattern Reference: zebra/rib.h in FRRouting
 */

#ifndef _ZEBRA_RIB_H
#define _ZEBRA_RIB_H

#include "common/prefix.h"
#include "common/linklist.h"
#include "common/memory.h"

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Route types (protocol sources) */
typedef enum {
    ZEBRA_ROUTE_SYSTEM = 0,
    ZEBRA_ROUTE_KERNEL,
    ZEBRA_ROUTE_CONNECT,
    ZEBRA_ROUTE_STATIC,
    ZEBRA_ROUTE_RIP,
    ZEBRA_ROUTE_RIPNG,
    ZEBRA_ROUTE_OSPF,
    ZEBRA_ROUTE_OSPF6,
    ZEBRA_ROUTE_ISIS,
    ZEBRA_ROUTE_BGP,
    ZEBRA_ROUTE_PIM,
    ZEBRA_ROUTE_EIGRP,
    ZEBRA_ROUTE_NHRP,
    ZEBRA_ROUTE_HSLS,
    ZEBRA_ROUTE_OLSR,
    ZEBRA_ROUTE_TABLE,
    ZEBRA_ROUTE_LDP,
    ZEBRA_ROUTE_VNC,
    ZEBRA_ROUTE_VNC_DIRECT,
    ZEBRA_ROUTE_VNC_DIRECT_RH,
    ZEBRA_ROUTE_BGP_DIRECT,
    ZEBRA_ROUTE_BGP_DIRECT_EXT,
    ZEBRA_ROUTE_BABEL,
    ZEBRA_ROUTE_SHARP,
    ZEBRA_ROUTE_PBR,
    ZEBRA_ROUTE_BFD,
    ZEBRA_ROUTE_OPENFABRIC,
    ZEBRA_ROUTE_VRRP,
    ZEBRA_ROUTE_NHG,
    ZEBRA_ROUTE_SRTE,
    ZEBRA_ROUTE_ALL,
    ZEBRA_ROUTE_MAX
} route_type_t;

/* Route flags */
#define ZEBRA_FLAG_SELECTED         (1 << 0)    /* Currently best route */
#define ZEBRA_FLAG_FIB_OVERRIDE     (1 << 1)    /* Override kernel route */
#define ZEBRA_FLAG_EVPN_ROUTE       (1 << 2)    /* EVPN route */
#define ZEBRA_FLAG_RR_USE_DISTANCE  (1 << 3)    /* Use distance for RR */
#define ZEBRA_FLAG_REJECT           (1 << 4)    /* Reject route */
#define ZEBRA_FLAG_BLACKHOLE        (1 << 5)    /* Blackhole route */
#define ZEBRA_FLAG_IBGP             (1 << 6)    /* IBGP route */
#define ZEBRA_FLAG_ALLOW_RECURSION  (1 << 7)    /* Allow recursive resolution */

/* Nexthop types */
typedef enum {
    NEXTHOP_TYPE_IFINDEX = 1,       /* Interface only */
    NEXTHOP_TYPE_IPV4,              /* IPv4 address */
    NEXTHOP_TYPE_IPV4_IFINDEX,      /* IPv4 + interface */
    NEXTHOP_TYPE_IPV6,              /* IPv6 address */
    NEXTHOP_TYPE_IPV6_IFINDEX,      /* IPv6 + interface */
    NEXTHOP_TYPE_BLACKHOLE,         /* Blackhole/null route */
} nexthop_type_t;

/* Nexthop flags */
#define NEXTHOP_FLAG_ACTIVE     (1 << 0)    /* Nexthop is active */
#define NEXTHOP_FLAG_FIB        (1 << 1)    /* Installed in FIB */
#define NEXTHOP_FLAG_RECURSIVE  (1 << 2)    /* Recursive nexthop */
#define NEXTHOP_FLAG_ONLINK     (1 << 3)    /* Onlink nexthop */
#define NEXTHOP_FLAG_DUPLICATE  (1 << 4)    /* Duplicate of another NH */

/* Nexthop structure */
struct nexthop {
    nexthop_type_t type;
    uint8_t flags;

    /* Nexthop address */
    union {
        struct in_addr ipv4;
        struct in6_addr ipv6;
    } gate;

    /* Interface index */
    uint32_t ifindex;

    /* For recursive nexthops */
    struct nexthop *resolved;

    /* ECMP weight */
    uint8_t weight;

    /* Nexthop group linkage */
    struct nexthop *next;
    struct nexthop *prev;
};

/* Nexthop group */
struct nexthop_group {
    struct nexthop *nexthop;
    uint32_t id;
    uint8_t nexthop_count;
};

/* Route entry */
struct route_entry {
    /* Route type */
    route_type_t type;
    uint16_t instance;

    /* Distance and metric */
    uint8_t distance;
    uint32_t metric;

    /* MTU */
    uint32_t mtu;

    /* Flags */
    uint32_t flags;

    /* Tag */
    uint32_t tag;

    /* Timestamps */
    time_t uptime;

    /* Nexthops */
    struct nexthop_group nhe;

    /* Selected/FIB status */
    bool selected;
    bool installed;

    /* Linkage in route node */
    struct route_entry *next;
    struct route_entry *prev;
};

/* Route node (prefix + routes) */
struct route_node {
    struct prefix p;

    /* Route entries for this prefix */
    struct route_entry *routes;

    /* Info pointer (for protocol use) */
    void *info;

    /* Reference count */
    uint32_t lock;

    /* Table linkage */
    struct route_node *parent;
    struct route_node *link[2];     /* 0 = left, 1 = right */
    struct route_table *table;
};

/* Route table */
struct route_table {
    struct route_node *top;
    uint32_t count;

    /* Table identification */
    uint32_t tableid;
    uint8_t afi;
    uint8_t safi;

    /* Info pointer */
    void *info;

    /* Cleanup callback */
    void (*cleanup)(struct route_table *table, struct route_node *node);
};

/* Route table operations */
struct route_table *route_table_init(void);
struct route_table *route_table_init_with_delegate(
    void (*cleanup)(struct route_table *, struct route_node *));
void route_table_finish(struct route_table *table);

/* Route node operations */
struct route_node *route_node_get(struct route_table *table, struct prefix *p);
struct route_node *route_node_lookup(struct route_table *table, struct prefix *p);
struct route_node *route_node_match(struct route_table *table, struct prefix *p);
struct route_node *route_node_match_ipv4(struct route_table *table,
                                         const struct in_addr *addr);
struct route_node *route_node_match_ipv6(struct route_table *table,
                                         const struct in6_addr *addr);

void route_lock_node(struct route_node *node);
void route_unlock_node(struct route_node *node);

/* Iteration */
struct route_node *route_top(struct route_table *table);
struct route_node *route_next(struct route_node *node);
struct route_node *route_next_until(struct route_node *node,
                                    struct route_node *limit);

/* Route entry operations */
struct route_entry *rib_add(struct route_table *table, struct prefix *p,
                            route_type_t type, uint8_t distance,
                            uint32_t metric);
void rib_delete(struct route_table *table, struct prefix *p,
                route_type_t type);
struct route_entry *rib_lookup(struct route_table *table, struct prefix *p,
                               route_type_t type);

/* Best route selection */
void rib_process(struct route_table *table, struct route_node *rn);

/* Nexthop operations */
struct nexthop *nexthop_new(void);
void nexthop_free(struct nexthop *nh);
struct nexthop *nexthop_dup(struct nexthop *nh);
void nexthop_add(struct nexthop_group *nhg, struct nexthop *nh);
void nexthop_del(struct nexthop_group *nhg, struct nexthop *nh);

/* Nexthop creation helpers */
struct nexthop *nexthop_from_ipv4(const struct in_addr *addr, uint32_t ifindex);
struct nexthop *nexthop_from_ipv6(const struct in6_addr *addr, uint32_t ifindex);
struct nexthop *nexthop_from_ifindex(uint32_t ifindex);
struct nexthop *nexthop_from_blackhole(void);

/* Route type names */
const char *route_type_str(route_type_t type);
route_type_t route_type_from_str(const char *str);

/* Default distances by protocol */
uint8_t route_distance_default(route_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_RIB_H */
