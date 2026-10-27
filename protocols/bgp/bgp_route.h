/*
 * BGP Route Management
 *
 * Functions for managing BGP routes, including best path selection
 * and route propagation.
 *
 * Pattern Reference: bgpd/bgp_route.h in FRRouting
 */

#ifndef _BGP_ROUTE_H
#define _BGP_ROUTE_H

#include "bgpd.h"
#include "bgp_attr.h"
#include "bgp_neighbor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Path info flags */
#define BGP_PATH_VALID          (1 << 0)
#define BGP_PATH_HISTORY        (1 << 1)
#define BGP_PATH_DAMPED         (1 << 2)
#define BGP_PATH_SELECTED       (1 << 3)
#define BGP_PATH_STALE          (1 << 4)
#define BGP_PATH_REMOVED        (1 << 5)
#define BGP_PATH_COUNTED        (1 << 6)
#define BGP_PATH_DMED_CHECK     (1 << 7)
#define BGP_PATH_DMED_SELECTED  (1 << 8)
#define BGP_PATH_MULTIPATH      (1 << 9)
#define BGP_PATH_LLGR_STALE     (1 << 10)

/* Path sub-type */
typedef enum {
    BGP_PATH_NORMAL = 0,
    BGP_PATH_AGGREGATE,
    BGP_PATH_IMPORTED,
    BGP_PATH_STATIC,
} bgp_path_type_t;

/* BGP Path Info - represents a single path to a destination */
struct bgp_path_info {
    /* Path attributes */
    struct bgp_attr *attr;

    /* Source peer */
    struct peer *peer;

    /* Flags */
    uint32_t flags;

    /* Path type */
    bgp_path_type_t type;

    /* Extra info */
    struct bgp_path_info_extra *extra;

    /* Uptime */
    time_t uptime;

    /* MED for DMED */
    uint32_t med;

    /* For route selection */
    struct bgp_path_info *next;
    struct bgp_path_info *prev;
};

/* Extra path information */
struct bgp_path_info_extra {
    /* Label */
    uint32_t label;

    /* Original attributes (before modification) */
    struct bgp_attr *orig_attr;

    /* Addpath ID */
    uint32_t addpath_rx_id;
    uint32_t addpath_tx_id;
};

/* Route node info */
struct bgp_route_info {
    /* Paths for this prefix */
    struct bgp_path_info *paths;

    /* Selected path */
    struct bgp_path_info *selected;

    /* Multipath set */
    struct bgp_path_info *multipath;

    /* Path count */
    uint32_t path_count;
};

/* Route management */
struct bgp_path_info *bgp_path_info_new(void);
void bgp_path_info_free(struct bgp_path_info *pi);

struct bgp_path_info *bgp_path_info_create(struct peer *peer,
                                           struct bgp_attr *attr);

/* Add/remove paths from route node */
void bgp_path_info_add(struct route_node *rn, struct bgp_path_info *pi);
void bgp_path_info_delete(struct route_node *rn, struct bgp_path_info *pi);
struct bgp_path_info *bgp_path_info_lookup(struct route_node *rn,
                                           struct peer *peer);

/* Path flags */
void bgp_path_info_set_flag(struct bgp_path_info *pi, uint32_t flag);
void bgp_path_info_unset_flag(struct bgp_path_info *pi, uint32_t flag);
bool bgp_path_info_check_flag(struct bgp_path_info *pi, uint32_t flag);

/*
 * Best Path Selection (RFC 4271 Section 9.1.2.2)
 *
 * The BGP best path selection algorithm compares paths in the following order:
 * 1. Prefer higher weight (Cisco proprietary, not in RFC)
 * 2. Prefer higher LOCAL_PREF
 * 3. Prefer locally originated routes
 * 4. Prefer shorter AS_PATH
 * 5. Prefer lower ORIGIN (IGP < EGP < INCOMPLETE)
 * 6. Prefer lower MED (when comparing paths from same AS)
 * 7. Prefer eBGP over iBGP
 * 8. Prefer path through nearest IGP neighbor
 * 9. Prefer older route
 * 10. Prefer lower router ID
 * 11. Prefer shorter cluster list
 * 12. Prefer lower neighbor address
 */
int bgp_path_info_cmp(struct bgp *bgp, struct bgp_path_info *pi1,
                      struct bgp_path_info *pi2, int *paths_eq);

/* Run best path selection for a route node */
void bgp_best_selection(struct bgp *bgp, struct route_node *rn,
                        uint8_t afi, uint8_t safi);

/* Process route updates */
int bgp_update(struct peer *peer, struct prefix *p, struct bgp_attr *attr,
               uint8_t afi, uint8_t safi);
int bgp_withdraw(struct peer *peer, struct prefix *p, uint8_t afi, uint8_t safi);

/* Route announcement */
void bgp_announce_route(struct peer *peer, uint8_t afi, uint8_t safi);
void bgp_announce_route_all(struct bgp *bgp);

/* Route clearing */
void bgp_clear_route(struct peer *peer, uint8_t afi, uint8_t safi);
void bgp_clear_stale_routes(struct peer *peer, uint8_t afi, uint8_t safi);

/* Route table scanning */
void bgp_route_table_scan(struct bgp *bgp, uint8_t afi, uint8_t safi);

/*
 * Route Reflector Functions
 *
 * Route Reflectors (RFC 4456) allow iBGP scalability by reducing
 * the number of required iBGP sessions. A Route Reflector:
 *
 * 1. Reflects routes between clients
 * 2. Reflects routes from clients to non-clients
 * 3. Reflects routes from non-clients to clients
 * 4. Does NOT reflect routes from non-clients to non-clients
 *
 * To prevent loops:
 * - ORIGINATOR_ID is set to the originating router's ID
 * - CLUSTER_LIST is prepended with the RR's cluster ID
 */

/* Check if route should be reflected to peer */
bool bgp_rr_should_reflect(struct bgp *bgp, struct peer *from_peer,
                           struct peer *to_peer, struct bgp_path_info *pi);

/* Prepare attributes for reflection */
struct bgp_attr *bgp_rr_prepare_attr(struct bgp *bgp, struct peer *from_peer,
                                     struct peer *to_peer,
                                     struct bgp_attr *attr);

#ifdef __cplusplus
}
#endif

#endif /* _BGP_ROUTE_H */
