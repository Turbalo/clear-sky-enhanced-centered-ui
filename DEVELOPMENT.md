# Development

## Requirements

- Visual Studio 2022 with the x64 C++ and MASM toolchains
- CMake 3.24 or newer
- Python 3 with the packages in `tools/requirements.txt`

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config RelWithDebInfo
```

The resulting proxy is `build\RelWithDebInfo\dinput8.dll`. Copy it to
`installer\payload\dinput8.dll` before packaging.

## Tests

```powershell
ctest --test-dir build -C RelWithDebInfo --output-on-failure
python -m unittest tests.test_menu_backplates
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\test_installer.ps1 -CleanStaleWork
python tools\validate_ui_hud.py `
  installer\payload\gamedata\textures\ui\ui_hud.dds
```

The automated suite covers safe-zone math, DirectInput forwarding, menu XML,
installer backup and rollback, proxy chain-loading, unknown-SHA acceptance,
and rejection diagnostics for incompatible signatures.

## Release Package

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\package_release.ps1 -Version 1.0.0
```

The packaging script checks that the payload DLL matches the selected build,
creates `SHA256SUMS.txt`, and writes the archive under `dist\`.

## Runtime Design

- `dinput8.dll` forwards the DirectInput 8 exports to the system DLL or one
  existing `dinput8_chain.dll`.
- Runtime initialization begins on the first real `DirectInput8Create` call.
- The installer validates the on-disk PE image before copying files.
- The DLL independently validates all signatures before enabling any hook.
- Any validation or MinHook failure rolls back the complete hook set.
- UI vertices, fonts, clipping rectangles, and cursor coordinates share the
  centered safe-zone.
- 3D rendering, postprocess effects, crosshair primitives, and active weapon
  scope UI are excluded from the general transform.

## Game-Derived Assets

The payload contains two menu XML overrides and a modified 4096x4096 DXT5
`ui_hud.dds`. These assets derive from Enhanced Edition game data and are not
covered by the MIT license. The DDS must retain its dimensions and alpha
channel.
