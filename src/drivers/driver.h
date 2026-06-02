#ifndef GLEW_DRIVER_H
#define GLEW_DRIVER_H

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
} wm_driver_t;

const char *direction_str(direction_t dir);
direction_t direction_parse(const char *s);
direction_t direction_opposite(direction_t dir);

#endif
