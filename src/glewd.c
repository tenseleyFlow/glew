#include "config.h"
#include "log.h"
#include "version.h"
#include "layout.h"
#include "drivers/driver.h"
#include "net/proto.h"
#include "net/peer.h"
#include "input/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

static glew_config_t      g_cfg;
static layout_t           g_layout;
static const wm_driver_t *g_driver;
static peer_mgr_t         g_mgr;
static uv_loop_t         *g_loop;
static uv_signal_t        g_sigint, g_sigterm;

/* ── Input state machine ───────────────────────────────────────── */

typedef enum {
    MODE_LOCAL,
    MODE_REMOTE_SENDING,    /* we captured input, forwarding to a peer */
    MODE_REMOTE_RECEIVING,  /* a peer is sending input to us */
} input_mode_t;

static input_mode_t  g_input_mode = MODE_LOCAL;
static peer_t       *g_input_target;   /* peer we're sending input to */
static peer_t       *g_input_source;   /* peer sending input to us */
static int           g_input_ev_sent;
static int           g_input_ev_recv;

/* virtual cursor on receiving side [0,1] */
static double        g_virt_x, g_virt_y;
static uint32_t      g_send_seq;
static uint32_t      g_recv_seq;

/* receiver-side mouse edge — arm/disarm guard (same model as sender) */
static int           g_recv_edge_armed;
#define RECV_COOLDOWN_MS 200

/* cooldown: suppress edge checks briefly after entering a machine */
static uint64_t      g_edge_cooldown_until;

/* keysyms for the WM mod key — determined per WM type */
/* mod mask bit for this machine's WM — used for focus interception */
static uint32_t g_local_mod_bit;

static void init_local_mod(void)
{
    const char *wm = g_cfg.self_wm;
    if (strcmp(wm, "tarmac") == 0) {
        g_local_mod_bit = (1 << 3); /* Alt/Option */
    } else {
        g_local_mod_bit = (1 << 6); /* Super — i3, gar, sway */
    }
}

static direction_t arrow_keysym_to_dir(uint32_t keysym)
{
    switch (keysym) {
    case 0xff51: return DIR_LEFT;
    case 0xff52: return DIR_UP;
    case 0xff53: return DIR_RIGHT;
    case 0xff54: return DIR_DOWN;
    default:     return -1;
    }
}

static void start_edge_watching(void);
static void start_sending_input(peer_t *target, direction_t cross_dir);

static uv_timer_t  g_sender_cooldown;
static int         g_sender_cooldown_active;
#define SENDER_COOLDOWN_MS 200

static void on_mouse_edge(int dir, void *userdata)
{
    (void)userdata;
    if (g_sender_cooldown_active) return;
    direction_t d = (dir == 0) ? DIR_LEFT : DIR_RIGHT;

    int target_idx;
    direction_t enter_dir;
    if (layout_resolve(&g_layout, d, &target_idx, &enter_dir) != 0)
        return;

    peer_t *p = peer_by_index(&g_mgr, target_idx);
    if (!p || p->state != PEER_CONNECTED)
        return;

    LOG_INFO("mouse edge %s: crossing to %s", direction_str(d), p->name);

    glew_msg_t fe = { .type = MSG_FOCUS_ENTER };
    snprintf(fe.focus_enter.from_direction, sizeof(fe.focus_enter.from_direction),
             "%s", direction_str(enter_dir));
    snprintf(fe.focus_enter.source, sizeof(fe.focus_enter.source),
             "%s", g_cfg.self_name);
    fe.focus_enter.cursor_y = input_get_cursor_y();
    peer_send(p, &fe);

    input_edge_watch_stop();
    start_sending_input(p, d);
}

static void on_sender_cooldown_expire(uv_timer_t *timer)
{
    (void)timer;
    g_sender_cooldown_active = 0;
    LOG_DBG("sender cooldown expired");
}

static void start_edge_watching(void)
{
    if (g_input_mode == MODE_LOCAL)
        input_edge_watch_start(on_mouse_edge, NULL);
}

