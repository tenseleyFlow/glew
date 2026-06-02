#ifdef __APPLE__

#include "input.h"
#include "../log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <pthread.h>
#include <unistd.h>

#include "keymap_macos.h"

static CFMachPortRef       tap;
static CFRunLoopSourceRef  tap_source;
static CFRunLoopRef        tap_runloop;
static pthread_t           tap_thread;
static int                 tap_running;

static uv_poll_t           pipe_poll;
static int                 pipe_fds[2];   /* [0]=read, [1]=write */
static uv_loop_t          *g_loop;

static input_event_cb      g_cb;
static void               *g_userdata;
static int                 capturing;

static int screen_w, screen_h;

static CGEventRef tap_callback(CGEventTapProxy proxy, CGEventType type,
                               CGEventRef event, void *refcon)
{
    (void)proxy;
    (void)refcon;

    if (!capturing)
        return event;

    input_event_t ie = {0};
    CGPoint loc = CGEventGetLocation(event);

    switch (type) {
    case kCGEventKeyDown:
    case kCGEventKeyUp: {
        ie.type = (type == kCGEventKeyDown) ? INPUT_KEY_DOWN : INPUT_KEY_UP;
        CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        ie.keysym = macos_keycode_to_keysym(keycode);
        CGEventFlags flags = CGEventGetFlags(event);
        if (flags & kCGEventFlagMaskShift)   ie.mods |= (1 << 0);
        if (flags & kCGEventFlagMaskControl) ie.mods |= (1 << 2);
        if (flags & kCGEventFlagMaskAlternate) ie.mods |= (1 << 3);
        if (flags & kCGEventFlagMaskCommand) ie.mods |= (1 << 6);

        /* F12 (keysym 0xffc9) or Scroll_Lock (0xff14) releases the grab */
        if (ie.type == INPUT_KEY_DOWN &&
            (ie.keysym == 0xffc9 || ie.keysym == 0xff14)) {
            capturing = 0;
            write(pipe_fds[1], "\x01", 1);
            return event;
        }
        break;
    }
    case kCGEventMouseMoved:
    case kCGEventLeftMouseDragged:
    case kCGEventRightMouseDragged:
    case kCGEventOtherMouseDragged:
        ie.type = INPUT_MOTION;
        ie.x = loc.x / screen_w;
        ie.y = loc.y / screen_h;
        break;
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown:
    case kCGEventOtherMouseDown:
        ie.type = INPUT_BUTTON_DOWN;
        ie.button = (int)CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) + 1;
        ie.x = loc.x / screen_w;
        ie.y = loc.y / screen_h;
        break;
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp:
    case kCGEventOtherMouseUp:
        ie.type = INPUT_BUTTON_UP;
        ie.button = (int)CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber) + 1;
        ie.x = loc.x / screen_w;
        ie.y = loc.y / screen_h;
        break;
    case kCGEventScrollWheel:
        ie.type = INPUT_SCROLL;
        ie.scroll_y = (int)CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
        ie.scroll_x = (int)CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
        break;
    case kCGEventTapDisabledByTimeout:
    case kCGEventTapDisabledByUserInput:
        CGEventTapEnable(tap, true);
        return event;
    default:
        return event;
    }

    /* write event to pipe for libuv to pick up */
    write(pipe_fds[1], &ie, sizeof(ie));

    return NULL; /* swallow the event */
}

static void *tap_thread_fn(void *arg)
{
    (void)arg;
    tap_runloop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(tap_runloop, tap_source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    CFRunLoopRun();
    return NULL;
}

static void on_pipe_readable(uv_poll_t *handle, int status, int events)
{
    (void)handle;
    if (status < 0) return;
    if (!(events & UV_READABLE)) return;

    char buf[sizeof(input_event_t) * 64];
    ssize_t n = read(pipe_fds[0], buf, sizeof(buf));
    if (n <= 0) return;

    /* check for stop signal (single byte 0x01) */
    if (n == 1 && buf[0] == 0x01) {
        input_capture_stop();
        return;
    }

    size_t count = (size_t)n / sizeof(input_event_t);
    for (size_t i = 0; i < count; i++) {
        input_event_t *ie = (input_event_t *)(buf + i * sizeof(input_event_t));
        if (g_cb)
            g_cb(ie, g_userdata);
    }
}

int input_capture_init(uv_loop_t *loop)
{
    if (!AXIsProcessTrusted()) {
        LOG_ERR("macOS capture: Accessibility permission not granted");
        LOG_ERR("  → System Settings > Privacy & Security > Accessibility");
        return -1;
    }

    CGDirectDisplayID main_display = CGMainDisplayID();
    screen_w = (int)CGDisplayPixelsWide(main_display);
    screen_h = (int)CGDisplayPixelsHigh(main_display);

    CGEventMask mask =
        (1 << kCGEventKeyDown) | (1 << kCGEventKeyUp) |
        (1 << kCGEventMouseMoved) |
        (1 << kCGEventLeftMouseDown) | (1 << kCGEventLeftMouseUp) |
        (1 << kCGEventRightMouseDown) | (1 << kCGEventRightMouseUp) |
        (1 << kCGEventOtherMouseDown) | (1 << kCGEventOtherMouseUp) |
        (1 << kCGEventLeftMouseDragged) | (1 << kCGEventRightMouseDragged) |
        (1 << kCGEventOtherMouseDragged) |
        (1 << kCGEventScrollWheel);

    tap = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
                           kCGEventTapOptionDefault, mask,
                           tap_callback, NULL);
    if (!tap) {
        LOG_ERR("macOS capture: CGEventTapCreate failed");
        return -1;
    }

    tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CGEventTapEnable(tap, false);

    if (pipe(pipe_fds) != 0) {
        LOG_ERR("macOS capture: pipe() failed");
        return -1;
    }

    g_loop = loop;
    uv_poll_init(loop, &pipe_poll, pipe_fds[0]);

    /* start the tap thread */
    tap_running = 1;
    pthread_create(&tap_thread, NULL, tap_thread_fn, NULL);

    LOG_INFO("macOS capture: initialized (%dx%d)", screen_w, screen_h);
    return 0;
}

void input_capture_start(input_event_cb cb, void *userdata)
{
    if (capturing) return;
    g_cb = cb;
    g_userdata = userdata;
    capturing = 1;

    CGEventTapEnable(tap, true);
    uv_poll_start(&pipe_poll, UV_READABLE, on_pipe_readable);
    LOG_INFO("macOS capture: started");
}

void input_capture_stop(void)
{
    if (!capturing) return;
    capturing = 0;

    CGEventTapEnable(tap, false);
    uv_poll_stop(&pipe_poll);

    if (g_cb) {
        input_event_t stop = { .type = -1 };
        g_cb(&stop, g_userdata);
    }

    LOG_INFO("macOS capture: stopped");
}

void input_capture_shutdown(void)
{
    if (capturing)
        input_capture_stop();

    if (tap_runloop) {
        CFRunLoopStop(tap_runloop);
        pthread_join(tap_thread, NULL);
        tap_running = 0;
    }

    if (tap_source) CFRelease(tap_source);
    if (tap) CFRelease(tap);

    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

#endif /* __APPLE__ */
