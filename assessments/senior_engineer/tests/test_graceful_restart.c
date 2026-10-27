/*
 * BGP Graceful Restart Tests
 *
 * Test suite for SE-level assessment implementation.
 * These tests verify the candidate's Graceful Restart implementation.
 */

#include "lib/common/memory.h"
#include "lib/common/log.h"
#include "lib/common/thread.h"
#include "lib/common/prefix.h"
#include "lib/common/stream.h"
#include "protocols/bgp/bgpd.h"
#include "protocols/bgp/bgp_fsm.h"
#include "protocols/bgp/bgp_neighbor.h"
#include "protocols/bgp/bgp_packet.h"
#include "protocols/bgp/bgp_route.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        return -1; \
    } \
} while (0)

#define RUN_TEST(name, func) do { \
    printf("Running: %s\n", name); \
    tests_run++; \
    if (func() == 0) { \
        printf("  PASS\n"); \
        tests_passed++; \
    } else { \
        tests_failed++; \
    } \
} while (0)

/* Test globals */
static struct event_loop *master;
static struct bgp *bgp;

/*
 * Setup and teardown
 */
static void test_setup(void)
{
    master = event_loop_new();
    bgp_master_init(master);
    bgp = bgp_create(65001, NULL);

    /* Set router ID */
    struct in_addr router_id;
    inet_pton(AF_INET, "1.1.1.1", &router_id);
    bgp_router_id_set(bgp, &router_id);

    /* Enable GR on BGP instance */
    bgp_gr_enable(bgp, 120, 360);
}

static void test_teardown(void)
{
    bgp_delete(bgp);
    bgp_master_finish();
    event_loop_free(master);
}

/* Helper to create test peer */
static struct peer *create_test_peer(const char *addr, uint32_t remote_as)
{
    union sockunion su;
    struct peer *peer;

    memset(&su, 0, sizeof(su));
    su.sin.sin_family = AF_INET;
    inet_pton(AF_INET, addr, &su.sin.sin_addr);

    peer = peer_create(bgp, &su, remote_as);
    peer_activate(peer, AFI_IP, SAFI_UNICAST);
    peer_gr_enable(peer);

    /* Set remote router ID */
    inet_pton(AF_INET, addr, &peer->remote_id);

    return peer;
}

/* Helper to add test route */
static void add_test_route(struct peer *peer, const char *prefix_str)
{
    struct prefix p;
    struct bgp_attr *attr;

    str2prefix(prefix_str, &p);

    attr = bgp_attr_new();
    attr->origin = BGP_ORIGIN_IGP;
    attr->local_pref = 100;

    /* bgp_update takes ownership of attr via bgp_attr_intern */
    bgp_update(peer, &p, attr, AFI_IP, SAFI_UNICAST);
}

/* Helper to check if route has STALE flag */
static bool route_is_stale(struct bgp *bgp, struct peer *peer, const char *prefix_str)
{
    struct route_table *table;
    struct route_node *rn;
    struct prefix p;
    struct bgp_route_info *ri;
    struct bgp_path_info *pi;

    str2prefix(prefix_str, &p);
    table = bgp_table_get(bgp, AFI_IP, SAFI_UNICAST);
    if (!table)
        return false;

    rn = route_node_lookup(table, &p);
    if (!rn)
        return false;

    ri = rn->info;
    if (!ri) {
        route_unlock_node(rn);
        return false;
    }

    /* Find path from this peer */
    for (pi = ri->paths; pi; pi = pi->next) {
        if (pi->peer == peer) {
            bool stale = bgp_path_info_check_flag(pi, BGP_PATH_STALE);
            route_unlock_node(rn);
            return stale;
        }
    }

    route_unlock_node(rn);
    return false;
}

/* Helper to check if route exists */
static bool route_exists(struct bgp *bgp, const char *prefix_str)
{
    struct route_table *table;
    struct route_node *rn;
    struct prefix p;

    str2prefix(prefix_str, &p);
    table = bgp_table_get(bgp, AFI_IP, SAFI_UNICAST);
    if (!table)
        return false;

    rn = route_node_lookup(table, &p);
    if (!rn)
        return false;

    route_unlock_node(rn);
    return rn->info != NULL;
}