static void on_captured_input(const input_event_t *ev, void *userdata)
{
    (void)userdata;

    /* type == -1 is the synthetic "capture stopped" signal */
    if ((int)ev->type == -1) {
        if (g_input_mode == MODE_REMOTE_SENDING && g_input_target) {
            glew_msg_t stop = { .type = MSG_INPUT_STOP };
            peer_send(g_input_target, &stop);
            LOG_INFO("input: released, sent input_stop to %s (%d events forwarded)",
                     g_input_target->name, g_input_ev_sent);
        }
        g_input_mode = MODE_LOCAL;
        g_input_target = NULL;
        start_edge_watching();
        g_sender_cooldown_active = 1;
        uv_timer_start(&g_sender_cooldown, on_sender_cooldown_expire,
                        SENDER_COOLDOWN_MS, 0);
        return;
    }

    if (g_input_mode != MODE_REMOTE_SENDING || !g_input_target)
        return;

    g_input_ev_sent++;

    if (ev->type == INPUT_KEY_DOWN)
        LOG_DBG("capture: KEY_DN keysym=0x%x mods=0x%x", ev->keysym, ev->mods);
    else if (ev->type == INPUT_KEY_UP)
        LOG_DBG("capture: KEY_UP keysym=0x%x", ev->keysym);

    /* motion x/y are already deltas from center (computed in capture layer) */
    if (ev->type == INPUT_MOTION && ev->x == 0.0 && ev->y == 0.0)
        return;

    glew_msg_t msg = { .type = MSG_INPUT };
    msg.input.type = ev->type;
    msg.input.keysym = ev->keysym;
    msg.input.x = ev->x;
    msg.input.y = ev->y;
    msg.input.button = ev->button;
    msg.input.scroll_x = ev->scroll_x;
    msg.input.scroll_y = ev->scroll_y;
    msg.input.mods = ev->mods;
    msg.input.seq = g_send_seq;
    peer_send(g_input_target, &msg);
}

static void sync_modifiers(peer_t *target)
{
    uint32_t held = input_get_mod_mask();

    static const struct { uint32_t bit; uint32_t keysym; } mods[] = {
        { (1 << 0), 0xffe1 }, /* Shift → Shift_L */
        { (1 << 2), 0xffe3 }, /* Control → Control_L */
        { (1 << 3), 0xffe9 }, /* Alt → Alt_L */
        { (1 << 6), 0xffeb }, /* Super → Super_L */
    };

    LOG_DBG("input: syncing modifiers to %s (held=0x%x)", target->name, held);
    for (size_t i = 0; i < sizeof(mods)/sizeof(mods[0]); i++) {
        glew_msg_t m = { .type = MSG_INPUT };
        m.input.type = (held & mods[i].bit) ? INPUT_KEY_DOWN : INPUT_KEY_UP;
        m.input.keysym = mods[i].keysym;
        m.input.seq = g_send_seq;
        peer_send(target, &m);
    }
}

static void start_sending_input(peer_t *target, direction_t cross_dir)
{
    g_input_mode = MODE_REMOTE_SENDING;
    g_input_target = target;
    g_input_ev_sent = 0;
    g_send_seq++;

    glew_msg_t start = { .type = MSG_INPUT_START };
    snprintf(start.input_start.source, sizeof(start.input_start.source),
             "%s", g_cfg.self_name);
    start.input_start.mods = 0;
    start.input_start.seq = g_send_seq;
    peer_send(target, &start);

    sync_modifiers(target);

    int edge_dir = (cross_dir == DIR_LEFT) ? 0 : (cross_dir == DIR_RIGHT) ? 1 : -1;
    input_capture_start(on_captured_input, NULL, edge_dir);
    LOG_INFO("input: capturing and forwarding to %s", target->name);
}

static void stop_sending_input(void)
{
    input_capture_stop();
    /* the capture_stop callback sends input_stop and resets state */
}

