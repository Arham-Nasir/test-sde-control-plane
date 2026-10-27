/*
 * BGP Packet Implementation
 */

#include "bgp_packet.h"
#include "bgp_fsm.h"
#include "common/log.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#undef LOG_TAG
#define LOG_TAG "BGP"

/* BGP marker (all 1s) */
static const uint8_t bgp_marker[16] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static void bgp_packet_set_marker(struct stream *s)
{
    stream_put(s, bgp_marker, sizeof(bgp_marker));
}

static int bgp_packet_set_header(struct stream *s, uint8_t type)
{
    size_t cp = stream_get_endp(s);

    /* Update length in header */
    stream_putw_at(s, 16, cp);

    /* Set type */
    stream_putc_at(s, 18, type);

    return 0;
}

int bgp_open_send(struct peer *peer)
{
    struct stream *s = peer->obuf;
    uint16_t send_holdtime;
    uint32_t local_as;

    stream_reset(s);

    /* BGP header placeholder */
    bgp_packet_set_marker(s);
    stream_putw(s, 0);          /* Length placeholder */
    stream_putc(s, BGP_MSG_OPEN);

    /* BGP OPEN message */
    stream_putc(s, BGP_VERSION_4);

    /* AS number (use AS_TRANS for 4-byte AS) */
    local_as = peer->bgp->as;
    if (local_as > 65535)
        stream_putw(s, 23456);  /* AS_TRANS */
    else
        stream_putw(s, local_as);

    /* Hold Time */
    send_holdtime = peer->holdtime;
    stream_putw(s, send_holdtime);

    /* BGP Identifier */
    stream_putl(s, ntohl(peer->bgp->router_id.s_addr));

    /* Optional parameters length placeholder */
    size_t opt_len_pos = stream_get_endp(s);
    stream_putc(s, 0);

    /* Add capabilities */
    size_t cap_start = stream_get_endp(s);

    /* 4-byte AS capability */
    stream_putc(s, 2);          /* Parameter type: Capability */
    stream_putc(s, 6);          /* Length */
    stream_putc(s, BGP_CAP_CODE_AS4);
    stream_putc(s, 4);
    stream_putl(s, local_as);

    /* Multiprotocol capability for IPv4 unicast */
    stream_putc(s, 2);          /* Parameter type */
    stream_putc(s, 6);          /* Length */
    stream_putc(s, BGP_CAP_CODE_MP);
    stream_putc(s, 4);
    stream_putw(s, AFI_IP);
    stream_putc(s, 0);
    stream_putc(s, SAFI_UNICAST);

    /* Route refresh capability */
    stream_putc(s, 2);
    stream_putc(s, 2);
    stream_putc(s, BGP_CAP_CODE_RR);
    stream_putc(s, 0);

    /* Graceful Restart capability (if enabled) */
    if (peer->flags & PEER_FLAG_GRACEFUL_RESTART) {
        uint16_t gr_flags = 0;
        if (peer->bgp->gr_flags & BGP_FLAG_GR_PRESERVE_FWD)
            gr_flags |= BGP_GR_FLAG_RESTART;

        stream_putc(s, 2);          /* Parameter type */
        stream_putc(s, 8);          /* Length */
        stream_putc(s, BGP_CAP_CODE_GR);
        stream_putc(s, 6);
        stream_putw(s, (gr_flags << 12) | peer->bgp->gr_restart_time);

        /* IPv4 Unicast with forwarding state */
        stream_putw(s, AFI_IP);
        stream_putc(s, SAFI_UNICAST);
        stream_putc(s, BGP_GR_FLAG_FORWARDING);
    }

    /* Update optional parameters length */
    size_t opt_len = stream_get_endp(s) - cap_start;
    stream_putc_at(s, opt_len_pos, opt_len);

    /* Finalize header */
    bgp_packet_set_header(s, BGP_MSG_OPEN);

    /* Send */
    ssize_t written = write(peer->fd, STREAM_DATA(s), stream_get_endp(s));
    if (written < 0) {
        log_err("BGP peer %s OPEN send failed: %s", peer->host, strerror(errno));
        return -1;
    }

    peer->open_out++;
    log_debug("BGP peer %s sent OPEN (AS=%u hold=%u)",
              peer->host, peer->bgp->as, send_holdtime);

    return 0;
}

