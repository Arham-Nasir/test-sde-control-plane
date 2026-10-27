/*
 * BGP Route Reflector Tests
 *
 * Test suite for Principal-level assessment.
 */

#include "lib/common/memory.h"
#include "lib/common/log.h"
#include "lib/common/thread.h"
#include "protocols/bgp/bgpd.h"
#include "protocols/bgp/bgp_neighbor.h"
#include "protocols/bgp/bgp_packet.h"
#include "protocols/bgp/bgp_route.h"
#include "protocols/bgp/bgp_attr.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>

/* Forward declaration for candidate's implementation */
struct bgp_attr *bgp_rr_prepare_attr(struct bgp *bgp, struct peer *from,
                                       struct peer *to, struct bgp_attr *attr);

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

static void test_setup(void)
{
    struct in_addr router_id, cluster_id;

    master = event_loop_new();
    bgp_master_init(master);
    bgp = bgp_create(65001, NULL);

    inet_pton(AF_INET, "1.1.1.1", &router_id);
    bgp_router_id_set(bgp, &router_id);

    inet_pton(AF_INET, "10.0.0.1", &cluster_id);
    bgp_cluster_id_set(bgp, &cluster_id);

    /* Enable Route Reflector mode */
    bgp->rr_enabled = true;
}

static void test_teardown(void)
{
    bgp_delete(bgp);
    bgp_master_finish();
    event_loop_free(master);
}

static struct peer *create_test_peer(const char *addr, uint32_t remote_as,
                                     bool is_client)
{
    union sockunion su;
    struct peer *peer;

    memset(&su, 0, sizeof(su));
    su.sin.sin_family = AF_INET;
    inet_pton(AF_INET, addr, &su.sin.sin_addr);

    peer = peer_create(bgp, &su, remote_as);
    peer_activate(peer, AFI_IP, SAFI_UNICAST);

    if (is_client)
        peer_rr_client_set(peer);

    inet_pton(AF_INET, addr, &peer->remote_id);
    peer->state = BGP_STATUS_ESTABLISHED;

    return peer;
}

/* Helper to create a test path info */
static struct bgp_path_info *create_test_path(struct peer *peer)
{
    struct bgp_path_info *pi;
    struct bgp_attr *attr;

    attr = bgp_attr_new();
    attr->origin = BGP_ORIGIN_IGP;
    attr->local_pref = 100;

    pi = bgp_path_info_create(peer, attr);
    return pi;
}

/* Helper to free test path info */
static void free_test_path(struct bgp_path_info *pi)
{
    if (pi) {
        /* bgp_path_info_free calls bgp_attr_unintern internally */
        bgp_path_info_free(pi);
    }
}

/*
 * =============================================================================
 * Test Cases
 * =============================================================================
 */

/* Test: Basic client/non-client classification */
static int test_client_classification(void)
{
    struct peer *client, *non_client;

    test_setup();

    client = create_test_peer("10.0.0.2", 65001, true);
    non_client = create_test_peer("10.0.0.3", 65001, false);

    TEST_ASSERT(peer_is_rr_client(client), "Peer should be client");
    TEST_ASSERT(!peer_is_rr_client(non_client), "Peer should not be client");

    test_teardown();
    return 0;
}

