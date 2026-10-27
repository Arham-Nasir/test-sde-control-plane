/*
 * BGP Graceful Restart Implementation
 *
 * SE-Level Assessment: Implement BGP Graceful Restart (RFC 4724)
 *
 * Author: Muhammad Arham Nasir
 * Date: October 2026
 */

#include "protocols/bgp/bgpd.h"
#include "protocols/bgp/bgp_fsm.h"
#include "protocols/bgp/bgp_packet.h"
#include "protocols/bgp/bgp_neighbor.h"
#include "protocols/bgp/bgp_route.h"
#include "common/log.h"
#include "common/thread.h"

#undef LOG_TAG
#define LOG_TAG "BGP-GR"

/* Forward declarations */
static void bgp_gr_restart_timer_expire(struct event *event);
bool bgp_peer_gr_is_enabled(struct peer *peer);

/*
 * =============================================================================
 * Data Structures
 * =============================================================================
 */

/* Per-peer GR state tracking */
struct bgp_gr_info {
    bool gr_negotiated;           /* GR successfully negotiated */
    uint16_t restart_time;        /* Negotiated restart time (seconds) */

    /* Per-AFI/SAFI state */
    struct {
        bool capable;             /* Peer advertised GR for this AFI/SAFI */
        bool forwarding_state;    /* F-bit from peer */
        bool eor_received;        /* End-of-RIB received */
        bool eor_sent;            /* End-of-RIB sent */
    } af[AFI_MAX][SAFI_MAX];

    /* State flags */
    bool restarting;              /* Local speaker is restarting */
    bool peer_restarting;         /* Peer speaker is restarting */
    bool helper_mode;             /* Helper mode active for this peer */

    /* Timestamps */
    time_t restart_start_time;
};

/*
 * =============================================================================
 * GR Capability Encoding
 * =============================================================================
 */

/*
 * bgp_gr_capability_encode - Encode GR capability for OPEN message
 *
 * @s: Stream to write to
 * @peer: Peer sending the OPEN
 *
 * Returns: Number of bytes written, or -1 on error
 */
int bgp_gr_capability_encode(struct stream *s, struct peer *peer)
{
    if (s == NULL || peer == NULL || peer->bgp == NULL)
        return -1;

    size_t start_pos = stream_get_endp(s);
    uint16_t restart_time = peer->bgp->gr_restart_time;
    uint8_t restart_flags = 0;

    if (peer->bgp->gr_flags & BGP_FLAG_GR_PRESERVE_FWD)
        restart_flags |= BGP_GR_FLAG_RESTART;

    /* Count active AFI/SAFIs */
    uint8_t afi_count = 0;
    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            if (peer->af[afi][safi].active)
                afi_count++;
        }
    }

    if (afi_count == 0)
        afi_count = 1; /* Default to IPv4 Unicast */

    uint8_t cap_len = 2 + (afi_count * 4);

    stream_putc(s, BGP_CAP_CODE_GR);
    stream_putc(s, cap_len);
    /* RFC 4724 Section 3: 4-bit Restart Flags in high nibble of octet 0, 12-bit Restart Time */
    stream_putw(s, ((uint16_t)restart_flags << 8) | (restart_time & 0x0FFF));

    bool encoded_any = false;
    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            if (peer->af[afi][safi].active) {
                stream_putw(s, afi);
                stream_putc(s, safi);
                stream_putc(s, BGP_GR_FLAG_FORWARDING);
                encoded_any = true;
            }
        }
    }

    if (!encoded_any) {
        stream_putw(s, AFI_IP);
        stream_putc(s, SAFI_UNICAST);
        stream_putc(s, BGP_GR_FLAG_FORWARDING);
    }

    log_debug("GR: Encoded capability for peer %s (len=%u)", peer->host, cap_len);
    return (int)(stream_get_endp(s) - start_pos);
}

/*
 * =============================================================================
 * GR Capability Parsing
 * =============================================================================
 */

/*
 * bgp_gr_capability_parse - Parse GR capability from OPEN message
 *
 * @peer: Peer that sent the OPEN
 * @data: Capability data
 * @length: Length of capability data
 *
 * Returns: 0 on success, -1 on error
 */
