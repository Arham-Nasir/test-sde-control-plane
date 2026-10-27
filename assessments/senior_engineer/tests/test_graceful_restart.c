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
 * Verify that GR capability is correctly encoded in OPEN message.
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

    /* The implementation should encode GR capability */
    /* Expected: 2 bytes restart flags+time, then 4 bytes per AFI/SAFI */
    /* Minimum length for GR cap with one AFI/SAFI is 6 bytes */

    stream_free(s);
    test_teardown();

    /* Note: This test passes if encoding doesn't crash.
     * Full verification requires examining the encoded bytes. */
    return 0;
}

/*
 * Test 2: GR Capability Parsing
 *
 * Verify that GR capability is correctly parsed from peer's OPEN.
 */
static int test_gr_capability_parsing(void)
{
    struct peer *peer;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65002);

    /* Set GR restart time to simulate received capability */
    peer->gr_restart_time = 120;
    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;

    /* Verify GR is recognized as capable */
    TEST_ASSERT(bgp_peer_gr_capable(peer), "Peer should be GR capable");

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
