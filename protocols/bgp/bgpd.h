/*
 * BGP Daemon Main Header
 *
 * This is the main header for the BGP daemon, defining the core BGP
 * instance structure and global configuration.
 *
 * Pattern Reference: bgpd/bgpd.h in FRRouting
 */

#ifndef _BGP_BGPD_H
#define _BGP_BGPD_H

#include "common/prefix.h"
#include "common/linklist.h"
#include "common/hash.h"
#include "common/thread.h"
#include "zebra/rib.h"

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BGP Version */
#define BGP_VERSION_4   4

/* BGP Default Values */
#define BGP_DEFAULT_HOLDTIME            180
#define BGP_DEFAULT_KEEPALIVE           60
#define BGP_DEFAULT_CONNECT_RETRY       120
#define BGP_DEFAULT_EBGP_MULTIHOP       1
#define BGP_DEFAULT_IBGP_ROUTEADV       0
#define BGP_DEFAULT_EBGP_ROUTEADV       0

/* BGP Message Types */
#define BGP_MSG_OPEN            1
#define BGP_MSG_UPDATE          2
#define BGP_MSG_NOTIFICATION    3
#define BGP_MSG_KEEPALIVE       4
#define BGP_MSG_ROUTE_REFRESH   5

/* BGP Header Size */
#define BGP_MARKER_SIZE         16
#define BGP_HEADER_SIZE         19
#define BGP_MAX_PACKET_SIZE     4096
#define BGP_EXTENDED_MESSAGE_MAX_SIZE 65535

/* BGP Error Codes */
#define BGP_NOTIFY_HEADER_ERR           1
#define BGP_NOTIFY_OPEN_ERR             2
#define BGP_NOTIFY_UPDATE_ERR           3
#define BGP_NOTIFY_HOLD_ERR             4
#define BGP_NOTIFY_FSM_ERR              5
#define BGP_NOTIFY_CEASE                6

/* BGP OPEN Message Error Subcodes */
#define BGP_NOTIFY_OPEN_UNSUPPORTED_VERSION     1
#define BGP_NOTIFY_OPEN_BAD_PEER_AS             2
#define BGP_NOTIFY_OPEN_BAD_BGP_IDENTIFIER      3
#define BGP_NOTIFY_OPEN_UNSUPPORTED_PARAM       4
#define BGP_NOTIFY_OPEN_AUTH_FAILURE            5
#define BGP_NOTIFY_OPEN_UNACEP_HOLDTIME         6

/* BGP UPDATE Error Subcodes */
#define BGP_NOTIFY_UPDATE_MALFORMED_ATTR        1
#define BGP_NOTIFY_UPDATE_UNREC_ATTR            2
#define BGP_NOTIFY_UPDATE_MISSING_ATTR          3
#define BGP_NOTIFY_UPDATE_ATTR_FLAG_ERR         4
#define BGP_NOTIFY_UPDATE_ATTR_LEN_ERR          5
#define BGP_NOTIFY_UPDATE_INVAL_ORIGIN          6
#define BGP_NOTIFY_UPDATE_INVAL_NEXT_HOP        8
#define BGP_NOTIFY_UPDATE_OPT_ATTR_ERR          9
#define BGP_NOTIFY_UPDATE_INVAL_NETWORK         10
#define BGP_NOTIFY_UPDATE_MAL_AS_PATH           11

/* BGP CEASE Error Subcodes */
#define BGP_NOTIFY_CEASE_MAX_PREFIX             1
#define BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN         2
#define BGP_NOTIFY_CEASE_PEER_UNCONFIG          3
#define BGP_NOTIFY_CEASE_ADMIN_RESET            4
#define BGP_NOTIFY_CEASE_CONNECT_REJECT         5
#define BGP_NOTIFY_CEASE_CONFIG_CHANGE          6
#define BGP_NOTIFY_CEASE_COLLISION_RESOLUTION   7
#define BGP_NOTIFY_CEASE_OUT_OF_RESOURCE        8
#define BGP_NOTIFY_CEASE_HARD_RESET             9

/* BGP Capabilities */
#define BGP_CAP_CODE_MP                 1   /* Multiprotocol Extensions */
#define BGP_CAP_CODE_RR                 2   /* Route Refresh */
#define BGP_CAP_CODE_ORF                3   /* Cooperative Route Filtering */
#define BGP_CAP_CODE_GR                 64  /* Graceful Restart */
#define BGP_CAP_CODE_AS4                65  /* 4-byte AS Numbers */
#define BGP_CAP_CODE_ADDPATH            69  /* ADD-PATH */
#define BGP_CAP_CODE_ENHANCED_RR        70  /* Enhanced Route Refresh */
#define BGP_CAP_CODE_LLGR               71  /* Long-lived Graceful Restart */
#define BGP_CAP_CODE_FQDN               73  /* FQDN Capability */
#define BGP_CAP_CODE_ENHE               5   /* Extended Next Hop Encoding */

