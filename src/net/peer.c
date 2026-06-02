#include "peer.h"
#include "../log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

/* ── Write helper ───────────────────────────────────────────────── */

typedef struct {
    uv_write_t req;
    char      *data;
} write_ctx_t;

static void on_write_done(uv_write_t *req, int status)
{
    write_ctx_t *ctx = (write_ctx_t *)req;
    free(ctx->data);
    free(ctx);
    if (status < 0)
        LOG_WARN("write failed: %s", uv_strerror(status));
}

static int send_on_stream(uv_stream_t *stream, const glew_msg_t *msg)
{
    char *buf = NULL;
    size_t len = msg_serialize(msg, &buf);
    if (!len) return -1;

    write_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) { free(buf); return -1; }

    ctx->data = buf;
    ctx->req.data = ctx;

    uv_buf_t wbuf = uv_buf_init(buf, (unsigned int)len);
    int r = uv_write(&ctx->req, stream, &wbuf, 1, on_write_done);
    if (r < 0) {
        LOG_ERR("uv_write: %s", uv_strerror(r));
        free(buf);
        free(ctx);
        return -1;
    }
    return 0;
}

int peer_send(peer_t *p, const glew_msg_t *msg)
{
    if (p->state != PEER_CONNECTED && p->state != PEER_HANDSHAKING)
        return -1;
    return send_on_stream((uv_stream_t *)&p->handle, msg);
}

int stream_send(uv_stream_t *stream, const glew_msg_t *msg)
{
    return send_on_stream(stream, msg);
}

/* ── Message framing ────────────────────────────────────────────── */

static void process_buffer(char *rbuf, size_t *rlen,
                           void (*handler)(const glew_msg_t *, void *),
                           void *ctx)
{
    while (*rlen >= PROTO_HEADER_SIZE) {
        int32_t plen = proto_read_len(rbuf, *rlen);
        if (plen < 0) break;

        size_t total = PROTO_HEADER_SIZE + (size_t)plen;
        if (*rlen < total) break;

        glew_msg_t msg;
        if (msg_parse(rbuf + PROTO_HEADER_SIZE, (size_t)plen, &msg) == 0)
            handler(&msg, ctx);
        else
            LOG_WARN("failed to parse message");

        size_t remaining = *rlen - total;
        if (remaining > 0)
            memmove(rbuf, rbuf + total, remaining);
        *rlen = remaining;
    }
}

/* ── Peer connection ────────────────────────────────────────────── */

static void peer_schedule_reconnect(peer_t *p);
static void peer_connect(peer_t *p);

static void on_peer_msg_dispatch(const glew_msg_t *msg, void *ctx)
{
    peer_t *p = ctx;
    if (p->on_message)
        p->on_message(p, msg);
}

static void alloc_buf(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    (void)suggested;
    peer_t *p = handle->data;
    buf->base = p->rbuf + p->rlen;
    buf->len = PEER_BUF_SIZE - p->rlen;
}

static void on_peer_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    peer_t *p = stream->data;

    if (nread < 0) {
        if (nread != UV_EOF)
            LOG_WARN("peer %s: read error: %s", p->name, uv_strerror((int)nread));
        LOG_INFO("peer %s: disconnected", p->name);
        p->state = PEER_DISCONNECTED;
        uv_read_stop(stream);
        if (!uv_is_closing((uv_handle_t *)stream))
            uv_close((uv_handle_t *)stream, NULL);
        peer_schedule_reconnect(p);
        return;
    }

    p->rlen += (size_t)nread;
    process_buffer(p->rbuf, &p->rlen, on_peer_msg_dispatch, p);
}

static void on_connect(uv_connect_t *req, int status)
{
    peer_t *p = req->data;

    if (status < 0) {
        LOG_DBG("peer %s: connect failed: %s", p->name, uv_strerror(status));
        p->state = PEER_DISCONNECTED;
        if (!uv_is_closing((uv_handle_t *)&p->handle))
            uv_close((uv_handle_t *)&p->handle, NULL);
        peer_schedule_reconnect(p);
        return;
    }

    p->state = PEER_HANDSHAKING;
    p->reconnect_ms = RECONNECT_BASE_MS;
    p->rlen = 0;
    p->handle.data = p;

    uv_read_start((uv_stream_t *)&p->handle, alloc_buf, on_peer_read);

    /* send handshake */
    peer_mgr_t *mgr = p->userdata;
    glew_msg_t hello = { .type = MSG_HELLO };
    snprintf(hello.hello.name, sizeof(hello.hello.name), "%s", mgr->cfg->self_name);
    snprintf(hello.hello.secret, sizeof(hello.hello.secret), "%s", mgr->cfg->secret);
    peer_send(p, &hello);

    LOG_INFO("peer %s: connected, handshaking", p->name);
}

