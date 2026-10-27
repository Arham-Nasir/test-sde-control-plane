/*
 * BGP Neighbor/Peer Management
 *
 * Defines the peer structure and peer management functions.
 *
 * Pattern Reference: bgpd/bgpd.h (struct peer) in FRRouting
 */

#ifndef _BGP_NEIGHBOR_H
#define _BGP_NEIGHBOR_H

#include "bgpd.h"
#include "common/stream.h"

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

union sockunion {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

/* Peer flags */
#define PEER_FLAG_SHUTDOWN          (1 << 0)
#define PEER_FLAG_PASSIVE           (1 << 1)
#define PEER_FLAG_DONT_CAPABILITY   (1 << 2)
#define PEER_FLAG_OVERRIDE_CAPABILITY (1 << 3)
#define PEER_FLAG_STRICT_CAP_MATCH  (1 << 4)
#define PEER_FLAG_DYNAMIC_CAPABILITY (1 << 5)
#define PEER_FLAG_DISABLE_CONNECTED_CHECK (1 << 6)
#define PEER_FLAG_CAPABILITY_ENHE   (1 << 7)
#define PEER_FLAG_GRACEFUL_RESTART  (1 << 8)
#define PEER_FLAG_GRACEFUL_RESTART_HELPER (1 << 9)
#define PEER_FLAG_ROUTESERVER_CLIENT (1 << 10)
#define PEER_FLAG_RRCLIENT          (1 << 11)     /* Route Reflector Client */

/* Peer capability flags (negotiated) */
#define PEER_CAP_AS4_ADV            (1 << 0)
#define PEER_CAP_AS4_RCV            (1 << 1)
#define PEER_CAP_MP_IPV4_UNICAST    (1 << 2)
#define PEER_CAP_MP_IPV4_MULTICAST  (1 << 3)
#define PEER_CAP_MP_IPV6_UNICAST    (1 << 4)
#define PEER_CAP_MP_IPV6_MULTICAST  (1 << 5)
#define PEER_CAP_REFRESH_ADV        (1 << 6)
#define PEER_CAP_REFRESH_RCV        (1 << 7)
#define PEER_CAP_GRACEFUL_RESTART   (1 << 8)
#define PEER_CAP_ENHE_ADV           (1 << 9)
#define PEER_CAP_ENHE_RCV           (1 << 10)
#define PEER_CAP_ADDPATH_ADV        (1 << 11)
#define PEER_CAP_ADDPATH_RCV        (1 << 12)

/* Peer status flags */
#define PEER_STATUS_PREFIX_OVERFLOW (1 << 0)
#define PEER_STATUS_CAPABILITY_OPEN (1 << 1)
#define PEER_STATUS_HAVE_ACCEPT     (1 << 2)
#define PEER_STATUS_GROUP           (1 << 3)
#define PEER_STATUS_NSF_MODE        (1 << 4)    /* Non-Stop Forwarding mode */
#define PEER_STATUS_NSF_WAIT        (1 << 5)    /* Waiting for NSF restart */
#define PEER_STATUS_EOR_SEND        (1 << 6)    /* EOR sent for this peer */
#define PEER_STATUS_EOR_RECEIVED    (1 << 7)    /* EOR received from peer */

/* BGP Peer Structure */
struct peer {
    /* Linkage */
    struct bgp *bgp;
    struct listnode *node;

    /* Identification */
    char *host;                     /* Peer hostname/description */
    char *desc;                     /* Description */

    /* Remote information */
    union {
        struct sockaddr sa;
        struct sockaddr_in sin;
        struct sockaddr_in6 sin6;
    } remote_addr;
    uint32_t remote_as;
    struct in_addr remote_id;       /* BGP Identifier */

    /* Local information */
    union {
        struct sockaddr sa;
        struct sockaddr_in sin;
        struct sockaddr_in6 sin6;
    } local_addr;
    struct in_addr local_id;

    /* Socket */
    int fd;

    /* FSM state */
    bgp_fsm_state_t state;

