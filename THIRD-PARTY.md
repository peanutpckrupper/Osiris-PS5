# Third-party components & attribution

Osiris is only the glue. It stands on these projects — please credit and respect each one.

## Upstream project (required)
- **halo5onpc** — Daniel McCluskey — https://github.com/danielmccluskey/halo5onpc
  The Halo 5 campaign-on-PC work Osiris extends. **Not redistributed here.** Get it from his repo.

## Bundled in this source tree
- **MinHook** (`forge-shim/third_party/minhook`) — MIT — https://github.com/TsudaKageyu/minhook
- **WinDurango** — LGPL-3.0 — UWP/Xbox API groundwork the shim draws on (`forge-shim/LICENSE-WinDurango`)

## Used on the PS5 (shipped only in the PS5 release package, with their own SOURCES.txt + LICENSES)
- **prospero-win** — mpereiraesaa & contributors — https://github.com/mpereiraesaa/prospero-win
- **Wine**, **DXVK**, **vkd3d-proton**, **Mesa / RADV**

## Licensing note
Parts of the shim derive from WinDurango and the LGPL-3.0 unpackaged-Forge base, so the shim is effectively **LGPL-3.0**. Pick a license for the repo that is compatible (LGPL-3.0 is the safe default) and keep the notices above.
