# SE Assessment: Detailed Task Specification

## Graceful Restart Technical Requirements

### RFC 4724 Overview

BGP Graceful Restart adds a capability that's negotiated during the OPEN message exchange. When enabled:

```
                     ┌─────────────────────────────────────┐
                     │        Normal Operation             │
                     │   Sessions Established, Routes OK   │
                     └─────────────────────────────────────┘
                                      │
                                      │ Peer restarts (control plane)
                                      ▼
                     ┌─────────────────────────────────────┐
                     │    Helper Mode Activated            │
                     │  - Mark routes from peer as STALE   │
                     │  - Start Restart Timer              │
                     │  - Continue forwarding              │
                     └─────────────────────────────────────┘
                                      │
                     ┌────────────────┴────────────────┐
                     │                                 │
             Peer reconnects                  Timer expires
             (within timer)                   (peer didn't reconnect)
                     │                                 │
                     ▼                                 ▼
        ┌───────────────────────┐        ┌───────────────────────┐
        │   Route Refresh       │        │   Remove Stale Routes │
        │ - Receive new routes  │        │ - Delete all STALE    │
        │ - Clear stale flag    │        │ - Re-run best path    │
        │ - Wait for EOR        │        └───────────────────────┘
        └───────────────────────┘
                     │
                     │ End-of-RIB received
                     ▼
        ┌───────────────────────┐
        │   Cleanup             │
        │ - Delete remaining    │
        │   stale routes        │
        │ - Normal operation    │
        └───────────────────────┘
```

### GR Capability Format (RFC 4724 Section 3)

```
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |            AFI (2 octets)     |    SAFI (1 octet)   |  Flags  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                        ....                                  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

      Restart Flags (4 bits):
        Bit 0 (R-bit): Restart State - set by restarting speaker
        Bit 1 (N-bit): Notification (RFC 8538)
        Bits 2-3: Reserved

      Restart Time: 12 bits, in seconds (0-4095)

      Per-AFI/SAFI Flags (1 octet per AFI/SAFI):
        Bit 0 (F-bit): Forwarding State preserved for this AFI/SAFI
        Bits 1-7: Reserved
```

### Implementation Tasks

#### Task 1: Data Structures

Create or extend structures to track GR state:

```c
/* In bgp_neighbor.h or new file */

/* Per-peer GR state */
struct bgp_gr_info {
    /* Negotiated GR parameters */
    bool gr_negotiated;           /* GR successfully negotiated */
    uint16_t restart_time;        /* Negotiated restart time */

    /* Per-AFI/SAFI state */
    struct {
        bool capable;             /* Peer advertised GR for this AFI/SAFI */
        bool forwarding_state;    /* F-bit from peer */
        bool eor_received;        /* End-of-RIB received */
        bool eor_sent;            /* End-of-RIB sent */
    } af[AFI_MAX][SAFI_MAX];

    /* State flags */
    bool restarting;              /* We are restarting */
    bool peer_restarting;         /* Peer is restarting */
    bool helper_mode;             /* We are in helper mode for this peer */

    /* Timestamps */
    time_t restart_start_time;
};
```

Note: See "Starter Code Location" below for the relevant file path.

#### Task 2: Capability Encoding

Extend `bgp_open_send()` in `bgp_packet.c`:

```c
/* Add GR capability to OPEN message */
if (peer->flags & PEER_FLAG_GRACEFUL_RESTART) {
    /* Calculate capability length:
     * 2 bytes for Restart Flags + Time
     * 4 bytes per AFI/SAFI (AFI=2 + SAFI=1 + Flags=1)
     */

    /* Restart Flags (4 bits) + Restart Time (12 bits) */
    uint16_t restart_value = 0;
    if (/* we are restarting */)
        restart_value |= BGP_GR_FLAG_RESTART << 12;
    restart_value |= (bgp->gr_restart_time & 0x0FFF);

    /* Write capability */
    stream_putc(s, BGP_CAP_CODE_GR);
    stream_putc(s, /* capability length */);
    stream_putw(s, restart_value);

    /* Add AFI/SAFI entries */
    for (/* each active AFI/SAFI */) {
        stream_putw(s, afi);
        stream_putc(s, safi);
        stream_putc(s, /* F-bit if forwarding preserved */);
    }
}
```

#### Task 3: Capability Parsing

Extend `bgp_capability_parse()`:

```c
case BGP_CAP_CODE_GR:
    if (cap_len >= 2) {
        uint16_t gr_header = (data[pos] << 8) | data[pos + 1];
        peer->gr_info.restart_time = gr_header & 0x0FFF;
        peer->gr_info.peer_restarting = (gr_header >> 12) & BGP_GR_FLAG_RESTART;

        /* Parse AFI/SAFI entries */
        int remaining = cap_len - 2;
        int offset = pos + 2;
        while (remaining >= 4) {
            uint16_t afi = (data[offset] << 8) | data[offset + 1];
            uint8_t safi = data[offset + 2];
            uint8_t flags = data[offset + 3];

            /* Store per-AFI/SAFI GR info */
            peer->gr_info.af[afi][safi].capable = true;
            peer->gr_info.af[afi][safi].forwarding_state =
                (flags & BGP_GR_FLAG_FORWARDING);

            offset += 4;
            remaining -= 4;
        }

        peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;
    }
    break;
```

