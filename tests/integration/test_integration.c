/*
 * Integration Tests
 *
 * Tests that verify the overall system works correctly.
 */

#include "lib/common/memory.h"
#include "lib/common/log.h"
#include "lib/common/thread.h"
#include "lib/common/prefix.h"
#include "lib/zebra/rib.h"
#include "protocols/bgp/bgpd.h"
#include "protocols/bgp/bgp_neighbor.h"
#include "protocols/bgp/bgp_route.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

static int tests_passed = 0;
static int tests_run = 0;

#define RUN_TEST(name, func) do { \
    printf("Running: %s\n", name); \
    tests_run++; \
    if (func() == 0) { \
        printf("  PASS\n"); \
        tests_passed++; \
    } else { \
        printf("  FAIL\n"); \
    } \
} while (0)

/* Test: Complete BGP instance lifecycle */
static int test_bgp_lifecycle(void)
{
    struct event_loop *master;
    struct bgp *bgp;
    struct in_addr router_id;

    master = event_loop_new();
    if (!master) return -1;

    bgp_master_init(master);

    bgp = bgp_create(65001, NULL);
    if (!bgp) {
        event_loop_free(master);
        return -1;
    }

    inet_pton(AF_INET, "1.1.1.1", &router_id);
    bgp_router_id_set(bgp, &router_id);

    if (bgp->as != 65001) return -1;
    if (bgp->router_id.s_addr != router_id.s_addr) return -1;

    bgp_delete(bgp);
    bgp_master_finish();
    event_loop_free(master);

    return 0;
}

/* Test: Peer creation and management */
static int test_peer_management(void)
{
    struct event_loop *master;
    struct bgp *bgp;
    struct peer *peer;
    union sockunion su;
    struct in_addr router_id;

    master = event_loop_new();
    bgp_master_init(master);
    bgp = bgp_create(65001, NULL);

    inet_pton(AF_INET, "1.1.1.1", &router_id);
    bgp_router_id_set(bgp, &router_id);

    memset(&su, 0, sizeof(su));
    su.sin.sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.2", &su.sin.sin_addr);

    peer = peer_create(bgp, &su, 65002);
    if (!peer) {
        bgp_delete(bgp);
        bgp_master_finish();
        event_loop_free(master);
        return -1;
    }

    if (peer->remote_as != 65002) return -1;
    if (peer->bgp != bgp) return -1;

    peer_delete(peer);
    bgp_delete(bgp);
    bgp_master_finish();
    event_loop_free(master);

    return 0;
}

/* Test: Route table operations */
static int test_route_table(void)
{
    struct route_table *table;
    struct route_node *rn;
    struct prefix p;

    table = route_table_init();
    if (!table) return -1;

    str2prefix("192.168.1.0/24", &p);
    rn = route_node_get(table, &p);
    if (!rn) {
        route_table_finish(table);
        return -1;
    }

    route_unlock_node(rn);

    rn = route_node_lookup(table, &p);
    if (!rn) {
        route_table_finish(table);
        return -1;
    }

    route_unlock_node(rn);
    route_table_finish(table);

    return 0;
}

/* Test: Prefix operations */
static int test_prefix_operations(void)
{
    struct prefix p1, p2;
    char buf[PREFIX_STRLEN];

    str2prefix("10.0.0.0/8", &p1);
    str2prefix("10.1.2.0/24", &p2);

    if (!prefix_match(&p1, &p2)) return -1;

    prefix2str(&p1, buf, sizeof(buf));
    if (strcmp(buf, "10.0.0.0/8") != 0) return -1;

    return 0;
}

/* Test: Memory tracking */
static int test_memory_tracking(void)
{
    void *ptr;
    mem_stats_t stats;

    mem_stats_reset();

    ptr = XMALLOC(MTYPE_TMP, 100);
    if (!ptr) return -1;

    mem_stats_get(MTYPE_TMP, &stats);
    if (stats.alloc_count == 0) return -1;

    XFREE(MTYPE_TMP, ptr);

    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("\n");
    printf("==========================================\n");
    printf("Integration Test Suite\n");
    printf("==========================================\n\n");

    log_init("integration", LOG_DEST_STDOUT);
    log_set_level(LOG_LEVEL_INFO);

    RUN_TEST("BGP Lifecycle", test_bgp_lifecycle);
    RUN_TEST("Peer Management", test_peer_management);
    RUN_TEST("Route Table", test_route_table);
    RUN_TEST("Prefix Operations", test_prefix_operations);
    RUN_TEST("Memory Tracking", test_memory_tracking);

    printf("\n");
    printf("==========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("==========================================\n\n");

    log_close();

    return (tests_passed == tests_run) ? 0 : 1;
}
