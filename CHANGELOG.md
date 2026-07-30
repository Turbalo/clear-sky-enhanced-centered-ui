# Changelog

## 1.0.0 - 2026-07-30

- Added a runtime centered 16:10 UI safe-zone for ultrawide displays.
- Kept the 3D scene, weapon view, postprocess effects, and scope overlays on
  their native rendering paths.
- Corrected UI geometry, fonts, clipping, mouse coordinates, and controller
  cursor movement.
- Preserved normal crosshair proportions.
- Added a narrow native-aspect bypass for sniper scope rendering.
- Added main-menu side-region backplates and corrected hover/click geometry.
- Added the validated replacement `ui_hud.dds` gameplay atlas.
- Added verified and signature-compatible game validation, diagnostic logging,
  fail-safe hook rollback, and one-level DirectInput proxy chain-loading.
- Added automatic Steam discovery, transactional backup, uninstall, and
  reinstall support.
- Added an `xrEngine.exe` picker with Steam path preselection.
- Added x64/classic-engine detection and shareable failure diagnostics with
  per-signature counts and RVAs.
