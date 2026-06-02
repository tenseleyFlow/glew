#ifndef GLEW_PEER_H
#define GLEW_PEER_H

#include "proto.h"
#include "../config.h"
#include <uv.h>

#define PEER_BUF_SIZE  65536
#define HEARTBEAT_INTERVAL_MS  5000
#define RECONNECT_BASE_MS      1000
#define RECONNECT_MAX_MS       30000

typedef enum {
    PEER_DISCONNECTED,
    PEER_CONNECTING,
    PEER_HANDSHAKING,
    PEER_CONNECTED,
} peer_state_t;

typedef struct peer {
    char            name[64];
    char            address[256];
    int             port;
    int             machine_idx;
    peer_state_t    state;

    uv_tcp_t        handle;
    uv_connect_t    connect_req;
    uv_timer_t      reconnect_timer;

    /* read buffer for length-prefixed framing */
    char            rbuf[PEER_BUF_SIZE];
    size_t          rlen;

    /* backoff state */
    int             reconnect_ms;

    /* callback for received messages */
    void          (*on_message)(struct peer *p, const glew_msg_t *msg);
    void           *userdata;
} peer_t;

typedef struct {
    uv_loop_t      *loop;
    uv_tcp_t        server;
    uv_pipe_t       local_server;
    uv_timer_t      heartbeat_timer;

    peer_t         *peers;
    int             peer_count;

    const glew_config_t *cfg;

    /* callback for messages from local CLI */
    void          (*on_local_message)(const glew_msg_t *msg, uv_stream_t *client);
    /* callback for messages from remote peers */
    void          (*on_peer_message)(peer_t *p, const glew_msg_t *msg);
} peer_mgr_t;

int  peer_mgr_init(peer_mgr_t *mgr, uv_loop_t *loop, const glew_config_t *cfg);
void peer_mgr_start(peer_mgr_t *mgr);
void peer_mgr_stop(peer_mgr_t *mgr);

/* Send a message to a specific peer. Returns 0 on success. */
int  peer_send(peer_t *p, const glew_msg_t *msg);

/* Send a message on a raw uv_stream (for local CLI responses). */
int  stream_send(uv_stream_t *stream, const glew_msg_t *msg);

/* Find a peer by machine index. */
peer_t *peer_by_index(peer_mgr_t *mgr, int idx);

/* Find a peer by name. */
peer_t *peer_by_name(peer_mgr_t *mgr, const char *name);

#endif
