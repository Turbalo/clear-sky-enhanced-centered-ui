# Clear Sky Enhanced Edition - Centered UI Fix

Runtime ultrawide UI fix for **S.T.A.L.K.E.R.: Clear Sky - Enhanced
Edition**.

The Enhanced Edition renders its 3D scene correctly on ultrawide displays,
but stretches menus, HUD elements, dialogs, text, cursor coordinates, and
clickable zones across the full screen. This mod places those UI paths inside
a proportional centered 16:10 safe-zone without changing the selected
resolution or the 3D aspect ratio.

## Features

- Runtime safe-zone calculation with no resolution presets
- 16:9, 21:9, 24:10, and 32:9 support
- Native 3D scene, weapon view, postprocess effects, crosshair, and sniper
  scope proportions
- Corrected menus, HUD, dialogs, text, clipping, mouse input, and clickable
  zones
- Corrected minimap centering, rotation, and aspect ratio
- Corrected gameplay HUD atlas
- Automatic Steam detection with manual `xrEngine.exe` selection
- Transactional backup, uninstall, and one-level `dinput8.dll` chain-loading
- Exact runtime signature validation and shareable diagnostics

Displays at 16:10 or narrower are left at their native UI width. At
5120x1440, the centered UI region is 2304x1440.

## Download

Download the latest binary package from
[GitHub Releases](https://github.com/Turbalo/clear-sky-enhanced-centered-ui/releases/latest).

## Installation

1. Exit the game.
2. Extract the release archive.
3. Run `install.cmd`.
4. Select the game's `xrEngine.exe`. The detected Steam installation is
   preselected when available.
5. Start the game normally.

No Steam launch arguments are required. Do not add `-nointro` for this mod;
Steam may display an additional launch confirmation for custom arguments.

Run `uninstall.cmd` to remove the mod. Files replaced during installation are
restored when they have not been modified afterward.

## Game Compatibility

The verified profile is Steam Enhanced Edition `1.10.3+68-42`, AppID
`2427420`.

Verified `xrEngine.exe` SHA-256:

```text
89BA7FC6B84BB18A3D0B47936B2E67BD1B7CC8B642A4F322B068C9774A8741E1
```

Version `1.0.1` payload `dinput8.dll` SHA-256:

```text
CC1F4C2ACF605A4441C2994F07E037924A575F199D7FDBE192F8CD14D6F21C8B
```

Executables with another SHA-256 can be accepted in
`Signature-compatible` mode. The installer requires a 64-bit Enhanced
Edition executable and eleven unique runtime signatures at their validated
offsets. Classic 32-bit Clear Sky and modified engine builds are rejected.

When validation fails, the installer creates
`%TEMP%\ClearSky-Centered-UI-diagnostics.txt` with the executable hash,
version, architecture, and per-signature results.

See [COMPATIBILITY.md](COMPATIBILITY.md) for display, executable, and proxy
compatibility details.

## Antivirus Notes

The mod uses a local `dinput8.dll` proxy and the open-source MinHook library to
intercept UI rendering inside `xrEngine.exe`. This behavior can trigger
generic heuristic detections.

The mod does not connect to the internet, download code, collect telemetry,
install services or scheduled tasks, modify `xrEngine.exe`, or hook other
processes. The complete runtime and installer source is available in this
repository.

## Known Limitations

- Stock level-loading artwork can remain horizontally stretched on ultrawide;
  loading text and UI use the centered canvas.
- UI replacement mods can conflict with the two menu XML files or
  `textures\ui\ui_hud.dds`.
- Multiplayer and controller-only workflows have less live coverage than the
  5120x1440 single-player path.
- Engine updates that change runtime signatures require a new compatibility
  profile.

## Development

Build instructions, architecture notes, and test commands are in
[DEVELOPMENT.md](DEVELOPMENT.md).

## License

Original source code and documentation are available under the
[MIT License](LICENSE). MinHook and game-derived UI assets retain their own
terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
