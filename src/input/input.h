#ifndef GLEW_INPUT_H
#define GLEW_INPUT_H

#include <stdint.h>
#include <uv.h>

typedef enum {
    INPUT_KEY_DOWN,
    INPUT_KEY_UP,
    INPUT_MOTION,
    INPUT_BUTTON_DOWN,
    INPUT_BUTTON_UP,
    INPUT_SCROLL,
} input_type_t;

typedef struct {
    input_type_t type;
    uint32_t     keysym;
    double       x, y;       /* normalized [0.0, 1.0] */
    int          button;
    int          scroll_x, scroll_y;
    uint32_t     mods;
} input_event_t;

typedef void (*input_event_cb)(const input_event_t *ev, void *userdata);

/* ── Capture (grabs input, forwards to callback) ────────────────── */

int  input_capture_init(uv_loop_t *loop);
void input_capture_start(input_event_cb cb, void *userdata, int crossing_dir);
void input_capture_stop(void);
void input_capture_shutdown(void);

/* ── Cursor/modifier query ──────────────────────────────────────── */

double input_get_cursor_y(void);
void   input_get_cursor_pos(double *x, double *y);
uint32_t input_get_mod_mask(void);

/* ── Injection (receives events, injects locally) ───────────────── */

int  input_inject_init(void);
void input_inject_event(const input_event_t *ev);
void input_inject_shutdown(void);

/* ── Edge watching (detect mouse at screen edge while in local mode) */

typedef void (*edge_cb_t)(int dir, void *userdata); /* dir: 0=left 1=right */
void input_edge_watch_start(edge_cb_t cb, void *userdata);
void input_edge_watch_stop(void);

#endif