/* Test: Client to client reflection */
static int test_client_to_client_reflection(void)
{
    struct peer *client1, *client2;
    struct bgp_path_info *pi;
    bool should_reflect;

    test_setup();

    client1 = create_test_peer("10.0.0.2", 65001, true);
    client2 = create_test_peer("10.0.0.3", 65001, true);
    pi = create_test_path(client1);

    /* Route from client1 should be reflected to client2 */
    should_reflect = bgp_rr_should_reflect(bgp, client1, client2, pi);
    TEST_ASSERT(should_reflect, "Route from client should be reflected to another client");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Test: Client to non-client reflection */
static int test_client_to_non_client_reflection(void)
{
    struct peer *client, *non_client;
    struct bgp_path_info *pi;
    bool should_reflect;

    test_setup();

    client = create_test_peer("10.0.0.2", 65001, true);
    non_client = create_test_peer("10.0.0.3", 65001, false);
    pi = create_test_path(client);

    /* Route from client should be reflected to non-client */
    should_reflect = bgp_rr_should_reflect(bgp, client, non_client, pi);
    TEST_ASSERT(should_reflect, "Route from client should be reflected to non-client");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Test: Non-client to client reflection */
static int test_non_client_to_client_reflection(void)
{
    struct peer *non_client, *client;
    struct bgp_path_info *pi;
    bool should_reflect;

    test_setup();

    non_client = create_test_peer("10.0.0.2", 65001, false);
    client = create_test_peer("10.0.0.3", 65001, true);
    pi = create_test_path(non_client);

    /* Route from non-client should be reflected to client */
    should_reflect = bgp_rr_should_reflect(bgp, non_client, client, pi);
    TEST_ASSERT(should_reflect, "Route from non-client should be reflected to client");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Test: Non-client to non-client - NO reflection */
static int test_non_client_to_non_client_no_reflection(void)
{
    struct peer *non_client1, *non_client2;
    struct bgp_path_info *pi;
    bool should_reflect;

    test_setup();

    non_client1 = create_test_peer("10.0.0.2", 65001, false);
    non_client2 = create_test_peer("10.0.0.3", 65001, false);
    pi = create_test_path(non_client1);

    /* Route from non-client should NOT be reflected to another non-client */
    should_reflect = bgp_rr_should_reflect(bgp, non_client1, non_client2, pi);
    TEST_ASSERT(!should_reflect, "Route from non-client should NOT be reflected to another non-client");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Test: eBGP to all reflection */
static int test_ebgp_to_all_reflection(void)
{
    struct peer *ebgp_peer, *client, *non_client;
    struct bgp_path_info *pi;
    bool reflect_to_client, reflect_to_non_client;

    test_setup();

    ebgp_peer = create_test_peer("10.0.0.2", 65002, false);  /* Different AS */
    client = create_test_peer("10.0.0.3", 65001, true);
    non_client = create_test_peer("10.0.0.4", 65001, false);
    pi = create_test_path(ebgp_peer);

    /* Route from eBGP should be reflected to all iBGP peers */
    reflect_to_client = bgp_rr_should_reflect(bgp, ebgp_peer, client, pi);
    reflect_to_non_client = bgp_rr_should_reflect(bgp, ebgp_peer, non_client, pi);

    TEST_ASSERT(reflect_to_client, "eBGP route should be reflected to client");
    TEST_ASSERT(reflect_to_non_client, "eBGP route should be reflected to non-client");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Test: Cluster loop detection */
static int test_cluster_loop_detection(void)
{
    struct peer *client1, *client2;
    struct bgp_path_info *pi;
    struct in_addr cluster_id;
    bool should_reflect;

    test_setup();

    client1 = create_test_peer("10.0.0.2", 65001, true);
    client2 = create_test_peer("10.0.0.3", 65001, true);
    pi = create_test_path(client1);

    /* Add our cluster ID to the cluster list - simulates a loop */
    inet_pton(AF_INET, "10.0.0.1", &cluster_id);  /* Same as our cluster ID */
    pi->attr->cluster_list = cluster_list_prepend(NULL, &cluster_id);
    pi->attr->flags |= BGP_ATTR_FLAG_HAS_CLUSTER;

    /* Route with our cluster ID in cluster list should NOT be reflected (loop) */
    should_reflect = bgp_rr_should_reflect(bgp, client1, client2, pi);
    TEST_ASSERT(!should_reflect, "Route with our cluster ID should NOT be reflected (cluster loop)");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Test: ORIGINATOR_ID attribute setting */
static int test_originator_id_setting(void)
{
    struct peer *client1, *client2;
    struct bgp_attr *attr, *new_attr;

    test_setup();

    client1 = create_test_peer("10.0.0.2", 65001, true);
    client2 = create_test_peer("10.0.0.3", 65001, true);

    attr = bgp_attr_new();
    attr->origin = BGP_ORIGIN_IGP;

    /* Prepare attributes for reflection */
    new_attr = bgp_rr_prepare_attr(bgp, client1, client2, attr);

    TEST_ASSERT(new_attr != NULL, "bgp_rr_prepare_attr should return non-NULL");
    TEST_ASSERT(new_attr->flags & BGP_ATTR_FLAG_HAS_ORIGINATOR,
                "ORIGINATOR_ID flag should be set");
    TEST_ASSERT(new_attr->originator_id.s_addr == client1->remote_id.s_addr,
                "ORIGINATOR_ID should be set to source peer's router ID");

    bgp_attr_free(attr);
    bgp_attr_free(new_attr);
    test_teardown();
    return 0;
}

/* Test: CLUSTER_LIST prepending */
static int test_cluster_list_prepending(void)
{
    struct peer *client1, *client2;
    struct bgp_attr *attr, *new_attr;

    test_setup();

    client1 = create_test_peer("10.0.0.2", 65001, true);
    client2 = create_test_peer("10.0.0.3", 65001, true);

    attr = bgp_attr_new();
    attr->origin = BGP_ORIGIN_IGP;

    /* Prepare attributes for reflection */
    new_attr = bgp_rr_prepare_attr(bgp, client1, client2, attr);

    TEST_ASSERT(new_attr != NULL, "bgp_rr_prepare_attr should return non-NULL");
    TEST_ASSERT(new_attr->flags & BGP_ATTR_FLAG_HAS_CLUSTER,
                "CLUSTER_LIST flag should be set");
    TEST_ASSERT(new_attr->cluster_list != NULL,
                "CLUSTER_LIST should not be NULL");
    TEST_ASSERT(new_attr->cluster_list->count >= 1,
                "CLUSTER_LIST should have at least one entry");
    TEST_ASSERT(new_attr->cluster_list->clusters[0].s_addr == bgp->cluster_id.s_addr,
                "First cluster ID should be our cluster ID");

    bgp_attr_free(attr);
    bgp_attr_free(new_attr);
    test_teardown();
    return 0;
}

/* Test: No reflection back to sender */
static int test_no_reflection_to_sender(void)
{
    struct peer *peer;
    struct bgp_path_info *pi;
    bool should_reflect;

    test_setup();

    peer = create_test_peer("10.0.0.2", 65001, true);
    pi = create_test_path(peer);

    /* Route from peer should NOT be reflected back to same peer */
    should_reflect = bgp_rr_should_reflect(bgp, peer, peer, pi);
    TEST_ASSERT(!should_reflect, "Route should NOT be reflected back to sender");

    free_test_path(pi);
    test_teardown();
    return 0;
}

/* Scalability test: Many clients */
static int test_scalability_many_clients(void)
{
    struct peer *peers[100];
    char addr[16];
    int i;
    int client_count = 0;

    test_setup();

    /* Create 100 clients */
    for (i = 0; i < 100; i++) {
        snprintf(addr, sizeof(addr), "10.0.%d.%d", i / 256, i % 256);
        peers[i] = create_test_peer(addr, 65001, true);
    }

    /* Verify all clients are tracked correctly */
    for (i = 0; i < 100; i++) {
        if (peer_is_rr_client(peers[i]))
            client_count++;
    }

    TEST_ASSERT(client_count == 100, "All 100 peers should be RR clients");

    test_teardown();
    return 0;
}

/* Test: RR disabled - no reflection */
static int test_rr_disabled_no_reflection(void)
{
    struct peer *client1, *client2;
    struct bgp_path_info *pi;
    bool should_reflect;

    test_setup();

    /* Disable RR mode */
    bgp->rr_enabled = false;

    client1 = create_test_peer("10.0.0.2", 65001, true);
    client2 = create_test_peer("10.0.0.3", 65001, true);
    pi = create_test_path(client1);

    /* With RR disabled, no reflection should occur */
    should_reflect = bgp_rr_should_reflect(bgp, client1, client2, pi);
    TEST_ASSERT(!should_reflect, "With RR disabled, no reflection should occur");

    free_test_path(pi);
    test_teardown();
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf("===========================================\n");
    printf("BGP Route Reflector Test Suite\n");
    printf("===========================================\n\n");

    log_init("test_rr", LOG_DEST_STDOUT);
    log_set_level(LOG_LEVEL_DEBUG);

    RUN_TEST("Client Classification", test_client_classification);
    RUN_TEST("Client to Client Reflection", test_client_to_client_reflection);
    RUN_TEST("Client to Non-Client Reflection", test_client_to_non_client_reflection);
    RUN_TEST("Non-Client to Client Reflection", test_non_client_to_client_reflection);
    RUN_TEST("Non-Client to Non-Client No Reflection", test_non_client_to_non_client_no_reflection);
    RUN_TEST("eBGP to All Reflection", test_ebgp_to_all_reflection);
    RUN_TEST("Cluster Loop Detection", test_cluster_loop_detection);
    RUN_TEST("ORIGINATOR_ID Setting", test_originator_id_setting);
    RUN_TEST("CLUSTER_LIST Prepending", test_cluster_list_prepending);
    RUN_TEST("No Reflection to Sender", test_no_reflection_to_sender);
    RUN_TEST("Scalability: Many Clients", test_scalability_many_clients);
    RUN_TEST("RR Disabled - No Reflection", test_rr_disabled_no_reflection);

    printf("\n");
    printf("===========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("         %d tests FAILED\n", tests_failed);
    printf("===========================================\n\n");

    log_close();

    return (tests_failed > 0) ? 1 : 0;
}
