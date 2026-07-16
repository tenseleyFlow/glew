# Sprint 5: Packaging, Services, Documentation

## Targets
- glewd runs as a system service (launchd on macOS, rc.d on FreeBSD, systemd on NixOS)
- Man pages for glewd(1) and glew(1)
- Example WM configs for i3, tarmac, and gar
- Nix flake for hasu
- FreeBSD port Makefile (optional, local only)
- Performance profiling and latency optimization

## Scope
- No new features — hardening and distribution
- Focus on making the setup experience smooth for all three machines

## Service Files

### macOS (launchd)
`~/Library/LaunchAgents/com.glew.glewd.plist` — user-level daemon, starts on login.

### FreeBSD (rc.d)
`/usr/local/etc/rc.d/glewd` — system service, or user-level via cron @reboot.

### NixOS (systemd user unit)
`~/.config/systemd/user/glewd.service` — user service with `systemctl --user enable glewd`.

Nix flake provides the package + a NixOS module for declarative config.

## Example WM Configs

### i3 (dorado / hasu)
```
# Replace default focus binds with glew
bindsym $mod+Left  exec --no-startup-id glew focus left
bindsym $mod+Right exec --no-startup-id glew focus right
bindsym $mod+Up    exec --no-startup-id glew focus up
bindsym $mod+Down  exec --no-startup-id glew focus down
```

### tarmac (nomad)
```lua
gar.bind("mod+h", "!glew focus left")
gar.bind("mod+l", "!glew focus right")
gar.bind("mod+k", "!glew focus up")
gar.bind("mod+j", "!glew focus down")
```

(Need to verify tarmac's exec-command syntax — the `!` prefix is speculative.)

### gar (hasu)
Same as i3 if using i3-compat config, or gar-native equivalent.

## Performance

Profile input forwarding latency:
- Measure round-trip time: key press on source → event arrives at target
- Target: < 5ms on LAN, < 50ms on Tailscale WAN
- If JSON serialization is the bottleneck, consider binary encoding for input events only (msgpack or raw struct)
- Mouse event batching: if multiple motion events queue up, only send the latest position

## Pitfalls
- launchd plist requires absolute paths to binary
- NixOS systemd unit needs the glew binary in the Nix store — flake must handle this
- FreeBSD rc.d scripts have a specific format — follow `/usr/local/etc/rc.d/` conventions
- Man pages: use mandoc format for portability across FreeBSD and Linux
- tarmac's exec syntax needs testing — may need `os.execute()` in Lua callback instead of inline

## DoD
- [ ] `glewd` starts automatically on login on all three machines
- [ ] `man glew` and `man glewd` display correctly
- [ ] Example i3 config works on dorado — mod+arrows cross machines
- [ ] Example tarmac config works on nomad
- [ ] `nix build` produces working glew + glewd binaries for hasu
- [ ] Input forwarding latency profiled and documented
- [ ] README.md in repo with setup instructions
