# Sprint 2: Networking + Spatial Layout + Cross-Machine Focus

## Targets
- `glewd` daemon running on libuv event loop with TCP server
- Machines connect to each other on startup using layout config
- Spatial layout engine resolves "I overflowed left on dorado" → "enter hasu from the right"
- Focus handoff works end-to-end: mod+arrow overflows → remote WM focuses edge window

## Scope
- Focus crossing only — no input forwarding yet
- After crossing, you still need to physically switch keyboard/mouse to the other machine
- This sprint proves the WM integration and networking are solid before adding input capture

## Protocol (length-prefixed JSON over TCP)

Wire format: `[u32 length (network byte order)][JSON payload]`

Messages:

```json
// Handshake (client → server on connect)
{"type":"hello","name":"dorado","secret":"..."}

// Handshake response
{"type":"hello_ok"}
{"type":"hello_err","reason":"bad secret"}

// Focus entering a machine
{"type":"focus_enter","from_direction":"right","source":"dorado"}

// Acknowledgment
{"type":"focus_ack","success":true}

// Heartbeat (bidirectional, every 5s)
{"type":"ping"}
{"type":"pong"}
```

## Spatial Layout Engine

The layout config defines machines left-to-right. Within each machine, monitors are listed left-to-right. The engine needs to resolve:

Given: "dorado overflowed LEFT on monitor DP-1"
Result: "hasu, rightmost monitor (HDMI-1), rightmost window"

Given: "dorado overflowed RIGHT on monitor HDMI-1"
Result: "nomad, leftmost monitor (eDP-1), leftmost window"

For v1, the layout is a 1D horizontal strip (left-to-right). Vertical adjacency (above/below between machines) is out of scope — up/down overflow stays within the local machine's multi-monitor setup.

## glewd Architecture

```
┌─────────────────────────────────────────────────┐
│ glewd                                            │
│                                                  │
│  libuv event loop                                │
│  ├── TCP server (port 9437)                      │
│  │   └── peer connections (one per remote machine)│
│  ├── Unix socket listener                        │
│  │   └── local glew CLI connects here            │
│  └── signal handler (SIGTERM, SIGHUP)            │
│                                                  │
│  Components:                                     │
│  ├── config (loaded from glew.toml)              │
│  ├── layout (spatial graph)                      │
│  ├── peer_manager (track connected machines)      │
│  └── wm_driver (initialized from config)         │
└─────────────────────────────────────────────────┘
```

Flow:
1. `glew focus left` connects to local glewd Unix socket
2. Sends `{"type":"focus","direction":"left"}`
3. glewd asks WM driver: `can_focus(LEFT)`?
4. If yes: `do_focus(LEFT)`, respond success
5. If no: layout engine resolves target machine, glewd sends `focus_enter` to remote glewd
6. Remote glewd calls `driver->focus_edge(RIGHT)` on its WM
7. Responds with `focus_ack`

## Pitfalls
- TCP connections over Tailscale need reconnection logic — Tailscale IPs are stable but connections can drop
- Peer discovery is manual (config-driven) — no mDNS for now
- Need to handle the case where a remote machine is offline (timeout, skip, focus stays local)
- glewd must not block on WM IPC — use libuv's thread pool or async Unix socket I/O
- Secret comparison must be constant-time to avoid timing attacks

## DoD
- [ ] `glewd` starts, loads config, listens on TCP port and Unix socket
- [ ] `glewd` connects to remote peers on startup, completes handshake
- [ ] `glew focus left` on dorado at the left edge sends focus_enter to hasu's glewd
- [ ] hasu's glewd receives focus_enter and focuses the rightmost window via gar/i3
- [ ] `glew focus right` on dorado at the right edge sends focus_enter to nomad's glewd
- [ ] nomad's glewd focuses the leftmost tarmac window
- [ ] Heartbeat keeps connections alive
- [ ] Graceful handling when a peer is offline (timeout, log, no crash)
- [ ] Test over Tailscale between dorado ↔ nomad (WAN)
