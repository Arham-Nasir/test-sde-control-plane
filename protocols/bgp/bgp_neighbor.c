/*
 * BGP Neighbor Implementation
 */

#include "bgp_neighbor.h"
#include "bgp_fsm.h"
#include "common/log.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

#undef LOG_TAG
#define LOG_TAG "BGP"

#define BGP_PORT 179

/* Identity allocation function for hash_get - returns the data as-is */
static void *peer_hash_alloc(void *data)
{
    return data;
}

struct peer *peer_new(struct bgp *bgp)
{
    struct peer *peer;

    peer = XCALLOC(MTYPE_BGP_PEER, sizeof(struct peer));
    peer->bgp = bgp;
    peer->fd = -1;
    peer->state = BGP_STATUS_IDLE;

    /* Default timers */
    peer->holdtime = BGP_DEFAULT_HOLDTIME;
    peer->keepalive = BGP_DEFAULT_KEEPALIVE;
    peer->connect_retry = BGP_DEFAULT_CONNECT_RETRY;

    /* Allocate buffers */
    peer->ibuf = stream_new(BGP_MAX_PACKET_SIZE);
    peer->obuf = stream_new(BGP_MAX_PACKET_SIZE);
    peer->work = stream_new(BGP_MAX_PACKET_SIZE);
    peer->obuf_fifo = stream_fifo_new();

    /* Allocate EOR tracking array */
    peer->gr_eor_received = XCALLOC(MTYPE_BGP_GR, AFI_MAX * SAFI_MAX * sizeof(bool));

    return peer;
}

struct peer *peer_create(struct bgp *bgp, union sockunion *su, uint32_t remote_as)
{
    struct peer *peer;
    char buf[64];

    peer = peer_new(bgp);
    peer->remote_as = remote_as;
    memcpy(&peer->remote_addr, su, sizeof(*su));

    /* Generate host string */
    sockunion_str(su, buf, sizeof(buf));
    peer->host = XSTRDUP(MTYPE_BGP_PEER_HOST, buf);

    /* Add to BGP instance */
    listnode_add(bgp->peer, peer);
    peer->node = listtail(bgp->peer);
    hash_get(bgp->peerhash, peer, peer_hash_alloc);

    log_info("BGP peer %s AS %u created", peer->host, remote_as);

    return peer;
}

void peer_delete(struct peer *peer)
{
    if (peer == NULL)
        return;

    log_info("BGP peer %s deleting", peer->host);

    /* Stop FSM and close connection */
    peer_stop(peer);

    /* Remove from BGP instance */
    if (peer->bgp) {
        listnode_delete(peer->bgp->peer, peer->node);
        hash_release(peer->bgp->peerhash, peer);
    }

    /* Free buffers */
    stream_free(peer->ibuf);
    stream_free(peer->obuf);
    stream_free(peer->work);
    stream_fifo_free(peer->obuf_fifo);

    /* Free GR tracking */
    XFREE(MTYPE_BGP_GR, peer->gr_eor_received);

    /* Free strings */
    XFREE(MTYPE_BGP_PEER_HOST, peer->host);
    XFREE(MTYPE_BGP_PEER_HOST, peer->desc);

    XFREE(MTYPE_BGP_PEER, peer);
}

struct peer *peer_lookup(struct bgp *bgp, union sockunion *su)
{
    struct peer tmp;
    struct peer *peer;

    memcpy(&tmp.remote_addr, su, sizeof(*su));
    peer = hash_lookup(bgp->peerhash, &tmp);

    return peer;
}

struct peer *peer_lookup_by_hostname(struct bgp *bgp, const char *hostname)
{
    struct listnode *node;
    struct peer *peer;

    for_each_list_node(bgp->peer, node) {
        peer = listgetdata(node);
        if (peer->host && strcmp(peer->host, hostname) == 0)
            return peer;
    }

    return NULL;
}

void peer_flag_set(struct peer *peer, uint32_t flag)
{
    peer->flags |= flag;
}

void peer_flag_unset(struct peer *peer, uint32_t flag)
{
    peer->flags &= ~flag;
}

bool peer_flag_check(struct peer *peer, uint32_t flag)
{
    return (peer->flags & flag) != 0;
}

void peer_timers_set(struct peer *peer, uint16_t holdtime, uint16_t keepalive)
{
    peer->holdtime = holdtime;
    peer->keepalive = keepalive ? keepalive : holdtime / 3;
}

void peer_description_set(struct peer *peer, const char *desc)
{
    XFREE(MTYPE_BGP_PEER_HOST, peer->desc);
    if (desc)
        peer->desc = XSTRDUP(MTYPE_BGP_PEER_HOST, desc);
}

void peer_activate(struct peer *peer, uint8_t afi, uint8_t safi)
{
    if (afi < AFI_MAX && safi < SAFI_MAX)
        peer->af[afi][safi].active = true;
}

void peer_deactivate(struct peer *peer, uint8_t afi, uint8_t safi)
{
    if (afi < AFI_MAX && safi < SAFI_MAX)
        peer->af[afi][safi].active = false;
}

bool peer_active(struct peer *peer, uint8_t afi, uint8_t safi)
{
    if (afi < AFI_MAX && safi < SAFI_MAX)
        return peer->af[afi][safi].active;
    return false;
}

