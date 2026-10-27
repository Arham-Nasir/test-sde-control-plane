/*
 * BGP Graceful Restart Implementation
 *
 * SE-Level Assessment: Implement BGP Graceful Restart (RFC 4724)
 *
 * This file contains the skeleton for your implementation.
 * See TASK.md for detailed requirements.
 *
 * Author: [Your Name]
 * Date: [Date]
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

/*
 * =============================================================================
 * Data Structures
 * =============================================================================
 */

/*
 * TODO: Define GR-specific data structures
 *
 * Consider:
 * - Per-peer GR state tracking
 * - Per-AFI/SAFI GR information
 * - Timer references
 */

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
 *
 * TODO: Implement capability encoding per RFC 4724 Section 3
 *
 * Capability format:
 *   - 2 bytes: Restart Flags (4 bits) + Restart Time (12 bits)
 *   - For each AFI/SAFI:
 *     - 2 bytes: AFI
 *     - 1 byte: SAFI
 *     - 1 byte: Flags (F-bit for forwarding state)
 */
int bgp_gr_capability_encode(struct stream *s, struct peer *peer)
{
    (void)s;
    /* TODO: Implement */
    log_debug("GR: Encoding capability for peer %s (TODO)", peer->host);
    return 0;
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
 *
 * TODO: Parse the GR capability and store in peer structure
 */
int bgp_gr_capability_parse(struct peer *peer, uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
    /* TODO: Implement */
    log_debug("GR: Parsing capability from peer %s (TODO)", peer->host);
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
 *
 * When a GR-capable peer's session goes down, we mark routes as stale
 * rather than deleting them. This preserves forwarding state.
 *
 * TODO: Iterate through all route tables and mark paths from this peer
 */
void bgp_gr_mark_routes_stale(struct peer *peer)
{
    /* TODO: Implement */
    log_info("GR: Marking routes as stale for peer %s (TODO)", peer->host);
}

/*
 * bgp_gr_clear_stale_route - Clear stale flag from a route
 *
 * @peer: Peer that sent the route update
 * @rn: Route node
 *
 * Called when we receive an UPDATE for a route during GR.
 * The route is no longer stale since we got a fresh update.
 *
 * TODO: Find the path from this peer and clear the stale flag
 */
void bgp_gr_clear_stale_route(struct peer *peer, struct route_node *rn)
{
    (void)peer;
    (void)rn;
    /* TODO: Implement */
}

/*
 * bgp_gr_delete_stale_routes - Delete all remaining stale routes
 *
 * @peer: Peer whose stale routes should be deleted
 *
 * Called when:
 * 1. Restart timer expires (peer didn't reconnect)
 * 2. End-of-RIB received (routes not refreshed are gone)
 *
 * TODO: Delete all paths from this peer that still have STALE flag
 */
void bgp_gr_delete_stale_routes(struct peer *peer)
{
    /* TODO: Implement */
    log_info("GR: Deleting stale routes for peer %s (TODO)", peer->host);
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
 *
 * The peer didn't reconnect within the restart time.
 * We must delete all stale routes and exit helper mode.
 *
 * TODO: Clean up stale routes and reset GR state
 */
__attribute__((unused))
static void bgp_gr_restart_timer_expire(struct event *event)
{
    struct peer *peer = EVENT_ARG(event);

    log_warn("GR: Restart timer expired for peer %s", peer->host);

    /* TODO: Implement
     * 1. Delete stale routes
     * 2. Reset GR state
     * 3. Log the event
     */
}

/*
 * bgp_gr_start_restart_timer - Start the restart timer
 *
 * @peer: Peer whose restart timer to start
 *
 * Called when a GR-capable peer's session goes down.
 *
 * TODO: Start timer with negotiated restart time
 */
void bgp_gr_start_restart_timer(struct peer *peer)
{
    /* TODO: Implement */
    log_info("GR: Starting restart timer for peer %s (TODO)", peer->host);
}

/*
 * bgp_gr_stop_restart_timer - Stop the restart timer
 *
 * @peer: Peer whose restart timer to stop
 *
 * Called when peer reconnects or session is cleared.
 *
 * TODO: Cancel the timer if running
 */
void bgp_gr_stop_restart_timer(struct peer *peer)
{
    (void)peer;
    /* TODO: Implement */
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
 *
 * End-of-RIB is signaled by an empty UPDATE message.
 * When received, we know the peer has finished sending all its routes.
 *
 * TODO: Track EOR per AFI/SAFI and clean up when all received
 */
void bgp_gr_receive_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi)
{
    log_info("GR: Received End-of-RIB from %s for afi=%d safi=%d",
             peer->host, afi, safi);

    /* TODO: Implement
     * 1. Mark EOR received for this AFI/SAFI
     * 2. Check if all AFI/SAFI have received EOR
     * 3. If all received, delete remaining stale routes
     * 4. Cancel restart timer
     * 5. Exit helper mode
     */
}

/*
 * bgp_gr_send_end_of_rib - Send End-of-RIB marker
 *
 * @peer: Peer to send EOR to
 * @afi: Address Family Identifier
 * @safi: Subsequent Address Family Identifier
 *
 * Send an empty UPDATE to signal we've finished sending routes.
 *
 * TODO: Use bgp_end_of_rib_send() from bgp_packet.c
 */
void bgp_gr_send_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi)
{
    log_info("GR: Sending End-of-RIB to %s for afi=%d safi=%d",
             peer->host, afi, safi);

    /* TODO: Implement */
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
 * Called from FSM when transitioning to Idle.
 * Determine if we should enter helper mode.
 *
 * Returns: true if we entered helper mode, false otherwise
 */
bool bgp_gr_peer_down(struct peer *peer, int reason)
{
    /* TODO: Implement
     *
     * Enter helper mode if:
     * 1. GR was negotiated with this peer
     * 2. Reason is not administrative (hard reset)
     * 3. Peer indicated forwarding state preserved
     *
     * In helper mode:
     * 1. Mark routes as stale
     * 2. Start restart timer
     * 3. Set helper mode flag
     */

    log_debug("GR: Peer %s down, reason=%d (TODO)", peer->host, reason);
    return false;
}

/*
 * bgp_gr_peer_reconnect - Handle peer reconnecting
 *
 * @peer: Peer that reconnected
 *
 * Called when OPEN received from a peer in helper mode.
 *
 * TODO: Handle the restart scenario
 */
void bgp_gr_peer_reconnect(struct peer *peer)
{
    /* TODO: Implement
     *
     * 1. Check if peer has R-bit set (it's restarting)
     * 2. If so, we continue in helper mode
     * 3. Wait for route updates and EOR
     * 4. Clear stale flag on received routes
     */

    log_info("GR: Peer %s reconnected (TODO)", peer->host);
}

/*
 * =============================================================================
 * Initialization
 * =============================================================================
 */

/*
 * bgp_gr_init - Initialize GR module
 *
 * Called during BGP daemon startup.
 *
 * TODO: Register FSM hooks, initialize data structures
 */
void bgp_gr_init(void)
{
    log_info("GR: Graceful Restart module initialized");

    /* TODO: Register FSM hooks if needed */
}

/*
 * bgp_gr_finish - Cleanup GR module
 *
 * Called during BGP daemon shutdown.
 */
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
 *
 * @peer: Peer to check
 *
 * Returns: true if GR is enabled and negotiated
 */
bool bgp_peer_gr_is_enabled(struct peer *peer)
{
    /* TODO: Check peer flags and negotiated state */
    return (peer->flags & PEER_FLAG_GRACEFUL_RESTART) &&
           (peer->cap_flags & PEER_CAP_GRACEFUL_RESTART);
}

/*
 * bgp_gr_in_helper_mode - Check if we're in helper mode for peer
 *
 * @peer: Peer to check
 *
 * Returns: true if in helper mode
 */
bool bgp_gr_in_helper_mode(struct peer *peer)
{
    (void)peer;
    /* TODO: Check helper mode state */
    return false;
}

/*
 * bgp_gr_restart_time_remaining - Get remaining restart time
 *
 * @peer: Peer to check
 *
 * Returns: Seconds remaining, or 0 if not in restart
 */
uint16_t bgp_gr_restart_time_remaining(struct peer *peer)
{
    (void)peer;
    /* TODO: Calculate remaining time */
    return 0;
}