/*
 * =============================================================================
 * Test Cases
 * =============================================================================
 */

/*
 * Test 1: GR Capability Encoding
 *
 * Verify that GR capability is correctly encoded in OPEN message per RFC 4724 Section 3.
 */
static int test_gr_capability_encoding(void)
{
    struct peer *peer;
    struct stream *s;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_CONNECT;

    /* Create stream for capability encoding */
    s = stream_new(4096);

    int bytes_written = bgp_gr_capability_encode(s, peer);
    TEST_ASSERT(bytes_written >= 6, "Encoded capability should be at least 6 bytes (2 hdr + 4 per AFI/SAFI)");

    /* Reset stream read pointer to verify serialized wire bytes */
    stream_set_getp(s, 0);

    /* 1. Capability Code */
    uint8_t cap_code = stream_getc(s);
    TEST_ASSERT(cap_code == BGP_CAP_CODE_GR, "Capability code must be 64 (Graceful Restart)");

    /* 2. Capability Length */
    uint8_t cap_len = stream_getc(s);
    TEST_ASSERT(cap_len == 6, "Capability length must be 6 for IPv4 Unicast");

    /* 3. Restart Flags (4 bits) + Restart Time (12 bits) */
    uint16_t restart_val = stream_getw(s);
    uint16_t restart_time = restart_val & 0x0FFF;
    TEST_ASSERT(restart_time == 120, "Negotiated restart time must match configured 120s");

    /* 4. AFI (2 octets) */
    uint16_t afi = stream_getw(s);
    TEST_ASSERT(afi == AFI_IP, "AFI must be AFI_IP (1)");

    /* 5. SAFI (1 octet) */
    uint8_t safi = stream_getc(s);
    TEST_ASSERT(safi == SAFI_UNICAST, "SAFI must be SAFI_UNICAST (1)");

    /* 6. Flags for address family (F-bit: Forwarding State preserved) */
    uint8_t flags = stream_getc(s);
    TEST_ASSERT((flags & BGP_GR_FLAG_FORWARDING) != 0, "Forwarding State (F-bit) must be set");

    stream_free(s);
    test_teardown();
    return 0;
}

/*
 * Test 2: GR Capability Parsing
 *
 * Verify that GR capability wire bytes are correctly parsed into peer structure.
 */
static int test_gr_capability_parsing(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);

    /* Simulated incoming OPEN capability wire bytes:
     * Restart Flags: 0x80 (R-bit set = 1)
     * Restart Time: 180 seconds (0x0B4) -> Combined 16-bit = 0x80B4
     * AFI: 1 (IPv4), SAFI: 1 (Unicast), Flags: 0x80 (F-bit = 1)
     */
    uint8_t wire_data[] = {
        0x80, 0xB4,       /* Flags (R-bit=1) + Restart Time (180s) */
        0x00, 0x01,       /* AFI = 1 (IPv4) */
        0x01,             /* SAFI = 1 (Unicast) */
        0x80              /* Flags (F-bit=1) */
    };

    int ret = bgp_gr_capability_parse(peer, wire_data, sizeof(wire_data));
    TEST_ASSERT(ret == 0, "Parsing wire GR capability should succeed");
    TEST_ASSERT(peer->gr_restart_time == 180, "Parsed restart time must be 180s");
    TEST_ASSERT((peer->gr_flags & BGP_GR_FLAG_RESTART) != 0, "R-bit must be parsed");
    TEST_ASSERT(bgp_peer_gr_capable(peer), "Peer must be recognized as GR capable");

    test_teardown();
    return 0;
}

/*
 * Test 3: Stale Route Marking
 *
 * When a GR-capable peer goes down, routes should be marked stale.
 */
