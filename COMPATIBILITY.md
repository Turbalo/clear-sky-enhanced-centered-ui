# Compatibility

## Display Formats

The safe-zone is calculated from the actual backbuffer dimensions. No
resolution-specific profile is selected.

| Format | Representative resolution | Expected UI area |
| --- | --- | --- |
| 4:3 | 1600x1200 | 1600x1200 |
| 16:9 | 1920x1080 | 1728x1080 |
| 21:9 | 2560x1080, 3440x1440 | 1728x1080, 2304x1440 |
| 24:10 | 3840x1600 | 2560x1600 |
| 32:9 | 5120x1440 | 2304x1440 |

Fullscreen, borderless, and windowed modes use the same backbuffer-based
calculation. Each mode still needs visual validation in the supported build.
Displays wider than 16:10 are centered; 16:10 and narrower displays keep their
native UI area.

The primary live gameplay pass was completed at 5120x1440 (32:9). Safe-zone
math and startup were additionally exercised at representative 4:3, 16:9,
21:9, and 24:10 resolutions.

## Proxy DLLs

The installer supports one existing `dinput8.dll` by moving it to
`dinput8_chain.dll`. The runtime validates that all required DirectInput
exports exist before using it.

Installation stops when both names already exist because the intended chain is
ambiguous. Other proxy names and proxy stacks are not modified.

## Executable Profiles

The Steam Enhanced Edition `1.10.3+68-42` executable is the verified profile.
Executables with another SHA-256 are accepted only when they are x64 PE32+
images and all ten runtime signatures occur exactly once at their validated
RVAs. The installer records either `Verified` or `Signature-compatible` in
its manifest.

Missing, duplicate, moved, or altered signatures reject the build. The error
report is saved to `%TEMP%\ClearSky-Centered-UI-diagnostics.txt` and lists the
expected and discovered RVAs. The recommended fallback is Steam Enhanced
Edition `1.10.3+68-42`.

Classic 32-bit Clear Sky, OpenXRay derivatives, and engine overhauls are not
made compatible by the unknown-SHA path; they need separate profiles or a
separate runtime port.

## UI Coverage Checklist

- Main menu and confirmation dialogs
- HUD, notifications, subtitles, and tutorials
- Conversation and character dialogs
- Inventory and item properties
- Trade
- Upgrades
- PDA, tasks, ranking, faction war, and map
- Save/load screens
- Multiplayer menus
- Mouse cursor, clickable regions, and clipping
- Controller focus movement and virtual cursor warps

The 32:9 single-player pass verified the main menu and clicks, gameplay HUD,
normal crosshair, sniper scope, dialogs and text bounds, inventory weapon
slot, intro cutscene, physical mouse mapping, and Alt+Tab cursor recovery.
Items elsewhere in this checklist describe intended coverage and may not all
have the same depth of manual testing.

## Known Conflicts

- UI replacement mods that overwrite the same files under
  `gamedata\configs\ui`
- Another proxy stack that already occupies both `dinput8.dll` and
  `dinput8_chain.dll`
- A game update that changes runtime signatures or their validated offsets

Stock level-loading artwork is not replaced and can still appear stretched on
ultrawide displays. This is a visual limitation, not an installation conflict.

The uninstaller preserves a payload file if its hash changed after
installation, and reports it instead of deleting user modifications.