int bgp_gr_capability_parse(struct peer *peer, uint8_t *data, size_t length)
{
    if (peer == NULL || data == NULL || length < 2)
        return -1;

    /* RFC 4724 Section 3: High 4 bits of octet 0 are flags (R-bit, N-bit); 12 bits are restart time */
    peer->gr_flags = data[0] & 0xF0;
    peer->gr_restart_time = ((uint16_t)(data[0] & 0x0F) << 8) | data[1];
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    size_t pos = 2;
    while (pos + 4 <= length) {
        uint16_t afi = (data[pos] << 8) | data[pos + 1];
        uint8_t safi = data[pos + 2];
        (void)data[pos + 3]; /* Flags / F-bit */

        if (afi < AFI_MAX && safi < SAFI_MAX) {
            if (peer->gr_eor_received)
                peer->gr_eor_received[afi * SAFI_MAX + safi] = false;
        }
        pos += 4;
    }

    log_debug("GR: Parsed capability from peer %s: restart_time=%u flags=0x%x",
              peer->host, peer->gr_restart_time, peer->gr_flags);
    return 0;
}

/*
 * =============================================================================
 * Stale Route Management
 * =============================================================================
 */

/*
 * bgp_gr_mark_routes_stale - Mark all routes from peer as stale
 *
 * @peer: Peer whose routes should be marked
 */
void bgp_gr_mark_routes_stale(struct peer *peer)
{
    if (peer == NULL || peer->bgp == NULL)
        return;

    struct bgp *bgp = peer->bgp;
    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            struct route_table *table = bgp_table_get(bgp, afi, safi);
            if (table == NULL)
                continue;

            struct route_node *rn;
            for (rn = route_top(table); rn; rn = route_next(rn)) {
                struct bgp_route_info *ri = rn->info;
                if (ri == NULL)
                    continue;

                for (struct bgp_path_info *pi = ri->paths; pi; pi = pi->next) {
                    if (pi->peer == peer) {
                        bgp_path_info_set_flag(pi, BGP_PATH_STALE);
                    }
                }
            }
        }
    }

    log_info("GR: Marked all routes from peer %s as STALE", peer->host);
}

/*
 * bgp_gr_clear_stale_route - Clear stale flag from a route
 *
 * @peer: Peer that sent the route update
 * @rn: Route node
 */
void bgp_gr_clear_stale_route(struct peer *peer, struct route_node *rn)
{
    if (peer == NULL || rn == NULL || rn->info == NULL)
        return;

    struct bgp_route_info *ri = rn->info;
    for (struct bgp_path_info *pi = ri->paths; pi; pi = pi->next) {
        if (pi->peer == peer) {
            bgp_path_info_unset_flag(pi, BGP_PATH_STALE);
        }
    }
}

/*
 * bgp_gr_delete_stale_routes - Delete all remaining stale routes
 *
 * @peer: Peer whose stale routes should be deleted
 */
void bgp_gr_delete_stale_routes(struct peer *peer)
{
    if (peer == NULL || peer->bgp == NULL)
        return;

    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            bgp_clear_stale_routes(peer, afi, safi);
        }
    }

    log_info("GR: Deleted all stale routes for peer %s", peer->host);
}

/*
 * =============================================================================
 * Timer Management
 * =============================================================================
 */

/*
 * bgp_gr_restart_timer_expire - Callback when restart timer expires
 *
 * @event: Timer event
 */
static void bgp_gr_restart_timer_expire(struct event *event)
{
    struct peer *peer = EVENT_ARG(event);
    if (peer == NULL)
        return;

    peer->t_gr_restart = NULL;
    log_warn("GR: Restart timer expired for peer %s", peer->host);

    /* 1. Delete all remaining unrefreshed stale routes */
    bgp_gr_delete_stale_routes(peer);

    /* 2. Reset Helper Mode state */
    peer->status_flags &= ~PEER_STATUS_NSF_MODE;
}

/*
 * bgp_gr_start_restart_timer - Start the restart timer
 *
 * @peer: Peer whose restart timer to start
 */
void bgp_gr_start_restart_timer(struct peer *peer)
{
    if (peer == NULL || peer->bgp == NULL || peer->bgp->master == NULL)
        return;

    event_cancel(&peer->t_gr_restart);

    uint16_t restart_time = peer->gr_restart_time ? peer->gr_restart_time : 120;
    peer->t_gr_restart = EVENT_ADD_TIMER(peer->bgp->master,
                                         bgp_gr_restart_timer_expire,
                                         peer,
                                         (long)restart_time * 1000);

    log_info("GR: Started restart timer (%u seconds) for peer %s",
             restart_time, peer->host);
}

/*
 * bgp_gr_stop_restart_timer - Stop the restart timer
 *
 * @peer: Peer whose restart timer to stop
 */
void bgp_gr_stop_restart_timer(struct peer *peer)
{
    if (peer == NULL)
        return;

    if (peer->t_gr_restart) {
        event_cancel(&peer->t_gr_restart);
        log_info("GR: Stopped restart timer for peer %s", peer->host);
    }
}

/*
 * =============================================================================
 * End-of-RIB Handling
 * =============================================================================
 */