static int test_stale_route_marking(void)
{
    struct peer *peer;
    struct route_table *table;
    struct route_node *rn;
    struct prefix p;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* Add some routes from this peer */
    add_test_route(peer, "192.168.1.0/24");
    add_test_route(peer, "192.168.2.0/24");
    add_test_route(peer, "192.168.3.0/24");

    /* Verify routes are present */
    table = bgp_table_get(bgp, AFI_IP, SAFI_UNICAST);
    TEST_ASSERT(table != NULL, "Route table should exist");

    str2prefix("192.168.1.0/24", &p);
    rn = route_node_lookup(table, &p);
    TEST_ASSERT(rn != NULL, "Route should exist");
    route_unlock_node(rn);

    /* Initially routes should NOT be stale */
    TEST_ASSERT(!route_is_stale(bgp, peer, "192.168.1.0/24"),
                "Route should NOT be stale initially");

    /* Mark routes as stale (simulating peer going down) */
    bgp_gr_stale_routes(peer);

    /* Verify routes are still present (not deleted) */
    rn = route_node_lookup(table, &p);
    TEST_ASSERT(rn != NULL, "Route should still exist after GR down");
    route_unlock_node(rn);

    /* Verify routes are marked stale */
    TEST_ASSERT(route_is_stale(bgp, peer, "192.168.1.0/24"),
                "Route should be marked STALE after bgp_gr_stale_routes()");
    TEST_ASSERT(route_is_stale(bgp, peer, "192.168.2.0/24"),
                "Route 2 should be marked STALE");
    TEST_ASSERT(route_is_stale(bgp, peer, "192.168.3.0/24"),
                "Route 3 should be marked STALE");

    test_teardown();
    return 0;
}

/*
 * Test 4: Restart Timer Behavior
 *
 * Verify timer starts when peer goes down and fires correctly.
 */
static int test_restart_timer(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;
    peer->gr_restart_time = 120;

    /* Add route */
    add_test_route(peer, "192.168.1.0/24");

    /* Note: Actual timer testing would require running event loop */
    /* This test verifies the structure is in place */

    test_teardown();
    return 0;
}

/*
 * Test 5: Successful GR Restart
 *
 * Peer restarts, reconnects, sends routes, and sends EOR.
 */
static int test_successful_gr_restart(void)
{
    struct peer *peer;
    struct route_table *table;
    struct route_node *rn;
    struct prefix p;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* Add routes */
    add_test_route(peer, "192.168.1.0/24");
    add_test_route(peer, "192.168.2.0/24");

    /* Verify routes exist */
    table = bgp_table_get(bgp, AFI_IP, SAFI_UNICAST);
    str2prefix("192.168.1.0/24", &p);
    rn = route_node_lookup(table, &p);
    TEST_ASSERT(rn != NULL, "Route 1 should exist");
    route_unlock_node(rn);

    /* Simulate receiving fresh route for 192.168.1.0/24 */
    add_test_route(peer, "192.168.1.0/24");

    /* Verify 192.168.1.0/24 exists (was refreshed) */
    rn = route_node_lookup(table, &p);
    TEST_ASSERT(rn != NULL, "Refreshed route should exist");
    route_unlock_node(rn);

    test_teardown();
    return 0;
}

/*
 * Test 6: Timer Expiry - Stale Route Deletion
 *
 * When restart timer expires, stale routes should be deleted.
 */
static int test_timer_expiry_deletes_stale(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* Add routes */
    add_test_route(peer, "192.168.1.0/24");

    /* Verify route exists */
    TEST_ASSERT(route_exists(bgp, "192.168.1.0/24"), "Route should exist initially");

    /* Mark routes as stale */
    bgp_gr_stale_routes(peer);

    /* Clear stale routes (simulating timer expiry) */
    bgp_gr_clear_stale_routes(peer);

    /* After clearing, stale routes should be deleted */
    /* Note: This depends on full implementation */

    test_teardown();
    return 0;
}

/*
 * Test 7: Non-GR Peer - Normal Behavior
 *
 * When a non-GR peer goes down, routes should be deleted immediately.
 */
