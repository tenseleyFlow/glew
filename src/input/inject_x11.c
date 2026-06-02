#include "input.h"
#include "../log.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

static Display *dpy;
static int screen_w, screen_h;

int input_inject_init(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        LOG_ERR("x11 inject: cannot open display");
        return -1;
    }

    int ev, err, maj, min;
    if (!XTestQueryExtension(dpy, &ev, &err, &maj, &min)) {
        LOG_ERR("x11 inject: XTest extension not available");
        XCloseDisplay(dpy);
        dpy = NULL;
        return -1;
    }

    int screen = DefaultScreen(dpy);
    screen_w = DisplayWidth(dpy, screen);
    screen_h = DisplayHeight(dpy, screen);

    LOG_INFO("x11 inject: initialized (XTest %d.%d, %dx%d)",
             maj, min, screen_w, screen_h);
    return 0;
}

void input_inject_event(const input_event_t *ev)
{
    if (!dpy) return;

    switch (ev->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP: {
        KeyCode kc = XKeysymToKeycode(dpy, ev->keysym);
        if (kc == 0) break;
        XTestFakeKeyEvent(dpy, kc, ev->type == INPUT_KEY_DOWN, CurrentTime);
        break;
    }
    case INPUT_MOTION: {
        int x = (int)(ev->x * screen_w);
        int y = (int)(ev->y * screen_h);
        XTestFakeMotionEvent(dpy, DefaultScreen(dpy), x, y, CurrentTime);
        break;
    }
    case INPUT_BUTTON_DOWN:
    case INPUT_BUTTON_UP:
        XTestFakeButtonEvent(dpy, (unsigned int)ev->button,
                             ev->type == INPUT_BUTTON_DOWN, CurrentTime);
        break;
    case INPUT_SCROLL:
        if (ev->scroll_y < 0) {
            XTestFakeButtonEvent(dpy, 4, True, CurrentTime);
            XTestFakeButtonEvent(dpy, 4, False, CurrentTime);
        } else if (ev->scroll_y > 0) {
            XTestFakeButtonEvent(dpy, 5, True, CurrentTime);
            XTestFakeButtonEvent(dpy, 5, False, CurrentTime);
        }
        if (ev->scroll_x < 0) {
            XTestFakeButtonEvent(dpy, 6, True, CurrentTime);
            XTestFakeButtonEvent(dpy, 6, False, CurrentTime);
        } else if (ev->scroll_x > 0) {
            XTestFakeButtonEvent(dpy, 7, True, CurrentTime);
            XTestFakeButtonEvent(dpy, 7, False, CurrentTime);
        }
        break;
    }

    XFlush(dpy);
}

void input_inject_shutdown(void)
{
    if (dpy) {
        XCloseDisplay(dpy);
        dpy = NULL;
    }
}
