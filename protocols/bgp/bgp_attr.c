/*
 * BGP Path Attributes Implementation
 */

#include "bgp_attr.h"
#include "bgp_packet.h"
#include "common/memory.h"
#include "common/stream.h"
#include "common/hash.h"
#include "common/log.h"

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

#undef LOG_TAG
#define LOG_TAG "BGP"

/* Attribute hash tables for interning */
__attribute__((unused)) static struct hash *attrhash = NULL;
__attribute__((unused)) static struct hash *aspathash = NULL;
__attribute__((unused)) static struct hash *commhash = NULL;
__attribute__((unused)) static struct hash *clusterhash = NULL;

struct bgp_attr *bgp_attr_new(void)
{
    return XCALLOC(MTYPE_BGP_ATTR, sizeof(struct bgp_attr));
}

void bgp_attr_free(struct bgp_attr *attr)
{
    if (attr == NULL)
        return;

    if (attr->aspath)
        aspath_unintern(&attr->aspath);
    if (attr->community)
        community_unintern(&attr->community);
    if (attr->cluster_list)
        cluster_list_unintern(&attr->cluster_list);

    XFREE(MTYPE_BGP_ATTR, attr);
}

struct bgp_attr *bgp_attr_dup(struct bgp_attr *attr)
{
    struct bgp_attr *new;

    if (attr == NULL)
        return NULL;

    new = bgp_attr_new();
    memcpy(new, attr, sizeof(struct bgp_attr));
    new->refcnt = 0;

    if (attr->aspath)
        new->aspath = aspath_dup(attr->aspath);
    if (attr->community)
        new->community = community_dup(attr->community);
    if (attr->cluster_list)
        new->cluster_list = cluster_list_dup(attr->cluster_list);

    return new;
}

int bgp_attr_cmp(struct bgp_attr *a, struct bgp_attr *b)
{
    if (a->origin != b->origin)
        return a->origin - b->origin;
    if (a->local_pref != b->local_pref)
        return (int)b->local_pref - (int)a->local_pref;  /* Higher is better */
    if (a->med != b->med)
        return (int)a->med - (int)b->med;  /* Lower is better */

    return 0;
}

bool bgp_attr_same(struct bgp_attr *a, struct bgp_attr *b)
{
    return bgp_attr_cmp(a, b) == 0;
}

struct bgp_attr *bgp_attr_intern(struct bgp_attr *attr)
{
    if (attr == NULL)
        return NULL;

    attr->refcnt++;
    return attr;
}

void bgp_attr_unintern(struct bgp_attr **attr)
{
    if (*attr == NULL)
        return;

    if (--(*attr)->refcnt == 0)
        bgp_attr_free(*attr);

    *attr = NULL;
}

/* AS Path implementation */
struct aspath *aspath_new(void)
{
    return XCALLOC(MTYPE_BGP_PATH, sizeof(struct aspath));
}

void aspath_free(struct aspath *aspath)
{
    struct as_segment *seg, *next;

    if (aspath == NULL)
        return;

    for (seg = aspath->segments; seg; seg = next) {
        next = seg->next;
        XFREE(MTYPE_BGP_PATH, seg->asns);
        XFREE(MTYPE_BGP_PATH, seg);
    }

    XFREE(MTYPE_BGP_PATH, aspath->str);
    XFREE(MTYPE_BGP_PATH, aspath);
}

struct aspath *aspath_dup(struct aspath *aspath)
{
    struct aspath *new;
    struct as_segment *seg, **new_seg_ptr;

    if (aspath == NULL)
        return NULL;

    new = aspath_new();
    new_seg_ptr = &new->segments;

    for (seg = aspath->segments; seg; seg = seg->next) {
        struct as_segment *new_seg = XCALLOC(MTYPE_BGP_PATH,
                                             sizeof(struct as_segment));
        new_seg->type = seg->type;
        new_seg->count = seg->count;
        new_seg->asns = XMALLOC(MTYPE_BGP_PATH, seg->count * sizeof(uint32_t));
        memcpy(new_seg->asns, seg->asns, seg->count * sizeof(uint32_t));

        *new_seg_ptr = new_seg;
        new_seg_ptr = &new_seg->next;
    }

    new->length = aspath->length;
    return new;
}

struct aspath *aspath_intern(struct aspath *aspath)
{
    aspath->refcnt++;
    return aspath;
}

void aspath_unintern(struct aspath **aspath)
{
    if (*aspath == NULL)
        return;

    if (--(*aspath)->refcnt == 0)
        aspath_free(*aspath);

    *aspath = NULL;
}

struct aspath *aspath_parse(uint8_t *data, size_t length, bool as4)
{
    struct aspath *aspath;
    struct as_segment **seg_ptr;
    size_t pos = 0;
    int asn_size = as4 ? 4 : 2;

    aspath = aspath_new();
    seg_ptr = &aspath->segments;

