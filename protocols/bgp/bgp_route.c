/*
 * BGP Route Management Implementation
 */

#include "bgp_route.h"
#include "common/log.h"

#include <string.h>
#include <arpa/inet.h>

#undef LOG_TAG
#define LOG_TAG "BGP"

struct bgp_path_info *bgp_path_info_new(void)
{
    return XCALLOC(MTYPE_BGP_ROUTE, sizeof(struct bgp_path_info));
}

void bgp_path_info_free(struct bgp_path_info *pi)
{
    if (pi == NULL)
        return;

    if (pi->attr)
        bgp_attr_unintern(&pi->attr);

    if (pi->extra) {
        if (pi->extra->orig_attr)
            bgp_attr_unintern(&pi->extra->orig_attr);
        XFREE(MTYPE_BGP_ROUTE, pi->extra);
    }

    XFREE(MTYPE_BGP_ROUTE, pi);
}

struct bgp_path_info *bgp_path_info_create(struct peer *peer,
                                           struct bgp_attr *attr)
{
    struct bgp_path_info *pi;

    pi = bgp_path_info_new();
    pi->peer = peer;
    pi->attr = bgp_attr_intern(attr);
    pi->uptime = time(NULL);
    pi->type = BGP_PATH_NORMAL;
    pi->flags = BGP_PATH_VALID;

    return pi;
}

void bgp_path_info_add(struct route_node *rn, struct bgp_path_info *pi)
{
    struct bgp_route_info *ri = rn->info;

    if (ri == NULL) {
        ri = XCALLOC(MTYPE_BGP_ROUTE, sizeof(struct bgp_route_info));
        rn->info = ri;
    }

    /* Add to head of list */
    pi->next = ri->paths;
    pi->prev = NULL;

    if (ri->paths)
        ri->paths->prev = pi;
    ri->paths = pi;
    ri->path_count++;
}

void bgp_path_info_delete(struct route_node *rn, struct bgp_path_info *pi)
{
    struct bgp_route_info *ri = rn->info;

    if (ri == NULL)
        return;

    if (pi->prev)
        pi->prev->next = pi->next;
    else
        ri->paths = pi->next;

    if (pi->next)
        pi->next->prev = pi->prev;

    if (ri->selected == pi)
        ri->selected = NULL;

    ri->path_count--;
}

struct bgp_path_info *bgp_path_info_lookup(struct route_node *rn,
                                           struct peer *peer)
{
    struct bgp_route_info *ri = rn->info;
    struct bgp_path_info *pi;

    if (ri == NULL)
        return NULL;

    for (pi = ri->paths; pi; pi = pi->next) {
        if (pi->peer == peer)
            return pi;
    }

    return NULL;
}

void bgp_path_info_set_flag(struct bgp_path_info *pi, uint32_t flag)
{
    pi->flags |= flag;
}

void bgp_path_info_unset_flag(struct bgp_path_info *pi, uint32_t flag)
{
    pi->flags &= ~flag;
}

bool bgp_path_info_check_flag(struct bgp_path_info *pi, uint32_t flag)
{
    return (pi->flags & flag) != 0;
}

/*
 * BGP Best Path Selection
 *
 * Returns:
 *  -1 if pi1 is better
 *   1 if pi2 is better
 *   0 if equal
 */
int bgp_path_info_cmp(struct bgp *bgp, struct bgp_path_info *pi1,
                      struct bgp_path_info *pi2, int *paths_eq)
{
    struct bgp_attr *attr1 = pi1->attr;
    struct bgp_attr *attr2 = pi2->attr;
    int ret;

    *paths_eq = 0;

    /* 1. Higher LOCAL_PREF is better */
    if (attr1->local_pref != attr2->local_pref) {
        ret = (attr1->local_pref > attr2->local_pref) ? -1 : 1;
        log_debug("Best path: LOCAL_PREF %u vs %u -> %d",
                  attr1->local_pref, attr2->local_pref, ret);
        return ret;
    }

    /* 2. Locally originated routes are better */
    if ((pi1->type == BGP_PATH_STATIC) != (pi2->type == BGP_PATH_STATIC)) {
        ret = (pi1->type == BGP_PATH_STATIC) ? -1 : 1;
        log_debug("Best path: local origin -> %d", ret);
        return ret;
    }

    /* 3. Shorter AS_PATH is better */
    uint32_t aspath1_len = aspath_count(attr1->aspath);
    uint32_t aspath2_len = aspath_count(attr2->aspath);
    if (aspath1_len != aspath2_len) {
        ret = (aspath1_len < aspath2_len) ? -1 : 1;
        log_debug("Best path: AS_PATH length %u vs %u -> %d",
                  aspath1_len, aspath2_len, ret);
        return ret;
    }

