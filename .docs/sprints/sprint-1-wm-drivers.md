# Sprint 1: WM Driver Interface + Local Focus Proxy

## Targets
- WM driver vtable (C function pointer struct) defined and working
- i3 driver: connect to i3 IPC socket, query tree, detect edge overflow, send focus commands
- tarmac driver: connect to tarmac Unix socket, query state, detect edge overflow, send focus commands
- `glew focus {left|right|up|down}` works as a transparent proxy for WM focus

## Scope
- Local-only — no networking between machines
- Two drivers: i3 (covers dorado + hasu/gar via i3 compat) and tarmac (covers nomad)
- Edge overflow detection: when `glew focus left` can't go left, it prints "overflow:left" to stdout and exits 1

## Driver Interface

```c
typedef struct {
    const char *name;
    int  (*init)(const char *socket_path);
    void (*shutdown)(void);
    int  (*can_focus)(direction_t dir);   // 1 = yes, 0 = edge
    int  (*do_focus)(direction_t dir);    // execute the focus move
    int  (*get_tree)(wm_tree_t *out);     // populate tree snapshot
    int  (*focus_edge)(direction_t dir);  // focus the edge-most window from dir
} wm_driver_t;
```

Key operations:
- `can_focus(LEFT)`: walk the BSP tree from the focused node — is there a sibling/container to the left?
- `do_focus(LEFT)`: tell the WM to execute `focus left`
- `focus_edge(RIGHT)`: focus the rightmost window on the rightmost monitor (called when receiving focus from a machine to the right)

## i3 Driver Details
- Socket: `$I3SOCK` or `i3 --get-socketpath`
- Binary wire protocol: `i3-ipc` magic + u32 length + u32 type + JSON payload
- `GET_TREE` (type 4): returns full container tree as JSON
- Tree walking: from focused container, check if parent split matches direction and if sibling exists
- `RUN_COMMAND` (type 0): `focus left` etc.

## tarmac Driver Details
- Socket: `/tmp/tarmac-$USER.sock`
- JSON protocol: `{"command":"focus","args":["left"]}` → `{"success":true/false}`
- `get-workspaces` for state queries
- `subscribe` for event streaming (used later in sprint 2 for glewd)
- Edge detection: send `focus left`, check if focus actually changed (compare window_id before/after)
  - Alternative: query workspace tree, walk BSP — need to check if tarmac exposes tree structure

## Pitfalls
- i3's `focus left` returns `{"success":true}` even when nothing happens — must query tree to detect edges
- tarmac may not expose its full BSP tree via IPC — may need to infer edges from workspace/window queries
- gar's i3 compat layer may have subtle differences from real i3 — test on hasu early
- Socket paths differ per platform — handle `$I3SOCK`, `$SWAYSOCK`, `$TARMAC_SOCKET` env vars

## DoD
- [ ] `wm_driver_t` interface defined in `driver.h`
- [ ] i3 driver connects to i3 socket and successfully queries tree
- [ ] i3 driver correctly identifies edge containers (no neighbor in direction)
- [ ] `glew focus left` on dorado (i3) focuses the window to the left
- [ ] `glew focus left` on dorado at the leftmost window prints overflow and exits 1
- [ ] tarmac driver connects to tarmac socket and sends focus commands
- [ ] `glew focus right` on nomad (tarmac) at rightmost window prints overflow and exits 1
- [ ] SSH test: run `glew focus left` on hasu via `ssh mfwolffe@hasu` to verify gar i3-compat works
