# Sprint 3 — Open Issues

## Verified root causes

### 1. macOS: CGEventPost doesn't trigger Carbon RegisterEventHotKey
**Affects**: Alt+Enter (spawn terminal), Alt+numbers (workspace), Alt+arrows (focus)
**Root cause**: Tarmac uses Carbon RegisterEventHotKey for all keybinds. CGEventPost synthetic events don't trigger Carbon hotkeys — macOS limitation.
**Evidence**: tarmac.log shows zero hotkey events during glew injection. Same keys on physical keyboard trigger hotkeys immediately.
**Fix options**:
  a. Map mod+key combos to tarmacctl IPC commands (focus, workspace, etc.)
  b. Add a tarmac IPC command for raw key injection that goes through the hotkey handler
  c. Use IOHIDPostEvent (lower-level, may bypass Carbon too)

### 2. X11: Alt+arrow word jump not working on hasu
**Affects**: Alt+Left/Right for word navigation in terminals (fish, zellij)
**Status**: Events ARE being captured on dorado and injected on hasu via XTestFakeKeyEvent. Need to verify Alt KEY_DOWN arrives BEFORE arrow KEY_DOWN in injection sequence. May be a zellij-specific issue (zellij uses Alt heavily for its own binds).
**Next step**: Test in a raw terminal (garterm without zellij) to isolate

### 3. Stuck modifier keys (partially fixed)
**Affects**: "f" triggering Alt+F, random modifier combos
**Root cause**: Modifier sync sends KEY_DOWN for held mods, but session end didn't release them.
**Fix status**: `release_all_modifiers()` added to all session-end paths. NEEDS TESTING.

### 4. Mouse edge bounce loop
**Affects**: dorado ↔ hasu, dorado ↔ nomad mouse transitions
**Root cause**: Cursor restore near edge + insufficient cooldown + edge watcher arms in one poll cycle
**Fix status**: 200ms sender cooldown + arm/disarm guard. Reduced but not eliminated. 
**Remaining**: After cooldown expires, user's active mouse movement can reach edge in <16ms.

### 5. Non-server machines initiate crossings
**Affects**: hasu edge watcher fires when user physically switches KVM
**Root cause**: Every machine runs an edge watcher. Architecture doesn't distinguish "server" from "client".
**Fix**: Only the machine where the physical keyboard is should initiate crossings. Could disable edge watcher on machines that are currently in REMOTE_RECEIVING mode, or only enable edge watcher after first receiving a keyboard crossing.

## Priority order
1 → 3 → 2 → 4 → 5