/*
 * bgp_gr_receive_end_of_rib - Process End-of-RIB marker
 *
 * @peer: Peer that sent the EOR
 * @afi: Address Family Identifier
 * @safi: Subsequent Address Family Identifier
 */
void bgp_gr_receive_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi)
{
    if (peer == NULL)
        return;

    log_info("GR: Received End-of-RIB from %s for afi=%d safi=%d",
             peer->host, afi, safi);

    if (peer->gr_eor_received && afi < AFI_MAX && safi < SAFI_MAX) {
        peer->gr_eor_received[afi * SAFI_MAX + safi] = true;
    }

    /* Check if all active AFI/SAFIs have received EOR */
    bool all_eor = true;
    for (int a = 0; a < AFI_MAX; a++) {
        for (int s = 0; s < SAFI_MAX; s++) {
            if (peer->af[a][s].active) {
                if (peer->gr_eor_received && !peer->gr_eor_received[a * SAFI_MAX + s]) {
                    all_eor = false;
                    break;
                }
            }
        }
        if (!all_eor)
            break;
    }

    if (all_eor) {
        log_info("GR: All End-of-RIB received from %s, clearing stale routes",
                 peer->host);
        bgp_gr_stop_restart_timer(peer);
        bgp_gr_delete_stale_routes(peer);
        peer->status_flags &= ~PEER_STATUS_NSF_MODE;
    }
}

/*
 * bgp_gr_send_end_of_rib - Send End-of-RIB marker
 *
 * @peer: Peer to send EOR to
 * @afi: Address Family Identifier
 * @safi: Subsequent Address Family Identifier
 */
void bgp_gr_send_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi)
{
    if (peer == NULL)
        return;

    log_info("GR: Sending End-of-RIB to %s for afi=%d safi=%d",
             peer->host, afi, safi);
    bgp_end_of_rib_send(peer, afi, safi);
}

/*
 * =============================================================================
 * FSM Integration
 * =============================================================================
 */

/*
 * bgp_gr_peer_down - Handle peer session going down
 *
 * @peer: Peer whose session went down
 * @reason: Reason for session down (notification code)
 *
 * Returns: true if we entered helper mode, false otherwise
 */
bool bgp_gr_peer_down(struct peer *peer, int reason)
{
    if (peer == NULL)
        return false;

    if (bgp_peer_gr_is_enabled(peer) && reason != BGP_NOTIFY_CEASE_ADMIN_RESET) {
        log_info("GR: Peer %s session down (reason=%d), entering Helper Mode",
                 peer->host, reason);
        bgp_gr_mark_routes_stale(peer);
        bgp_gr_start_restart_timer(peer);
        peer->status_flags |= PEER_STATUS_NSF_MODE;
        return true;
    }

    log_debug("GR: Peer %s session down (reason=%d), normal teardown",
              peer->host, reason);
    return false;
}

/*
 * bgp_gr_peer_reconnect - Handle peer reconnecting
 *
 * @peer: Peer that reconnected
 */
void bgp_gr_peer_reconnect(struct peer *peer)
{
    if (peer == NULL)
        return;

    if (peer->gr_flags & BGP_GR_FLAG_RESTART) {
        log_info("GR: Peer %s reconnected with Restart State (R-bit set)", peer->host);
    } else {
        log_info("GR: Peer %s reconnected without Restart State", peer->host);
    }
}

/*
 * =============================================================================
 * Initialization & Teardown
 * =============================================================================
 */

void bgp_gr_init(void)
{
    log_info("GR: Graceful Restart module initialized");
}

void bgp_gr_finish(void)
{
    log_info("GR: Graceful Restart module finished");
}

/*
 * =============================================================================
 * Utility Functions
 * =============================================================================
 */

/*
 * bgp_peer_gr_is_enabled - Check if GR is enabled for a peer
 */
bool bgp_peer_gr_is_enabled(struct peer *peer)
{
    if (peer == NULL)
        return false;

    return (peer->flags & PEER_FLAG_GRACEFUL_RESTART) ||
           (peer->cap_flags & PEER_CAP_GRACEFUL_RESTART) ||
           (peer->gr_restart_time > 0);
}

/*
 * bgp_gr_in_helper_mode - Check if we're in helper mode for peer
 */
bool bgp_gr_in_helper_mode(struct peer *peer)
{
    if (peer == NULL)
        return false;

    return (peer->status_flags & PEER_STATUS_NSF_MODE) != 0;
}

/*
 * bgp_gr_restart_time_remaining - Get remaining restart time
 */
uint16_t bgp_gr_restart_time_remaining(struct peer *peer)
{
    if (peer == NULL || peer->t_gr_restart == NULL)
        return 0;

    return peer->gr_restart_time;
}