/* ── Focus handling ─────────────────────────────────────────────── */

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

    /* send focus_enter */
    glew_msg_t fe = { .type = MSG_FOCUS_ENTER };
    snprintf(fe.focus_enter.from_direction, sizeof(fe.focus_enter.from_direction),
             "%s", direction_str(enter_dir));
    snprintf(fe.focus_enter.source, sizeof(fe.focus_enter.source),
             "%s", g_cfg.self_name);
    fe.focus_enter.cursor_y = input_get_cursor_y();
    peer_send(p, &fe);

    /* start capturing and forwarding input (skip if already capturing to this peer) */
    if (g_input_mode != MODE_REMOTE_SENDING || g_input_target != p)
        start_sending_input(p, dir);

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

        if (g_input_mode == MODE_REMOTE_SENDING) {
            /* focus returning from remote — release grab, refocus locally */
            LOG_INFO("focus returning from %s (entering from %s)",
                     msg->focus_enter.source, msg->focus_enter.from_direction);
            input_capture_stop(); /* releases grab, triggers on_captured_input(-1) which restarts edge watch */
            g_driver->focus_edge(from);
            break;
        }

        /* normal incoming focus — set virtual cursor at entry edge */
        LOG_INFO("focus entering from %s (source: %s)",
                 msg->focus_enter.from_direction, msg->focus_enter.source);

        double cy = msg->focus_enter.cursor_y;
        if (cy < 0.0 || cy > 1.0) cy = 0.5;
        /* start cursor inset from the entry edge to prevent immediate
         * edge dwell triggering on the first mouse movement */
        if (from == DIR_LEFT)       { g_virt_x = 0.02; g_virt_y = cy; }
        else if (from == DIR_RIGHT) { g_virt_x = 0.98; g_virt_y = cy; }
        else                        { g_virt_x = 0.5;  g_virt_y = cy; }

        int rc = g_driver->focus_edge(from);

        glew_msg_t ack = { .type = MSG_FOCUS_ACK };
        ack.focus_ack.success = (rc == 0) ? 1 : 0;
        peer_send(p, &ack);
        break;
    }

    case MSG_FOCUS_ACK:
        LOG_DBG("peer %s: focus_ack success=%d", p->name, msg->focus_ack.success);
        break;

    case MSG_INPUT_START:
        LOG_INFO("input: receiving from %s (seq=%u)", msg->input_start.source, msg->input_start.seq);
        g_input_mode = MODE_REMOTE_RECEIVING;
        g_input_source = p;
        g_input_ev_recv = 0;
        g_recv_seq = msg->input_start.seq;
        g_edge_cooldown_until = uv_now(g_loop) + RECV_COOLDOWN_MS;
        g_recv_edge_armed = 0;
        break;

    case MSG_INPUT_STOP:
        LOG_INFO("input: %s stopped sending (%d events received)", p->name, g_input_ev_recv);
        g_input_mode = MODE_LOCAL;
        g_input_source = NULL;
        break;

    case MSG_INPUT: {
        if (g_input_mode != MODE_REMOTE_RECEIVING) break;
        if (msg->input.seq != g_recv_seq) {
            LOG_DBG("input: stale event (seq %u, expected %u)", msg->input.seq, g_recv_seq);
            break;
        }

        input_event_t ev = {
            .type     = msg->input.type,
            .keysym   = msg->input.keysym,
            .x        = msg->input.x,
            .y        = msg->input.y,
            .button   = msg->input.button,
            .scroll_x = msg->input.scroll_x,
            .scroll_y = msg->input.scroll_y,
            .mods     = msg->input.mods,
        };
        g_input_ev_recv++;

        if (ev.type == INPUT_KEY_DOWN)
            LOG_DBG("inject: KEY_DN keysym=0x%x mods=0x%x", ev.keysym, ev.mods);
        else if (ev.type == INPUT_KEY_UP)
            LOG_DBG("inject: KEY_UP keysym=0x%x", ev.keysym);

        /* intercept mod+direction: route through local WM driver.
         * check mods field — only intercept the LOCAL WM's mod key,
         * not all modifiers (alt+arrow for terminal word jump must pass through) */
        int mod_held = (ev.mods & g_local_mod_bit);
        if (mod_held && ev.type == INPUT_KEY_DOWN) {
            direction_t dir = arrow_keysym_to_dir(ev.keysym);
            if ((int)dir >= 0) {
                int can = g_driver->can_focus(dir);
                if (can) {
                    g_driver->do_focus(dir);
                    LOG_DBG("remote focus %s: moved within %s",
                            direction_str(dir), g_driver->name);
                    break;
                }
                /* at WM edge — try cross to neighbor */
                int target_idx;
                direction_t enter_dir;
                if (layout_resolve(&g_layout, dir, &target_idx, &enter_dir) == 0) {
                    peer_t *target = peer_by_index(&g_mgr, target_idx);
                    if (target && target->state == PEER_CONNECTED) {
                        LOG_INFO("focus %s: crossing to %s",
                                 direction_str(dir), target->name);

                        glew_msg_t fe = { .type = MSG_FOCUS_ENTER };
                        snprintf(fe.focus_enter.from_direction,
                                 sizeof(fe.focus_enter.from_direction),
                                 "%s", direction_str(enter_dir));
                        snprintf(fe.focus_enter.source,
                                 sizeof(fe.focus_enter.source),
                                 "%s", g_cfg.self_name);
                        fe.focus_enter.cursor_y = g_virt_y;
                        peer_send(target, &fe);

                        g_input_mode = MODE_LOCAL;
                        g_input_source = NULL;
                        break;
                    }
                }
                /* no neighbor — fall through to inject */
            }
        }

        /* motion: accumulate deltas into virtual cursor */
        if (ev.type == INPUT_MOTION) {
            g_virt_x += ev.x;
            g_virt_y += ev.y;

            /* clamp to screen bounds */
            if (g_virt_x < 0.0) g_virt_x = 0.0;
            if (g_virt_x > 1.0) g_virt_x = 1.0;
            if (g_virt_y < 0.0) g_virt_y = 0.0;
            if (g_virt_y > 1.0) g_virt_y = 1.0;

            /* arm when cursor is well inside the screen */
            if (g_virt_x > 0.05 && g_virt_x < 0.95)
                g_recv_edge_armed = 1;

            /* check edge crossing (armed + past cooldown) */
            direction_t edge_dir = -1;
            if (g_recv_edge_armed && uv_now(g_loop) >= g_edge_cooldown_until) {
                if (g_virt_x <= 0.001)      edge_dir = DIR_LEFT;
                else if (g_virt_x >= 0.999) edge_dir = DIR_RIGHT;
            }

            if ((int)edge_dir >= 0)
                g_recv_edge_armed = 0;

            if ((int)edge_dir >= 0) {
                int target_idx;
                direction_t enter_dir;
                if (layout_resolve(&g_layout, edge_dir, &target_idx, &enter_dir) == 0) {
                    peer_t *target = peer_by_index(&g_mgr, target_idx);
                    if (target && target->state == PEER_CONNECTED) {
                        LOG_INFO("mouse edge %s: crossing back to %s",
                                 direction_str(edge_dir), target->name);

                        glew_msg_t fe = { .type = MSG_FOCUS_ENTER };
                        snprintf(fe.focus_enter.from_direction,
                                 sizeof(fe.focus_enter.from_direction),
                                 "%s", direction_str(enter_dir));
                        snprintf(fe.focus_enter.source,
                                 sizeof(fe.focus_enter.source),
                                 "%s", g_cfg.self_name);
                        fe.focus_enter.cursor_y = g_virt_y;
                        peer_send(target, &fe);

                        g_input_mode = MODE_LOCAL;
                        g_input_source = NULL;
                        break;
                    }
                }
            }

            /* inject at virtual position */
            ev.x = g_virt_x;
            ev.y = g_virt_y;
        }

        input_inject_event(&ev);
        break;
    }

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

    if (g_input_mode == MODE_REMOTE_SENDING)
        stop_sending_input();

    input_capture_shutdown();
    input_inject_shutdown();
    uv_timer_stop(&g_sender_cooldown);
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

    init_local_mod();
    g_loop = uv_default_loop();

    /* init input subsystems */
    if (input_capture_init(g_loop) != 0)
        LOG_WARN("input capture init failed (capture will be unavailable)");

    if (input_inject_init() != 0)
        LOG_WARN("input inject init failed (injection will be unavailable)");

    if (peer_mgr_init(&g_mgr, g_loop, &g_cfg) != 0) {
        LOG_ERR("peer manager init failed");
        return 1;
    }

    g_mgr.on_local_message = on_local_message;
    g_mgr.on_peer_message = on_peer_message;

    for (int i = 0; i < g_mgr.peer_count; i++)
        g_mgr.peers[i].on_message = on_peer_message;

    uv_timer_init(g_loop, &g_sender_cooldown);
    uv_signal_init(g_loop, &g_sigint);
    uv_signal_init(g_loop, &g_sigterm);
    uv_signal_start(&g_sigint, on_signal, SIGINT);
    uv_signal_start(&g_sigterm, on_signal, SIGTERM);

    peer_mgr_start(&g_mgr);
    start_edge_watching();

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