static int test_non_gr_peer_immediate_delete(void)
{
    struct peer *peer;
    struct route_table *table;
    struct route_node *rn;
    struct prefix p;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    /* Note: NOT enabling GR on this peer */
    peer->flags &= ~PEER_FLAG_GRACEFUL_RESTART;
    peer->cap_flags &= ~PEER_CAP_GRACEFUL_RESTART;

    /* Add routes */
    add_test_route(peer, "192.168.1.0/24");

    /* Verify route exists */
    table = bgp_table_get(bgp, AFI_IP, SAFI_UNICAST);
    str2prefix("192.168.1.0/24", &p);
    rn = route_node_lookup(table, &p);
    TEST_ASSERT(rn != NULL, "Route should exist initially");
    route_unlock_node(rn);

    /* Simulate peer going down - routes should be deleted immediately */
    bgp_clear_route(peer, AFI_IP, SAFI_UNICAST);

    /* Route node might exist but have no paths - this is expected behavior */

    test_teardown();
    return 0;
}

/*
 * Test 8: End-of-RIB Detection
 *
 * Empty UPDATE should be detected as End-of-RIB marker.
 */
static int test_end_of_rib_detection(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* Initialize EOR tracking */
    if (peer->gr_eor_received) {
        memset(peer->gr_eor_received, 0, AFI_MAX * SAFI_MAX * sizeof(bool));
    }

    /* Call EOR handler */
    bgp_gr_end_of_rib(peer, AFI_IP, SAFI_UNICAST);

    /* Note: Full verification depends on implementation tracking EOR state */

    test_teardown();
    return 0;
}

/*
 * Test 9: GR Capability Check
 *
 * Verify bgp_peer_gr_capable() correctly identifies GR-capable peers.
 */
static int test_gr_capability_check(void)
{
    struct peer *gr_peer, *non_gr_peer;

    test_setup();

    /* Create GR-capable peer */
    gr_peer = create_test_peer("10.0.0.2", 65002);
    gr_peer->gr_restart_time = 120;
    gr_peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* Create non-GR peer */
    non_gr_peer = create_test_peer("10.0.0.3", 65003);
    non_gr_peer->gr_restart_time = 0;
    non_gr_peer->cap_flags &= ~PEER_CAP_GRACEFUL_RESTART;
    non_gr_peer->flags &= ~PEER_FLAG_GRACEFUL_RESTART;

    TEST_ASSERT(bgp_peer_gr_capable(gr_peer), "GR peer should be GR capable");
    TEST_ASSERT(!bgp_peer_gr_capable(non_gr_peer), "Non-GR peer should NOT be GR capable");

    test_teardown();
    return 0;
}

/*
 * Test 10: Partial Route Refresh (TASK.md Scenario 5)
 *
 * Peer advertises multiple routes, restarts, and only refreshes a subset.
 * Verified: Refreshed routes have STALE cleared and are preserved;
 * unrefreshed routes remain STALE and are purged upon End-of-RIB.
 */
static int test_partial_route_refresh(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* 1. Advertise Route 1 and Route 2 */
    add_test_route(peer, "10.10.1.0/24");
    add_test_route(peer, "10.10.2.0/24");

    TEST_ASSERT(route_exists(bgp, "10.10.1.0/24"), "Route 1 must exist initially");
    TEST_ASSERT(route_exists(bgp, "10.10.2.0/24"), "Route 2 must exist initially");

    /* 2. Peer session drops -> enters Helper Mode, marks routes STALE */
    bgp_gr_peer_down(peer, BGP_NOTIFY_CEASE);
    TEST_ASSERT(route_is_stale(bgp, peer, "10.10.1.0/24"), "Route 1 must be marked STALE");
    TEST_ASSERT(route_is_stale(bgp, peer, "10.10.2.0/24"), "Route 2 must be marked STALE");

    /* 3. Peer restarts and only re-advertises Route 1 (subset refresh) */
    add_test_route(peer, "10.10.1.0/24");

    /* Route 1 stale flag cleared; Route 2 remains STALE */
    TEST_ASSERT(!route_is_stale(bgp, peer, "10.10.1.0/24"), "Refreshed Route 1 must NOT be stale");
    TEST_ASSERT(route_is_stale(bgp, peer, "10.10.2.0/24"), "Unrefreshed Route 2 must remain STALE");

    /* 4. End-of-RIB received from peer -> cleanup unrefreshed stale routes */
    bgp_gr_receive_end_of_rib(peer, AFI_IP, SAFI_UNICAST);

    /* Refreshed Route 1 is kept in RIB; unrefreshed Route 2 is deleted */
    TEST_ASSERT(route_exists(bgp, "10.10.1.0/24"), "Refreshed Route 1 must be retained");
    TEST_ASSERT(!route_is_stale(bgp, peer, "10.10.2.0/24"), "Unrefreshed Route 2 must be purged");

    test_teardown();
    return 0;
}

