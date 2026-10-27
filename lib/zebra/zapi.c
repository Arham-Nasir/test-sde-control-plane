/*
 * Zebra API Implementation
 */

#include "zapi.h"
#include "common/log.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#undef LOG_TAG
#define LOG_TAG "ZAPI"
#define ZAPI_VERSION 6
#define ZEBRA_MAX_PACKET_SIZ 65536

struct zclient *zclient_new(void)
{
    struct zclient *zclient;

    zclient = XCALLOC(MTYPE_TMP, sizeof(struct zclient));
    zclient->sock = -1;
    zclient->ibuf = stream_new(ZEBRA_MAX_PACKET_SIZ);
    zclient->obuf = stream_new(ZEBRA_MAX_PACKET_SIZ);

    return zclient;
}

void zclient_free(struct zclient *zclient)
{
    if (zclient == NULL)
        return;

    if (zclient->sock >= 0)
        close(zclient->sock);

    stream_free(zclient->ibuf);
    stream_free(zclient->obuf);
    XFREE(MTYPE_TMP, zclient);
}

void zclient_init(struct zclient *zclient, route_type_t type, uint16_t instance)
{
    zclient->redist_default = type;
    zclient->instance = instance;
}

int zclient_connect(struct zclient *zclient, const char *path)
{
    struct sockaddr_un addr;
    int sock;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        log_err("ZAPI socket creation failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_err("ZAPI connect failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    zclient->sock = sock;
    log_info("ZAPI connected to %s", path);

    return 0;
}

void zclient_disconnect(struct zclient *zclient)
{
    if (zclient->sock >= 0) {
        close(zclient->sock);
        zclient->sock = -1;
        log_info("ZAPI disconnected");
    }
}

static int zclient_send_message(struct zclient *zclient)
{
    struct stream *s = zclient->obuf;
    size_t len = stream_get_endp(s);
    ssize_t written;

    /* Write length at start of message */
    stream_putw_at(s, 0, len);

    written = write(zclient->sock, STREAM_DATA(s), len);
    if (written < 0) {
        log_err("ZAPI send failed: %s", strerror(errno));
        return -1;
    }

    if ((size_t)written != len) {
        log_err("ZAPI partial send: %zd/%zu", written, len);
        return -1;
    }

    return 0;
}

static void zclient_create_header(struct stream *s, uint16_t cmd, uint16_t vrf_id)
{
    stream_reset(s);

    stream_putw(s, 0);              /* Length placeholder */
    stream_putc(s, 0xFF);           /* Marker */
    stream_putc(s, ZAPI_VERSION);   /* Version */
    stream_putw(s, vrf_id);         /* VRF ID */
    stream_putw(s, cmd);            /* Command */
}

int zclient_send_hello(struct zclient *zclient)
{
    struct stream *s = zclient->obuf;

    zclient_create_header(s, ZEBRA_HELLO, 0);
    stream_putc(s, zclient->redist_default);
    stream_putw(s, zclient->instance);
    stream_putl(s, 0);  /* Session ID */
    stream_putl(s, 0);  /* Capabilities */

    return zclient_send_message(zclient);
}

int zclient_send_router_id_request(struct zclient *zclient)
{
    struct stream *s = zclient->obuf;

    zclient_create_header(s, ZEBRA_ROUTER_ID_ADD, 0);
    stream_putc(s, AF_INET);

    return zclient_send_message(zclient);
}

int zclient_send_redistribute(struct zclient *zclient, route_type_t type)
{
    struct stream *s = zclient->obuf;

    zclient_create_header(s, ZEBRA_REDISTRIBUTE_ADD, 0);
    stream_putc(s, AFI_IP);
    stream_putc(s, type);
    stream_putw(s, 0);  /* Instance */

    return zclient_send_message(zclient);
}

int zclient_send_route(struct zclient *zclient, zapi_message_t cmd,
                       struct zapi_route *route)
{
    struct stream *s = zclient->obuf;

    zclient_create_header(s, cmd, 0);

    if (zapi_route_encode(s, route) < 0)
        return -1;

    return zclient_send_message(zclient);
}

int zclient_send_nexthop_register(struct zclient *zclient,
                                  struct prefix *p, bool connected)
{
    struct stream *s = zclient->obuf;

    zclient_create_header(s, ZEBRA_NEXTHOP_REGISTER, 0);

    stream_putc(s, connected ? 1 : 0);
    stream_putw(s, p->family);
    stream_put_prefix(s, p);

    return zclient_send_message(zclient);
}

int zclient_send_nexthop_unregister(struct zclient *zclient, struct prefix *p)
{
    struct stream *s = zclient->obuf;

    zclient_create_header(s, ZEBRA_NEXTHOP_UNREGISTER, 0);

    stream_putw(s, p->family);
    stream_put_prefix(s, p);

    return zclient_send_message(zclient);
}

int zclient_read(struct zclient *zclient)
{
    struct stream *s = zclient->ibuf;
    struct zapi_header hdr;
    ssize_t nbytes;

    stream_reset(s);

    /* Read header */
    nbytes = read(zclient->sock, STREAM_DATA(s), ZEBRA_HEADER_SIZE);
    if (nbytes < 0) {
        log_err("ZAPI read header failed: %s", strerror(errno));
        return -1;
    }

    if (nbytes == 0) {
        log_info("ZAPI connection closed");
        return -1;
    }

    stream_set_endp(s, nbytes);

    /* Parse header */
    hdr.length = stream_getw(s);
    hdr.marker = stream_getc(s);
    hdr.version = stream_getc(s);
    hdr.vrf_id = stream_getw(s);
    hdr.command = stream_getw(s);

    if (hdr.marker != 0xFF || hdr.version != ZAPI_VERSION) {
        log_err("ZAPI invalid header marker=%02x version=%d",
                hdr.marker, hdr.version);
        return -1;
    }

    /* Read body */
    if (hdr.length > ZEBRA_HEADER_SIZE) {
        size_t body_len = hdr.length - ZEBRA_HEADER_SIZE;
        nbytes = read(zclient->sock, STREAM_DATA(s) + ZEBRA_HEADER_SIZE,
                     body_len);
        if (nbytes < 0 || (size_t)nbytes != body_len) {
            log_err("ZAPI read body failed");
            return -1;
        }
        stream_set_endp(s, hdr.length);
    }

    /* Dispatch to callback */
    switch (hdr.command) {
    case ZEBRA_ROUTER_ID_UPDATE:
        if (zclient->router_id_update)
            zclient->router_id_update(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_INTERFACE_ADD:
        if (zclient->interface_add)
            zclient->interface_add(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_INTERFACE_DELETE:
        if (zclient->interface_delete)
            zclient->interface_delete(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_INTERFACE_UP:
        if (zclient->interface_up)
            zclient->interface_up(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_INTERFACE_DOWN:
        if (zclient->interface_down)
            zclient->interface_down(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_REDISTRIBUTE_ROUTE_ADD:
        if (zclient->route_add)
            zclient->route_add(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_REDISTRIBUTE_ROUTE_DEL:
        if (zclient->route_delete)
            zclient->route_delete(zclient, hdr.length, hdr.vrf_id);
        break;
    case ZEBRA_NEXTHOP_UPDATE:
        if (zclient->nexthop_update)
            zclient->nexthop_update(zclient, hdr.length, hdr.vrf_id);
        break;
    default:
        log_debug("ZAPI unhandled command %d", hdr.command);
        break;
    }

    return 0;
}

int zapi_route_encode(struct stream *s, struct zapi_route *route)
{
    int i;

    stream_putc(s, route->type);
    stream_putw(s, route->instance);
    stream_putl(s, route->flags);
    stream_putc(s, route->message);
    stream_putc(s, route->safi);
    stream_put_prefix(s, &route->prefix);

    if (route->message & ZAPI_MESSAGE_NEXTHOP) {
        stream_putc(s, route->nexthop_num);
        for (i = 0; i < route->nexthop_num; i++) {
            struct zapi_nexthop *nh = &route->nexthops[i];
            stream_putl(s, 0);  /* VRF ID */
            stream_putc(s, nh->type);

            switch (nh->type) {
            case NEXTHOP_TYPE_IPV4:
            case NEXTHOP_TYPE_IPV4_IFINDEX:
                stream_put_ipv4(s, &nh->gate.ipv4);
                if (nh->type == NEXTHOP_TYPE_IPV4_IFINDEX)
                    stream_putl(s, nh->ifindex);
                break;
            case NEXTHOP_TYPE_IPV6:
            case NEXTHOP_TYPE_IPV6_IFINDEX:
                stream_put_ipv6(s, &nh->gate.ipv6);
                if (nh->type == NEXTHOP_TYPE_IPV6_IFINDEX)
                    stream_putl(s, nh->ifindex);
                break;
            case NEXTHOP_TYPE_IFINDEX:
                stream_putl(s, nh->ifindex);
                break;
            case NEXTHOP_TYPE_BLACKHOLE:
                stream_putc(s, 0);  /* Blackhole type */
                break;
            }

            /* Labels */
            if (route->message & ZAPI_MESSAGE_LABEL) {
                stream_putc(s, nh->label_num);
                for (int j = 0; j < nh->label_num; j++)
                    stream_putl(s, nh->labels[j]);
            }
        }
    }

    if (route->message & ZAPI_MESSAGE_DISTANCE)
        stream_putc(s, route->distance);

    if (route->message & ZAPI_MESSAGE_METRIC)
        stream_putl(s, route->metric);

    if (route->message & ZAPI_MESSAGE_TAG)
        stream_putl(s, route->tag);

    if (route->message & ZAPI_MESSAGE_MTU)
        stream_putl(s, route->mtu);

    if (route->message & ZAPI_MESSAGE_TABLEID)
        stream_putl(s, route->tableid);

    return 0;
}

int zapi_route_decode(struct stream *s, struct zapi_route *route)
{
    int i;

    memset(route, 0, sizeof(*route));

    route->type = stream_getc(s);
    route->instance = stream_getw(s);
    route->flags = stream_getl(s);
    route->message = stream_getc(s);
    route->safi = stream_getc(s);
    stream_get_prefix(s, &route->prefix);

    if (route->message & ZAPI_MESSAGE_NEXTHOP) {
        route->nexthop_num = stream_getc(s);
        for (i = 0; i < route->nexthop_num && i < 64; i++) {
            struct zapi_nexthop *nh = &route->nexthops[i];
            stream_getl(s);  /* VRF ID */
            nh->type = stream_getc(s);

            switch (nh->type) {
            case NEXTHOP_TYPE_IPV4:
            case NEXTHOP_TYPE_IPV4_IFINDEX:
                stream_get_ipv4(s, &nh->gate.ipv4);
                if (nh->type == NEXTHOP_TYPE_IPV4_IFINDEX)
                    nh->ifindex = stream_getl(s);
                break;
            case NEXTHOP_TYPE_IPV6:
            case NEXTHOP_TYPE_IPV6_IFINDEX:
                stream_get_ipv6(s, &nh->gate.ipv6);
                if (nh->type == NEXTHOP_TYPE_IPV6_IFINDEX)
                    nh->ifindex = stream_getl(s);
                break;
            case NEXTHOP_TYPE_IFINDEX:
                nh->ifindex = stream_getl(s);
                break;
            case NEXTHOP_TYPE_BLACKHOLE:
                stream_getc(s);
                break;
            }

            if (route->message & ZAPI_MESSAGE_LABEL) {
                nh->label_num = stream_getc(s);
                for (int j = 0; j < nh->label_num && j < 4; j++)
                    nh->labels[j] = stream_getl(s);
            }
        }
    }

    if (route->message & ZAPI_MESSAGE_DISTANCE)
        route->distance = stream_getc(s);

    if (route->message & ZAPI_MESSAGE_METRIC)
        route->metric = stream_getl(s);

    if (route->message & ZAPI_MESSAGE_TAG)
        route->tag = stream_getl(s);

    if (route->message & ZAPI_MESSAGE_MTU)
        route->mtu = stream_getl(s);

    if (route->message & ZAPI_MESSAGE_TABLEID)
        route->tableid = stream_getl(s);

    return 0;
}
