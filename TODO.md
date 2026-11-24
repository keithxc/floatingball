# FloatingBall Roadmap

Updated: 2026-11-24. Reason: complete Touch-PC redesign combining Xiaomi Mi 5 Quick Ball radial ergonomics with Android fooView crosshair-to-HUD workflow, Duo combined status orb, 4-edge docking, and KWin live window/desktop integration; Phase 1 targets macOS and Linux KDE Wayland, with Windows in Phase 2.

## Phase 1: Touch-PC Core on macOS & Linux KDE Wayland

- [x] Unified single-canvas 5-slot Xiaomi Mi 5 inward semicircle radial fan (`RadialFanOverlay`) with zero hover geometry jitter.
- [x] Idle edge half-ring tucking (`EdgeTucked`) flush against left/right screen bezels, waking immediately on touch or pointer entry.
- [x] Orthogonal single-finger state machine:
  - Short tap -> toggle 5-slot semicircle fan
  - Immediate inward pull -> `FlickSelect` (magnetized slot highlight + single-stroke release)
  - Pull past fan outer ring -> seamless **fooView Crosshair (`🎯`)** with 320ms dwell-to-crop rectangle
  - Stationary 380ms hold -> reposition ball and snap to left/right edge
- [x] Instant **ActionResultHud** beside captured regions with automatic background local OCR (`Apple Vision` / `Tesseract`), 1-tap Copy Image, Copy OCR Text, Pin Reference (`ImageReferenceCard`), Annotate handoff (`ScreenshotPanel`), and Save PNG.
- [x] Non-activating native floating surfaces (`NSWindowStyleMaskNonactivatingPanel` + `CanJoinAllSpaces | FullScreenAuxiliary` on macOS; `LayerShellQt` `KeyboardInteractivityNone` on KDE Wayland) so touch actions operate directly on the user's active window.
- [x] **ClipboardBubble** with 1-tap Paste to Active Window, pinned snippets, quick scratchpad notes, and bounded text/image memory history.
- [x] **QuickDeck** with 58pt+ touch window controls (`Close`, `Minimize`, `Fullscreen`, `Next Tab`), navigation (`Back`, `Forward`, `Copy`, `Paste`), volume touch slider, and input-method toggle.
- [x] Native build and automated test suite verification on macOS arm64 and NixOS Linux x86_64 KDE Wayland.

## Phase 2: Windows Touch Parity & Extended Capabilities

- [ ] Native Windows `WS_EX_NOACTIVATE` touch surface integration, `SendInput` navigation/window chords, and CoreAudio endpoint volume slider.
- [ ] Direct screen-pixel magnifier loupe inside `CrosshairAimMode` and 1-tap screen HEX/RGB eyedropper.
- [ ] Two-finger pinch zoom inside `ImageReferenceCard` alongside wheel zoom and opacity slider.
- [ ] Optional inline local LLM / translation provider inside `ActionResultHud`.
- [ ] Live multi-monitor mixed-DPI touchscreen drag acceptance across macOS, KDE Wayland, and Windows 11.
