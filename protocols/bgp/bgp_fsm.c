/*
 * BGP Finite State Machine Implementation
 *
 * This implementation follows RFC 4271 Section 8.
 */

#include "bgp_fsm.h"
#include "bgp_packet.h"
#include "bgp_route.h"
#include "common/log.h"

#include <string.h>

#undef LOG_TAG
#define LOG_TAG "BGP"

/* State names */
static const char *state_names[] = {
    [BGP_STATUS_IDLE]        = "Idle",
    [BGP_STATUS_CONNECT]     = "Connect",
    [BGP_STATUS_ACTIVE]      = "Active",
    [BGP_STATUS_OPENSENT]    = "OpenSent",
    [BGP_STATUS_OPENCONFIRM] = "OpenConfirm",
    [BGP_STATUS_ESTABLISHED] = "Established",
};

/* Event names */
static const char *event_names[] = {
    [BGP_EVENT_START]               = "Start",
    [BGP_EVENT_STOP]                = "Stop",
    [BGP_EVENT_CONNECT_RETRY_TIMER] = "ConnectRetryTimer",
    [BGP_EVENT_HOLD_TIMER]          = "HoldTimer",
    [BGP_EVENT_KEEPALIVE_TIMER]     = "KeepaliveTimer",
    [BGP_EVENT_DELAY_OPEN_TIMER]    = "DelayOpenTimer",
    [BGP_EVENT_IDLE_HOLD_TIMER]     = "IdleHoldTimer",
    [BGP_EVENT_TCP_CONNECT_SUCCESS] = "TcpConnectSuccess",
    [BGP_EVENT_TCP_CONNECT_FAIL]    = "TcpConnectFail",
    [BGP_EVENT_TCP_FATAL_ERROR]     = "TcpFatalError",
    [BGP_EVENT_TCP_ACCEPT]          = "TcpAccept",
    [BGP_EVENT_TCP_CLOSE]           = "TcpClose",
    [BGP_EVENT_OPEN_RCVD]           = "OpenReceived",
    [BGP_EVENT_OPEN_COLLISION]      = "OpenCollision",
    [BGP_EVENT_HEADER_ERR]          = "HeaderError",
    [BGP_EVENT_OPEN_ERR]            = "OpenError",
    [BGP_EVENT_NOTIFY_RCVD]         = "NotifyReceived",
    [BGP_EVENT_NOTIFY_MSG_ERR]      = "NotifyMsgError",
    [BGP_EVENT_KEEPALIVE_RCVD]      = "KeepaliveReceived",
    [BGP_EVENT_UPDATE_RCVD]         = "UpdateReceived",
    [BGP_EVENT_UPDATE_ERR]          = "UpdateError",
    [BGP_EVENT_ROUTE_REFRESH_RCVD]  = "RouteRefreshReceived",
    [BGP_EVENT_GR_TIMER]            = "GRTimer",
    [BGP_EVENT_GR_LLGR_TIMER]       = "LLGRTimer",
    [BGP_EVENT_GR_RESTART]          = "GRRestart",
};

/* FSM hooks */
#define MAX_FSM_HOOKS 8
static bgp_fsm_hook_t fsm_hooks[MAX_FSM_HOOKS];
static int num_fsm_hooks = 0;

const char *bgp_state_str(bgp_fsm_state_t state)
{
    if (state > 0 && state < BGP_STATUS_MAX)
        return state_names[state];
    return "Unknown";
}

const char *bgp_event_str(bgp_fsm_event_t event)
{
    if (event > 0 && event < BGP_EVENT_MAX)
        return event_names[event];
    return "Unknown";
}

void bgp_fsm_register_hook(bgp_fsm_hook_t hook)
{
    if (num_fsm_hooks < MAX_FSM_HOOKS)
        fsm_hooks[num_fsm_hooks++] = hook;
}

void bgp_fsm_unregister_hook(bgp_fsm_hook_t hook)
{
    for (int i = 0; i < num_fsm_hooks; i++) {
        if (fsm_hooks[i] == hook) {
            memmove(&fsm_hooks[i], &fsm_hooks[i + 1],
                    (num_fsm_hooks - i - 1) * sizeof(bgp_fsm_hook_t));
            num_fsm_hooks--;
            return;
        }
    }
}

static void fsm_call_hooks(struct peer *peer, bgp_fsm_state_t old_state,
                           bgp_fsm_state_t new_state)
{
    for (int i = 0; i < num_fsm_hooks; i++) {
        if (fsm_hooks[i])
            fsm_hooks[i](peer, old_state, new_state);
    }
}