    while (pos < length) {
        uint8_t type = data[pos++];
        uint8_t count = data[pos++];

        struct as_segment *seg = XCALLOC(MTYPE_BGP_PATH, sizeof(struct as_segment));
        seg->type = type;
        seg->count = count;
        seg->asns = XMALLOC(MTYPE_BGP_PATH, count * sizeof(uint32_t));

        for (int i = 0; i < count; i++) {
            if (as4) {
                seg->asns[i] = (data[pos] << 24) | (data[pos + 1] << 16) |
                               (data[pos + 2] << 8) | data[pos + 3];
            } else {
                seg->asns[i] = (data[pos] << 8) | data[pos + 1];
            }
            pos += asn_size;
            aspath->length++;
        }

        *seg_ptr = seg;
        seg_ptr = &seg->next;
    }

    return aspath;
}

const char *aspath_str(struct aspath *aspath)
{
    struct as_segment *seg;
    char *str;
    size_t len = 0;

    if (aspath == NULL)
        return "";

    if (aspath->str)
        return aspath->str;

    /* Calculate string length */
    for (seg = aspath->segments; seg; seg = seg->next)
        len += seg->count * 12 + 4;  /* "NNNNNNNNNN " per ASN */

    str = XMALLOC(MTYPE_BGP_PATH, len + 1);
    str[0] = '\0';

    for (seg = aspath->segments; seg; seg = seg->next) {
        if (seg->type == BGP_AS_SET)
            strcat(str, "{");

        for (int i = 0; i < seg->count; i++) {
            char asn_str[16];
            snprintf(asn_str, sizeof(asn_str), "%u", seg->asns[i]);
            strcat(str, asn_str);
            if (i < seg->count - 1)
                strcat(str, " ");
        }

        if (seg->type == BGP_AS_SET)
            strcat(str, "}");

        if (seg->next)
            strcat(str, " ");
    }

    aspath->str = str;
    return str;
}

uint32_t aspath_count(struct aspath *aspath)
{
    return aspath ? aspath->length : 0;
}

uint32_t aspath_first_as(struct aspath *aspath)
{
    if (aspath == NULL || aspath->segments == NULL)
        return 0;

    if (aspath->segments->count == 0)
        return 0;

    return aspath->segments->asns[0];
}

uint32_t aspath_origin_as(struct aspath *aspath)
{
    struct as_segment *seg;

    if (aspath == NULL)
        return 0;

    /* Find last segment */
    for (seg = aspath->segments; seg && seg->next; seg = seg->next)
        ;

    if (seg == NULL || seg->count == 0)
        return 0;

    return seg->asns[seg->count - 1];
}

bool aspath_loop_check(struct aspath *aspath, uint32_t asn)
{
    struct as_segment *seg;

    if (aspath == NULL)
        return false;

    for (seg = aspath->segments; seg; seg = seg->next) {
        for (int i = 0; i < seg->count; i++) {
            if (seg->asns[i] == asn)
                return true;
        }
    }

    return false;
}

struct aspath *aspath_prepend(struct aspath *aspath, uint32_t asn)
{
    struct as_segment *seg;

    if (aspath == NULL)
        aspath = aspath_new();

    /* Prepend to first segment if it's a sequence */
    if (aspath->segments && aspath->segments->type == BGP_AS_SEQUENCE) {
        seg = aspath->segments;
        seg->asns = XREALLOC(MTYPE_BGP_PATH, seg->asns,
                             (seg->count + 1) * sizeof(uint32_t));
        memmove(&seg->asns[1], &seg->asns[0], seg->count * sizeof(uint32_t));
        seg->asns[0] = asn;
        seg->count++;
    } else {
        /* Create new segment */
        seg = XCALLOC(MTYPE_BGP_PATH, sizeof(struct as_segment));
        seg->type = BGP_AS_SEQUENCE;
        seg->count = 1;
        seg->asns = XMALLOC(MTYPE_BGP_PATH, sizeof(uint32_t));
        seg->asns[0] = asn;
        seg->next = aspath->segments;
        aspath->segments = seg;
    }

    aspath->length++;
    XFREE(MTYPE_BGP_PATH, aspath->str);
    aspath->str = NULL;

    return aspath;
}

/* Community implementation */
struct community *community_new(void)
{
    return XCALLOC(MTYPE_BGP_ATTR, sizeof(struct community));
}

void community_free(struct community *com)
{
    if (com == NULL)
        return;

    XFREE(MTYPE_BGP_ATTR, com->val);
    XFREE(MTYPE_BGP_ATTR, com->str);
    XFREE(MTYPE_BGP_ATTR, com);
}

struct community *community_dup(struct community *com)
{
    struct community *new;

    if (com == NULL)
        return NULL;

    new = community_new();
    new->size = com->size;
    if (com->size > 0) {
        new->val = XMALLOC(MTYPE_BGP_ATTR, com->size * sizeof(uint32_t));
        memcpy(new->val, com->val, com->size * sizeof(uint32_t));
    }

    return new;
}

struct community *community_intern(struct community *com)
{
    com->refcnt++;
    return com;
}

void community_unintern(struct community **com)
{
    if (*com == NULL)
        return;

    if (--(*com)->refcnt == 0)
        community_free(*com);

    *com = NULL;
}

