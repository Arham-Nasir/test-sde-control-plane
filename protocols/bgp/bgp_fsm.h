/*
 * BGP Finite State Machine
 *
 * Implements the BGP FSM as defined in RFC 4271 Section 8.
 * This is a key component that candidates may need to extend
 * for Graceful Restart support.
 *
 * Pattern Reference: bgpd/bgp_fsm.h in FRRouting
 */

#ifndef _BGP_FSM_H
#define _BGP_FSM_H

#include "bgpd.h"
#include "bgp_neighbor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Note: bgp_fsm_state_t is defined in bgpd.h */

/*
 * BGP FSM Events (RFC 4271 Section 8.1.2)
 */
typedef enum {
    /* Administrative Events */
    BGP_EVENT_START = 1,            /* Event 1: Manual Start */
    BGP_EVENT_STOP,                 /* Event 2: Manual Stop */

    /* Timer Events */
    BGP_EVENT_CONNECT_RETRY_TIMER,  /* Event 9: ConnectRetryTimer_Expires */
    BGP_EVENT_HOLD_TIMER,           /* Event 10: HoldTimer_Expires */
    BGP_EVENT_KEEPALIVE_TIMER,      /* Event 11: KeepaliveTimer_Expires */
    BGP_EVENT_DELAY_OPEN_TIMER,     /* Event 12: DelayOpenTimer_Expires */
    BGP_EVENT_IDLE_HOLD_TIMER,      /* Event 13: IdleHoldTimer_Expires */

    /* TCP Connection Events */
    BGP_EVENT_TCP_CONNECT_SUCCESS,  /* Event 16: Tcp_CR_Acked */
    BGP_EVENT_TCP_CONNECT_FAIL,     /* Event 17: TcpConnectionFails */
    BGP_EVENT_TCP_FATAL_ERROR,      /* Event 18: TCP fatal error */
    BGP_EVENT_TCP_ACCEPT,           /* Event 14: Tcp_Connection_Confirmed */
    BGP_EVENT_TCP_CLOSE,            /* Connection closed */

    /* BGP Message Events */
    BGP_EVENT_OPEN_RCVD,            /* Event 19: BGPOpen */
    BGP_EVENT_OPEN_COLLISION,       /* Event 23: OpenCollisionDump */
    BGP_EVENT_HEADER_ERR,           /* Event 21: BGPHeaderErr */
    BGP_EVENT_OPEN_ERR,             /* Event 22: BGPOpenMsgErr */
    BGP_EVENT_NOTIFY_RCVD,          /* Event 24: NotifMsgVerErr */
    BGP_EVENT_NOTIFY_MSG_ERR,       /* Event 25: NotifMsg */
    BGP_EVENT_KEEPALIVE_RCVD,       /* Event 26: KeepAliveMsg */
    BGP_EVENT_UPDATE_RCVD,          /* Event 27: UpdateMsg */
    BGP_EVENT_UPDATE_ERR,           /* Event 28: UpdateMsgErr */
    BGP_EVENT_ROUTE_REFRESH_RCVD,   /* Route Refresh received */

    /* Graceful Restart Events (Assessment Extension Point) */
    BGP_EVENT_GR_TIMER,             /* GR stale timer expired */
    BGP_EVENT_GR_LLGR_TIMER,        /* Long-lived GR timer expired */
    BGP_EVENT_GR_RESTART,           /* Peer restarting with GR */

    BGP_EVENT_MAX
} bgp_fsm_event_t;

/* State name lookup */
const char *bgp_state_str(bgp_fsm_state_t state);
const char *bgp_event_str(bgp_fsm_event_t event);

/*
 * FSM State Change Callback
 * Called whenever a peer transitions between FSM states.
 */
typedef void (*bgp_fsm_hook_t)(struct peer *peer,
                               bgp_fsm_state_t old_state,
                               bgp_fsm_state_t new_state);

/* Register/unregister FSM hooks */
void bgp_fsm_register_hook(bgp_fsm_hook_t hook);
void bgp_fsm_unregister_hook(bgp_fsm_hook_t hook);

/*
 * Main FSM Entry Point
 *
 * Process an event for a peer and transition states accordingly.
 * Returns 0 on success, -1 on error.
 */
int bgp_fsm_event(struct peer *peer, bgp_fsm_event_t event);

/*
 * FSM Timer Management
 */
void bgp_fsm_timer_set(struct peer *peer);
void bgp_fsm_timer_cancel(struct peer *peer);

/* Individual timer control */
void bgp_timer_set_connect_retry(struct peer *peer);
void bgp_timer_set_holdtime(struct peer *peer);
void bgp_timer_set_keepalive(struct peer *peer);

/* Timer callbacks (internal use) */
void bgp_connect_retry_timer(struct event *event);
void bgp_hold_timer(struct event *event);
void bgp_keepalive_timer(struct event *event);

/*
 * FSM State Machine Actions
 *
 * These functions perform the actual work when transitioning
 * between states. Candidates may need to modify these for
 * Graceful Restart support.
 */

/* Change to Idle state */
int bgp_fsm_to_idle(struct peer *peer, int reason);

/* Change to Connect state */
int bgp_fsm_to_connect(struct peer *peer);

/* Change to Active state */
int bgp_fsm_to_active(struct peer *peer);

/* Change to OpenSent state */
int bgp_fsm_to_opensent(struct peer *peer);

/* Change to OpenConfirm state */
int bgp_fsm_to_openconfirm(struct peer *peer);

/* Change to Established state */
int bgp_fsm_to_established(struct peer *peer);

/*
 * Graceful Restart FSM Extensions
 *
 * TODO: Candidates implementing GR should extend these functions
 * to handle restart scenarios, stale route marking, and timer
 * management.
 */

/* Check if peer supports Graceful Restart */
bool bgp_peer_gr_capable(struct peer *peer);

/* Mark routes as stale when peer goes down */
void bgp_gr_stale_routes(struct peer *peer);

/* Clear stale routes after GR timer expires */
void bgp_gr_clear_stale_routes(struct peer *peer);

/* Process End-of-RIB marker */
void bgp_gr_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi);

#ifdef __cplusplus
}
#endif

#endif /* _BGP_FSM_H */
