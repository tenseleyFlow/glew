#include "config.h"
#include "log.h"
#include "version.h"
#include "layout.h"
#include "drivers/driver.h"
#include "net/proto.h"
#include "net/peer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

static glew_config_t  g_cfg;
static layout_t       g_layout;
static const wm_driver_t *g_driver;
static peer_mgr_t     g_mgr;
static uv_loop_t     *g_loop;
static uv_signal_t    g_sigint, g_sigterm;

static void usage(void)
{
    fprintf(stderr,
        "usage: glewd [options]\n"
        "\n"
        "options:\n"
        "  -c, --config <path>   config file (default: ~/.config/glew/glew.toml)\n"
        "  -v, --verbose         enable debug logging\n"
        "  -V, --version         print version\n"
        "  -h, --help            show this help\n"
    );
}

static const struct option longopts[] = {
    { "config",  required_argument, NULL, 'c' },
    { "verbose", no_argument,       NULL, 'v' },
    { "version", no_argument,       NULL, 'V' },
    { "help",    no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 },
};

/* ── Focus handling ─────────────────────────────────────────────── */

static void handle_focus(const char *dir_str, uv_stream_t *client)
{
    direction_t dir = direction_parse(dir_str);

    int can = g_driver->can_focus(dir);
    if (can) {
        g_driver->do_focus(dir);
        LOG_DBG("focus %s: moved within %s", dir_str, g_driver->name);

        glew_msg_t reply = { .type = MSG_FOCUS_RESULT };
        reply.focus_result.crossed = 0;
        stream_send(client, &reply);
        return;
    }

    /* at the edge — try cross-machine */
    int target_idx;
    direction_t enter_dir;
    if (layout_resolve(&g_layout, dir, &target_idx, &enter_dir) != 0) {
        LOG_DBG("focus %s: at absolute edge, no neighbor", dir_str);
        glew_msg_t reply = { .type = MSG_FOCUS_RESULT };
        reply.focus_result.crossed = 0;
        stream_send(client, &reply);
        return;
    }

    const machine_t *target = layout_machine(&g_layout, target_idx);
    peer_t *p = peer_by_index(&g_mgr, target_idx);

    if (!p || p->state != PEER_CONNECTED) {
        LOG_WARN("focus %s: peer %s not connected", dir_str,
                 target ? target->name : "unknown");
        glew_msg_t reply = { .type = MSG_FOCUS_RESULT };
        reply.focus_result.crossed = 0;
        stream_send(client, &reply);
        return;
    }

    LOG_INFO("focus %s: crossing to %s (entering from %s)",
             dir_str, p->name, direction_str(enter_dir));

    glew_msg_t fe = { .type = MSG_FOCUS_ENTER };
    snprintf(fe.focus_enter.from_direction, sizeof(fe.focus_enter.from_direction),
             "%s", direction_str(enter_dir));
    snprintf(fe.focus_enter.source, sizeof(fe.focus_enter.source),
             "%s", g_cfg.self_name);
    peer_send(p, &fe);

    glew_msg_t reply = { .type = MSG_FOCUS_RESULT };
    reply.focus_result.crossed = 1;
    snprintf(reply.focus_result.target, sizeof(reply.focus_result.target),
             "%s", p->name);
    stream_send(client, &reply);
}

/* ── Message callbacks ──────────────────────────────────────────── */

static void on_local_message(const glew_msg_t *msg, uv_stream_t *client)
{
    switch (msg->type) {
    case MSG_FOCUS:
        handle_focus(msg->focus.direction, client);
        break;
    default:
        LOG_WARN("unexpected local message type");
        break;
    }
}

static void on_peer_message(peer_t *p, const glew_msg_t *msg)
{
    switch (msg->type) {
    case MSG_HELLO_OK:
        p->state = PEER_CONNECTED;
        LOG_INFO("peer %s: handshake complete", p->name);
        break;

    case MSG_HELLO_ERR:
        LOG_ERR("peer %s: handshake rejected: %s", p->name, msg->hello_err.reason);
        break;

    case MSG_FOCUS_ENTER: {
        direction_t from = direction_parse(msg->focus_enter.from_direction);
        LOG_INFO("focus entering from %s (source: %s)",
                 msg->focus_enter.from_direction, msg->focus_enter.source);

        int rc = g_driver->focus_edge(from);

        glew_msg_t ack = { .type = MSG_FOCUS_ACK };
        ack.focus_ack.success = (rc == 0) ? 1 : 0;
        peer_send(p, &ack);
        break;
    }

    case MSG_FOCUS_ACK:
        LOG_DBG("peer %s: focus_ack success=%d", p->name, msg->focus_ack.success);
        break;

    case MSG_PING: {
        glew_msg_t pong = { .type = MSG_PONG };
        peer_send(p, &pong);
        break;
    }

    case MSG_PONG:
        break;

    default:
        LOG_WARN("unexpected message from peer %s", p->name);
        break;
    }
}

/* ── Signal handling ────────────────────────────────────────────── */

static void on_signal(uv_signal_t *handle, int signum)
{
    (void)signum;
    LOG_INFO("signal received, shutting down");
    peer_mgr_stop(&g_mgr);
    uv_signal_stop(&g_sigint);
    uv_signal_stop(&g_sigterm);
    uv_stop(g_loop);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    char config_path[512] = {0};
    int verbose = 0;

    int ch;
    while ((ch = getopt_long(argc, argv, "c:vVh", longopts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            snprintf(config_path, sizeof(config_path), "%s", optarg);
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            printf("glewd %s\n", GLEW_VERSION_STR);
            return 0;
        case 'h':
            usage();
            return 0;
        default:
            usage();
            return 1;
        }
    }

    if (verbose)
        log_set_level(LOG_DEBUG);

    if (!config_path[0])
        config_default_path(config_path, sizeof(config_path));

    if (config_load(config_path, &g_cfg) != 0)
        return 1;

    if (verbose)
        config_dump(&g_cfg);

    if (layout_init(&g_layout, &g_cfg) != 0)
        return 1;

    g_driver = driver_by_name(g_cfg.self_wm);
    if (!g_driver) {
        LOG_ERR("unsupported wm: %s", g_cfg.self_wm);
        return 1;
    }

    if (g_driver->init(NULL) != 0)
        return 1;

    g_loop = uv_default_loop();

    if (peer_mgr_init(&g_mgr, g_loop, &g_cfg) != 0) {
        LOG_ERR("peer manager init failed");
        return 1;
    }

    g_mgr.on_local_message = on_local_message;
    g_mgr.on_peer_message = on_peer_message;

    /* set the peer message callback on each peer */
    for (int i = 0; i < g_mgr.peer_count; i++)
        g_mgr.peers[i].on_message = on_peer_message;

    uv_signal_init(g_loop, &g_sigint);
    uv_signal_init(g_loop, &g_sigterm);
    uv_signal_start(&g_sigint, on_signal, SIGINT);
    uv_signal_start(&g_sigterm, on_signal, SIGTERM);

    peer_mgr_start(&g_mgr);

    LOG_INFO("glewd %s running (self=%s, wm=%s, port=%d, peers=%d)",
             GLEW_VERSION_STR, g_cfg.self_name, g_cfg.self_wm,
             g_cfg.port, g_mgr.peer_count);

    uv_run(g_loop, UV_RUN_DEFAULT);

    g_driver->shutdown();
    uv_loop_close(g_loop);
    free(g_mgr.peers);

    LOG_INFO("glewd shut down cleanly");
    return 0;
}
