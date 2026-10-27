# Principal Engineer Assessment: BGP Route Reflector with Cluster Optimization

## Overview

This assessment evaluates your ability to design and implement a complex BGP feature: an optimized **Route Reflector (RR)** implementation with cluster awareness and scalability considerations.

Route Reflectors (RFC 4456) are essential for iBGP scalability, but naive implementations can create suboptimal routing or even routing loops. This assessment requires you to implement a production-quality RR with proper loop prevention and path selection.

## Time Allocation

- **Take-home phase**: 8-12 hours
- **Live discussion**: 60-90 minutes (architecture deep-dive)

## Background

### The iBGP Full Mesh Problem

In iBGP, routes learned from one iBGP peer are not re-advertised to other iBGP peers (split-horizon). This requires a full mesh of iBGP sessions:
- N routers require N(N-1)/2 sessions
- 100 routers = 4,950 sessions
- 1000 routers = 499,500 sessions

### Route Reflectors

Route Reflectors solve this by:
1. Dividing routers into "clients" and "non-clients"
2. Reflecting routes between clients
3. Reflecting routes from clients to non-clients
4. Reflecting routes from non-clients to clients
5. NOT reflecting routes from non-clients to non-clients

### The Challenge

Simple RR implementations can cause:
- **Routing loops**: Without proper ORIGINATOR_ID and CLUSTER_LIST checking
- **Suboptimal paths**: The RR may not have visibility into all path attributes
- **Convergence issues**: Large clusters can slow convergence
- **Memory pressure**: RRs must store all client routes

## Your Task

Implement a production-grade BGP Route Reflector with the following capabilities:

### Core Requirements

#### Part 1: Basic Route Reflection (Required)
- Implement client/non-client classification
- Correct route reflection rules per RFC 4456
- ORIGINATOR_ID attribute handling
- CLUSTER_LIST attribute handling
- Loop detection via cluster list

#### Part 2: Optimal Path Selection (Required)
- Extend best path selection for RR scenarios
- Handle the "MED oscillation" problem
- Implement deterministic MED comparison
- Consider IGP cost to next-hop

#### Part 3: Scalability Optimizations (Required)
- Implement efficient route storage (path sharing)
- Batch UPDATE generation for clients
- Memory-efficient attribute interning
- O(1) or O(log n) route lookups

### Advanced Requirements (Choose 2)

#### Option A: Hierarchical Route Reflectors
- Support multi-level RR hierarchy
- Proper cluster ID propagation
- Loop prevention across hierarchy levels

#### Option B: ADD-PATH Support
- Implement RFC 7911 ADD-PATH
- Send multiple paths to clients
- Allow clients to make locally optimal decisions

#### Option C: Optimal Route Reflection (ORR)
- Calculate optimal paths from client's perspective
- Consider client's IGP topology
- Implement draft-ietf-idr-bgp-optimal-route-reflection

#### Option D: Route Reflector Redundancy
- Implement cluster-aware failover
- Consistent route selection across redundant RRs
- Graceful RR restart handling

## Design Expectations

As a Principal-level assessment, we expect:

1. **Architecture Document** (in DESIGN.md)
   - High-level design with diagrams
   - Data structure choices and rationale
   - Complexity analysis (time and space)
   - Trade-off discussion

2. **Scalability Analysis**
   - How does your design scale with number of clients?
   - How does it scale with number of routes?
   - Memory footprint analysis
   - CPU utilization considerations

3. **Testing Strategy**
   - Unit test plan
   - Integration test scenarios
   - Performance benchmarks
   - Chaos/failure testing approach

## Files to Modify

Primary implementation:
- `assessments/principal_engineer/starter/bgp_route_reflector.c`
- `assessments/principal_engineer/starter/bgp_route_reflector.h`
- `assessments/principal_engineer/DESIGN.md` (create this)

Supporting modifications:
- `protocols/bgp/bgp_route.c` (path selection extensions)
- `protocols/bgp/bgp_attr.c` (attribute handling)

## Building and Testing

```bash
# Build
meson setup builddir -Dassessment_level=principal_engineer
cd builddir && ninja

# Run tests
ninja test

# Run benchmarks
./assessments/principal_engineer/tests/bench_route_reflector
```

## Evaluation Criteria

| Criteria | Weight |
|----------|--------|
| Architecture & Design | 30% |
| Implementation Quality | 25% |
| Scalability | 20% |
| RFC Compliance | 15% |
| Testing & Documentation | 10% |

### Architecture & Design
- Clear, well-reasoned design
- Appropriate abstractions
- Consideration of edge cases
- Trade-off analysis

### Implementation Quality
- Clean, maintainable code
- Follows codebase patterns
- Proper error handling
- No resource leaks

### Scalability
- Efficient algorithms
- Memory-conscious implementation
- Handles large-scale scenarios
- Performance under load

### RFC Compliance
- Correct loop prevention
- Proper attribute handling
- Correct reflection rules

### Testing & Documentation
- Comprehensive test coverage
- Clear documentation
- Design decisions explained

## Live Discussion Topics

Be prepared to discuss:

1. Your design choices and trade-offs
2. How you would handle 100K clients with 1M routes
3. Failure scenarios and recovery
4. How to integrate with SONiC/FRR production systems
5. Operational considerations (monitoring, debugging)
6. Alternative approaches you considered

## Resources

- [RFC 4456 - BGP Route Reflection](https://tools.ietf.org/html/rfc4456)
- [RFC 7911 - ADD-PATH](https://tools.ietf.org/html/rfc7911)
- [RFC 9107 - BGP Optimal Route Reflection](https://tools.ietf.org/html/rfc9107)
- [FRR Route Reflector Documentation](https://docs.frrouting.org/en/latest/bgp.html#route-reflector)

## Submission

1. Complete implementation with passing tests
2. DESIGN.md with architecture documentation
3. Clear commit history showing development progression
4. Performance benchmark results

Good luck!