#### Task 4: FSM Extensions

Modify state transitions for GR:

```c
/* In bgp_fsm_to_idle() */
int bgp_fsm_to_idle(struct peer *peer, int reason)
{
    /* Check if we should enter helper mode */
    if (bgp_peer_gr_capable(peer) &&
        reason != BGP_NOTIFY_CEASE_ADMIN_RESET) {

        /* Mark routes as stale instead of deleting */
        bgp_gr_stale_routes(peer);

        /* Start restart timer */
        bgp_gr_start_restart_timer(peer);

        peer->gr_info.helper_mode = true;
    } else {
        /* Normal teardown - delete routes */
        bgp_clear_route(peer, AFI_IP, SAFI_UNICAST);
    }

    /* ... rest of existing code ... */
}
```

#### Task 5: Stale Route Handling

Implement route marking:

```c
void bgp_gr_stale_routes(struct peer *peer)
{
    struct bgp *bgp = peer->bgp;
    struct route_table *table;
    struct route_node *rn;
    struct bgp_route_info *ri;
    struct bgp_path_info *pi;

    for (int afi = 0; afi < AFI_MAX; afi++) {
        for (int safi = 0; safi < SAFI_MAX; safi++) {
            if (!peer->af[afi][safi].active)
                continue;

            table = bgp_table_get(bgp, afi, safi);
            if (!table)
                continue;

            for (rn = route_top(table); rn; rn = route_next(rn)) {
                ri = rn->info;
                if (!ri)
                    continue;

                for (pi = ri->paths; pi; pi = pi->next) {
                    if (pi->peer == peer) {
                        bgp_path_info_set_flag(pi, BGP_PATH_STALE);
                        log_debug("GR: Marked route as stale from %s",
                                  peer->host);
                    }
                }
            }
        }
    }
}
```

#### Task 6: Timer Implementation

```c
/* Restart timer callback */
void bgp_gr_restart_timer(struct event *event)
{
    struct peer *peer = EVENT_ARG(event);
    peer->t_gr_restart = NULL;

    log_warn("GR: Restart timer expired for peer %s", peer->host);

    /* Peer didn't reconnect in time - remove stale routes */
    bgp_gr_clear_stale_routes(peer);
    peer->gr_info.helper_mode = false;
}

/* Start restart timer */
void bgp_gr_start_restart_timer(struct peer *peer)
{
    uint16_t restart_time = peer->gr_info.restart_time;
    if (restart_time == 0)
        restart_time = 120;  /* Default */

    event_cancel(&peer->t_gr_restart);
    peer->t_gr_restart = EVENT_ADD_TIMER(peer->bgp->master,
                                         bgp_gr_restart_timer,
                                         peer,
                                         restart_time * 1000);

    peer->gr_info.restart_start_time = time(NULL);
    log_info("GR: Started restart timer (%u seconds) for peer %s",
             restart_time, peer->host);
}
```

#### Task 7: End-of-RIB Handling

```c
void bgp_gr_end_of_rib(struct peer *peer, uint8_t afi, uint8_t safi)
{
    struct bgp *bgp = peer->bgp;

    log_info("GR: End-of-RIB received from %s for afi=%d safi=%d",
             peer->host, afi, safi);

    peer->gr_info.af[afi][safi].eor_received = true;

    /* Check if all AFI/SAFI have received EOR */
    bool all_eor = true;
    for (int a = 0; a < AFI_MAX; a++) {
        for (int s = 0; s < SAFI_MAX; s++) {
            if (peer->af[a][s].active &&
                peer->gr_info.af[a][s].capable &&
                !peer->gr_info.af[a][s].eor_received) {
                all_eor = false;
                break;
            }
        }
        if (!all_eor)
            break;
    }

    if (all_eor) {
        log_info("GR: All End-of-RIB received from %s, clearing stale routes",
                 peer->host);

        /* Cancel restart timer */
        event_cancel(&peer->t_gr_restart);

        /* Clear remaining stale routes */
        bgp_gr_clear_stale_routes(peer);

        /* Exit helper mode */
        peer->gr_info.helper_mode = false;
    }
}
```

### Test Cases

Your implementation should pass these test scenarios:

1. **Basic GR Negotiation**
   - Two peers establish session with GR capability
   - Verify capability is correctly encoded/decoded

2. **Stale Route Marking**
   - Peer A has routes from Peer B
   - Peer B's session goes down
   - Verify routes are marked STALE, not deleted

3. **Successful Restart**
   - Peer B restarts and reconnects within timer
   - Peer B sends new routes
   - Verify stale routes are cleared only after EOR

4. **Timer Expiry**
   - Peer B's session goes down
   - Peer B doesn't reconnect within timer
   - Verify stale routes are deleted when timer expires

5. **Partial Update**
   - Peer B restarts and only sends subset of routes
   - Verify routes not refreshed are deleted after EOR

### Starter Code Location

Your main implementation goes in:
```
assessments/senior_engineer/starter/bgp_graceful_restart.c
```

This file should contain:
- GR data structures
- GR capability encoding/decoding
- Stale route management
- Timer callbacks
- Integration hooks into existing code
