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
static Cursor        blank_cursor;
static uv_timer_t    poll_timer;
static uv_timer_t    safety_timer;
static uv_timer_t    grab_retry_timer;
static input_event_cb g_cb;
static void          *g_userdata;
static int            grabbing;
static int            grab_retries;
static uv_timer_t    edge_timer;
static int            center_x, center_y;
static int            saved_x, saved_y;

#define POLL_INTERVAL_MS   2
#define SAFETY_TIMEOUT_MS  60000
#define EDGE_POLL_MS       50
#define EDGE_ZONE_PX       2
#define EDGE_REARM_PX      100

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

static void process_x_keys(void)
{
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        input_event_t ie = {0};

        switch (ev.type) {
        case KeyPress:
        case KeyRelease:
            ie.type = (ev.type == KeyPress) ? INPUT_KEY_DOWN : INPUT_KEY_UP;
            ie.keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            ie.mods = x_mods_to_mask(ev.xkey.state);

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

    /* keyboard and buttons via X events */
    process_x_keys();
    if (!grabbing) return;

    /* mouse via XQueryPointer (core MotionNotify unreliable on modern X) */
    Window rr, cr;
    int rx, ry, wx, wy;
    unsigned int mask;
    if (!XQueryPointer(dpy, root, &rr, &cr, &rx, &ry, &wx, &wy, &mask))
        return;

    if (rx == center_x && ry == center_y)
        return;

    input_event_t ie = {
        .type = INPUT_MOTION,
        .x = (double)(rx - center_x) / screen_w,
        .y = (double)(ry - center_y) / screen_h,
    };

    XWarpPointer(dpy, None, root, 0, 0, 0, 0, center_x, center_y);
    XFlush(dpy);

    if (g_cb)
        g_cb(&ie, g_userdata);
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

    /* create an invisible cursor for hiding during grab */
    Pixmap pm = XCreatePixmap(dpy, root, 1, 1, 1);
    XColor black = {0};
    blank_cursor = XCreatePixmapCursor(dpy, pm, pm, &black, &black, 0, 0);
    XFreePixmap(dpy, pm);

    center_x = screen_w / 2;
    center_y = screen_h / 2;

    uv_timer_init(loop, &poll_timer);
    uv_timer_init(loop, &safety_timer);
    uv_timer_init(loop, &grab_retry_timer);
    uv_timer_init(loop, &edge_timer);

    signal(SIGABRT, crash_handler);
    signal(SIGSEGV, crash_handler);
    atexit(force_ungrab);

    LOG_INFO("x11 capture: initialized (%dx%d)", screen_w, screen_h);
    return 0;
}

#define GRAB_RETRY_MS      50
#define GRAB_MAX_RETRIES   10

static int try_grab(void)
{
    int kb = XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (kb != GrabSuccess) {
        XFlush(dpy);
        return kb;
    }

    int ptr = XGrabPointer(dpy, root, True,
                 PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, blank_cursor, CurrentTime);
    XFlush(dpy);

    if (ptr != GrabSuccess) {
        XUngrabKeyboard(dpy, CurrentTime);
        XFlush(dpy);
        return ptr;
    }

    grabbing = 1;

    /* save cursor position so we can restore it on release */
    Window rr, cr;
    int wx, wy;
    unsigned int mask;
    XQueryPointer(dpy, root, &rr, &cr, &saved_x, &saved_y, &wx, &wy, &mask);

    XWarpPointer(dpy, None, root, 0, 0, 0, 0, center_x, center_y);
    XFlush(dpy);

    uv_timer_start(&poll_timer, on_poll_timer, POLL_INTERVAL_MS, POLL_INTERVAL_MS);
    uv_timer_start(&safety_timer, on_safety_timeout, SAFETY_TIMEOUT_MS, 0);

    LOG_INFO("x11 capture: grab active (F12 to release, auto-release in %ds)",
             SAFETY_TIMEOUT_MS / 1000);
    return GrabSuccess;
}

static void on_grab_retry(uv_timer_t *timer)
{
    (void)timer;
    grab_retries++;
    int rc = try_grab();
    if (rc == GrabSuccess)
        return;

    if (grab_retries >= GRAB_MAX_RETRIES) {
        LOG_ERR("x11 capture: grab failed after %d retries (last error: %d)",
                grab_retries, rc);
        if (g_cb) {
            input_event_t stop = { .type = -1 };
            g_cb(&stop, g_userdata);
        }
        return;
    }

    LOG_DBG("x11 capture: grab busy, retry %d/%d in %dms",
            grab_retries, GRAB_MAX_RETRIES, GRAB_RETRY_MS);
    uv_timer_start(&grab_retry_timer, on_grab_retry, GRAB_RETRY_MS, 0);
}

void input_capture_start(input_event_cb cb, void *userdata)
{
    if (grabbing) return;

    g_cb = cb;
    g_userdata = userdata;
    grab_retries = 0;

    int rc = try_grab();
    if (rc == GrabSuccess)
        return;

    LOG_DBG("x11 capture: grab busy (WM keybind active), retrying in %dms", GRAB_RETRY_MS);
    uv_timer_start(&grab_retry_timer, on_grab_retry, GRAB_RETRY_MS, 0);
}

void input_capture_stop(void)
{
    if (!grabbing) return;

    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);

    /* restore cursor near saved position but offset away from screen edges
     * to prevent immediate re-trigger by the edge watcher */
    int restore_x = saved_x;
    int restore_y = saved_y;
    if (restore_x <= EDGE_ZONE_PX + 50)
        restore_x = 50;
    if (restore_x >= screen_w - 1 - EDGE_ZONE_PX - 50)
        restore_x = screen_w - 50;

    XWarpPointer(dpy, None, root, 0, 0, 0, 0, restore_x, restore_y);
    XFlush(dpy);

    grabbing = 0;
    uv_timer_stop(&poll_timer);
    uv_timer_stop(&safety_timer);
    uv_timer_stop(&grab_retry_timer);

    if (g_cb) {
        input_event_t stop = { .type = -1 };
        g_cb(&stop, g_userdata);
    }

    LOG_INFO("x11 capture: grab released");
}

