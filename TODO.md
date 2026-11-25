# FloatingBall Roadmap

Updated: 2026-11-25. Reason: completed 2026 Liquid Glass UI & macOS 26/27 native parity (`NSGlassEffectView`, CoreWLAN, IOBluetooth, CoreAudio, DisplayServices, MediaRemote), unified Duo Status Orb + 2-Ring Branching Hub across Linux and macOS, and replaced the heavy built-in screenshot/recording/OCR stack with lightweight native OS bridges (KDE Spectacle `PrtSc` on Linux; `macshot://` / `Screenshot.app` on macOS).

## Phase 1: Touch-PC Core on macOS & Linux KDE Wayland

- [x] **Duo Status Orb + 2-Ring Branching Hub (`RadialFanOverlay`)**: single-canvas zero-jitter radial fan, continuous-drag drill-down into `Windows ›`, `Tray ›`, and `Tools ›` rings with `←` backtrack breadcrumb, plus 4-edge docking and per-screen multi-monitor replicas.
- [x] **2026 Liquid Glass Control Center (`SystemPopover`)**: Bento home layout with left-orb instant toggles, inline Wi-Fi / Hotspot / Bluetooth / Mihomo subpages, vertical Volume & Brightness touch sliders (with desktop quick-tile fallback when display brightness is unavailable), DND, Lock / Power controls, and auto-hiding media bar.
- [x] **7-Column Categorized App Launcher (`AppLauncherPopover`)**: inertial touch/trackpad scrolling, live search, `.desktop` and `LSApplicationCategoryType` categorization.
- [x] **Non-Activating Native Surfaces**: `NSWindowStyleMaskNonactivatingPanel` + macOS 26/27 `NSGlassEffectView` on macOS; `LayerShellQt` `LayerOverlay` + `KeyboardInteractivityNone` on KDE Plasma Wayland (remaining visible during recording and modal dialogs).
- [x] **Touch Clipboard (`ClipboardBubble`) & Quick Deck (`QuickDeck`)**: 1-tap Paste to Active Window, pinned snippets, scratchpad, and `58pt` touch window-rescue tiles.
- [x] **Lightweight Native Screenshot & Recording Bridges**: dropped `Qt6::Multimedia`, PipeWire, Tesseract, and Apple Vision dependencies; Linux bridges cleanly to KDE Spectacle (`spectacle`, matching native `PrtSc` for both screenshots and full-screen/region recording); macOS bridges to `macshot://capture` / `macshot://record` with graceful `Screenshot.app` fallback.
- [x] Automated `touch-ux-workflow` verification on macOS arm64 and NixOS Linux x86_64 KDE Wayland.

## Phase 2: Windows Touch Parity & Extended Polish

- [ ] Native Windows `WS_EX_NOACTIVATE` touch surface integration, `SendInput` navigation/window chords, and CoreAudio endpoint volume slider.
- [ ] Live multi-monitor mixed-DPI touchscreen drag acceptance across macOS, KDE Wayland, and Windows 11.
- [ ] Optional user-configurable external command overrides for screenshot/recording actions in Settings.
