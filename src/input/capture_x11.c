#include "input.h"
#include "../log.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <string.h>

static Display      *dpy;
static Window        root;
static int           screen_w, screen_h;
static uv_poll_t     x_poll;
static input_event_cb g_cb;
static void          *g_userdata;
static int            grabbing;

static uint32_t x_mods_to_mask(unsigned int state)
{
    uint32_t m = 0;
    if (state & ShiftMask)   m |= (1 << 0);
    if (state & ControlMask) m |= (1 << 2);
    if (state & Mod1Mask)    m |= (1 << 3); /* Alt */
    if (state & Mod4Mask)    m |= (1 << 6); /* Super */
    return m;
}

static void process_x_events(void)
{
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        input_event_t ie = {0};

        switch (ev.type) {
        case KeyPress:
        case KeyRelease: {
            ie.type = (ev.type == KeyPress) ? INPUT_KEY_DOWN : INPUT_KEY_UP;
            ie.keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            ie.mods = x_mods_to_mask(ev.xkey.state);

            /* escape key releases the grab */
            if (ie.type == INPUT_KEY_DOWN && ie.keysym == XK_Scroll_Lock) {
                input_capture_stop();
                return;
            }
            break;
        }
        case MotionNotify:
            ie.type = INPUT_MOTION;
            ie.x = (double)ev.xmotion.x_root / screen_w;
            ie.y = (double)ev.xmotion.y_root / screen_h;
            ie.mods = x_mods_to_mask(ev.xmotion.state);
            break;
        case ButtonPress:
        case ButtonRelease:
            if (ev.xbutton.button >= 4 && ev.xbutton.button <= 7) {
                if (ev.type == ButtonRelease) continue;
                ie.type = INPUT_SCROLL;
                switch (ev.xbutton.button) {
                case 4: ie.scroll_y = -3; break;  /* up */
                case 5: ie.scroll_y =  3; break;  /* down */
                case 6: ie.scroll_x = -3; break;  /* left */
                case 7: ie.scroll_x =  3; break;  /* right */
                }
            } else {
                ie.type = (ev.type == ButtonPress) ? INPUT_BUTTON_DOWN : INPUT_BUTTON_UP;
                ie.button = ev.xbutton.button;
            }
            ie.x = (double)ev.xbutton.x_root / screen_w;
            ie.y = (double)ev.xbutton.y_root / screen_h;
            ie.mods = x_mods_to_mask(ev.xbutton.state);
            break;
        default:
            continue;
        }

        if (g_cb)
            g_cb(&ie, g_userdata);
    }
}

static void on_x_readable(uv_poll_t *handle, int status, int events)
{
    (void)handle;
    if (status < 0) return;
    if (events & UV_READABLE)
        process_x_events();
}

int input_capture_init(uv_loop_t *loop)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        LOG_ERR("x11 capture: cannot open display");
        return -1;
    }

    int screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    screen_w = DisplayWidth(dpy, screen);
    screen_h = DisplayHeight(dpy, screen);

    int xfd = ConnectionNumber(dpy);
    uv_poll_init(loop, &x_poll, xfd);

    LOG_INFO("x11 capture: initialized (%dx%d)", screen_w, screen_h);
    return 0;
}

void input_capture_start(input_event_cb cb, void *userdata)
{
    if (grabbing) return;

    g_cb = cb;
    g_userdata = userdata;

    XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    XGrabPointer(dpy, root, True,
                 PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XFlush(dpy);

    grabbing = 1;
    uv_poll_start(&x_poll, UV_READABLE, on_x_readable);
    LOG_INFO("x11 capture: grab started");
}

void input_capture_stop(void)
{
    if (!grabbing) return;

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XFlush(dpy);

    grabbing = 0;
    uv_poll_stop(&x_poll);

    /* notify via a synthetic "stop" event */
    if (g_cb) {
        input_event_t stop = { .type = -1 };
        g_cb(&stop, g_userdata);
    }

    LOG_INFO("x11 capture: grab released");
}

void input_capture_shutdown(void)
{
    if (grabbing)
        input_capture_stop();
    if (dpy) {
        uv_poll_stop(&x_poll);
        XCloseDisplay(dpy);
        dpy = NULL;
    }
}
