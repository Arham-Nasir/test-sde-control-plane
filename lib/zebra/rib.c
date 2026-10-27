/*
 * Routing Information Base Implementation
 */

#include "rib.h"
#include "common/log.h"

#include <string.h>
#include <stdlib.h>

#undef LOG_TAG
#define LOG_TAG "RIB"

/* Route type names */
static const char *route_type_names[] = {
    [ZEBRA_ROUTE_SYSTEM]        = "system",
    [ZEBRA_ROUTE_KERNEL]        = "kernel",
    [ZEBRA_ROUTE_CONNECT]       = "connected",
    [ZEBRA_ROUTE_STATIC]        = "static",
    [ZEBRA_ROUTE_RIP]           = "rip",
    [ZEBRA_ROUTE_RIPNG]         = "ripng",
    [ZEBRA_ROUTE_OSPF]          = "ospf",
    [ZEBRA_ROUTE_OSPF6]         = "ospf6",
    [ZEBRA_ROUTE_ISIS]          = "isis",
    [ZEBRA_ROUTE_BGP]           = "bgp",
    [ZEBRA_ROUTE_PIM]           = "pim",
    [ZEBRA_ROUTE_EIGRP]         = "eigrp",
    [ZEBRA_ROUTE_NHRP]          = "nhrp",
};

/* Default administrative distances */
static const uint8_t default_distances[] = {
    [ZEBRA_ROUTE_SYSTEM]        = 0,
    [ZEBRA_ROUTE_KERNEL]        = 0,
    [ZEBRA_ROUTE_CONNECT]       = 0,
    [ZEBRA_ROUTE_STATIC]        = 1,
    [ZEBRA_ROUTE_EIGRP]         = 90,
    [ZEBRA_ROUTE_OSPF]          = 110,
    [ZEBRA_ROUTE_OSPF6]         = 110,
    [ZEBRA_ROUTE_ISIS]          = 115,
    [ZEBRA_ROUTE_RIP]           = 120,
    [ZEBRA_ROUTE_RIPNG]         = 120,
    [ZEBRA_ROUTE_BGP]           = 20,   /* eBGP */
};

const char *route_type_str(route_type_t type)
{
    if (type < ZEBRA_ROUTE_MAX && route_type_names[type])
        return route_type_names[type];
    return "unknown";
}

route_type_t route_type_from_str(const char *str)
{
    for (int i = 0; i < ZEBRA_ROUTE_MAX; i++) {
        if (route_type_names[i] && strcmp(route_type_names[i], str) == 0)
            return i;
    }
    return ZEBRA_ROUTE_MAX;
}

uint8_t route_distance_default(route_type_t type)
{
    if (type < ZEBRA_ROUTE_MAX)
        return default_distances[type];
    return 255;
}

/* Route table implementation using radix tree */
struct route_table *route_table_init(void)
{
    return route_table_init_with_delegate(NULL);
}

struct route_table *route_table_init_with_delegate(
    void (*cleanup)(struct route_table *, struct route_node *))
{
    struct route_table *table;

    table = XCALLOC(MTYPE_ROUTE_TABLE, sizeof(struct route_table));
    table->cleanup = cleanup;

    return table;
}

static void route_node_free(struct route_table *table, struct route_node *node)
{
    struct route_entry *re, *next;

    if (table->cleanup)
        table->cleanup(table, node);

    /* Free all route entries */
    for (re = node->routes; re; re = next) {
        next = re->next;
        /* Free nexthops */
        struct nexthop *nh, *nhnext;
        for (nh = re->nhe.nexthop; nh; nh = nhnext) {
            nhnext = nh->next;
            nexthop_free(nh);
        }
        XFREE(MTYPE_ROUTE_NODE, re);
    }

    XFREE(MTYPE_ROUTE_NODE, node);
}

static void route_table_free_nodes(struct route_table *table,
                                   struct route_node *node)
{
    if (node == NULL)
        return;

    route_table_free_nodes(table, node->link[0]);
    route_table_free_nodes(table, node->link[1]);
    route_node_free(table, node);
}

void route_table_finish(struct route_table *table)
{
    if (table == NULL)
        return;

    route_table_free_nodes(table, table->top);
    XFREE(MTYPE_ROUTE_TABLE, table);
}

