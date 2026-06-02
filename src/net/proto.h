#ifndef GLEW_PROTO_H
#define GLEW_PROTO_H

#include <stddef.h>
#include <stdint.h>

/*
 * Wire format: [u32 length (network byte order)][JSON payload]
 *
 * Message types:
 *   hello        — handshake (client → server)
 *   hello_ok     — handshake accepted
 *   hello_err    — handshake rejected
 *   focus_enter  — focus crossing into this machine
 *   focus_ack    — acknowledgment of focus_enter
 *   ping / pong  — heartbeat
 *
 * Local (Unix socket) messages:
 *   focus        — CLI → glewd: move focus in direction
 *   focus_result — glewd → CLI: result of focus command
 */

typedef enum {
    MSG_UNKNOWN,
    MSG_HELLO,
    MSG_HELLO_OK,
    MSG_HELLO_ERR,
    MSG_FOCUS_ENTER,
    MSG_FOCUS_ACK,
    MSG_PING,
    MSG_PONG,
    /* input forwarding */
    MSG_INPUT_START,
    MSG_INPUT_STOP,
    MSG_INPUT,
    /* local only */
    MSG_FOCUS,
    MSG_FOCUS_RESULT,
} msg_type_t;

typedef struct {
    msg_type_t type;
    union {
        struct { char name[64]; char secret[256]; }         hello;
        struct { char reason[128]; }                        hello_err;
        struct { char from_direction[8]; char source[64]; double cursor_y; } focus_enter;
        struct { int success; }                             focus_ack;
        struct { char source[64]; uint32_t mods; uint32_t seq; } input_start;
        struct {
            int      type;
            uint32_t keysym;
            double   x, y;
            int      button;
            int      scroll_x, scroll_y;
            uint32_t mods;
            uint32_t seq;
        }                                                   input;
        struct { char direction[8]; }                       focus;
        struct { int crossed; char target[64]; }            focus_result;
    };
} glew_msg_t;

/* Serialize a message to a malloc'd buffer with length prefix.
 * Caller must free the returned buffer.
 * Returns total size (4-byte header + payload), or 0 on error. */
size_t msg_serialize(const glew_msg_t *msg, char **out);

/* Parse a JSON payload (without length prefix) into a message.
 * Returns 0 on success, -1 on error. */
int msg_parse(const char *json, size_t len, glew_msg_t *out);

/* Read the 4-byte length prefix and return the payload length.
 * Returns -1 if the buffer is too small. */
int32_t proto_read_len(const char *buf, size_t avail);

#define PROTO_HEADER_SIZE 4

#endif
