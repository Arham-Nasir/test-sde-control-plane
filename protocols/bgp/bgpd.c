/*
 * BGP Daemon Implementation
 */

#include "bgpd.h"
#include "bgp_fsm.h"
#include "bgp_neighbor.h"
#include "common/log.h"

#include <string.h>
#include <arpa/inet.h>

#undef LOG_TAG
#define LOG_TAG "BGP"

/* Global BGP master */
struct bgp_master *bm = NULL;

/* Hash functions for peer lookup */
static uint32_t peer_hash_key(const void *data)
{
    const struct peer *peer = data;
    return jhash(&peer->remote_addr, sizeof(peer->remote_addr), 0);
}

static bool peer_hash_cmp(const void *a, const void *b)
{
    const struct peer *p1 = a;
    const struct peer *p2 = b;

    return memcmp(&p1->remote_addr, &p2->remote_addr,
                  sizeof(p1->remote_addr)) == 0;
}

void bgp_master_init(struct event_loop *master)
{
    if (bm != NULL)
        return;

    bm = XCALLOC(MTYPE_BGP, sizeof(struct bgp_master));
    bm->bgp = list_new();
    bm->master = master;
    bm->listen_fd = -1;
    bm->listen_fd6 = -1;
    bm->start_time = time(NULL);

    log_info("BGP master initialized");
}

void bgp_master_finish(void)
{
    struct listnode *node, *next;
    struct bgp *bgp;

    if (bm == NULL)
        return;

    /* Delete all BGP instances */
    for_each_list_node_safe(bm->bgp, node, next) {
        bgp = listgetdata(node);
        bgp_delete(bgp);
    }

    list_delete(&bm->bgp);
    XFREE(MTYPE_BGP, bm);
    bm = NULL;

    log_info("BGP master finished");
}

struct bgp *bgp_create(uint32_t as, const char *name)
{
    struct bgp *bgp;

    if (bm == NULL)
        return NULL;

    /* Check for existing instance */
    bgp = bgp_lookup(as, name);
    if (bgp != NULL) {
        log_warn("BGP instance AS %u already exists", as);
        return bgp;
    }

    bgp = XCALLOC(MTYPE_BGP, sizeof(struct bgp));
    bgp->as = as;
    bgp->master = bm->master;

    if (name)
        bgp->name = XSTRDUP(MTYPE_BGP, name);

    /* Initialize peer list and hash */
    bgp->peer = list_new();
    bgp->peerhash = hash_create(peer_hash_key, peer_hash_cmp, "BGP Peer Hash");

    /* Initialize route tables */
    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            bgp->rib[afi][safi] = route_table_init();
        }
    }

    /* Default timers */
    bgp->gr_restart_time = 120;
    bgp->gr_stale_time = 360;

    /* Add to master list */
    listnode_add(bm->bgp, bgp);

    log_info("BGP instance AS %u created", as);
    return bgp;
}

void bgp_delete(struct bgp *bgp)
{
    struct listnode *node, *next;
    struct peer *peer;

    if (bgp == NULL)
        return;

    log_info("BGP instance AS %u deleting", bgp->as);

    /* Delete all peers */
    for_each_list_node_safe(bgp->peer, node, next) {
        peer = listgetdata(node);
        peer_delete(peer);
    }

    list_delete(&bgp->peer);
    hash_clean_and_free(&bgp->peerhash, NULL);

    /* Delete route tables */
    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            if (bgp->rib[afi][safi])
                route_table_finish(bgp->rib[afi][safi]);
        }
    }

    /* Remove from master list */
    listnode_remove(bm->bgp, bgp);

    if (bgp->name)
        XFREE(MTYPE_BGP, bgp->name);
    if (bgp->confed_peers)
        XFREE(MTYPE_BGP, bgp->confed_peers);

    XFREE(MTYPE_BGP, bgp);
}

