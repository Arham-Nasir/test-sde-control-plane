/*
 * Zebra API - Inter-daemon Communication
 *
 * The Zebra API provides communication between routing protocol daemons
 * (BGP, OSPF, etc.) and the Zebra RIB manager. In FRR, this is a Unix
 * socket protocol. In SONiC, similar functionality is provided by the
 * Redis database (CONFIG_DB, APPL_DB).
 *
 * Pattern Reference: lib/zclient.h in FRRouting
 */

#ifndef _ZEBRA_ZAPI_H
#define _ZEBRA_ZAPI_H

#include "rib.h"
#include "common/stream.h"
#include "common/prefix.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ZAPI message types */
typedef enum {
    ZEBRA_INTERFACE_ADD,
    ZEBRA_INTERFACE_DELETE,
    ZEBRA_INTERFACE_ADDRESS_ADD,
    ZEBRA_INTERFACE_ADDRESS_DELETE,
    ZEBRA_INTERFACE_UP,
    ZEBRA_INTERFACE_DOWN,
    ZEBRA_ROUTE_ADD,
    ZEBRA_ROUTE_DELETE,
    ZEBRA_REDISTRIBUTE_ADD,
    ZEBRA_REDISTRIBUTE_DELETE,
    ZEBRA_ROUTER_ID_ADD,
    ZEBRA_ROUTER_ID_DELETE,
    ZEBRA_ROUTER_ID_UPDATE,
    ZEBRA_HELLO,
    ZEBRA_CAPABILITIES,
    ZEBRA_NEXTHOP_REGISTER,
    ZEBRA_NEXTHOP_UNREGISTER,
    ZEBRA_NEXTHOP_UPDATE,
    ZEBRA_INTERFACE_NBR_ADDRESS_ADD,
    ZEBRA_INTERFACE_NBR_ADDRESS_DELETE,
    ZEBRA_INTERFACE_BFD_DEST_UPDATE,
    ZEBRA_BFD_DEST_REGISTER,
    ZEBRA_BFD_DEST_DEREGISTER,
    ZEBRA_BFD_DEST_UPDATE,
    ZEBRA_BFD_DEST_REPLAY,
    ZEBRA_REDISTRIBUTE_ROUTE_ADD,
    ZEBRA_REDISTRIBUTE_ROUTE_DEL,
    ZEBRA_VRF_UNREGISTER,
    ZEBRA_VRF_ADD,
    ZEBRA_VRF_DELETE,
    ZEBRA_VRF_LABEL,
    ZEBRA_LABEL_MANAGER_CONNECT,
    ZEBRA_LABEL_MANAGER_CONNECT_ASYNC,
    ZEBRA_GET_LABEL_CHUNK,
    ZEBRA_RELEASE_LABEL_CHUNK,
    ZEBRA_FEC_REGISTER,
    ZEBRA_FEC_UNREGISTER,
    ZEBRA_FEC_UPDATE,
    ZEBRA_ADVERTISE_DEFAULT_GW,
    ZEBRA_ADVERTISE_SVI_MACIP,
    ZEBRA_ADVERTISE_SUBNET,
    ZEBRA_ADVERTISE_ALL_VNI,
    ZEBRA_LOCAL_ES_ADD,
    ZEBRA_LOCAL_ES_DEL,
    ZEBRA_REMOTE_ES_VTEP_ADD,
    ZEBRA_REMOTE_ES_VTEP_DEL,
    ZEBRA_LOCAL_ES_EVI_ADD,
    ZEBRA_LOCAL_ES_EVI_DEL,
    ZEBRA_VNI_ADD,
    ZEBRA_VNI_DEL,
    ZEBRA_L3VNI_ADD,
    ZEBRA_L3VNI_DEL,
    ZEBRA_REMOTE_VTEP_ADD,
    ZEBRA_REMOTE_VTEP_DEL,
    ZEBRA_MACIP_ADD,
    ZEBRA_MACIP_DEL,
    ZEBRA_IP_PREFIX_ROUTE_ADD,
    ZEBRA_IP_PREFIX_ROUTE_DEL,
    ZEBRA_REMOTE_MACIP_ADD,
    ZEBRA_REMOTE_MACIP_DEL,
    ZEBRA_DUPLICATE_ADDR_DETECTION,
    ZEBRA_PW_ADD,
    ZEBRA_PW_DELETE,
    ZEBRA_PW_SET,
    ZEBRA_PW_UNSET,
    ZEBRA_PW_STATUS_UPDATE,
    ZEBRA_RULE_ADD,
    ZEBRA_RULE_DELETE,
    ZEBRA_RULE_NOTIFY_OWNER,
    ZEBRA_TABLE_MANAGER_CONNECT,
    ZEBRA_GET_TABLE_CHUNK,
    ZEBRA_RELEASE_TABLE_CHUNK,
    ZEBRA_IPSET_CREATE,
    ZEBRA_IPSET_DESTROY,
    ZEBRA_IPSET_ENTRY_ADD,
    ZEBRA_IPSET_ENTRY_DELETE,
    ZEBRA_IPSET_NOTIFY_OWNER,
    ZEBRA_IPSET_ENTRY_NOTIFY_OWNER,
    ZEBRA_IPTABLE_ADD,
    ZEBRA_IPTABLE_DELETE,
    ZEBRA_IPTABLE_NOTIFY_OWNER,
    ZEBRA_VXLAN_FLOOD_CONTROL,
    ZEBRA_VXLAN_SG_ADD,
    ZEBRA_VXLAN_SG_DEL,
    ZEBRA_VXLAN_SG_REPLAY,
    ZEBRA_ERROR,
} zapi_message_t;