static void peer_connect(peer_t *p)
{
    if (p->state != PEER_DISCONNECTED)
        return;

    peer_mgr_t *mgr = p->userdata;
    p->state = PEER_CONNECTING;

    uv_tcp_init(mgr->loop, &p->handle);
    p->handle.data = p;
    p->connect_req.data = p;

    struct sockaddr_in addr;
    int r = uv_ip4_addr(p->address, p->port, &addr);
    if (r < 0) {
        /* try DNS resolve */
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", p->port);

        if (getaddrinfo(p->address, port_str, &hints, &res) == 0 && res) {
            memcpy(&addr, res->ai_addr, sizeof(addr));
            freeaddrinfo(res);
        } else {
            LOG_ERR("peer %s: cannot resolve %s", p->name, p->address);
            p->state = PEER_DISCONNECTED;
            uv_close((uv_handle_t *)&p->handle, NULL);
            peer_schedule_reconnect(p);
            return;
        }
    }

    r = uv_tcp_connect(&p->connect_req, &p->handle,
                        (struct sockaddr *)&addr, on_connect);
    if (r < 0) {
        LOG_ERR("peer %s: uv_tcp_connect: %s", p->name, uv_strerror(r));
        p->state = PEER_DISCONNECTED;
        uv_close((uv_handle_t *)&p->handle, NULL);
        peer_schedule_reconnect(p);
    }
}

static void on_reconnect_timer(uv_timer_t *timer)
{
    peer_t *p = timer->data;
    LOG_DBG("peer %s: attempting reconnect", p->name);
    peer_connect(p);
}

static void peer_schedule_reconnect(peer_t *p)
{
    p->reconnect_ms = p->reconnect_ms * 2;
    if (p->reconnect_ms > RECONNECT_MAX_MS)
        p->reconnect_ms = RECONNECT_MAX_MS;

    LOG_DBG("peer %s: reconnecting in %dms", p->name, p->reconnect_ms);
    uv_timer_start(&p->reconnect_timer, on_reconnect_timer,
                    (uint64_t)p->reconnect_ms, 0);
}

/* ── Incoming peer connections (server side) ────────────────────── */

typedef struct {
    uv_tcp_t    handle;
    char        rbuf[PEER_BUF_SIZE];
    size_t      rlen;
    peer_mgr_t *mgr;
} incoming_t;

static void on_incoming_msg(const glew_msg_t *msg, void *ctx);

static void alloc_incoming_buf(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    (void)suggested;
    incoming_t *inc = handle->data;
    buf->base = inc->rbuf + inc->rlen;
    buf->len = PEER_BUF_SIZE - inc->rlen;
}

static void on_incoming_close(uv_handle_t *handle)
{
    incoming_t *inc = handle->data;
    free(inc);
}

static void on_incoming_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    incoming_t *inc = stream->data;

    if (nread < 0) {
        uv_close((uv_handle_t *)stream, on_incoming_close);
        return;
    }

    inc->rlen += (size_t)nread;
    process_buffer(inc->rbuf, &inc->rlen, on_incoming_msg, inc);
}

