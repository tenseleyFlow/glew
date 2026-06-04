#ifdef __APPLE__

#include "input.h"
#include "../log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

#include "keymap_macos.h"

static int screen_w, screen_h;
static int trusted;
static CGEventSourceRef event_source;

int input_inject_init(void)
{
    trusted = AXIsProcessTrusted();
    if (!trusted) {
        LOG_ERR("macOS inject: Accessibility permission not granted — injection will fail silently");
        LOG_ERR("  → System Settings > Privacy & Security > Accessibility > add glewd");
    }

    CGDirectDisplayID main_display = CGMainDisplayID();
    screen_w = (int)CGDisplayPixelsWide(main_display);
    screen_h = (int)CGDisplayPixelsHigh(main_display);

    event_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!event_source) {
        LOG_ERR("macOS inject: failed to create event source");
        return -1;
    }

    LOG_INFO("macOS inject: initialized (%dx%d, trusted=%d)", screen_w, screen_h, trusted);
    return trusted ? 0 : -1;
}

void input_inject_event(const input_event_t *ev)
{
    switch (ev->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP: {
        CGKeyCode kc = keysym_to_macos_keycode(ev->keysym);
        if (kc == 0xFFFF) {
            LOG_DBG("inject: unknown keysym 0x%x, skipping", ev->keysym);
            break;
        }
        CGEventRef e = CGEventCreateKeyboardEvent(event_source, kc, ev->type == INPUT_KEY_DOWN);
        /* set unicode character for printable ASCII keys */
        if (ev->keysym >= 0x20 && ev->keysym <= 0x7e) {
            UniChar ch = (UniChar)ev->keysym;
            CGEventKeyboardSetUnicodeString(e, 1, &ch);
        }
        /* inject at HID level so WM event taps (tarmac) see the events.
         * private event source prevents system shortcut interference. */
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
        break;
    }
    case INPUT_MOTION: {
        CGFloat x = ev->x * screen_w;
        CGFloat y = ev->y * screen_h;
        CGEventRef e = CGEventCreateMouseEvent(event_source, kCGEventMouseMoved,
                                               CGPointMake(x, y), kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
        break;
    }
    case INPUT_BUTTON_DOWN:
    case INPUT_BUTTON_UP: {
        CGFloat x = ev->x * screen_w;
        CGFloat y = ev->y * screen_h;
        CGEventType etype;
        CGMouseButton btn;

        if (ev->button == 1) {
            etype = (ev->type == INPUT_BUTTON_DOWN) ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
            btn = kCGMouseButtonLeft;
        } else if (ev->button == 3) {
            etype = (ev->type == INPUT_BUTTON_DOWN) ? kCGEventRightMouseDown : kCGEventRightMouseUp;
            btn = kCGMouseButtonRight;
        } else {
            etype = (ev->type == INPUT_BUTTON_DOWN) ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
            btn = (CGMouseButton)(ev->button - 1);
        }

        CGEventRef e = CGEventCreateMouseEvent(event_source, etype, CGPointMake(x, y), btn);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
        break;
    }
    case INPUT_SCROLL: {
        CGEventRef e = CGEventCreateScrollWheelEvent(event_source, kCGScrollEventUnitLine, 2,
                                                      ev->scroll_y, ev->scroll_x);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
        break;
    }
    }
}

void input_inject_shutdown(void)
{
    if (event_source) {
        CFRelease(event_source);
        event_source = NULL;
    }
}

#endif /* __APPLE__ */
