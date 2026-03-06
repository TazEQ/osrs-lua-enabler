# osrs-lua-enabler

Unlocks the closed Lua plugin beta in the live OSRS client by patching three
signatures in `osclient.exe` at load time: the beta gate, the VM-init gate, and
the pcall timeout enforcer.

## Build & run

Requires MSVC (`cl.exe` reachable via `tools/vcvars.sh`) and GNU make.

```sh
make            # build build/lua_enabler.dll and build/inject.exe
make run        # kill osclient, incremental rebuild, launch, inject
make clean      # remove build/
```

`inject.exe` prints its own progress; the DLL prints one line to the same
terminal when patching finishes, e.g. `[+] lua_enabler: applied 3/3 patches`.

## Useful links

- Storm Flag's Lua plugin development documentation (Storm Flag is the
  consulting firm building the Lua plugin system for Jagex; this is what the
  beta unlocks access to):
  https://d16t5jyl0qyupa.cloudfront.net/
- Archived posts (newest first) — background on the Lua plugin beta and related
  announcements:
  https://archive.lostcity.rs/oldschool.runescape.com/mods/?C=M&O=D
