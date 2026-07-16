# Sprint 3: Input Capture + Forwarding (Full KVM)

## Targets
- When focus crosses to a remote machine, keyboard and mouse input is captured locally and forwarded
- Remote machine injects the forwarded input as if it were local
- Mouse cursor position is mapped between different screen geometries
- Escape key returns control to the local machine

## Scope
- Two platform backends: X11 (dorado + hasu) and macOS (nomad)
- Keyboard and mouse (pointer motion, clicks, scroll)
- No clipboard yet (sprint 4)
- No drag-and-drop

## Input Capture

### X11 (FreeBSD + Linux)
- `XGrabKeyboard()` + `XGrabPointer()` to capture all input
- Read events from the X connection in the libuv event loop (fd polling)
- Events: `KeyPress`, `KeyRelease`, `MotionNotify`, `ButtonPress`, `ButtonRelease`
- Convert X keycodes to a platform-neutral format (XKB keysyms)
- On escape key: `XUngrabKeyboard()` + `XUngrabPointer()`, return to local mode

### macOS (CoreGraphics)
- `CGEventTapCreate()` with `kCGEventTapOptionDefault` to intercept events
- Tap location: `kCGHIDEventTap` (captures all input including system shortcuts)
- Event mask: key up/down, mouse move, mouse button, scroll wheel
- Return `NULL` from the tap callback to swallow events (prevent local delivery)
- Requires Accessibility permission — detect and prompt if missing
- On escape key: disable the event tap, return to local mode

## Input Injection

### X11
- `XTestFakeKeyEvent()` for keyboard
- `XTestFakeMotionEvent()` for mouse movement
- `XTestFakeButtonEvent()` for clicks
- Requires XTest extension (`XTestQueryExtension()` to verify)

### macOS
- `CGEventPost(kCGHIDEventTap, event)` for all event types
- `CGEventCreateKeyboardEvent()` for key events
- `CGEventCreateMouseEvent()` for mouse events

## Wire Protocol Extension

New message type for input forwarding:

```json
// Input event (source → target, high frequency)
{
    "type": "input",
    "event": "key_down",      // key_down | key_up | motion | button_down | button_up | scroll
    "keysym": 65361,          // XKB keysym (for key events)
    "x": 0.75,               // normalized [0.0, 1.0] relative to target screen
    "y": 0.50,
    "button": 1,              // mouse button (for button events)
    "scroll_x": 0,
    "scroll_y": -3,
    "mods": 64                // modifier mask
}

// Input mode change
{"type": "input_start", "source": "dorado"}
{"type": "input_stop"}
```

## Coordinate Mapping

Mouse positions are normalized to [0.0, 1.0] on the source machine before sending:
- Source: pixel (x, y) on monitor with geometry (w, h) → (x/w, y/h)
- Target: (nx, ny) → pixel (nx * target_w, ny * target_h)

This handles different resolutions naturally. The edge entry point matters:
- Entering from the left: cursor appears at x=0.0, y preserved
- Entering from the right: cursor appears at x=1.0, y preserved

Multi-monitor on the target: position maps to the entry-edge monitor specifically, not the full virtual screen.

## State Machine

```
LOCAL mode (default):
  - All input goes to local WM normally
  - glew focus overflow detected → send focus_enter + input_start → transition to REMOTE

REMOTE mode:
  - All input captured and forwarded to remote glewd
  - Remote glewd injects events locally
  - Escape key detected → send input_stop → transition to LOCAL
  - glew focus on remote overflows back → send focus_enter + input_start back → transition to LOCAL
```

## Pitfalls
- **Latency**: JSON serialization per input event may be too slow at high mouse polling rates (1000Hz). May need to batch events or switch to binary encoding for input messages.
- **Key repeat**: X11 and macOS handle key repeat differently. Forwarding raw up/down should be enough — let the target OS generate repeats.
- **Modifier state sync**: If you cross machines while holding Shift, the target doesn't know Shift is down. Need to send full modifier state on input_start.
- **Mouse acceleration**: Source captures raw motion, target may apply its own acceleration curve → double acceleration. Consider sending absolute positions, not deltas.
- **CGEventTap dying**: macOS kills event taps that take too long in the callback. Must return immediately — buffer events and process async.
- **XGrab and WM conflicts**: The grab may interfere with the WM's own grabs. Need to test carefully with i3's grab behavior.
- **Privileged input on macOS**: Some system shortcuts (Cmd+Space, Ctrl+arrows for spaces) may not be capturable even with kCGHIDEventTap.

## DoD
- [ ] X11 input capture grabs keyboard and mouse on dorado
- [ ] macOS input capture works on nomad (with Accessibility permission granted)
- [ ] Key events forwarded from dorado to nomad and injected correctly
- [ ] Mouse movement forwarded and cursor moves on target machine
- [ ] Mouse clicks forwarded and register on target
- [ ] Coordinate mapping handles dorado (3 monitors) → nomad (1 laptop display)
- [ ] Escape key returns control to local machine
- [ ] Modifier state synced on input_start (no stuck modifiers)
- [ ] Latency acceptable on LAN (< 10ms perceived)
- [ ] Latency measured over Tailscale WAN (document results)
- [ ] Full round trip: focus from dorado → nomad → type in a terminal → Escape → back on dorado
