# Control Plane Technical Assessment

> **Candidate:** Muhammad Arham Nasir  
> **Assessment Track:** Senior Software Engineer - BGP Graceful Restart (RFC 4724)  
> **Reviewer:** Mohamed EL GAMAL (`melgamal@jobsaicloud.com`)  
> **Status:** 100% Tests Passing (`12/12` Unit Tests, `5/5` Integration Tests)  
> **Sanitizers:** Clean under AddressSanitizer & UBSan (0 bytes leaked)  
> **CI/CD Pipeline:** `✔ 3/3 checks passing` on GitHub Actions  
> **Engineering Report:** Please refer to [SUBMISSION_REPORT.md](SUBMISSION_REPORT.md) for complete architecture, implementation sequence, and test logs.

---


A comprehensive technical assessment for Software Engineers specializing in Control Plane and Network Operating System (NOS) subsystems.

## Overview

This repository provides a production-quality technical assessment based on real-world patterns from:
- **FRR (Free Range Routing)** - Open source routing protocol suite
- **SONiC** - Software for Open Networking in the Cloud

## Assessment Tracks

### SE Level: BGP Graceful Restart
Implement BGP Graceful Restart (RFC 4724) to maintain forwarding state during control plane restarts.

- **Time**: 4-6 hours take-home + 45-60 minute discussion
- **Focus**: RFC compliance, state machine implementation, timer management
- **Path**: `assessments/senior_engineer/`

### Principal Level: BGP Route Reflector
Implement an optimized BGP Route Reflector (RFC 4456) with scalability and advanced features.

- **Time**: 8-12 hours take-home + 60-90 minute discussion
- **Focus**: Architecture, scalability, design trade-offs
- **Path**: `assessments/principal_engineer/`

## Repository Structure

```
test-sde-control-plane/
├── README.md                   # This file
├── meson.build                 # Build configuration
├── meson_options.txt           # Build options
│
├── lib/                        # Core libraries (FRR-style)
│   ├── common/                 # Common utilities
│   │   ├── memory.{h,c}        # Memory management
│   │   ├── linklist.{h,c}      # Linked list
│   │   ├── prefix.{h,c}        # IP prefix handling
│   │   ├── thread.{h,c}        # Event loop
│   │   ├── log.{h,c}           # Logging
│   │   ├── hash.{h,c}          # Hash tables
│   │   └── stream.{h,c}        # Packet buffers
│   │
│   └── zebra/                  # RIB abstraction
│       ├── rib.{h,c}           # Routing Information Base
│       └── zapi.{h,c}          # Zebra API
│
├── protocols/                  # Protocol implementations
│   └── bgp/                    # BGP daemon
│       ├── bgpd.{h,c}          # Main BGP instance
│       ├── bgp_fsm.{h,c}       # Finite State Machine
│       ├── bgp_packet.{h,c}    # Packet encoding/decoding
│       ├── bgp_neighbor.{h,c}  # Peer management
│       ├── bgp_attr.{h,c}      # Path attributes
│       └── bgp_route.{h,c}     # Route management
│
├── assessments/                # Assessment materials
│   ├── senior_engineer/        # SE track
│   │   ├── README.md           # Task description
│   │   ├── TASK.md             # Detailed requirements
│   │   ├── starter/            # Starter code
│   │   └── tests/              # Test suite
│   │
│   └── principal_engineer/     # Principal track
│       ├── README.md           # Task description
│       ├── starter/            # Starter code
│       └── tests/              # Test suite
│
├── tests/                      # Common test infrastructure
│   ├── framework/              # Test framework
│   └── integration/            # Integration tests
│
└── scripts/                    # Utility scripts
    ├── setup.sh                # Environment setup
    └── run_tests.sh            # Test runner
```

## Quick Start

### Prerequisites

- GCC or Clang (C11 support)
- Meson (>= 0.56.0)
- Ninja
- Python 3.x (for meson)

### Building

```bash
# Clone the repository
git clone <repo-url>
cd test-sde-control-plane

# Configure build (choose assessment level)
meson setup builddir -Dassessment_level=senior_engineer     # For Senior Engineer track
# OR
meson setup builddir -Dassessment_level=principal_engineer  # For Principal Engineer track
# OR
meson setup builddir -Dassessment_level=all                 # Both tracks

# Build
cd builddir
ninja

# Run tests
ninja test
```

### Build Options

| Option | Values | Description |
|--------|--------|-------------|
| `assessment_level` | senior_engineer, principal_engineer, all | Which assessment to build |
| `enable_integration_tests` | true, false | Enable integration tests |
| `enable_verbose_logging` | true, false | Debug logging |

## For Candidates

### Getting Started

1. Read the assessment README for your track:
   - SE: `assessments/senior_engineer/README.md`
   - Principal: `assessments/principal_engineer/README.md`

2. Familiarize yourself with the codebase structure

3. Build and run the existing tests to verify setup

4. Implement your solution in the starter files

5. Write tests for your implementation

6. Document your design decisions

### Tips

- Study the existing code patterns before implementing
- The codebase follows FRR conventions - maintain consistency
- RFC documents are your primary specification
- Run with address sanitizer during development
- Commit frequently with clear messages

### Resources

- [RFC 4271 - BGP-4](https://tools.ietf.org/html/rfc4271)
- [RFC 4724 - Graceful Restart](https://tools.ietf.org/html/rfc4724)
- [RFC 4456 - Route Reflection](https://tools.ietf.org/html/rfc4456)
- [FRR Documentation](https://docs.frrouting.org)
- [SONiC Architecture](https://github.com/sonic-net/SONiC/wiki/Architecture)

## Architecture Notes

### Design Philosophy

This codebase mimics production NOS patterns:

1. **Event-Driven Architecture**: Single-threaded event loop (like FRR's `thread_master`)
2. **Memory Type Tracking**: Tagged allocations for debugging (like FRR's MTYPE)
3. **Protocol State Machines**: Clean FSM implementations
4. **Zebra Integration**: RIB abstraction and ZAPI communication

### Key Patterns

**Memory Management**
```c
// Use typed allocation
peer = XCALLOC(MTYPE_BGP_PEER, sizeof(struct peer));
// Always use XFREE which NULLs the pointer
XFREE(MTYPE_BGP_PEER, peer);
```

**Event Loop**
```c
// Schedule timer
peer->t_holdtime = EVENT_ADD_TIMER(master, hold_timer, peer, time_ms);
// Cancel timer
event_cancel(&peer->t_holdtime);
```

**Logging**
```c
log_info("BGP peer %s established", peer->host);
log_debug("Processing UPDATE from %s", peer->host);
log_err("Memory allocation failed");
```

## Contributing

### For Internal Development

1. Make changes on feature branch
2. Ensure all tests pass
3. Update documentation if needed
4. Submit for review

### Code Style

- Follow existing FRR-style conventions
- 4-space indentation
- 80-character line limit
- Document all public functions

## License

Apache 2.0 - See LICENSE file

## Contact

For questions about this assessment, contact the hiring team.
