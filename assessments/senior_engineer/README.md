# SE-Level Assessment: BGP Graceful Restart

## Overview

This assessment evaluates your ability to implement a core BGP feature: **Graceful Restart (GR)** as defined in [RFC 4724](https://tools.ietf.org/html/rfc4724).

Graceful Restart allows BGP peers to maintain forwarding state during a control plane restart, minimizing traffic disruption during software upgrades or daemon restarts.

## Time Allocation

- **Take-home phase**: 4-6 hours
- **Live discussion**: 45-60 minutes

## Background

In production networks, BGP daemon restarts (for upgrades, bug fixes, or configuration changes) traditionally cause:

1. All BGP sessions to reset
2. All routes from those peers to be withdrawn
3. Traffic blackholing until sessions re-establish and routes re-converge

Graceful Restart solves this by:
- Preserving forwarding state in the data plane during restart
- Allowing peers to mark routes as "stale" rather than removing them
- Re-establishing sessions and re-synchronizing routes before stale timer expires

## Your Task

Implement BGP Graceful Restart functionality in the provided codebase. The code follows FRR (Free Range Routing) patterns and includes a skeleton BGP implementation.

### Requirements

#### Part 1: GR Capability Negotiation (Required)
- Advertise GR capability in OPEN message
- Parse GR capability from peer's OPEN message
- Store negotiated GR parameters per-peer
- Handle the Restart Flag (R-bit) and Forwarding State flag (F-bit)

#### Part 2: Restart Handling (Required)
- When a GR-capable peer's session goes down:
  - Mark all routes from that peer as STALE
  - Start the Restart Timer (configurable, default 120 seconds)
  - Preserve routes for forwarding during the timer period

#### Part 3: Session Re-establishment (Required)
- When a restarting peer reconnects:
  - Detect it's a GR restart (R-bit set in OPEN)
  - Clear stale routes that aren't refreshed
  - Handle End-of-RIB marker (empty UPDATE)
  - Update peer state appropriately

#### Part 4: Timer Management (Required)
- Implement Restart Timer
- Implement Stale Path Timer
- Clean up stale routes when timers expire
- Cancel timers when session re-establishes

### Bonus Points
- Long-lived Graceful Restart (LLGR) support
- Per-AFI/SAFI GR handling
- Graceful Restart helper mode
- Comprehensive logging for debugging

## Files to Modify

Primary implementation should be in:
- `assessments/senior_engineer/starter/bgp_graceful_restart.c` (main implementation)
- `protocols/bgp/bgp_fsm.c` (FSM extensions)
- `protocols/bgp/bgp_packet.c` (capability encoding/decoding)

## Building and Testing

```bash
# Build
meson setup builddir -Dassessment_level=senior_engineer
cd builddir && ninja

# Run tests
ninja test

# Run specific test
./assessments/senior_engineer/tests/test_graceful_restart
```

## Evaluation Criteria

| Criteria | Weight |
|----------|--------|
| Correctness | 35% |
| Code Quality | 25% |
| RFC Compliance | 20% |
| Testing Coverage | 10% |
| Documentation | 10% |

### Correctness
- All test cases pass
- Edge cases handled properly
- No memory leaks or undefined behavior

### Code Quality
- Follows existing codebase patterns (FRR style)
- Clear, well-organized code
- Appropriate abstractions
- Error handling

### RFC Compliance
- Correct capability encoding/decoding
- Proper state machine transitions
- Timer behavior matches RFC

### Testing
- Unit tests for new functionality
- Edge case coverage
- Integration with existing tests

### Documentation
- Clear code comments
- Function documentation
- Design decisions explained

## Hints

1. Study the existing FSM implementation in `bgp_fsm.c`
2. Look at how capabilities are encoded in `bgp_packet.c`
3. Understand the route path info flags in `bgp_route.h`
4. The `BGP_PATH_STALE` flag is already defined but not used

## Resources

- [RFC 4724 - Graceful Restart Mechanism for BGP](https://tools.ietf.org/html/rfc4724)
- [RFC 8538 - Notification Message Support for BGP GR](https://tools.ietf.org/html/rfc8538)
- [FRR BGP Documentation](https://docs.frrouting.org/en/latest/bgp.html)

## Submission

1. Ensure all tests pass
2. Commit your changes with clear commit messages
3. Push to your assessment branch
4. Notify the interviewer when ready for review

Good luck!