int bgp_keepalive_send(struct peer *peer)
{
    struct stream *s = peer->obuf;

    stream_reset(s);

    bgp_packet_set_marker(s);
    stream_putw(s, BGP_HEADER_SIZE);
    stream_putc(s, BGP_MSG_KEEPALIVE);

    ssize_t written = write(peer->fd, STREAM_DATA(s), stream_get_endp(s));
    if (written < 0) {
        log_err("BGP peer %s KEEPALIVE send failed: %s",
                peer->host, strerror(errno));
        return -1;
    }

    peer->keepalive_out++;
    peer->last_write = time(NULL);

    return 0;
}

int bgp_notify_send(struct peer *peer, uint8_t code, uint8_t subcode)
{
    return bgp_notify_send_with_data(peer, code, subcode, NULL, 0);
}

int bgp_notify_send_with_data(struct peer *peer, uint8_t code, uint8_t subcode,
                              uint8_t *data, size_t datalen)
{
    struct stream *s = peer->obuf;

    stream_reset(s);

    bgp_packet_set_marker(s);
    stream_putw(s, 0);          /* Length placeholder */
    stream_putc(s, BGP_MSG_NOTIFICATION);

    stream_putc(s, code);
    stream_putc(s, subcode);

    if (data && datalen > 0)
        stream_put(s, data, datalen);

    bgp_packet_set_header(s, BGP_MSG_NOTIFICATION);

    if (peer->fd < 0) {
        peer->notify_out++;
        log_debug("BGP peer %s NOTIFICATION simulated: %s/%s",
                  peer->host, bgp_error_str(code), bgp_error_subcode_str(code, subcode));
        return 0;
    }

    ssize_t written = write(peer->fd, STREAM_DATA(s), stream_get_endp(s));
    if (written < 0) {
        log_err("BGP peer %s NOTIFICATION send failed: %s",
                peer->host, strerror(errno));
        return -1;
    }

    peer->notify_out++;
    log_warn("BGP peer %s sent NOTIFICATION: %s/%s",
             peer->host, bgp_error_str(code), bgp_error_subcode_str(code, subcode));

    return 0;
}

int bgp_end_of_rib_send(struct peer *peer, uint8_t afi, uint8_t safi)
{
    struct stream *s = peer->obuf;

    stream_reset(s);

    bgp_packet_set_marker(s);
    stream_putw(s, 0);          /* Length placeholder */
    stream_putc(s, BGP_MSG_UPDATE);

    /* Empty UPDATE (no withdrawn, no attributes, no NLRI) */
    stream_putw(s, 0);          /* Withdrawn routes length */
    stream_putw(s, 0);          /* Total path attribute length */

    bgp_packet_set_header(s, BGP_MSG_UPDATE);

    if (peer->fd < 0) {
        peer->update_out++;
        log_debug("BGP peer %s sent End-of-RIB for afi=%d safi=%d (simulated)",
                  peer->host, afi, safi);
        return 0;
    }

    ssize_t written = write(peer->fd, STREAM_DATA(s), stream_get_endp(s));
    if (written < 0) {
        log_err("BGP peer %s End-of-RIB send failed: %s",
                peer->host, strerror(errno));
        return -1;
    }

    peer->update_out++;
    log_debug("BGP peer %s sent End-of-RIB for afi=%d safi=%d",
              peer->host, afi, safi);

    return 0;
}

int bgp_route_refresh_send(struct peer *peer, uint8_t afi, uint8_t safi)
{
    struct stream *s = peer->obuf;

    stream_reset(s);

    bgp_packet_set_marker(s);
    stream_putw(s, 0);
    stream_putc(s, BGP_MSG_ROUTE_REFRESH);

    stream_putw(s, afi);
    stream_putc(s, 0);
    stream_putc(s, safi);

    bgp_packet_set_header(s, BGP_MSG_ROUTE_REFRESH);

    ssize_t written = write(peer->fd, STREAM_DATA(s), stream_get_endp(s));
    if (written < 0) {
        log_err("BGP peer %s ROUTE-REFRESH send failed: %s",
                peer->host, strerror(errno));
        return -1;
    }

    peer->refresh_out++;
    return 0;
}