    /* Flags */
    uint32_t flags;
    uint32_t cap_flags;             /* Negotiated capabilities */
    uint32_t status_flags;

    /* Timers (configured) */
    uint16_t holdtime;
    uint16_t keepalive;
    uint16_t connect_retry;

    /* Timers (negotiated) */
    uint16_t v_holdtime;
    uint16_t v_keepalive;

    /* Timer threads */
    struct event *t_connect_retry;
    struct event *t_holdtime;
    struct event *t_keepalive;
    struct event *t_read;
    struct event *t_write;

    /* Graceful Restart */
    uint16_t gr_restart_time;       /* Restart Time from peer */
    uint32_t gr_flags;
    struct event *t_gr_restart;
    struct event *t_gr_stale;
    bool *gr_eor_received;          /* EOR received per AFI/SAFI */

    /* Route Reflector */
    bool rr_client;                 /* This peer is an RR client */

    /* Packet buffers */
    struct stream *ibuf;            /* Input buffer */
    struct stream *obuf;            /* Output buffer */
    struct stream *work;            /* Work buffer */
    struct stream_fifo *obuf_fifo;  /* Output FIFO */

    /* Statistics */
    uint64_t open_in;
    uint64_t open_out;
    uint64_t update_in;
    uint64_t update_out;
    uint64_t keepalive_in;
    uint64_t keepalive_out;
    uint64_t notify_in;
    uint64_t notify_out;
    uint64_t refresh_in;
    uint64_t refresh_out;

    uint64_t established_count;
    uint64_t dropped_count;

    time_t uptime;
    time_t last_read;
    time_t last_write;
    time_t last_reset;
    int last_reset_cause;

    /* Per AFI/SAFI state */
    struct {
        bool active;
        uint64_t prefix_count;
        uint64_t prefix_limit;
    } af[AFI_MAX][SAFI_MAX];
};

/* Peer management */
struct peer *peer_new(struct bgp *bgp);
struct peer *peer_create(struct bgp *bgp, union sockunion *su,
                         uint32_t remote_as);
void peer_delete(struct peer *peer);
struct peer *peer_lookup(struct bgp *bgp, union sockunion *su);
struct peer *peer_lookup_by_hostname(struct bgp *bgp, const char *hostname);

/* Peer configuration */
void peer_flag_set(struct peer *peer, uint32_t flag);
void peer_flag_unset(struct peer *peer, uint32_t flag);
bool peer_flag_check(struct peer *peer, uint32_t flag);

void peer_timers_set(struct peer *peer, uint16_t holdtime, uint16_t keepalive);
void peer_description_set(struct peer *peer, const char *desc);

/* Peer activation per AFI/SAFI */
void peer_activate(struct peer *peer, uint8_t afi, uint8_t safi);
void peer_deactivate(struct peer *peer, uint8_t afi, uint8_t safi);
bool peer_active(struct peer *peer, uint8_t afi, uint8_t safi);

/* Route Reflector client */
void peer_rr_client_set(struct peer *peer);
void peer_rr_client_unset(struct peer *peer);
bool peer_is_rr_client(struct peer *peer);

/* Graceful Restart configuration */
void peer_gr_enable(struct peer *peer);
void peer_gr_disable(struct peer *peer);

/* Administrative operations */
int peer_start(struct peer *peer);
int peer_stop(struct peer *peer);
int peer_clear(struct peer *peer, bool hard);

/* TCP connection management */
int peer_tcp_connect(struct peer *peer);
void peer_tcp_close(struct peer *peer);
int peer_tcp_accept(struct peer *peer, int accept_fd);

/* Buffer management */
void peer_clear_buffers(struct peer *peer);

/* Sockunion utilities */
int sockunion_family(union sockunion *su);
const char *sockunion_str(union sockunion *su, char *buf, size_t size);
bool sockunion_same(union sockunion *a, union sockunion *b);
int sockunion_cmp(union sockunion *a, union sockunion *b);

#ifdef __cplusplus
}
#endif

#endif /* _BGP_NEIGHBOR_H */