/* ── Edge watching ───────────────────────────────────────────────── */

static edge_cb_t   g_edge_cb;
static void        *g_edge_ud;
static int          edge_triggered;
static uint64_t     edge_start_time;

static void on_edge_poll(uv_timer_t *timer)
{
    (void)timer;
    if (!dpy) return;

    Window root_ret, child_ret;
    int rx, ry, wx, wy;
    unsigned int mask;
    if (!XQueryPointer(dpy, root, &root_ret, &child_ret, &rx, &ry, &wx, &wy, &mask))
        return;

    int at_left  = (rx <= EDGE_ZONE_PX);
    int at_right = (rx >= screen_w - 1 - EDGE_ZONE_PX);
    int in_safe_zone = (rx > EDGE_REARM_PX && rx < screen_w - 1 - EDGE_REARM_PX);

    if (in_safe_zone)
        edge_triggered = 0;

    if (!at_left && !at_right)
        return;

    if (edge_triggered)
        return;

    edge_triggered = 1;
    if (g_edge_cb) {
        int dir = at_left ? 0 : 1;
        g_edge_cb(dir, g_edge_ud);
    }
}

void input_edge_watch_start(edge_cb_t cb, void *userdata)
{
    g_edge_cb = cb;
    g_edge_ud = userdata;
    edge_triggered = 1;  /* start suppressed — require mouse to leave edge first */
    edge_start_time = uv_now(edge_timer.loop);
    uv_timer_start(&edge_timer, on_edge_poll, EDGE_POLL_MS, EDGE_POLL_MS);
    LOG_DBG("edge watch: started (poll every %dms)", EDGE_POLL_MS);
}

void input_edge_watch_stop(void)
{
    uv_timer_stop(&edge_timer);
    g_edge_cb = NULL;
    LOG_DBG("edge watch: stopped");
}

void input_capture_shutdown(void)
{
    if (grabbing)
        input_capture_stop();
    input_edge_watch_stop();
    if (dpy) {
        uv_timer_stop(&poll_timer);
        uv_timer_stop(&safety_timer);
        uv_timer_stop(&grab_retry_timer);
        uv_timer_stop(&edge_timer);
        XCloseDisplay(dpy);
        dpy = NULL;
    }
}