/* Find common prefix length between two prefixes */
static uint8_t prefix_common_bits(const struct prefix *p1, const struct prefix *p2)
{
    int length = (p1->prefixlen < p2->prefixlen) ? p1->prefixlen : p2->prefixlen;
    int i;

    if (p1->family != p2->family)
        return 0;

    for (i = 0; i < length; i++) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);

        if (((p1->u.val[byte_idx] >> bit_idx) & 1) !=
            ((p2->u.val[byte_idx] >> bit_idx) & 1))
            break;
    }

    return i;
}

/* Get or create route node */
struct route_node *route_node_get(struct route_table *table, struct prefix *p)
{
    struct route_node *node, *new;
    struct route_node *match = NULL;
    uint8_t prefixlen = p->prefixlen;
    int check_bit;

    node = table->top;

    while (node && node->p.prefixlen <= prefixlen &&
           prefix_match(&node->p, p)) {
        if (node->p.prefixlen == prefixlen) {
            route_lock_node(node);
            return node;
        }
        match = node;
        check_bit = (p->u.val[node->p.prefixlen / 8] >>
                    (7 - (node->p.prefixlen % 8))) & 1;
        node = node->link[check_bit];
    }

    /* Create new node */
    new = XCALLOC(MTYPE_ROUTE_NODE, sizeof(struct route_node));
    prefix_copy(&new->p, p);
    new->table = table;
    new->lock = 1;

    /* If we have a node that doesn't match (sibling case), we need to create
     * an intermediate node at the point where the prefixes diverge */
    if (node) {
        /* Find where the prefixes diverge */
        uint8_t common_bits = prefix_common_bits(&node->p, p);

        /* Create intermediate node at the common prefix point */
        struct route_node *intermediate = XCALLOC(MTYPE_ROUTE_NODE, sizeof(struct route_node));
        intermediate->p.family = p->family;
        intermediate->p.prefixlen = common_bits;
        /* Copy the common prefix bits */
        memcpy(intermediate->p.u.val, p->u.val, (common_bits + 7) / 8);
        /* Apply mask to ensure only common bits are set */
        if (common_bits % 8) {
            uint8_t mask = 0xff << (8 - (common_bits % 8));
            intermediate->p.u.val[common_bits / 8] &= mask;
        }
        intermediate->table = table;
        intermediate->lock = 0;  /* Intermediate nodes don't need to be locked */

        /* Link intermediate to parent (or make it top) */
        intermediate->parent = node->parent;
        if (node->parent) {
            if (node->parent->link[0] == node)
                node->parent->link[0] = intermediate;
            else
                node->parent->link[1] = intermediate;
        } else {
            table->top = intermediate;
        }

        /* Link both the existing node and new node under intermediate */
        check_bit = (node->p.u.val[common_bits / 8] >>
                    (7 - (common_bits % 8))) & 1;
        intermediate->link[check_bit] = node;
        node->parent = intermediate;

        check_bit = (p->u.val[common_bits / 8] >>
                    (7 - (common_bits % 8))) & 1;
        intermediate->link[check_bit] = new;
        new->parent = intermediate;

        table->count++;
        return new;
    }

    /* Link into tree */
    if (match) {
        check_bit = (p->u.val[match->p.prefixlen / 8] >>
                    (7 - (match->p.prefixlen % 8))) & 1;
        match->link[check_bit] = new;
        new->parent = match;
    } else {
        table->top = new;
    }

    /* Handle existing node at this position */
    if (node) {
        check_bit = (node->p.u.val[prefixlen / 8] >>
                    (7 - (prefixlen % 8))) & 1;
        new->link[check_bit] = node;
        node->parent = new;
    }

    table->count++;
    return new;
}

struct route_node *route_node_lookup(struct route_table *table, struct prefix *p)
{
    struct route_node *node;

    node = table->top;

    while (node && node->p.prefixlen <= p->prefixlen &&
           prefix_match(&node->p, p)) {
        if (node->p.prefixlen == p->prefixlen) {
            route_lock_node(node);
            return node;
        }
        int check_bit = (p->u.val[node->p.prefixlen / 8] >>
                        (7 - (node->p.prefixlen % 8))) & 1;
        node = node->link[check_bit];
    }

    return NULL;
}

struct route_node *route_node_match(struct route_table *table, struct prefix *p)
{
    struct route_node *node, *matched = NULL;

    node = table->top;