/* Graceful Restart Flags (in capability) */
#define BGP_GR_FLAG_RESTART         (1 << 7)    /* Restart State */
#define BGP_GR_FLAG_NOTIFICATION    (1 << 6)    /* Notification */

/* Graceful Restart AFI/SAFI Flags */
#define BGP_GR_FLAG_FORWARDING      (1 << 7)    /* Forwarding State */

/* BGP Instance Flags */
#define BGP_FLAG_DETERMINISTIC_MED      (1 << 0)
#define BGP_FLAG_ALWAYS_COMPARE_MED     (1 << 1)
#define BGP_FLAG_MED_CONFED             (1 << 2)
#define BGP_FLAG_MED_MISSING_AS_WORST   (1 << 3)
#define BGP_FLAG_NO_CLIENT_TO_CLIENT    (1 << 4)
#define BGP_FLAG_NO_DEFAULT_IPV4        (1 << 5)
#define BGP_FLAG_GR_PRESERVE_FWD        (1 << 6)

/* Forward declarations */
struct peer;
struct bgp;
struct bgp_path_info;

/*
 * BGP FSM States (RFC 4271 Section 8.2.2)
 *
 *   IDLE         - Initial state, no connection
 *   CONNECT      - Waiting for TCP connection
 *   ACTIVE       - Trying to initiate TCP connection
 *   OPENSENT     - TCP connected, OPEN sent, waiting for peer OPEN
 *   OPENCONFIRM  - OPEN received, waiting for KEEPALIVE
 *   ESTABLISHED  - Session established, routes exchanged
 */
typedef enum bgp_fsm_state {
    BGP_STATUS_IDLE = 1,
    BGP_STATUS_CONNECT,
    BGP_STATUS_ACTIVE,
    BGP_STATUS_OPENSENT,
    BGP_STATUS_OPENCONFIRM,
    BGP_STATUS_ESTABLISHED,
    BGP_STATUS_MAX
} bgp_fsm_state_t;

/* BGP Instance */
struct bgp {
    /* AS Number */
    uint32_t as;

    /* Router ID */
    struct in_addr router_id;
    bool router_id_static;

    /* Instance name (for VRF) */
    char *name;

    /* Flags */
    uint32_t flags;

    /* Neighbors */
    struct list *peer;
    struct hash *peerhash;

    /* Route tables (per AFI/SAFI) */
    struct route_table *rib[AFI_MAX][SAFI_MAX];

    /* Event loop */
    struct event_loop *master;

    /* Graceful Restart */
    uint16_t gr_restart_time;
    uint16_t gr_stale_time;
    uint32_t gr_flags;

    /* Route Reflector */
    struct in_addr cluster_id;
    bool cluster_id_set;
    bool rr_enabled;

    /* Confederation */
    uint32_t confed_id;
    uint32_t *confed_peers;
    int confed_peers_cnt;

    /* Statistics */
    uint64_t established_peers;
    uint64_t updates_sent;
    uint64_t updates_received;
};

/* BGP Master (global state) */
struct bgp_master {
    struct list *bgp;           /* List of BGP instances */
    struct event_loop *master;  /* Global event loop */

    /* Listening sockets */
    int listen_fd;
    int listen_fd6;

    /* Start time */
    time_t start_time;
};

/* Global BGP master */
extern struct bgp_master *bm;

/* BGP instance management */
struct bgp *bgp_create(uint32_t as, const char *name);
void bgp_delete(struct bgp *bgp);
struct bgp *bgp_lookup(uint32_t as, const char *name);
struct bgp *bgp_lookup_by_name(const char *name);
struct bgp *bgp_get_default(void);

/* BGP master management */
void bgp_master_init(struct event_loop *master);
void bgp_master_finish(void);

/* Router ID */
void bgp_router_id_set(struct bgp *bgp, struct in_addr *router_id);
void bgp_router_id_unset(struct bgp *bgp);

/* Cluster ID (Route Reflector) */
void bgp_cluster_id_set(struct bgp *bgp, struct in_addr *cluster_id);
void bgp_cluster_id_unset(struct bgp *bgp);

/* Graceful Restart configuration */
void bgp_gr_enable(struct bgp *bgp, uint16_t restart_time, uint16_t stale_time);
void bgp_gr_disable(struct bgp *bgp);
bool bgp_gr_is_enabled(struct bgp *bgp);

/* Timer functions */
void bgp_timers_set(struct bgp *bgp, uint16_t holdtime, uint16_t keepalive);

/* Route table access */
struct route_table *bgp_table_get(struct bgp *bgp, uint8_t afi, uint8_t safi);

#ifdef __cplusplus
}
#endif

#endif /* _BGP_BGPD_H */