/* ZAPI header */
#define ZEBRA_HEADER_SIZE   10

struct zapi_header {
    uint16_t length;
    uint8_t marker;     /* Always 0xFF */
    uint8_t version;    /* ZAPI version */
    uint16_t vrf_id;
    uint16_t command;
};

/* ZAPI route message */
struct zapi_route {
    uint8_t type;
    uint16_t instance;
    uint32_t flags;
    uint8_t message;
    uint8_t safi;
    struct prefix prefix;
    uint8_t nexthop_num;
    struct zapi_nexthop {
        uint8_t type;
        uint32_t ifindex;
        union {
            struct in_addr ipv4;
            struct in6_addr ipv6;
        } gate;
        uint8_t label_num;
        uint32_t labels[4];
    } nexthops[64];
    uint8_t distance;
    uint32_t metric;
    uint32_t tag;
    uint32_t mtu;
    uint32_t tableid;
};

/* ZAPI message flags */
#define ZAPI_MESSAGE_NEXTHOP    (1 << 0)
#define ZAPI_MESSAGE_DISTANCE   (1 << 1)
#define ZAPI_MESSAGE_METRIC     (1 << 2)
#define ZAPI_MESSAGE_TAG        (1 << 3)
#define ZAPI_MESSAGE_MTU        (1 << 4)
#define ZAPI_MESSAGE_SRCPFX     (1 << 5)
#define ZAPI_MESSAGE_LABEL      (1 << 6)
#define ZAPI_MESSAGE_TABLEID    (1 << 7)

/* ZAPI client */
struct zclient {
    int sock;
    struct stream *ibuf;
    struct stream *obuf;

    route_type_t redist_default;
    uint16_t instance;

    /* Callbacks */
    void (*interface_add)(struct zclient *, int, uint16_t);
    void (*interface_delete)(struct zclient *, int, uint16_t);
    void (*interface_up)(struct zclient *, int, uint16_t);
    void (*interface_down)(struct zclient *, int, uint16_t);
    void (*interface_address_add)(struct zclient *, int, uint16_t);
    void (*interface_address_delete)(struct zclient *, int, uint16_t);
    void (*route_add)(struct zclient *, int, uint16_t);
    void (*route_delete)(struct zclient *, int, uint16_t);
    void (*nexthop_update)(struct zclient *, int, uint16_t);
    void (*router_id_update)(struct zclient *, int, uint16_t);
};

/* Client management */
struct zclient *zclient_new(void);
void zclient_free(struct zclient *zclient);
void zclient_init(struct zclient *zclient, route_type_t type, uint16_t instance);

/* Connection */
int zclient_connect(struct zclient *zclient, const char *path);
void zclient_disconnect(struct zclient *zclient);
int zclient_read(struct zclient *zclient);

/* Message sending */
int zclient_send_hello(struct zclient *zclient);
int zclient_send_router_id_request(struct zclient *zclient);
int zclient_send_redistribute(struct zclient *zclient, route_type_t type);
int zclient_send_route(struct zclient *zclient, zapi_message_t cmd,
                       struct zapi_route *route);
int zclient_send_nexthop_register(struct zclient *zclient,
                                  struct prefix *p, bool connected);
int zclient_send_nexthop_unregister(struct zclient *zclient, struct prefix *p);

/* Message parsing */
int zapi_route_encode(struct stream *s, struct zapi_route *route);
int zapi_route_decode(struct stream *s, struct zapi_route *route);

#ifdef __cplusplus
}
#endif

#endif /* _ZEBRA_ZAPI_H */
