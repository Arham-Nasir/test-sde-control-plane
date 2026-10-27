/*
 * BGP Path Attributes
 *
 * Structures and functions for BGP path attributes used in route
 * selection and propagation.
 *
 * Pattern Reference: bgpd/bgp_attr.h in FRRouting
 */

#ifndef _BGP_ATTR_H
#define _BGP_ATTR_H

#include "common/prefix.h"
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct stream;

/* AS Path segment */
struct as_segment {
    uint8_t type;
    uint8_t count;
    uint32_t *asns;
    struct as_segment *next;
};

/* AS Path */
struct aspath {
    struct as_segment *segments;
    char *str;              /* String representation */
    uint32_t length;        /* Total ASN count */
    uint32_t refcnt;
};

/* Community */
struct community {
    uint32_t *val;
    uint32_t size;
    uint32_t refcnt;
    char *str;
};

/* Extended Community */
struct ecommunity {
    uint8_t *val;
    uint32_t size;
    uint32_t refcnt;
    char *str;
};

/* Large Community */
struct lcommunity {
    uint8_t *val;
    uint32_t size;
    uint32_t refcnt;
    char *str;
};

/* Cluster List (Route Reflector) */
struct cluster_list {
    struct in_addr *clusters;
    uint8_t count;
    uint32_t refcnt;
};

/* BGP Attributes */
struct bgp_attr {
    /* Well-known mandatory */
    uint8_t origin;                 /* ORIGIN */
    struct aspath *aspath;          /* AS_PATH */
    struct in_addr nexthop;         /* NEXT_HOP */

    /* Well-known discretionary */
    uint32_t local_pref;            /* LOCAL_PREF */
    bool atomic_aggregate;          /* ATOMIC_AGGREGATE */

    /* Optional transitive */
    uint32_t med;                   /* MULTI_EXIT_DISC */
    struct community *community;    /* COMMUNITIES */
    uint32_t aggregator_as;         /* AGGREGATOR AS */
    struct in_addr aggregator_addr; /* AGGREGATOR address */

    /* Optional non-transitive */
    struct in_addr originator_id;       /* ORIGINATOR_ID (RR) */
    struct cluster_list *cluster_list;  /* CLUSTER_LIST (RR) */

    /* Extended attributes */
    struct ecommunity *ecommunity;
    struct lcommunity *lcommunity;

    /* Multiprotocol extensions */
    struct in6_addr mp_nexthop;
    uint8_t mp_nexthop_len;

    /* Flags */
    uint32_t flags;
#define BGP_ATTR_FLAG_HAS_NEXTHOP       (1 << 0)
#define BGP_ATTR_FLAG_HAS_LOCAL_PREF    (1 << 1)
#define BGP_ATTR_FLAG_HAS_MED           (1 << 2)
#define BGP_ATTR_FLAG_HAS_ORIGINATOR    (1 << 3)
#define BGP_ATTR_FLAG_HAS_CLUSTER       (1 << 4)

    /* Reference count */
    uint32_t refcnt;
};

/* Attribute creation/destruction */
struct bgp_attr *bgp_attr_new(void);
void bgp_attr_free(struct bgp_attr *attr);
struct bgp_attr *bgp_attr_dup(struct bgp_attr *attr);
struct bgp_attr *bgp_attr_intern(struct bgp_attr *attr);
void bgp_attr_unintern(struct bgp_attr **attr);

/* Attribute comparison */
int bgp_attr_cmp(struct bgp_attr *a, struct bgp_attr *b);
bool bgp_attr_same(struct bgp_attr *a, struct bgp_attr *b);

/* AS Path functions */
struct aspath *aspath_new(void);
void aspath_free(struct aspath *aspath);
struct aspath *aspath_dup(struct aspath *aspath);
struct aspath *aspath_intern(struct aspath *aspath);
void aspath_unintern(struct aspath **aspath);

struct aspath *aspath_parse(uint8_t *data, size_t length, bool as4);
const char *aspath_str(struct aspath *aspath);
uint32_t aspath_count(struct aspath *aspath);
uint32_t aspath_first_as(struct aspath *aspath);
uint32_t aspath_origin_as(struct aspath *aspath);
bool aspath_loop_check(struct aspath *aspath, uint32_t asn);

struct aspath *aspath_prepend(struct aspath *aspath, uint32_t asn);
struct aspath *aspath_add_seq(struct aspath *aspath, uint32_t asn);

/* Community functions */
struct community *community_new(void);
void community_free(struct community *com);
struct community *community_dup(struct community *com);
struct community *community_intern(struct community *com);
void community_unintern(struct community **com);

struct community *community_parse(uint8_t *data, size_t length);
const char *community_str(struct community *com);
bool community_include(struct community *com, uint32_t val);
struct community *community_add_val(struct community *com, uint32_t val);
struct community *community_del_val(struct community *com, uint32_t val);

/* Well-known communities */
#define COMMUNITY_NO_EXPORT             0xFFFFFF01
#define COMMUNITY_NO_ADVERTISE          0xFFFFFF02
#define COMMUNITY_NO_EXPORT_SUBCONFED   0xFFFFFF03
#define COMMUNITY_GRACEFUL_SHUTDOWN     0xFFFF0000

/* Cluster List functions (Route Reflector) */
struct cluster_list *cluster_list_new(void);
void cluster_list_free(struct cluster_list *cl);
struct cluster_list *cluster_list_dup(struct cluster_list *cl);
struct cluster_list *cluster_list_intern(struct cluster_list *cl);
void cluster_list_unintern(struct cluster_list **cl);

struct cluster_list *cluster_list_parse(uint8_t *data, size_t length);
bool cluster_loop_check(struct cluster_list *cl, struct in_addr *cluster_id);
struct cluster_list *cluster_list_prepend(struct cluster_list *cl,
                                          struct in_addr *cluster_id);

/* Attribute encoding/decoding */
int bgp_attr_encode(struct stream *s, struct bgp_attr *attr,
                    uint8_t afi, uint8_t safi);
int bgp_attr_decode(struct stream *s, struct bgp_attr *attr, size_t length);

/* Attribute modification for propagation */
void bgp_attr_filter_ebgp(struct bgp_attr *attr);
void bgp_attr_filter_ibgp(struct bgp_attr *attr);

#ifdef __cplusplus
}
#endif

#endif /* _BGP_ATTR_H */