static void fsm_change_state(struct peer *peer, bgp_fsm_state_t new_state)
{
    bgp_fsm_state_t old_state = peer->state;

    if (old_state == new_state)
        return;

    log_info("BGP peer %s state changed: %s -> %s",
             peer->host, bgp_state_str(old_state), bgp_state_str(new_state));

    peer->state = new_state;

    /* Call registered hooks */
    fsm_call_hooks(peer, old_state, new_state);

    /* Update statistics */
    if (new_state == BGP_STATUS_ESTABLISHED) {
        peer->established_count++;
        peer->bgp->established_peers++;
    } else if (old_state == BGP_STATUS_ESTABLISHED) {
        if (peer->bgp->established_peers > 0)
            peer->bgp->established_peers--;
    }
}

/* Timer callbacks */
void bgp_connect_retry_timer(struct event *event)
{
    struct peer *peer = EVENT_ARG(event);
    peer->t_connect_retry = NULL;

    log_debug("BGP peer %s connect retry timer expired", peer->host);
    bgp_fsm_event(peer, BGP_EVENT_CONNECT_RETRY_TIMER);
}

void bgp_hold_timer(struct event *event)
{
    struct peer *peer = EVENT_ARG(event);
    peer->t_holdtime = NULL;

    log_warn("BGP peer %s hold timer expired", peer->host);
    bgp_fsm_event(peer, BGP_EVENT_HOLD_TIMER);
}

void bgp_keepalive_timer(struct event *event)
{
    struct peer *peer = EVENT_ARG(event);
    peer->t_keepalive = NULL;

    bgp_fsm_event(peer, BGP_EVENT_KEEPALIVE_TIMER);
}

void bgp_timer_set_connect_retry(struct peer *peer)
{
    event_cancel(&peer->t_connect_retry);
    peer->t_connect_retry = EVENT_ADD_TIMER(peer->bgp->master,
                                            bgp_connect_retry_timer,
                                            peer,
                                            peer->connect_retry * 1000);
}

void bgp_timer_set_holdtime(struct peer *peer)
{
    if (peer->holdtime == 0)
        return;

    event_cancel(&peer->t_holdtime);
    peer->t_holdtime = EVENT_ADD_TIMER(peer->bgp->master,
                                       bgp_hold_timer,
                                       peer,
                                       peer->holdtime * 1000);
}

void bgp_timer_set_keepalive(struct peer *peer)
{
    if (peer->keepalive == 0)
        return;

    event_cancel(&peer->t_keepalive);
    peer->t_keepalive = EVENT_ADD_TIMER(peer->bgp->master,
                                        bgp_keepalive_timer,
                                        peer,
                                        peer->keepalive * 1000);
}

void bgp_fsm_timer_set(struct peer *peer)
{
    switch (peer->state) {
    case BGP_STATUS_IDLE:
        event_cancel(&peer->t_holdtime);
        event_cancel(&peer->t_keepalive);
        break;

    case BGP_STATUS_CONNECT:
    case BGP_STATUS_ACTIVE:
        bgp_timer_set_connect_retry(peer);
        break;

    case BGP_STATUS_OPENSENT:
    case BGP_STATUS_OPENCONFIRM:
        bgp_timer_set_holdtime(peer);
        break;

    case BGP_STATUS_ESTABLISHED:
        bgp_timer_set_holdtime(peer);
        bgp_timer_set_keepalive(peer);
        break;

    default:
        break;
    }
}

void bgp_fsm_timer_cancel(struct peer *peer)
{
    event_cancel(&peer->t_connect_retry);
    event_cancel(&peer->t_holdtime);
    event_cancel(&peer->t_keepalive);
    event_cancel(&peer->t_gr_stale);
    event_cancel(&peer->t_gr_restart);
}

/* FSM state transition functions */
int bgp_fsm_to_idle(struct peer *peer, int reason)
{
    (void)reason;
    bgp_fsm_timer_cancel(peer);

    /* Close TCP connection */
    peer_tcp_close(peer);

    /* Clear work queues */
    peer_clear_buffers(peer);

    fsm_change_state(peer, BGP_STATUS_IDLE);
    return 0;
}

int bgp_fsm_to_connect(struct peer *peer)
{
    fsm_change_state(peer, BGP_STATUS_CONNECT);
    bgp_timer_set_connect_retry(peer);

    /* Initiate TCP connection */
    if (peer_tcp_connect(peer) < 0) {
        log_debug("BGP peer %s TCP connect failed, trying again", peer->host);
        fsm_change_state(peer, BGP_STATUS_ACTIVE);
    }

    return 0;
}

int bgp_fsm_to_active(struct peer *peer)
{
    fsm_change_state(peer, BGP_STATUS_ACTIVE);
    bgp_timer_set_connect_retry(peer);
    return 0;
}