    while (node && prefix_match(&node->p, p)) {
        if (node->routes)
            matched = node;

        if (node->p.prefixlen >= p->prefixlen)
            break;

        int check_bit = (p->u.val[node->p.prefixlen / 8] >>
                        (7 - (node->p.prefixlen % 8))) & 1;
        node = node->link[check_bit];
    }

    if (matched)
        route_lock_node(matched);

    return matched;
}

struct route_node *route_node_match_ipv4(struct route_table *table,
                                         const struct in_addr *addr)
{
    struct prefix p;

    p.family = AF_INET;
    p.prefixlen = IPV4_MAX_BITLEN;
    p.u.prefix4 = *addr;

    return route_node_match(table, &p);
}

struct route_node *route_node_match_ipv6(struct route_table *table,
                                         const struct in6_addr *addr)
{
    struct prefix p;

    p.family = AF_INET6;
    p.prefixlen = IPV6_MAX_BITLEN;
    p.u.prefix6 = *addr;

    return route_node_match(table, &p);
}

void route_lock_node(struct route_node *node)
{
    node->lock++;
}

void route_unlock_node(struct route_node *node)
{
    if (node->lock > 0)
        node->lock--;

    /* TODO: Garbage collect when lock reaches 0 and no routes */
}

struct route_node *route_top(struct route_table *table)
{
    if (table->top) {
        route_lock_node(table->top);
        return table->top;
    }
    return NULL;
}

struct route_node *route_next(struct route_node *node)
{
    struct route_node *next;
    struct route_node *start = node;

    /* Try left child */
    if (node->link[0]) {
        next = node->link[0];
        route_lock_node(next);
        route_unlock_node(start);
        return next;
    }

    /* Try right child */
    if (node->link[1]) {
        next = node->link[1];
        route_lock_node(next);
        route_unlock_node(start);
        return next;
    }

    /* Go up until we can go right */
    while (node->parent) {
        if (node->parent->link[0] == node && node->parent->link[1]) {
            next = node->parent->link[1];
            route_lock_node(next);
            route_unlock_node(start);
            return next;
        }
        node = node->parent;
    }

    route_unlock_node(start);
    return NULL;
}

struct route_node *route_next_until(struct route_node *node,
                                    struct route_node *limit)
{
    struct route_node *next = route_next(node);

    if (next == limit) {
        route_unlock_node(next);
        return NULL;
    }
    return next;
}

/* Route entry operations */
struct route_entry *rib_add(struct route_table *table, struct prefix *p,
                            route_type_t type, uint8_t distance,
                            uint32_t metric)
{
    struct route_node *rn;
    struct route_entry *re;

    rn = route_node_get(table, p);

    /* Check if route already exists */
    for (re = rn->routes; re; re = re->next) {
        if (re->type == type) {
            /* Update existing route */
            re->distance = distance;
            re->metric = metric;
            re->uptime = time(NULL);
            rib_process(table, rn);
            route_unlock_node(rn);
            return re;
        }
    }

    /* Create new route entry */
    re = XCALLOC(MTYPE_ROUTE_NODE, sizeof(struct route_entry));
    re->type = type;
    re->distance = distance;
    re->metric = metric;
    re->uptime = time(NULL);

    /* Add to list */
    re->next = rn->routes;
    if (rn->routes)
        rn->routes->prev = re;
    rn->routes = re;

    rib_process(table, rn);
    route_unlock_node(rn);

    return re;
}

void rib_delete(struct route_table *table, struct prefix *p, route_type_t type)
{
    struct route_node *rn;
    struct route_entry *re;

    rn = route_node_lookup(table, p);
    if (rn == NULL)
        return;

    for (re = rn->routes; re; re = re->next) {
        if (re->type == type) {
            /* Remove from list */
            if (re->prev)
                re->prev->next = re->next;
            else
                rn->routes = re->next;

            if (re->next)
                re->next->prev = re->prev;

            /* Free nexthops */
            struct nexthop *nh, *next;
            for (nh = re->nhe.nexthop; nh; nh = next) {
                next = nh->next;
                nexthop_free(nh);
            }

            XFREE(MTYPE_ROUTE_NODE, re);
            rib_process(table, rn);
            break;
        }
    }

    route_unlock_node(rn);
}

