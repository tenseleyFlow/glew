#ifndef GLEW_DRIVER_H
#define GLEW_DRIVER_H

#include <stdint.h>

typedef enum {
    DIR_LEFT,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN,
} direction_t;

typedef struct wm_driver {
    const char *name;

    int  (*init)(const char *socket_path);
    void (*shutdown)(void);

    /* Can focus move in this direction? 1 = yes, 0 = at edge. */
    int  (*can_focus)(direction_t dir);

    /* Execute focus move. Returns 0 on success. */
    int  (*do_focus)(direction_t dir);

    /* Focus the edge-most window when receiving focus from a direction.
     * e.g., focus_edge(DIR_RIGHT) focuses the rightmost window. */
    int  (*focus_edge)(direction_t dir);

    /* Dispatch a WM-agnostic action (e.g. "workspace 1", "close", "spawn-terminal").
     * Returns 0 on success, -1 if unsupported. NULL = no dispatch support. */
    int  (*dispatch_action)(const char *action);
} wm_driver_t;

const char *direction_str(direction_t dir);
direction_t direction_parse(const char *s);
direction_t direction_opposite(direction_t dir);

/* available drivers */
extern const wm_driver_t i3_driver;
extern const wm_driver_t tarmac_driver;
extern const wm_driver_t gar_driver;

/* look up a driver by name ("i3", "tarmac", "gar") */
const wm_driver_t *driver_by_name(const char *name);

#endif