int bgp_fsm_to_opensent(struct peer *peer)
{
    /* Send OPEN message */
    if (bgp_open_send(peer) < 0) {
        log_err("BGP peer %s failed to send OPEN", peer->host);
        return -1;
    }

    fsm_change_state(peer, BGP_STATUS_OPENSENT);
    bgp_timer_set_holdtime(peer);
    return 0;
}

int bgp_fsm_to_openconfirm(struct peer *peer)
{
    /* Send KEEPALIVE in response to OPEN */
    if (bgp_keepalive_send(peer) < 0) {
        log_err("BGP peer %s failed to send KEEPALIVE", peer->host);
        return -1;
    }

    fsm_change_state(peer, BGP_STATUS_OPENCONFIRM);
    return 0;
}

int bgp_fsm_to_established(struct peer *peer)
{
    fsm_change_state(peer, BGP_STATUS_ESTABLISHED);

    log_notice("BGP peer %s session established", peer->host);

    /* Set operational timers */
    bgp_timer_set_holdtime(peer);
    bgp_timer_set_keepalive(peer);

    /* Send initial UPDATE (RIB sync) */
    /* TODO: peer_start_rib_sync(peer); */

    return 0;
}

/*
 * Main FSM Event Handler
 *
 * This is the core FSM implementation. Each state handles events
 * according to RFC 4271 Section 8.2.2.
 */
int bgp_fsm_event(struct peer *peer, bgp_fsm_event_t event)
{
    log_debug("BGP peer %s FSM event: %s (state=%s)",
              peer->host, bgp_event_str(event), bgp_state_str(peer->state));

    switch (peer->state) {
    case BGP_STATUS_IDLE:
        switch (event) {
        case BGP_EVENT_START:
            return bgp_fsm_to_connect(peer);
        default:
            /* Ignore other events in Idle */
            break;
        }
        break;

    case BGP_STATUS_CONNECT:
        switch (event) {
        case BGP_EVENT_STOP:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);

        case BGP_EVENT_CONNECT_RETRY_TIMER:
            peer_tcp_close(peer);
            return bgp_fsm_to_connect(peer);

        case BGP_EVENT_TCP_CONNECT_SUCCESS:
        case BGP_EVENT_TCP_ACCEPT:
            return bgp_fsm_to_opensent(peer);

        case BGP_EVENT_TCP_CONNECT_FAIL:
            return bgp_fsm_to_active(peer);

        default:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_FSM_ERR);
        }
        break;

    case BGP_STATUS_ACTIVE:
        switch (event) {
        case BGP_EVENT_STOP:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);

        case BGP_EVENT_CONNECT_RETRY_TIMER:
            return bgp_fsm_to_connect(peer);

        case BGP_EVENT_TCP_ACCEPT:
            return bgp_fsm_to_opensent(peer);

        case BGP_EVENT_TCP_CONNECT_SUCCESS:
            return bgp_fsm_to_opensent(peer);

        default:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_FSM_ERR);
        }
        break;

    case BGP_STATUS_OPENSENT:
        switch (event) {
        case BGP_EVENT_STOP:
            bgp_notify_send(peer, BGP_NOTIFY_CEASE,
                            BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);

        case BGP_EVENT_HOLD_TIMER:
            bgp_notify_send(peer, BGP_NOTIFY_HOLD_ERR, 0);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_HOLD_ERR);

        case BGP_EVENT_TCP_CLOSE:
        case BGP_EVENT_TCP_FATAL_ERROR:
            return bgp_fsm_to_active(peer);

        case BGP_EVENT_OPEN_RCVD:
            return bgp_fsm_to_openconfirm(peer);

        case BGP_EVENT_HEADER_ERR:
        case BGP_EVENT_OPEN_ERR:
            /* Error already sent notification */
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_OPEN_ERR);

        case BGP_EVENT_OPEN_COLLISION:
            bgp_notify_send(peer, BGP_NOTIFY_CEASE,
                            BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        case BGP_EVENT_NOTIFY_RCVD:
        case BGP_EVENT_NOTIFY_MSG_ERR:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        default:
            bgp_notify_send(peer, BGP_NOTIFY_FSM_ERR, 0);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_FSM_ERR);
        }
        break;

    case BGP_STATUS_OPENCONFIRM:
        switch (event) {
        case BGP_EVENT_STOP:
            bgp_notify_send(peer, BGP_NOTIFY_CEASE,
                            BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);

        case BGP_EVENT_HOLD_TIMER:
            bgp_notify_send(peer, BGP_NOTIFY_HOLD_ERR, 0);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_HOLD_ERR);

        case BGP_EVENT_TCP_CLOSE:
        case BGP_EVENT_TCP_FATAL_ERROR:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        case BGP_EVENT_KEEPALIVE_RCVD:
            return bgp_fsm_to_established(peer);

        case BGP_EVENT_KEEPALIVE_TIMER:
            bgp_keepalive_send(peer);
            return 0;

        case BGP_EVENT_NOTIFY_RCVD:
        case BGP_EVENT_NOTIFY_MSG_ERR:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        case BGP_EVENT_OPEN_COLLISION:
            bgp_notify_send(peer, BGP_NOTIFY_CEASE,
                            BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        default:
            bgp_notify_send(peer, BGP_NOTIFY_FSM_ERR, 0);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_FSM_ERR);
        }
        break;

    case BGP_STATUS_ESTABLISHED:
        switch (event) {
        case BGP_EVENT_STOP:
            bgp_notify_send(peer, BGP_NOTIFY_CEASE,
                            BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN);

        case BGP_EVENT_HOLD_TIMER:
            bgp_notify_send(peer, BGP_NOTIFY_HOLD_ERR, 0);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_HOLD_ERR);

        case BGP_EVENT_KEEPALIVE_TIMER:
            bgp_keepalive_send(peer);
            bgp_timer_set_keepalive(peer);
            return 0;

        case BGP_EVENT_KEEPALIVE_RCVD:
            /* Reset hold timer */
            bgp_timer_set_holdtime(peer);
            return 0;

        case BGP_EVENT_UPDATE_RCVD:
            /* Reset hold timer */
            bgp_timer_set_holdtime(peer);
            /* Process update handled elsewhere */
            return 0;

        case BGP_EVENT_UPDATE_ERR:
            /* Notification already sent */
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_UPDATE_ERR);

        case BGP_EVENT_TCP_CLOSE:
        case BGP_EVENT_TCP_FATAL_ERROR:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        case BGP_EVENT_NOTIFY_RCVD:
        case BGP_EVENT_NOTIFY_MSG_ERR:
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        case BGP_EVENT_ROUTE_REFRESH_RCVD:
            /* Handle route refresh */
            return 0;

        case BGP_EVENT_OPEN_COLLISION:
            bgp_notify_send(peer, BGP_NOTIFY_CEASE,
                            BGP_NOTIFY_CEASE_COLLISION_RESOLUTION);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE);

        default:
            bgp_notify_send(peer, BGP_NOTIFY_FSM_ERR, 0);
            return bgp_fsm_to_idle(peer, BGP_NOTIFY_FSM_ERR);
        }
        break;

    default:
        log_err("BGP peer %s invalid state %d", peer->host, peer->state);
        return bgp_fsm_to_idle(peer, BGP_NOTIFY_FSM_ERR);
    }

    return 0;
}