struct route_entry *rib_lookup(struct route_table *table, struct prefix *p,
                               route_type_t type)
{
    struct route_node *rn;
    struct route_entry *re;

    rn = route_node_lookup(table, p);
    if (rn == NULL)
        return NULL;

    for (re = rn->routes; re; re = re->next) {
        if (re->type == type) {
            route_unlock_node(rn);
            return re;
        }
    }

    route_unlock_node(rn);
    return NULL;
}

/* Best route selection */
void rib_process(struct route_table *table, struct route_node *rn)
{
    struct route_entry *re, *selected = NULL;
    (void)table;

    /* Clear selection flags */
    for (re = rn->routes; re; re = re->next)
        re->selected = false;

    /* Select best route */
    for (re = rn->routes; re; re = re->next) {
        if (re->nhe.nexthop == NULL)
            continue;  /* No nexthops */

        if (selected == NULL) {
            selected = re;
            continue;
        }

        /* Lower distance wins */
        if (re->distance < selected->distance) {
            selected = re;
            continue;
        }

        if (re->distance > selected->distance)
            continue;

        /* Same distance: lower metric wins */
        if (re->metric < selected->metric)
            selected = re;
    }

    if (selected) {
        selected->selected = true;
        selected->flags |= ZEBRA_FLAG_SELECTED;

        char prefix_str[PREFIX_STRLEN];
        prefix2str(&rn->p, prefix_str, sizeof(prefix_str));
        log_debug("RIB: selected %s route for %s (distance=%d metric=%d)",
                  route_type_str(selected->type), prefix_str,
                  selected->distance, selected->metric);
    }
}

/* Nexthop operations */
struct nexthop *nexthop_new(void)
{
    return XCALLOC(MTYPE_BGP_PATH, sizeof(struct nexthop));
}

void nexthop_free(struct nexthop *nh)
{
    if (nh == NULL)
        return;

    if (nh->resolved)
        nexthop_free(nh->resolved);

    XFREE(MTYPE_BGP_PATH, nh);
}

struct nexthop *nexthop_dup(struct nexthop *nh)
{
    struct nexthop *new;

    if (nh == NULL)
        return NULL;

    new = nexthop_new();
    memcpy(new, nh, sizeof(struct nexthop));
    new->next = new->prev = NULL;

    if (nh->resolved)
        new->resolved = nexthop_dup(nh->resolved);

    return new;
}

void nexthop_add(struct nexthop_group *nhg, struct nexthop *nh)
{
    nh->next = nhg->nexthop;
    if (nhg->nexthop)
        nhg->nexthop->prev = nh;
    nhg->nexthop = nh;
    nhg->nexthop_count++;
}

void nexthop_del(struct nexthop_group *nhg, struct nexthop *nh)
{
    if (nh->prev)
        nh->prev->next = nh->next;
    else
        nhg->nexthop = nh->next;

    if (nh->next)
        nh->next->prev = nh->prev;

    nhg->nexthop_count--;
}

struct nexthop *nexthop_from_ipv4(const struct in_addr *addr, uint32_t ifindex)
{
    struct nexthop *nh = nexthop_new();

    nh->type = ifindex ? NEXTHOP_TYPE_IPV4_IFINDEX : NEXTHOP_TYPE_IPV4;
    nh->gate.ipv4 = *addr;
    nh->ifindex = ifindex;
    nh->flags = NEXTHOP_FLAG_ACTIVE;

    return nh;
}

struct nexthop *nexthop_from_ipv6(const struct in6_addr *addr, uint32_t ifindex)
{
    struct nexthop *nh = nexthop_new();

    nh->type = ifindex ? NEXTHOP_TYPE_IPV6_IFINDEX : NEXTHOP_TYPE_IPV6;
    nh->gate.ipv6 = *addr;
    nh->ifindex = ifindex;
    nh->flags = NEXTHOP_FLAG_ACTIVE;

    return nh;
}

struct nexthop *nexthop_from_ifindex(uint32_t ifindex)
{
    struct nexthop *nh = nexthop_new();

    nh->type = NEXTHOP_TYPE_IFINDEX;
    nh->ifindex = ifindex;
    nh->flags = NEXTHOP_FLAG_ACTIVE;

    return nh;
}

struct nexthop *nexthop_from_blackhole(void)
{
    struct nexthop *nh = nexthop_new();

    nh->type = NEXTHOP_TYPE_BLACKHOLE;
    nh->flags = NEXTHOP_FLAG_ACTIVE;

    return nh;
}
