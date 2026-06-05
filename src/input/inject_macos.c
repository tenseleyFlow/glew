#ifdef __APPLE__

#include "input.h"
#include "../log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

#include "keymap_macos.h"

static int screen_w, screen_h;
static int trusted;
static CGEventSourceRef event_source;

static int is_hold_modifier(uint32_t ks)
{
    return ks == 0xffe1 || ks == 0xffe2 ||  /* Shift_L, Shift_R */
           ks == 0xffe3 || ks == 0xffe4 ||  /* Control_L, Control_R */
           ks == 0xffe7 || ks == 0xffe8 ||  /* Meta_L, Meta_R */
           ks == 0xffe9 || ks == 0xffea ||  /* Alt_L, Alt_R */
           ks == 0xffeb || ks == 0xffec ||  /* Super_L, Super_R */
           ks == 0xffed || ks == 0xffee;    /* Hyper_L, Hyper_R */
}

static CGEventFlags mods_to_cgflags(uint32_t mods)
{
    CGEventFlags f = 0;
    if (mods & (1 << 0)) f |= kCGEventFlagMaskShift;
    if (mods & (1 << 1)) f |= kCGEventFlagMaskAlphaShift;
    if (mods & (1 << 2)) f |= kCGEventFlagMaskControl;
    if (mods & (1 << 3)) f |= kCGEventFlagMaskAlternate;
    if (mods & (1 << 6)) f |= kCGEventFlagMaskCommand;
    return f;
}

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
        if (is_hold_modifier(ev->keysym))
            break;

        CGKeyCode kc = keysym_to_macos_keycode(ev->keysym);
        if (kc == 0xFFFF) {
            LOG_DBG("inject: unknown keysym 0x%x, skipping", ev->keysym);
            break;
        }
        CGEventRef e = CGEventCreateKeyboardEvent(event_source, kc, ev->type == INPUT_KEY_DOWN);
        CGEventSetFlags(e, mods_to_cgflags(ev->mods));

        if (ev->keysym >= 0x20 && ev->keysym <= 0x7e) {
            UniChar ch = (UniChar)ev->keysym;
            int shifted = ((ev->mods & (1 << 0)) != 0) ^ ((ev->mods & (1 << 1)) != 0);
            if (ch >= 'a' && ch <= 'z' && shifted)
                ch -= 32;
            CGEventKeyboardSetUnicodeString(e, 1, &ch);
        }

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