int bgp_update_send(struct peer *peer, struct prefix *p, void *attr)
{
    (void)p;
    (void)attr;
    /* TODO: Implement UPDATE encoding */
    log_debug("BGP peer %s UPDATE send (TODO)", peer->host);
    peer->update_out++;
    return 0;
}

int bgp_withdraw_send(struct peer *peer, struct prefix *p)
{
    (void)p;
    /* TODO: Implement withdrawal encoding */
    log_debug("BGP peer %s WITHDRAW send (TODO)", peer->host);
    peer->update_out++;
    return 0;
}

int bgp_read(struct peer *peer)
{
    struct stream *s = peer->ibuf;
    ssize_t nbytes;

    nbytes = read(peer->fd, STREAM_DATA(s) + stream_get_endp(s),
                  stream_get_size(s) - stream_get_endp(s));

    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        log_err("BGP peer %s read error: %s", peer->host, strerror(errno));
        return -1;
    }

    if (nbytes == 0) {
        log_info("BGP peer %s connection closed", peer->host);
        return -1;
    }

    stream_forward_endp(s, nbytes);
    peer->last_read = time(NULL);

    return nbytes;
}

int bgp_process_packet(struct peer *peer)
{
    struct stream *s = peer->ibuf;
    struct bgp_header hdr;

    /* Check if we have a complete header */
    if (STREAM_READABLE(s) < BGP_HEADER_SIZE)
        return 0;

    /* Read header without advancing position */
    stream_get_from(&hdr.marker, s, 0, 16);
    hdr.length = stream_getw_from(s, 16);
    hdr.type = stream_getc_from(s, 18);

    /* Validate marker */
    if (memcmp(hdr.marker, bgp_marker, 16) != 0) {
        log_err("BGP peer %s invalid marker", peer->host);
        bgp_notify_send(peer, BGP_NOTIFY_HEADER_ERR, 1);
        return -1;
    }

    /* Validate length */
    if (hdr.length < BGP_HEADER_SIZE || hdr.length > BGP_MAX_PACKET_SIZE) {
        log_err("BGP peer %s invalid length %u", peer->host, hdr.length);
        bgp_notify_send(peer, BGP_NOTIFY_HEADER_ERR, 2);
        return -1;
    }

    /* Check if we have complete message */
    if (STREAM_READABLE(s) < hdr.length)
        return 0;

    /* Skip past header */
    stream_forward_getp(s, BGP_HEADER_SIZE);

    /* Process message */
    size_t msg_len = hdr.length - BGP_HEADER_SIZE;
    int ret = 0;

    switch (hdr.type) {
    case BGP_MSG_OPEN:
        ret = bgp_open_receive(peer, s, msg_len);
        break;
    case BGP_MSG_UPDATE:
        ret = bgp_update_receive(peer, s, msg_len);
        break;
    case BGP_MSG_NOTIFICATION:
        ret = bgp_notification_receive(peer, s, msg_len);
        break;
    case BGP_MSG_KEEPALIVE:
        ret = bgp_keepalive_receive(peer, s, msg_len);
        break;
    case BGP_MSG_ROUTE_REFRESH:
        ret = bgp_route_refresh_receive(peer, s, msg_len);
        break;
    default:
        log_err("BGP peer %s unknown message type %d", peer->host, hdr.type);
        bgp_notify_send(peer, BGP_NOTIFY_HEADER_ERR, 3);
        return -1;
    }

    /* Reset stream for next message */
    stream_reset(s);

    return ret;
}