    /* 4. Lower ORIGIN is better (IGP < EGP < INCOMPLETE) */
    if (attr1->origin != attr2->origin) {
        ret = (attr1->origin < attr2->origin) ? -1 : 1;
        log_debug("Best path: ORIGIN %u vs %u -> %d",
                  attr1->origin, attr2->origin, ret);
        return ret;
    }

    /* 5. Lower MED is better (when comparing paths from same AS) */
    uint32_t as1 = aspath_first_as(attr1->aspath);
    uint32_t as2 = aspath_first_as(attr2->aspath);

    if ((bgp->flags & BGP_FLAG_ALWAYS_COMPARE_MED) || as1 == as2) {
        if (attr1->med != attr2->med) {
            ret = (attr1->med < attr2->med) ? -1 : 1;
            log_debug("Best path: MED %u vs %u -> %d",
                      attr1->med, attr2->med, ret);
            return ret;
        }
    }

    /* 6. Prefer eBGP over iBGP */
    bool pi1_ebgp = (pi1->peer->remote_as != bgp->as);
    bool pi2_ebgp = (pi2->peer->remote_as != bgp->as);

    if (pi1_ebgp != pi2_ebgp) {
        ret = pi1_ebgp ? -1 : 1;
        log_debug("Best path: eBGP vs iBGP -> %d", ret);
        return ret;
    }

    /* 7. Prefer older route (for stability) */
    if (pi1->uptime != pi2->uptime) {
        ret = (pi1->uptime < pi2->uptime) ? -1 : 1;
        log_debug("Best path: uptime %ld vs %ld -> %d",
                  pi1->uptime, pi2->uptime, ret);
        return ret;
    }

    /* 8. Lower router ID is better */
    if (pi1->peer->remote_id.s_addr != pi2->peer->remote_id.s_addr) {
        ret = (ntohl(pi1->peer->remote_id.s_addr) <
               ntohl(pi2->peer->remote_id.s_addr)) ? -1 : 1;
        log_debug("Best path: router-id comparison -> %d", ret);
        return ret;
    }

    /* 9. Shorter cluster list is better (Route Reflector) */
    uint8_t cl1_len = attr1->cluster_list ? attr1->cluster_list->count : 0;
    uint8_t cl2_len = attr2->cluster_list ? attr2->cluster_list->count : 0;
    if (cl1_len != cl2_len) {
        ret = (cl1_len < cl2_len) ? -1 : 1;
        log_debug("Best path: cluster-list length %u vs %u -> %d",
                  cl1_len, cl2_len, ret);
        return ret;
    }

    /* Paths are equal for multipath */
    *paths_eq = 1;
    return 0;
}

void bgp_best_selection(struct bgp *bgp, struct route_node *rn,
                        uint8_t afi, uint8_t safi)
{
    struct bgp_route_info *ri = rn->info;
    struct bgp_path_info *pi, *best = NULL;
    int paths_eq;

    (void)afi;
    (void)safi;

    if (ri == NULL)
        return;

    /* Clear previous selection */
    for (pi = ri->paths; pi; pi = pi->next) {
        bgp_path_info_unset_flag(pi, BGP_PATH_SELECTED);
        bgp_path_info_unset_flag(pi, BGP_PATH_MULTIPATH);
    }

    /* Find best path */
    for (pi = ri->paths; pi; pi = pi->next) {
        /* Skip invalid paths */
        if (!bgp_path_info_check_flag(pi, BGP_PATH_VALID))
            continue;

        /* Skip stale paths (unless GR is active) */
        if (bgp_path_info_check_flag(pi, BGP_PATH_STALE))
            continue;

        if (best == NULL) {
            best = pi;
            continue;
        }

        if (bgp_path_info_cmp(bgp, pi, best, &paths_eq) < 0)
            best = pi;
    }

    /* Mark best path */
    if (best) {
        bgp_path_info_set_flag(best, BGP_PATH_SELECTED);
        ri->selected = best;

        char prefix_str[PREFIX_STRLEN];
        prefix2str(&rn->p, prefix_str, sizeof(prefix_str));
        log_debug("BGP best path for %s via %s",
                  prefix_str, best->peer->host);
    } else {
        ri->selected = NULL;
    }
}

