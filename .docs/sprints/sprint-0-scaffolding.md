# Sprint 0: Scaffolding

## Targets
- Git repo initialized with remote on tenseleyFlow
- CMake build system compiling on FreeBSD and macOS (cross-compile not required yet — native builds per machine)
- Directory structure reflecting final architecture
- Embedded deps (cJSON, tomlc99) building correctly
- Config schema defined and parser working
- Skeleton `glewd` and `glew` binaries that parse args and load config

## Scope
- No WM interaction, no networking, no input capture
- Just the project skeleton, build system, and config layer
- Config file: `~/.config/glew/glew.toml`

## Config Schema (draft)

```toml
[self]
name = "dorado"
wm = "i3"                        # "i3" | "tarmac" | "gar"

[layout]
# Machines listed left-to-right. Each machine's monitors listed left-to-right.
# This defines the spatial graph for focus overflow.

[[layout.machines]]
name = "hasu"
address = "100.x.x.x"            # Tailscale IP or hostname
monitors = ["DP-1", "DP-2", "HDMI-1"]  # left to right

[[layout.machines]]
name = "dorado"
address = "localhost"             # self
monitors = ["DP-1", "DP-2", "HDMI-1"]

[[layout.machines]]
name = "nomad"
address = "100.x.x.x"
monitors = ["eDP-1"]

[network]
port = 9437
secret = "changeme"              # shared auth secret

[input]
escape_key = "Scroll_Lock"       # key to return focus to local machine
```

## Directory Structure

```
glew/
├── CMakeLists.txt
├── src/
│   ├── glewd.c                  # daemon entry point
│   ├── glew.c                   # CLI entry point
│   ├── config.c / config.h      # TOML config parsing
│   ├── layout.c / layout.h      # spatial layout engine
│   ├── log.c / log.h            # logging
│   ├── net/
│   │   ├── server.c / server.h
│   │   ├── client.c / client.h
│   │   └── proto.c / proto.h    # wire protocol
│   ├── input/
│   │   ├── capture.h            # platform-agnostic interface
│   │   ├── capture_x11.c
│   │   ├── capture_macos.c
│   │   ├── inject.h
│   │   ├── inject_x11.c
│   │   └── inject_macos.c
│   └── drivers/
│       ├── driver.h             # WM driver vtable
│       ├── i3.c
│       ├── tarmac.c
│       └── gar.c
├── deps/
│   ├── cJSON/
│   └── tomlc99/
└── .gitignore
```

## Pitfalls
- CMake on FreeBSD may need explicit paths for X11 headers/libs (not in default locations on some setups)
- tomlc99 uses `strptime` which needs `_XOPEN_SOURCE` on some platforms
- Don't overthink the config schema now — it will evolve

## DoD
- [ ] `git init` + remote on github.com/tenseleyFlow/glew
- [ ] `.gitignore` covers build/, .docs/, deps downloads
- [ ] `cmake --build build/` succeeds on dorado (FreeBSD)
- [ ] `glewd --help` prints usage
- [ ] `glew --help` prints usage
- [ ] `glew --version` prints version
- [ ] Config file parses without error and values are accessible in code
- [ ] CI not required yet — local builds only