int bgp_open_receive(struct peer *peer, struct stream *s, size_t size)
{
    uint8_t version;
    uint16_t remote_as;
    uint16_t holdtime;
    uint32_t router_id;
    uint8_t opt_len;

    if (size < 10) {
        log_err("BGP peer %s OPEN too short", peer->host);
        bgp_notify_send(peer, BGP_NOTIFY_OPEN_ERR, 0);
        return -1;
    }

    version = stream_getc(s);
    remote_as = stream_getw(s);
    holdtime = stream_getw(s);
    router_id = stream_getl(s);
    opt_len = stream_getc(s);

    if (version != BGP_VERSION_4) {
        log_err("BGP peer %s unsupported version %d", peer->host, version);
        bgp_notify_send(peer, BGP_NOTIFY_OPEN_ERR,
                        BGP_NOTIFY_OPEN_UNSUPPORTED_VERSION);
        return -1;
    }

    /* Store peer information */
    if (remote_as != 23456)     /* AS_TRANS */
        peer->remote_as = remote_as;
    peer->remote_id.s_addr = htonl(router_id);

    /* Negotiate holdtime */
    if (holdtime < peer->holdtime)
        peer->v_holdtime = holdtime;
    else
        peer->v_holdtime = peer->holdtime;
    peer->v_keepalive = peer->v_holdtime / 3;

    /* Parse optional parameters */
    if (opt_len > 0 && STREAM_READABLE(s) >= opt_len) {
        bgp_capability_parse(peer, STREAM_PNT(s), opt_len);
        stream_forward_getp(s, opt_len);
    }

    peer->open_in++;

    char router_id_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer->remote_id, router_id_str, sizeof(router_id_str));
    log_info("BGP peer %s received OPEN (AS=%u router-id=%s hold=%u)",
             peer->host, peer->remote_as, router_id_str, holdtime);

    /* Trigger FSM event */
    return bgp_fsm_event(peer, BGP_EVENT_OPEN_RCVD);
}

int bgp_update_receive(struct peer *peer, struct stream *s, size_t size)
{
    uint16_t withdrawn_len;
    uint16_t attr_len;

    if (size < 4) {
        log_err("BGP peer %s UPDATE too short", peer->host);
        bgp_notify_send(peer, BGP_NOTIFY_UPDATE_ERR,
                        BGP_NOTIFY_UPDATE_MALFORMED_ATTR);
        return -1;
    }

    withdrawn_len = stream_getw(s);
    stream_forward_getp(s, withdrawn_len);  /* Skip withdrawn routes */

    attr_len = stream_getw(s);

    /* Check for End-of-RIB (empty UPDATE) */
    if (withdrawn_len == 0 && attr_len == 0) {
        log_info("BGP peer %s received End-of-RIB", peer->host);
        bgp_gr_end_of_rib(peer, AFI_IP, SAFI_UNICAST);
        peer->update_in++;
        return bgp_fsm_event(peer, BGP_EVENT_UPDATE_RCVD);
    }

    /* TODO: Parse path attributes and NLRI */
    stream_forward_getp(s, attr_len);

    peer->update_in++;
    log_debug("BGP peer %s received UPDATE (withdrawn=%u attr=%u)",
              peer->host, withdrawn_len, attr_len);

    return bgp_fsm_event(peer, BGP_EVENT_UPDATE_RCVD);
}

int bgp_keepalive_receive(struct peer *peer, struct stream *s, size_t size)
{
    (void)s;
    (void)size;
    peer->keepalive_in++;
    log_debug("BGP peer %s received KEEPALIVE", peer->host);

    return bgp_fsm_event(peer, BGP_EVENT_KEEPALIVE_RCVD);
}

int bgp_notification_receive(struct peer *peer, struct stream *s, size_t size)
{
    uint8_t code, subcode;

    if (size < 2) {
        peer->notify_in++;
        return bgp_fsm_event(peer, BGP_EVENT_NOTIFY_MSG_ERR);
    }

    code = stream_getc(s);
    subcode = stream_getc(s);

    peer->notify_in++;
    log_warn("BGP peer %s received NOTIFICATION: %s/%s",
             peer->host, bgp_error_str(code), bgp_error_subcode_str(code, subcode));

    return bgp_fsm_event(peer, BGP_EVENT_NOTIFY_RCVD);
}

int bgp_route_refresh_receive(struct peer *peer, struct stream *s, size_t size)
{
    (void)s;
    (void)size;
    peer->refresh_in++;
    log_debug("BGP peer %s received ROUTE-REFRESH", peer->host);

    return bgp_fsm_event(peer, BGP_EVENT_ROUTE_REFRESH_RCVD);
}

