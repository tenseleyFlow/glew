# Sprint 4: Clipboard, Visuals, Robustness

## Targets
- Text clipboard sharing between machines
- Visual indicator of which machine has input focus
- Reconnection logic for dropped peers
- Auth hardening
- gar driver validation on hasu (verify i3-compat layer covers our needs)

## Scope
- Clipboard: text/plain only — no images, HTML, or file URIs
- Visual indicators via existing WM mechanisms (border color, notification)
- No new platform backends

## Clipboard Sharing

On focus transition, sync clipboard from source → target:

```json
{"type": "clipboard_update", "format": "text/plain", "data": "copied text here"}
```

Platform clipboard access:
- **X11**: `XSetSelectionOwner` / `XGetSelectionOwner` for CLIPBOARD and PRIMARY selections. Use `XConvertSelection` + `SelectionNotify` event to read content.
- **macOS**: `NSPasteboard` general pasteboard. Read with `[pasteboard stringForType:NSPasteboardTypeString]`. Need a small Objective-C bridge file.

Trigger: on `input_start` (entering a machine), read clipboard from source and send to target. On `input_stop` (leaving), read target clipboard and send back to source. This way the clipboard follows the user naturally.

## Visual Indicators

When a machine has remote input focus (is being controlled remotely):
- Change WM border color on focused window to a distinct color (orange/green)
- i3: `for_window [focused] border pixel 4` or send `border` command dynamically
- tarmac: Lua callback or IPC command to ers (border renderer)
- Log to stdout/stderr for debugging

Optional: desktop notification on focus enter/leave (low priority, might be annoying).

## Reconnection

- glewd tracks peer connection state: `CONNECTED`, `DISCONNECTED`, `CONNECTING`
- On disconnect: attempt reconnect with exponential backoff (1s, 2s, 4s, 8s, cap at 30s)
- On reconnect: re-handshake, sync state
- During disconnection: focus overflow to that peer logs a warning, focus stays local
- libuv handles TCP keepalive (`uv_tcp_keepalive`) — enable with 10s interval

## Auth Hardening

Current: shared secret sent in plaintext during handshake (encrypted by Tailscale, but still).
Improved: HMAC-based challenge-response:
1. Server sends random nonce on connect
2. Client responds with HMAC-SHA256(secret, nonce)
3. Server verifies

This prevents replay attacks and avoids sending the secret over the wire, even within the Tailscale tunnel. Use OpenSSL's HMAC API (available on all three platforms).

## Pitfalls
- X11 clipboard is asynchronous and event-driven — can't just "read" it synchronously. Need to handle SelectionRequest/SelectionNotify in the event loop.
- macOS pasteboard access requires running on the main thread (or at least a thread with a run loop). May need to dispatch to main.
- Large clipboard contents could stall the protocol — cap at 1MB, reject larger.
- Reconnection during active input forwarding needs careful state cleanup.
- Border color changes via i3 IPC are per-container, not global — need to track and restore original colors.

## DoD
- [ ] Copy text on dorado, focus to nomad, paste → text appears
- [ ] Copy text on nomad, focus back to dorado, paste → text appears
- [ ] Visual border change on the remotely-controlled machine
- [ ] Kill glewd on nomad, dorado's glewd reconnects when nomad comes back
- [ ] HMAC challenge-response handshake replaces plaintext secret
- [ ] `glew focus left` on dorado at edge with hasu offline → graceful warning, focus stays
- [ ] Verify gar i3-compat on hasu via SSH: tree queries, focus commands, edge detection all work
- [ ] Clipboard size > 1MB gracefully rejected with log message