static void on_incoming_msg(const glew_msg_t *msg, void *ctx)
{
    incoming_t *inc = ctx;
    peer_mgr_t *mgr = inc->mgr;

    if (msg->type == MSG_HELLO) {
        /* verify secret (constant-time comparison) */
        const char *expected = mgr->cfg->secret;
        const char *got = msg->hello.secret;
        size_t elen = strlen(expected);
        size_t glen = strlen(got);
        size_t maxlen = elen > glen ? elen : glen;

        volatile int diff = (int)(elen ^ glen);
        for (size_t i = 0; i < maxlen; i++) {
            char a = i < elen ? expected[i] : 0;
            char b = i < glen ? got[i] : 0;
            diff |= a ^ b;
        }

        if (diff != 0) {
            LOG_WARN("peer %s: bad secret", msg->hello.name);
            glew_msg_t err = { .type = MSG_HELLO_ERR };
            snprintf(err.hello_err.reason, sizeof(err.hello_err.reason), "bad secret");
            send_on_stream((uv_stream_t *)&inc->handle, &err);
            uv_close((uv_handle_t *)&inc->handle, on_incoming_close);
            return;
        }

        /* find peer by name and migrate the connection */
        peer_t *p = peer_by_name(mgr, msg->hello.name);
        if (!p) {
            LOG_WARN("unknown peer: %s", msg->hello.name);
            uv_close((uv_handle_t *)&inc->handle, on_incoming_close);
            return;
        }

        if (p->state == PEER_CONNECTED) {
            LOG_DBG("peer %s: already connected, rejecting duplicate", p->name);
            uv_close((uv_handle_t *)&inc->handle, on_incoming_close);
            return;
        }

        /* stop any pending reconnect */
        uv_timer_stop(&p->reconnect_timer);
        if (p->state == PEER_CONNECTING || p->state == PEER_HANDSHAKING) {
            if (!uv_is_closing((uv_handle_t *)&p->handle))
                uv_close((uv_handle_t *)&p->handle, NULL);
        }

        /* take over the incoming connection's fd */
        uv_fileno((uv_handle_t *)&inc->handle, (uv_os_fd_t *)&p->handle);
        /* reinit handle on the loop */
        uv_read_stop((uv_stream_t *)&inc->handle);

        /* we can't simply move handles — reinit a new tcp for the peer */
        int fd;
        uv_fileno((uv_handle_t *)&inc->handle, &fd);
        uv_tcp_init(mgr->loop, &p->handle);
        uv_tcp_open(&p->handle, fd);
        p->handle.data = p;
        p->rlen = 0;
        p->state = PEER_CONNECTED;
        p->reconnect_ms = RECONNECT_BASE_MS;

        /* prevent incoming_t close callback from closing the fd */
        inc->handle.data = NULL;

        uv_read_start((uv_stream_t *)&p->handle, alloc_buf, on_peer_read);

        /* send hello_ok */
        glew_msg_t ok = { .type = MSG_HELLO_OK };
        peer_send(p, &ok);

        LOG_INFO("peer %s: accepted incoming connection", p->name);

        /* free the incoming struct without closing the fd */
        free(inc);
        return;
    }

    LOG_WARN("unexpected message from unauthenticated connection");
    uv_close((uv_handle_t *)&inc->handle, on_incoming_close);
}

static void on_new_connection(uv_stream_t *server, int status)
{
    if (status < 0) {
        LOG_WARN("connection error: %s", uv_strerror(status));
        return;
    }

    peer_mgr_t *mgr = server->data;

    incoming_t *inc = calloc(1, sizeof(*inc));
    if (!inc) return;

    uv_tcp_init(mgr->loop, &inc->handle);
    inc->handle.data = inc;
    inc->mgr = mgr;

    if (uv_accept(server, (uv_stream_t *)&inc->handle) == 0) {
        uv_read_start((uv_stream_t *)&inc->handle, alloc_incoming_buf, on_incoming_read);
    } else {
        uv_close((uv_handle_t *)&inc->handle, on_incoming_close);
    }
}

/* ── Local Unix socket (CLI connections) ────────────────────────── */

typedef struct {
    uv_pipe_t   handle;
    char        rbuf[PEER_BUF_SIZE];
    size_t      rlen;
    peer_mgr_t *mgr;
} local_client_t;

static void on_local_msg(const glew_msg_t *msg, void *ctx)
{
    local_client_t *lc = ctx;
    if (lc->mgr->on_local_message)
        lc->mgr->on_local_message(msg, (uv_stream_t *)&lc->handle);
}

static void alloc_local_buf(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    (void)suggested;
    local_client_t *lc = handle->data;
    buf->base = lc->rbuf + lc->rlen;
    buf->len = PEER_BUF_SIZE - lc->rlen;
}

static void on_local_close(uv_handle_t *handle)
{
    local_client_t *lc = handle->data;
    free(lc);
}

static void on_local_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    local_client_t *lc = stream->data;

    if (nread < 0) {
        uv_close((uv_handle_t *)stream, on_local_close);
        return;
    }

    lc->rlen += (size_t)nread;
    process_buffer(lc->rbuf, &lc->rlen, on_local_msg, lc);
}

static void on_local_connection(uv_stream_t *server, int status)
{
    if (status < 0) return;

    peer_mgr_t *mgr = server->data;

    local_client_t *lc = calloc(1, sizeof(*lc));
    if (!lc) return;

    uv_pipe_init(mgr->loop, &lc->handle, 0);
    lc->handle.data = lc;
    lc->mgr = mgr;

    if (uv_accept(server, (uv_stream_t *)&lc->handle) == 0) {
        uv_read_start((uv_stream_t *)&lc->handle, alloc_local_buf, on_local_read);
    } else {
        uv_close((uv_handle_t *)&lc->handle, on_local_close);
    }
}

/* ── Heartbeat ──────────────────────────────────────────────────── */