struct bgp *bgp_lookup(uint32_t as, const char *name)
{
    struct listnode *node;
    struct bgp *bgp;

    if (bm == NULL)
        return NULL;

    for_each_list_node(bm->bgp, node) {
        bgp = listgetdata(node);

        if (bgp->as != as)
            continue;

        if (name == NULL && bgp->name == NULL)
            return bgp;

        if (name && bgp->name && strcmp(name, bgp->name) == 0)
            return bgp;
    }

    return NULL;
}

struct bgp *bgp_lookup_by_name(const char *name)
{
    struct listnode *node;
    struct bgp *bgp;

    if (bm == NULL)
        return NULL;

    for_each_list_node(bm->bgp, node) {
        bgp = listgetdata(node);

        if (name == NULL && bgp->name == NULL)
            return bgp;

        if (name && bgp->name && strcmp(name, bgp->name) == 0)
            return bgp;
    }

    return NULL;
}

struct bgp *bgp_get_default(void)
{
    if (bm == NULL || listcount(bm->bgp) == 0)
        return NULL;

    return listgetdata(listhead(bm->bgp));
}

void bgp_router_id_set(struct bgp *bgp, struct in_addr *router_id)
{
    char buf[INET_ADDRSTRLEN];

    bgp->router_id = *router_id;
    bgp->router_id_static = true;

    inet_ntop(AF_INET, router_id, buf, sizeof(buf));
    log_info("BGP AS %u router-id set to %s", bgp->as, buf);

    /* TODO: Reset all peer sessions */
}

void bgp_router_id_unset(struct bgp *bgp)
{
    memset(&bgp->router_id, 0, sizeof(bgp->router_id));
    bgp->router_id_static = false;

    log_info("BGP AS %u router-id unset", bgp->as);
}

void bgp_cluster_id_set(struct bgp *bgp, struct in_addr *cluster_id)
{
    char buf[INET_ADDRSTRLEN];

    bgp->cluster_id = *cluster_id;
    bgp->cluster_id_set = true;
    bgp->rr_enabled = true;

    inet_ntop(AF_INET, cluster_id, buf, sizeof(buf));
    log_info("BGP AS %u cluster-id set to %s (Route Reflector enabled)",
             bgp->as, buf);
}

void bgp_cluster_id_unset(struct bgp *bgp)
{
    memset(&bgp->cluster_id, 0, sizeof(bgp->cluster_id));
    bgp->cluster_id_set = false;
    bgp->rr_enabled = false;

    log_info("BGP AS %u cluster-id unset (Route Reflector disabled)", bgp->as);
}

void bgp_gr_enable(struct bgp *bgp, uint16_t restart_time, uint16_t stale_time)
{
    bgp->gr_restart_time = restart_time;
    bgp->gr_stale_time = stale_time;
    bgp->gr_flags |= BGP_FLAG_GR_PRESERVE_FWD;

    log_info("BGP AS %u Graceful Restart enabled (restart=%u stale=%u)",
             bgp->as, restart_time, stale_time);
}

void bgp_gr_disable(struct bgp *bgp)
{
    bgp->gr_flags &= ~BGP_FLAG_GR_PRESERVE_FWD;

    log_info("BGP AS %u Graceful Restart disabled", bgp->as);
}

bool bgp_gr_is_enabled(struct bgp *bgp)
{
    return (bgp->gr_flags & BGP_FLAG_GR_PRESERVE_FWD) != 0;
}

void bgp_timers_set(struct bgp *bgp, uint16_t holdtime, uint16_t keepalive)
{
    /* This sets default timers for new peers */
    log_info("BGP AS %u default timers: hold=%u keepalive=%u",
             bgp->as, holdtime, keepalive);
}

struct route_table *bgp_table_get(struct bgp *bgp, uint8_t afi, uint8_t safi)
{
    if (afi >= AFI_MAX || safi >= SAFI_MAX)
        return NULL;

    return bgp->rib[afi][safi];
}
