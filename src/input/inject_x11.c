#include "input.h"
#include "../log.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

static Display *dpy;
static int screen_w, screen_h;
static Window root;

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
    root = RootWindow(dpy, screen);

    LOG_INFO("x11 inject: initialized (XTest %d.%d, %dx%d)",
             maj, min, screen_w, screen_h);
    return 0;
}

static unsigned int mods_to_x11(uint32_t mods)
{
    unsigned int state = 0;
    if (mods & (1 << 0)) state |= ShiftMask;
    if (mods & (1 << 1)) state |= LockMask;
    if (mods & (1 << 2)) state |= ControlMask;
    if (mods & (1 << 3)) state |= Mod1Mask;
    if (mods & (1 << 6)) state |= Mod4Mask;
    return state;
}

static int is_hold_modifier(uint32_t ks)
{
    /* skip hold-type modifiers — their state is carried in the mods field.
     * Caps_Lock (0xffe5) and Num_Lock (0xffe6) are toggles and must be injected. */
    return ks == 0xffe1 || ks == 0xffe2 ||  /* Shift_L, Shift_R */
           ks == 0xffe3 || ks == 0xffe4 ||  /* Control_L, Control_R */
           ks == 0xffe7 || ks == 0xffe8 ||  /* Meta_L, Meta_R */
           ks == 0xffe9 || ks == 0xffea ||  /* Alt_L, Alt_R */
           ks == 0xffeb || ks == 0xffec ||  /* Super_L, Super_R */
           ks == 0xffed || ks == 0xffee;    /* Hyper_L, Hyper_R */
}

void input_inject_event(const input_event_t *ev)
{
    if (!dpy) return;

    switch (ev->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP: {
        KeyCode kc = XKeysymToKeycode(dpy, ev->keysym);
        if (kc == 0) break;

        if (is_hold_modifier(ev->keysym)) {
            XTestFakeKeyEvent(dpy, kc, ev->type == INPUT_KEY_DOWN, CurrentTime);
            break;
        }

        Window focused;
        int revert;
        XGetInputFocus(dpy, &focused, &revert);

        XEvent xev = {0};
        xev.xkey.type = (ev->type == INPUT_KEY_DOWN) ? KeyPress : KeyRelease;
        xev.xkey.display = dpy;
        xev.xkey.window = focused;
        xev.xkey.root = root;
        xev.xkey.time = CurrentTime;
        xev.xkey.keycode = kc;
        xev.xkey.state = mods_to_x11(ev->mods);
        xev.xkey.same_screen = True;

        long mask = (ev->type == INPUT_KEY_DOWN) ? KeyPressMask : KeyReleaseMask;
        XSendEvent(dpy, focused, True, mask, &xev);
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