static void on_heartbeat(uv_timer_t *timer)
{
    peer_mgr_t *mgr = timer->data;
    glew_msg_t ping = { .type = MSG_PING };

    for (int i = 0; i < mgr->peer_count; i++) {
        if (mgr->peers[i].state == PEER_CONNECTED)
            peer_send(&mgr->peers[i], &ping);
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

peer_t *peer_by_index(peer_mgr_t *mgr, int idx)
{
    for (int i = 0; i < mgr->peer_count; i++) {
        if (mgr->peers[i].machine_idx == idx)
            return &mgr->peers[i];
    }
    return NULL;
}

peer_t *peer_by_name(peer_mgr_t *mgr, const char *name)
{
    for (int i = 0; i < mgr->peer_count; i++) {
        if (strcmp(mgr->peers[i].name, name) == 0)
            return &mgr->peers[i];
    }
    return NULL;
}

static char *local_socket_path(const glew_config_t *cfg)
{
    static char path[256];
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg)
        snprintf(path, sizeof(path), "%s/glew.sock", xdg);
    else
        snprintf(path, sizeof(path), "/tmp/glew-%s.sock", cfg->self_name);
    return path;
}

int peer_mgr_init(peer_mgr_t *mgr, uv_loop_t *loop, const glew_config_t *cfg)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    mgr->cfg = cfg;

    /* count remote peers */
    int n = 0;
    for (int i = 0; i < cfg->machine_count; i++) {
        if (strcmp(cfg->machines[i].name, cfg->self_name) != 0)
            n++;
    }

    mgr->peers = calloc((size_t)n, sizeof(peer_t));
    if (!mgr->peers && n > 0) return -1;
    mgr->peer_count = n;

    int pi = 0;
    for (int i = 0; i < cfg->machine_count; i++) {
        if (strcmp(cfg->machines[i].name, cfg->self_name) == 0)
            continue;

        peer_t *p = &mgr->peers[pi++];
        snprintf(p->name, sizeof(p->name), "%s", cfg->machines[i].name);
        snprintf(p->address, sizeof(p->address), "%s", cfg->machines[i].address);
        p->port = cfg->port;
        p->machine_idx = i;
        p->state = PEER_DISCONNECTED;
        p->reconnect_ms = RECONNECT_BASE_MS;
        p->userdata = mgr;

        uv_timer_init(loop, &p->reconnect_timer);
        p->reconnect_timer.data = p;
    }

    return 0;
}

void peer_mgr_start(peer_mgr_t *mgr)
{
    /* TCP server for peer connections */
    uv_tcp_init(mgr->loop, &mgr->server);
    mgr->server.data = mgr;

    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", mgr->cfg->port, &bind_addr);
    uv_tcp_bind(&mgr->server, (struct sockaddr *)&bind_addr, 0);

    int r = uv_listen((uv_stream_t *)&mgr->server, 8, on_new_connection);
    if (r < 0) {
        LOG_ERR("TCP listen failed: %s", uv_strerror(r));
    } else {
        LOG_INFO("TCP server listening on port %d", mgr->cfg->port);
    }

    /* Unix socket for local CLI */
    char *sock_path = local_socket_path(mgr->cfg);
    unlink(sock_path);

    uv_pipe_init(mgr->loop, &mgr->local_server, 0);
    mgr->local_server.data = mgr;

    r = uv_pipe_bind(&mgr->local_server, sock_path);
    if (r < 0) {
        LOG_ERR("Unix socket bind(%s) failed: %s", sock_path, uv_strerror(r));
    } else {
        r = uv_listen((uv_stream_t *)&mgr->local_server, 4, on_local_connection);
        if (r < 0)
            LOG_ERR("Unix socket listen failed: %s", uv_strerror(r));
        else
            LOG_INFO("local socket: %s", sock_path);
    }

    /* heartbeat timer */
    uv_timer_init(mgr->loop, &mgr->heartbeat_timer);
    mgr->heartbeat_timer.data = mgr;
    uv_timer_start(&mgr->heartbeat_timer, on_heartbeat,
                    HEARTBEAT_INTERVAL_MS, HEARTBEAT_INTERVAL_MS);

    /* connect to all peers */
    for (int i = 0; i < mgr->peer_count; i++)
        peer_connect(&mgr->peers[i]);
}

void peer_mgr_stop(peer_mgr_t *mgr)
{
    uv_timer_stop(&mgr->heartbeat_timer);

    for (int i = 0; i < mgr->peer_count; i++) {
        peer_t *p = &mgr->peers[i];
        uv_timer_stop(&p->reconnect_timer);
        if (p->state != PEER_DISCONNECTED && !uv_is_closing((uv_handle_t *)&p->handle))
            uv_close((uv_handle_t *)&p->handle, NULL);
    }

    char *sock_path = local_socket_path(mgr->cfg);
    unlink(sock_path);
}