void peer_rr_client_set(struct peer *peer)
{
    peer->rr_client = true;
    peer->flags |= PEER_FLAG_RRCLIENT;
}

void peer_rr_client_unset(struct peer *peer)
{
    peer->rr_client = false;
    peer->flags &= ~PEER_FLAG_RRCLIENT;
}

bool peer_is_rr_client(struct peer *peer)
{
    return peer->rr_client;
}

void peer_gr_enable(struct peer *peer)
{
    peer->flags |= PEER_FLAG_GRACEFUL_RESTART;
}

void peer_gr_disable(struct peer *peer)
{
    peer->flags &= ~PEER_FLAG_GRACEFUL_RESTART;
}

int peer_start(struct peer *peer)
{
    if (peer->flags & PEER_FLAG_SHUTDOWN) {
        log_info("BGP peer %s is shutdown", peer->host);
        return -1;
    }

    log_info("BGP peer %s starting", peer->host);
    return bgp_fsm_event(peer, BGP_EVENT_START);
}

int peer_stop(struct peer *peer)
{
    log_info("BGP peer %s stopping", peer->host);
    return bgp_fsm_event(peer, BGP_EVENT_STOP);
}

int peer_clear(struct peer *peer, bool hard)
{
    log_info("BGP peer %s clearing%s", peer->host, hard ? " (hard)" : "");

    if (hard) {
        /* Hard clear - no GR, immediate removal */
        bgp_fsm_to_idle(peer, BGP_NOTIFY_CEASE_ADMIN_RESET);
    } else {
        /* Soft clear - trigger GR if supported */
        bgp_fsm_event(peer, BGP_EVENT_STOP);
    }

    return 0;
}

static int socket_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int peer_tcp_connect(struct peer *peer)
{
    int sock;
    int ret;
    int family;

    family = peer->remote_addr.sa.sa_family;
    sock = socket(family, SOCK_STREAM, 0);
    if (sock < 0) {
        log_err("BGP peer %s socket failed: %s", peer->host, strerror(errno));
        return -1;
    }

    /* Set non-blocking */
    socket_set_nonblock(sock);

    /* Set TCP options */
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    /* Connect */
    struct sockaddr *sa;
    socklen_t len;

    if (family == AF_INET) {
        peer->remote_addr.sin.sin_port = htons(BGP_PORT);
        sa = &peer->remote_addr.sa;
        len = sizeof(struct sockaddr_in);
    } else {
        peer->remote_addr.sin6.sin6_port = htons(BGP_PORT);
        sa = &peer->remote_addr.sa;
        len = sizeof(struct sockaddr_in6);
    }

    ret = connect(sock, sa, len);
    if (ret < 0 && errno != EINPROGRESS) {
        log_err("BGP peer %s connect failed: %s", peer->host, strerror(errno));
        close(sock);
        return -1;
    }

    peer->fd = sock;
    log_debug("BGP peer %s TCP connecting (fd=%d)", peer->host, sock);

    return 0;
}

void peer_tcp_close(struct peer *peer)
{
    if (peer->fd >= 0) {
        log_debug("BGP peer %s TCP closing (fd=%d)", peer->host, peer->fd);
        close(peer->fd);
        peer->fd = -1;
    }

    /* Cancel I/O events */
    event_cancel(&peer->t_read);
    event_cancel(&peer->t_write);
}

int peer_tcp_accept(struct peer *peer, int accept_fd)
{
    if (peer->fd >= 0) {
        log_warn("BGP peer %s already has connection, closing old", peer->host);
        peer_tcp_close(peer);
    }

    peer->fd = accept_fd;
    socket_set_nonblock(accept_fd);

    log_debug("BGP peer %s TCP accepted (fd=%d)", peer->host, accept_fd);
    return 0;
}

void peer_clear_buffers(struct peer *peer)
{
    stream_reset(peer->ibuf);
    stream_reset(peer->obuf);
    stream_fifo_clean(peer->obuf_fifo);
}

/* Sockunion utilities */
int sockunion_family(union sockunion *su)
{
    return su->sa.sa_family;
}

const char *sockunion_str(union sockunion *su, char *buf, size_t size)
{
    if (su->sa.sa_family == AF_INET)
        return inet_ntop(AF_INET, &su->sin.sin_addr, buf, size);
    else if (su->sa.sa_family == AF_INET6)
        return inet_ntop(AF_INET6, &su->sin6.sin6_addr, buf, size);
    return "unknown";
}

bool sockunion_same(union sockunion *a, union sockunion *b)
{
    if (a->sa.sa_family != b->sa.sa_family)
        return false;

    if (a->sa.sa_family == AF_INET)
        return a->sin.sin_addr.s_addr == b->sin.sin_addr.s_addr;
    else if (a->sa.sa_family == AF_INET6)
        return memcmp(&a->sin6.sin6_addr, &b->sin6.sin6_addr, 16) == 0;

    return false;
}

int sockunion_cmp(union sockunion *a, union sockunion *b)
{
    if (a->sa.sa_family != b->sa.sa_family)
        return a->sa.sa_family - b->sa.sa_family;

    if (a->sa.sa_family == AF_INET)
        return memcmp(&a->sin.sin_addr, &b->sin.sin_addr, 4);
    else if (a->sa.sa_family == AF_INET6)
        return memcmp(&a->sin6.sin6_addr, &b->sin6.sin6_addr, 16);

    return 0;
}
