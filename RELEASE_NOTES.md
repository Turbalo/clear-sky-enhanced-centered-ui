# Version 1.0.0

Initial public release for the Steam Enhanced Edition build
`1.10.3+68-42`.

## Highlights

- Universal centered 16:10 UI safe-zone without resolution presets
- Native ultrawide 3D scene, weapon view, postprocess, and sniper scopes
- Corrected menus, HUD, dialogs, fonts, clipping, cursor, and clickable zones
- Corrected gameplay HUD atlas
- Automatic Steam installer with backup, uninstall, and proxy chain-loading
- Manual `xrEngine.exe` selection with Steam path preselection
- Verified and signature-compatible executable validation
- Shareable compatibility diagnostics and fail-safe hook rollback

## Verified

- Live 5120x1440 single-player gameplay
- Main menu rendering, hover states, and clicks
- Gameplay HUD and normal crosshair
- Sniper scope native aspect
- Dialog text bounds and inventory weapon slot
- Intro cutscene
- Physical mouse safe-zone mapping and Alt+Tab recovery
- Automated 4:3, 16:9, 21:9, 24:10, and 32:9 safe-zone tests
- DirectInput proxy smoke test and transactional installer tests

## Known Limitations

- Steam Enhanced Edition `1.10.3+68-42` is verified. Unknown x64 Enhanced
  Edition executables require a complete signature and RVA match.
- Stock level-loading artwork may remain horizontally stretched on ultrawide;
  loading text and UI still use the centered canvas.
- UI replacement mods can conflict with the menu XML files or HUD atlas.
- Multiplayer and controller-only workflows have less live coverage than the
  32:9 single-player path.
