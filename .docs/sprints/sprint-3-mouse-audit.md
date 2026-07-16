# Sprint 3: Mouse Crossing Audit — Barrier-Style Improvements

## Findings from Barrier/Input-Leap/Lan-Mouse

Our edge detection and cursor management has gaps compared to established KVM software.
These items bring us to parity with Barrier's proven approach.

## Todo List

### 1. Two-tap edge detection with direction consistency
**Current**: single touch at edge triggers crossing immediately.
**Target**: require two rapid touches at the same edge, with mouse momentum pointing INTO the edge on both touches.
- Track delta direction (dx sign) across consecutive edge polls
- Require same-sign delta on two touches within a configurable window (default 250ms)
- Configurable via `switch_delay` in glew.toml

### 2. Per-edge jump cursor positions
**Current**: save/restore a single cursor position, offset 50px from edge.
**Target**: save the exact crossing position per-edge (left/right). On return, restore to that exact position.
- Store `jump_x[DIR_LEFT]`, `jump_x[DIR_RIGHT]`, `jump_y[DIR_LEFT]`, `jump_y[DIR_RIGHT]`
- On cross: save current position for that edge
- On return: restore to saved position for the return edge

### 3. Transition sequence numbers
**Current**: no protection against stale events from before transition.
**Target**: increment a sequence counter on each transition. Tag events with sequence. Discard events with old sequence numbers.
- Increment `g_enter_seq` on each start_sending_input / focus_enter
- Include seq in input_start message
- Receiver discards events with stale seq

### 4. Switch delay (configurable wait before crossing)
**Current**: crossing happens immediately on edge detection.
**Target**: short delay (configurable, default 100ms) between edge detection and actual crossing. If cursor moves away from edge during delay, cancel the crossing.
- Start a one-shot timer on first tap
- On timer fire: check if cursor still at edge and direction consistent
- If yes: cross. If no: cancel.

### 5. Virtual cursor Y-position preservation
**Current**: virtual cursor starts at y=0.5 on entry.
**Target**: preserve the Y position from the source machine on crossing.
- Normalize Y on source at crossing moment
- Send normalized Y in focus_enter message
- Receiver sets g_virt_y from this value

## Implementation Order
1 → 4 → 2 → 5 → 3

Items 1+4 together solve the bounce/accidental crossing problem.
Item 2 makes cursor restore feel natural.
Item 5 makes vertical position consistent across screens.
Item 3 is defense-in-depth.