int bgp_capability_parse(struct peer *peer, uint8_t *data, size_t length)
{
    size_t pos = 0;

    while (pos < length) {
        uint8_t param_type = data[pos++];
        uint8_t param_len = data[pos++];

        if (param_type != 2) {  /* Not a capability */
            pos += param_len;
            continue;
        }

        /* Parse capabilities */
        size_t cap_end = pos + param_len;
        while (pos < cap_end) {
            uint8_t cap_code = data[pos++];
            uint8_t cap_len = data[pos++];

            switch (cap_code) {
            case BGP_CAP_CODE_AS4:
                if (cap_len >= 4) {
                    peer->remote_as = (data[pos] << 24) | (data[pos + 1] << 16) |
                                      (data[pos + 2] << 8) | data[pos + 3];
                    peer->cap_flags |= PEER_CAP_AS4_RCV;
                }
                break;

            case BGP_CAP_CODE_MP:
                if (cap_len >= 4) {
                    uint16_t afi = (data[pos] << 8) | data[pos + 1];
                    uint8_t safi = data[pos + 3];
                    if (afi == AFI_IP && safi == SAFI_UNICAST)
                        peer->cap_flags |= PEER_CAP_MP_IPV4_UNICAST;
                    else if (afi == AFI_IP6 && safi == SAFI_UNICAST)
                        peer->cap_flags |= PEER_CAP_MP_IPV6_UNICAST;
                }
                break;

            case BGP_CAP_CODE_RR:
                peer->cap_flags |= PEER_CAP_REFRESH_RCV;
                break;

            case BGP_CAP_CODE_GR:
                if (cap_len >= 2) {
                    uint16_t gr_value = (data[pos] << 8) | data[pos + 1];
                    peer->gr_restart_time = gr_value & 0x0FFF;
                    peer->gr_flags = (gr_value >> 12) & 0xF;
                    peer->cap_flags |= PEER_CAP_GRACEFUL_RESTART;
                    log_debug("BGP peer %s GR capability: restart=%u flags=%u",
                              peer->host, peer->gr_restart_time, peer->gr_flags);
                }
                break;

            default:
                log_debug("BGP peer %s unknown capability %d", peer->host, cap_code);
                break;
            }

            pos += cap_len;
        }
    }

    return 0;
}

const char *bgp_error_str(uint8_t code)
{
    static const char *errors[] = {
        [0] = "Unspecific",
        [BGP_NOTIFY_HEADER_ERR] = "Message Header Error",
        [BGP_NOTIFY_OPEN_ERR] = "OPEN Message Error",
        [BGP_NOTIFY_UPDATE_ERR] = "UPDATE Message Error",
        [BGP_NOTIFY_HOLD_ERR] = "Hold Timer Expired",
        [BGP_NOTIFY_FSM_ERR] = "FSM Error",
        [BGP_NOTIFY_CEASE] = "Cease",
    };

    if (code < sizeof(errors) / sizeof(errors[0]) && errors[code])
        return errors[code];
    return "Unknown";
}

const char *bgp_error_subcode_str(uint8_t code, uint8_t subcode)
{
    if (code == BGP_NOTIFY_CEASE) {
        static const char *cease_subcodes[] = {
            [0] = "Unspecified",
            [BGP_NOTIFY_CEASE_MAX_PREFIX] = "Maximum Prefixes",
            [BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN] = "Administrative Shutdown",
            [BGP_NOTIFY_CEASE_PEER_UNCONFIG] = "Peer Unconfigured",
            [BGP_NOTIFY_CEASE_ADMIN_RESET] = "Administrative Reset",
            [BGP_NOTIFY_CEASE_CONNECT_REJECT] = "Connection Rejected",
            [BGP_NOTIFY_CEASE_CONFIG_CHANGE] = "Configuration Change",
            [BGP_NOTIFY_CEASE_COLLISION_RESOLUTION] = "Collision Resolution",
            [BGP_NOTIFY_CEASE_OUT_OF_RESOURCE] = "Out of Resources",
        };
        if (subcode < sizeof(cease_subcodes) / sizeof(cease_subcodes[0]) &&
            cease_subcodes[subcode])
            return cease_subcodes[subcode];
    }

    return "Unspecified";
}
