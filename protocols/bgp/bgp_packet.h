/*
 * BGP Packet Handling
 *
 * Functions for encoding and decoding BGP protocol messages.
 *
 * Pattern Reference: bgpd/bgp_packet.h in FRRouting
 */

#ifndef _BGP_PACKET_H
#define _BGP_PACKET_H

#include "bgpd.h"
#include "bgp_neighbor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BGP Packet Header */
struct bgp_header {
    uint8_t marker[16];     /* All 1s */
    uint16_t length;        /* Total length including header */
    uint8_t type;           /* Message type */
} __attribute__((packed));

/* BGP OPEN Message */
struct bgp_open {
    uint8_t version;
    uint16_t my_as;
    uint16_t holdtime;
    uint32_t router_id;
    uint8_t opt_len;
    /* Optional parameters follow */
} __attribute__((packed));

/* BGP Optional Parameter */
struct bgp_opt_param {
    uint8_t type;
    uint8_t length;
    /* Value follows */
} __attribute__((packed));

/* BGP Capability */
struct bgp_capability {
    uint8_t code;
    uint8_t length;
    /* Value follows */
} __attribute__((packed));

/* BGP NOTIFICATION Message */
struct bgp_notification {
    uint8_t error_code;
    uint8_t error_subcode;
    /* Data follows */
} __attribute__((packed));

/* BGP UPDATE Message Path Attributes */
#define BGP_ATTR_FLAG_OPTIONAL      (1 << 7)
#define BGP_ATTR_FLAG_TRANSITIVE    (1 << 6)
#define BGP_ATTR_FLAG_PARTIAL       (1 << 5)
#define BGP_ATTR_FLAG_EXTLEN        (1 << 4)

/* BGP Attribute Types */
#define BGP_ATTR_ORIGIN             1
#define BGP_ATTR_AS_PATH            2
#define BGP_ATTR_NEXT_HOP           3
#define BGP_ATTR_MULTI_EXIT_DISC    4
#define BGP_ATTR_LOCAL_PREF         5
#define BGP_ATTR_ATOMIC_AGGREGATE   6
#define BGP_ATTR_AGGREGATOR         7
#define BGP_ATTR_COMMUNITIES        8
#define BGP_ATTR_ORIGINATOR_ID      9
#define BGP_ATTR_CLUSTER_LIST       10
#define BGP_ATTR_MP_REACH_NLRI      14
#define BGP_ATTR_MP_UNREACH_NLRI    15
#define BGP_ATTR_EXT_COMMUNITIES    16
#define BGP_ATTR_AS4_PATH           17
#define BGP_ATTR_AS4_AGGREGATOR     18
#define BGP_ATTR_LARGE_COMMUNITIES  32

/* ORIGIN values */
#define BGP_ORIGIN_IGP      0
#define BGP_ORIGIN_EGP      1
#define BGP_ORIGIN_INCOMPLETE 2

/* AS_PATH segment types */
#define BGP_AS_SET          1
#define BGP_AS_SEQUENCE     2
#define BGP_AS_CONFED_SET   3
#define BGP_AS_CONFED_SEQ   4

/* Message sending */
int bgp_open_send(struct peer *peer);
int bgp_update_send(struct peer *peer, struct prefix *p, void *attr);
int bgp_withdraw_send(struct peer *peer, struct prefix *p);
int bgp_keepalive_send(struct peer *peer);
int bgp_notify_send(struct peer *peer, uint8_t code, uint8_t subcode);
int bgp_notify_send_with_data(struct peer *peer, uint8_t code, uint8_t subcode,
                              uint8_t *data, size_t datalen);
int bgp_route_refresh_send(struct peer *peer, uint8_t afi, uint8_t safi);

/* End-of-RIB marker (empty UPDATE) */
int bgp_end_of_rib_send(struct peer *peer, uint8_t afi, uint8_t safi);

/* Message receiving */
int bgp_read(struct peer *peer);
int bgp_process_packet(struct peer *peer);

/* Message parsing */
int bgp_open_receive(struct peer *peer, struct stream *s, size_t size);
int bgp_update_receive(struct peer *peer, struct stream *s, size_t size);
int bgp_keepalive_receive(struct peer *peer, struct stream *s, size_t size);
int bgp_notification_receive(struct peer *peer, struct stream *s, size_t size);
int bgp_route_refresh_receive(struct peer *peer, struct stream *s, size_t size);

/* Capability parsing */
int bgp_capability_parse(struct peer *peer, uint8_t *data, size_t length);

/* Error names */
const char *bgp_error_str(uint8_t code);
const char *bgp_error_subcode_str(uint8_t code, uint8_t subcode);

#ifdef __cplusplus
}
#endif

#endif /* _BGP_PACKET_H */