int bgp_update(struct peer *peer, struct prefix *p, struct bgp_attr *attr,
               uint8_t afi, uint8_t safi)
{
    struct bgp *bgp = peer->bgp;
    struct route_table *table;
    struct route_node *rn;
    struct bgp_path_info *pi, *existing;

    table = bgp_table_get(bgp, afi, safi);
    if (table == NULL)
        return -1;

    rn = route_node_get(table, p);

    /* Check for existing path from this peer */
    existing = bgp_path_info_lookup(rn, peer);

    if (existing) {
        /* Update existing path */
        bgp_attr_unintern(&existing->attr);
        existing->attr = bgp_attr_intern(attr);
        existing->uptime = time(NULL);
        bgp_path_info_set_flag(existing, BGP_PATH_VALID);
        bgp_path_info_unset_flag(existing, BGP_PATH_STALE);
    } else {
        /* Create new path */
        pi = bgp_path_info_create(peer, attr);
        bgp_path_info_add(rn, pi);
    }

    /* Run best path selection */
    bgp_best_selection(bgp, rn, afi, safi);

    route_unlock_node(rn);

    peer->bgp->updates_received++;

    return 0;
}

int bgp_withdraw(struct peer *peer, struct prefix *p, uint8_t afi, uint8_t safi)
{
    struct bgp *bgp = peer->bgp;
    struct route_table *table;
    struct route_node *rn;
    struct bgp_path_info *pi;

    table = bgp_table_get(bgp, afi, safi);
    if (table == NULL)
        return -1;

    rn = route_node_lookup(table, p);
    if (rn == NULL)
        return 0;

    pi = bgp_path_info_lookup(rn, peer);
    if (pi) {
        bgp_path_info_delete(rn, pi);
        bgp_path_info_free(pi);
        bgp_best_selection(bgp, rn, afi, safi);
    }

    route_unlock_node(rn);
    return 0;
}

void bgp_clear_route(struct peer *peer, uint8_t afi, uint8_t safi)
{
    struct bgp *bgp = peer->bgp;
    struct route_table *table;
    struct route_node *rn;
    struct bgp_path_info *pi;

    table = bgp_table_get(bgp, afi, safi);
    if (table == NULL)
        return;

    for (rn = route_top(table); rn; rn = route_next(rn)) {
        pi = bgp_path_info_lookup(rn, peer);
        if (pi) {
            bgp_path_info_delete(rn, pi);
            bgp_path_info_free(pi);
            bgp_best_selection(bgp, rn, afi, safi);
        }
    }
}

void bgp_clear_stale_routes(struct peer *peer, uint8_t afi, uint8_t safi)
{
    struct bgp *bgp = peer->bgp;
    struct route_table *table;
    struct route_node *rn;
    struct bgp_route_info *ri;
    struct bgp_path_info *pi, *next;

    table = bgp_table_get(bgp, afi, safi);
    if (table == NULL)
        return;

    for (rn = route_top(table); rn; rn = route_next(rn)) {
        ri = rn->info;
        if (ri == NULL)
            continue;

        for (pi = ri->paths; pi; pi = next) {
            next = pi->next;

            if (pi->peer != peer)
                continue;

            if (bgp_path_info_check_flag(pi, BGP_PATH_STALE)) {
                log_debug("BGP clearing stale route from %s", peer->host);
                bgp_path_info_delete(rn, pi);
                bgp_path_info_free(pi);
            }
        }

        bgp_best_selection(bgp, rn, afi, safi);
    }
}

/*
 * Route Reflector Functions
 *
 * TODO: These are stub implementations. Candidates implementing the
 * Principal-level assessment should complete these functions in
 * assessments/principal_engineer/starter/bgp_route_reflector.c
 */

bool bgp_rr_should_reflect(struct bgp *bgp, struct peer *from_peer,
                           struct peer *to_peer, struct bgp_path_info *pi)
{
    /*
     * TODO: Implement reflection decision logic (RFC 4456)
     *
     * Reflection rules:
     * - Never reflect back to sender
     * - eBGP -> all iBGP peers: reflect
     * - Client -> Client: reflect
     * - Client -> Non-client: reflect
     * - Non-client -> Client: reflect
     * - Non-client -> Non-client: DON'T reflect
     * - Check for cluster loops
     */
    (void)bgp;
    (void)from_peer;
    (void)to_peer;
    (void)pi;
    return false;
}

struct bgp_attr *bgp_rr_prepare_attr(struct bgp *bgp, struct peer *from_peer,
                                     struct peer *to_peer,
                                     struct bgp_attr *attr)
{
    /*
     * TODO: Prepare attributes for reflection
     *
     * 1. Duplicate the attribute
     * 2. Set ORIGINATOR_ID if not present
     * 3. Prepend cluster ID to CLUSTER_LIST
     */
    (void)bgp;
    (void)from_peer;
    (void)to_peer;
    return bgp_attr_dup(attr);
}