struct community *community_parse(uint8_t *data, size_t length)
{
    struct community *com;
    size_t count = length / 4;

    com = community_new();
    com->size = count;
    com->val = XMALLOC(MTYPE_BGP_ATTR, count * sizeof(uint32_t));

    for (size_t i = 0; i < count; i++) {
        com->val[i] = (data[i * 4] << 24) | (data[i * 4 + 1] << 16) |
                      (data[i * 4 + 2] << 8) | data[i * 4 + 3];
    }

    return com;
}

const char *community_str(struct community *com)
{
    if (com == NULL)
        return "";

    if (com->str)
        return com->str;

    size_t len = com->size * 16;
    com->str = XMALLOC(MTYPE_BGP_ATTR, len + 1);
    com->str[0] = '\0';

    for (size_t i = 0; i < com->size; i++) {
        char buf[32];
        uint32_t val = com->val[i];

        if (val == COMMUNITY_NO_EXPORT)
            snprintf(buf, sizeof(buf), "no-export");
        else if (val == COMMUNITY_NO_ADVERTISE)
            snprintf(buf, sizeof(buf), "no-advertise");
        else
            snprintf(buf, sizeof(buf), "%u:%u", val >> 16, val & 0xFFFF);

        strcat(com->str, buf);
        if (i < com->size - 1)
            strcat(com->str, " ");
    }

    return com->str;
}

bool community_include(struct community *com, uint32_t val)
{
    if (com == NULL)
        return false;

    for (size_t i = 0; i < com->size; i++) {
        if (com->val[i] == val)
            return true;
    }

    return false;
}

/* Cluster List implementation */
struct cluster_list *cluster_list_new(void)
{
    return XCALLOC(MTYPE_BGP_RR, sizeof(struct cluster_list));
}

void cluster_list_free(struct cluster_list *cl)
{
    if (cl == NULL)
        return;

    XFREE(MTYPE_BGP_RR, cl->clusters);
    XFREE(MTYPE_BGP_RR, cl);
}

struct cluster_list *cluster_list_dup(struct cluster_list *cl)
{
    struct cluster_list *new;

    if (cl == NULL)
        return NULL;

    new = cluster_list_new();
    new->count = cl->count;
    if (cl->count > 0) {
        new->clusters = XMALLOC(MTYPE_BGP_RR, cl->count * sizeof(struct in_addr));
        memcpy(new->clusters, cl->clusters, cl->count * sizeof(struct in_addr));
    }

    return new;
}

struct cluster_list *cluster_list_intern(struct cluster_list *cl)
{
    cl->refcnt++;
    return cl;
}

void cluster_list_unintern(struct cluster_list **cl)
{
    if (*cl == NULL)
        return;

    if (--(*cl)->refcnt == 0)
        cluster_list_free(*cl);

    *cl = NULL;
}

struct cluster_list *cluster_list_parse(uint8_t *data, size_t length)
{
    struct cluster_list *cl;
    size_t count = length / 4;

    cl = cluster_list_new();
    cl->count = count;
    cl->clusters = XMALLOC(MTYPE_BGP_RR, count * sizeof(struct in_addr));

    for (size_t i = 0; i < count; i++) {
        memcpy(&cl->clusters[i], &data[i * 4], 4);
    }

    return cl;
}

bool cluster_loop_check(struct cluster_list *cl, struct in_addr *cluster_id)
{
    if (cl == NULL)
        return false;

    for (int i = 0; i < cl->count; i++) {
        if (cl->clusters[i].s_addr == cluster_id->s_addr)
            return true;
    }

    return false;
}

struct cluster_list *cluster_list_prepend(struct cluster_list *cl,
                                          struct in_addr *cluster_id)
{
    if (cl == NULL)
        cl = cluster_list_new();

    cl->clusters = XREALLOC(MTYPE_BGP_RR, cl->clusters,
                            (cl->count + 1) * sizeof(struct in_addr));
    memmove(&cl->clusters[1], &cl->clusters[0],
            cl->count * sizeof(struct in_addr));
    cl->clusters[0] = *cluster_id;
    cl->count++;

    return cl;
}

void bgp_attr_filter_ebgp(struct bgp_attr *attr)
{
    /* Remove LOCAL_PREF for eBGP */
    attr->flags &= ~BGP_ATTR_FLAG_HAS_LOCAL_PREF;
    attr->local_pref = 0;

    /* Remove cluster list and originator ID */
    if (attr->cluster_list) {
        cluster_list_unintern(&attr->cluster_list);
    }
    attr->flags &= ~(BGP_ATTR_FLAG_HAS_CLUSTER | BGP_ATTR_FLAG_HAS_ORIGINATOR);
}

void bgp_attr_filter_ibgp(struct bgp_attr *attr)
{
    /* Set default LOCAL_PREF if not present */
    if (!(attr->flags & BGP_ATTR_FLAG_HAS_LOCAL_PREF)) {
        attr->local_pref = 100;
        attr->flags |= BGP_ATTR_FLAG_HAS_LOCAL_PREF;
    }
}
