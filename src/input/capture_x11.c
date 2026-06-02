#include "input.h"
#include "../log.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static Display      *dpy;
static Window        root;
static int           screen_w, screen_h;
static uv_timer_t    poll_timer;
static uv_timer_t    safety_timer;
static input_event_cb g_cb;
static void          *g_userdata;
static int            grabbing;

#define POLL_INTERVAL_MS   2
#define SAFETY_TIMEOUT_MS  60000

static uint32_t x_mods_to_mask(unsigned int state)
{
    uint32_t m = 0;
    if (state & ShiftMask)   m |= (1 << 0);
    if (state & ControlMask) m |= (1 << 2);
    if (state & Mod1Mask)    m |= (1 << 3);
    if (state & Mod4Mask)    m |= (1 << 6);
    return m;
}

static void force_ungrab(void)
{
    if (dpy) {
        XUngrabKeyboard(dpy, CurrentTime);
        XUngrabPointer(dpy, CurrentTime);
        XFlush(dpy);
    }
}

static void crash_handler(int sig)
{
    force_ungrab();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void on_safety_timeout(uv_timer_t *timer)
{
    (void)timer;
    LOG_WARN("x11 capture: safety timeout reached, releasing grab");
    input_capture_stop();
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

            /* F12 or Ctrl+Alt+Escape releases the grab */
            if (ie.type == INPUT_KEY_DOWN &&
                (ie.keysym == XK_F12 || ie.keysym == XK_Scroll_Lock)) {
                LOG_INFO("x11 capture: release key pressed");
                input_capture_stop();
                return;
            }
            if (ie.type == INPUT_KEY_DOWN && ie.keysym == XK_Escape &&
                (ev.xkey.state & ControlMask) && (ev.xkey.state & Mod1Mask)) {
                LOG_INFO("x11 capture: emergency release (Ctrl+Alt+Escape)");
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
                case 4: ie.scroll_y = -3; break;
                case 5: ie.scroll_y =  3; break;
                case 6: ie.scroll_x = -3; break;
                case 7: ie.scroll_x =  3; break;
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

static void on_poll_timer(uv_timer_t *handle)
{
    (void)handle;
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

    uv_timer_init(loop, &poll_timer);
    uv_timer_init(loop, &safety_timer);

    signal(SIGABRT, crash_handler);
    signal(SIGSEGV, crash_handler);
    atexit(force_ungrab);

    LOG_INFO("x11 capture: initialized (%dx%d)", screen_w, screen_h);
    return 0;
}

void input_capture_start(input_event_cb cb, void *userdata)
{
    if (grabbing) return;

    g_cb = cb;
    g_userdata = userdata;

    int kb = XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    int ptr = XGrabPointer(dpy, root, True,
                 PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XFlush(dpy);

    if (kb != GrabSuccess) {
        LOG_ERR("x11 capture: XGrabKeyboard failed (%d)", kb);
        XUngrabPointer(dpy, CurrentTime);
        XFlush(dpy);
        return;
    }
    if (ptr != GrabSuccess) {
        LOG_ERR("x11 capture: XGrabPointer failed (%d)", ptr);
        XUngrabKeyboard(dpy, CurrentTime);
        XFlush(dpy);
        return;
    }

    grabbing = 1;
    uv_timer_start(&poll_timer, on_poll_timer, POLL_INTERVAL_MS, POLL_INTERVAL_MS);
    uv_timer_start(&safety_timer, on_safety_timeout, SAFETY_TIMEOUT_MS, 0);

    LOG_INFO("x11 capture: grab active (Scroll_Lock or Ctrl+Alt+Esc to release, "
             "auto-release in %ds)", SAFETY_TIMEOUT_MS / 1000);
}

void input_capture_stop(void)
{
    if (!grabbing) return;

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XFlush(dpy);

    grabbing = 0;
    uv_timer_stop(&poll_timer);
    uv_timer_stop(&safety_timer);

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
        uv_timer_stop(&poll_timer);
        uv_timer_stop(&safety_timer);
        XCloseDisplay(dpy);
        dpy = NULL;
    }
}