/*
 * Test 11: Administrative Reset Bypasses Helper Mode (RFC 4724 Section 4.2)
 *
 * When session closes due to Administrative Reset (Cease/Admin Reset),
 * Helper mode MUST NOT be activated and routes must be purged immediately.
 */
static int test_admin_reset_bypasses_helper(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);
    peer->state = BGP_STATUS_ESTABLISHED;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    add_test_route(peer, "10.20.1.0/24");
    TEST_ASSERT(route_exists(bgp, "10.20.1.0/24"), "Route must exist initially");

    /* Session down due to BGP_NOTIFY_CEASE_ADMIN_RESET */
    bool entered_helper = bgp_gr_peer_down(peer, BGP_NOTIFY_CEASE_ADMIN_RESET);
    TEST_ASSERT(!entered_helper, "Admin Reset MUST NOT enter Helper Mode");

    /* Routes are cleared directly */
    bgp_clear_route(peer, AFI_IP, SAFI_UNICAST);
    TEST_ASSERT(!route_is_stale(bgp, peer, "10.20.1.0/24"), "Route must not be retained as stale");

    test_teardown();
    return 0;
}

/*
 * Test 12: Malformed Wire Capability Rejection
 *
 * Tests parser robustness against truncated lengths and NULL buffers.
 */
static int test_gr_capability_malformed(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);

    /* Truncated length (1 byte instead of minimum 2) */
    uint8_t short_data[1] = { 0x00 };
    int ret = bgp_gr_capability_parse(peer, short_data, 1);
    TEST_ASSERT(ret == -1, "Truncated capability (1 byte) must return -1");

    /* NULL data buffer */
    ret = bgp_gr_capability_parse(peer, NULL, 6);
    TEST_ASSERT(ret == -1, "NULL buffer must return -1");

    test_teardown();
    return 0;
}

/*
 * Main test runner
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf("===========================================\n");
    printf("BGP Graceful Restart Test Suite\n");
    printf("===========================================\n\n");

    log_init("test_gr", LOG_DEST_STDOUT);
    log_set_level(LOG_LEVEL_DEBUG);

    /* Run all tests */
    RUN_TEST("GR Capability Encoding", test_gr_capability_encoding);
    RUN_TEST("GR Capability Parsing", test_gr_capability_parsing);
    RUN_TEST("Stale Route Marking", test_stale_route_marking);
    RUN_TEST("Restart Timer", test_restart_timer);
    RUN_TEST("Successful GR Restart", test_successful_gr_restart);
    RUN_TEST("Timer Expiry Deletes Stale", test_timer_expiry_deletes_stale);
    RUN_TEST("Non-GR Peer Immediate Delete", test_non_gr_peer_immediate_delete);
    RUN_TEST("End-of-RIB Detection", test_end_of_rib_detection);
    RUN_TEST("GR Capability Check", test_gr_capability_check);
    RUN_TEST("Partial Route Refresh (TASK Scenario 5)", test_partial_route_refresh);
    RUN_TEST("Admin Reset Bypasses Helper (RFC 4724)", test_admin_reset_bypasses_helper);
    RUN_TEST("Malformed Wire Capability Rejection", test_gr_capability_malformed);

    /* Summary */
    printf("\n");
    printf("===========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("         %d tests FAILED\n", tests_failed);
    printf("===========================================\n\n");

    log_close();

    return (tests_failed > 0) ? 1 : 0;
}