/*
 * Graceful Restart Functions
 *
 * TODO: These are stub implementations. Candidates implementing the
 * SE-level assessment should complete these functions.
 */

bool bgp_peer_gr_capable(struct peer *peer)
{
    /* TODO: Check peer's GR capability from OPEN message */
    return peer->gr_restart_time > 0;
}

void bgp_gr_stale_routes(struct peer *peer)
{
    if (!peer || !peer->bgp)
        return;

    struct bgp *bgp = peer->bgp;
    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            struct route_table *table = bgp_table_get(bgp, afi, safi);
            if (!table)
                continue;

            struct route_node *rn;
            for (rn = route_top(table); rn; rn = route_next(rn)) {
                struct bgp_route_info *ri = rn->info;
                if (!ri)
                    continue;

                for (struct bgp_path_info *pi = ri->paths; pi; pi = pi->next) {
                    if (pi->peer == peer) {
                        bgp_path_info_set_flag(pi, BGP_PATH_STALE);
                    }
                }
            }
        }
    }
    log_info("BGP peer %s: marked all routes as STALE", peer->host);
}

void bgp_gr_clear_stale_routes(struct peer *peer)
{
    /*
     * TODO: Remove stale routes when GR timer expires
     *
     * This is called when:
     * 1. GR stale timer expires
     * 2. Session is explicitly cleared
     */
    log_debug("BGP peer %s clearing stale routes (TODO)", peer->host);
}

void bgp_gr_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi)
{
    /*
     * TODO: Process End-of-RIB marker
     *
     * End-of-RIB is signaled by an empty UPDATE message.
     * When received:
     * 1. Mark peer as converged for this AFI/SAFI
     * 2. If all AFI/SAFI converged, clear stale routes
     */
    log_debug("BGP peer %s End-of-RIB for afi=%d safi=%d (TODO)",
              peer->host, afi, safi);
}
