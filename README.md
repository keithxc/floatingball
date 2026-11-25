# FloatingBall

Touch-first desktop floating orb, **2026 Liquid Glass Control Center**, **7-column App Launcher**, **Touch Clipboard**, and **Window Rescue Deck** for **macOS (26/27+ Liquid Glass)**, **Linux KDE Plasma Wayland**, and **Windows**.

Combines **Xiaomi Mi 5 (MIUI Quick Ball)** single-stroke radial ergonomics with a **Duo Status Orb + 2-Ring Branching Hub**, native OS screenshot/screen-recording bridges (**KDE Spectacle** on Linux, **macshot** / **Screenshot.app** on macOS), and non-activating floating surfaces that never steal focus from your active application.

See [FOOVIEW_STUDY.md](FOOVIEW_STUDY.md) for the reference gesture study and [TODO.md](TODO.md) for platform milestones.

---

## Core Touch-PC Interaction Grammar

Designed so touching or mousing on a **Touchscreen Mac** or **Touchscreen Linux Desktop (KDE Plasma Wayland)** feels immediate, legible, and conflict-free:

1. **Duo Status Orb & Idle Edge Half-Ring**
   - Docks cleanly to any of the four screen edges (Top / Bottom / Left / Right), with per-screen independent edge placement on multi-monitor setups.
   - Displays live glanceable indicators (brightness/volume rings, battery, Wi-Fi/network, input method, privacy mic/camera dots, and unread notification dot) or tucks into a subtle glowing half-ring crescent when idle.
2. **Short Tap (`Tap`) — 2-Ring Branching Hub**
   - Opens a unified single-canvas 2-ring hub (zero hover geometry jitter):
     - **Inner Ring**: 4 virtual desktops on Linux (`vdesk1..4`) or 4 high-frequency actions on macOS (`Mission Control`, `Minimize All`, `Screenshot`, `Input Method`).
     - **Outer Ring**: 5 hubs (`Launcher`, `Windows ›`, `Tray ›` / `Clipboard`, `Control Center`, `Tools ›`).
   - Sliding or tapping into any `›` hub expands a clean single ring with an explicit `←` backtrack breadcrumb orb.
3. **Press & Immediate Pull (`Press-Slide-Release`)**
   - Pull inward from the orb without waiting: radial slots appear immediately with magnetized angular highlight; lift your finger to execute in one continuous stroke, or return to the center `×` to cancel.
   - Pulling past the outer ring and releasing triggers the native OS screenshot bridge directly.
4. **Stationary Hold (`~0.38s`) & Drag**
   - Hold still to pick up the orb, drag freely across displays, and drop to snap cleanly to the nearest screen edge. Desktop mouse users can also `Alt/Cmd/Ctrl + Drag` or middle-drag immediately.

---

## Built-In Surfaces

| Surface | Highlights |
| --- | --- |
| **Control Center (`SystemPopover`)** | 2026 Liquid Glass Bento layout with left-orb instant toggles (Wi-Fi, Hotspot, Bluetooth, Proxy/Mihomo), inline drill-down subpages, vertical touch sliders (Volume + Brightness, or desktop quick-tiles when hardware brightness is unavailable), Do Not Disturb, Lock / Power controls, and an auto-hiding media playback bar (`MPRIS` / `MediaRemote`). |
| **App Launcher (`AppLauncherPopover`)** | Centered 7-column categorized grid with touch-flick inertial scrolling, live search, and native app metadata (`.desktop` categories on Linux, `LSApplicationCategoryType` on macOS). |
| **Touch Clipboard (`ClipboardBubble`)** | Non-activating floating clipboard history with 1-tap **Paste to Active Window**, pinned snippets (`★`), quick scratchpad notes, and bounded text/image memory history. |
| **Quick Deck (`QuickDeck`)** | Giant `58pt` touch tiles rescuing tiny window chrome: **Close Tab/Window**, **Minimize**, **Fullscreen**, **Next Tab**, **Back / Forward**, **Copy / Paste**, **Volume & Brightness Sliders**, **Input Method**, and native **Screenshot / Recording**. |

---

## Native Screenshot & Screen Recording Bridges

FloatingBall intentionally delegates screenshot capture, annotation, OCR, and screen recording to best-in-class native tools instead of bundling a heavy multimedia/OCR stack:

- **Linux (KDE Plasma Wayland)**:
  - Unified **截屏 / 录屏** entry launches `spectacle` (matching KDE Plasma's native `PrtSc` overlay so you can take screenshots or pick region/full-screen recording directly inside Spectacle), with fallbacks to `flameshot gui`, `gnome-screenshot -a`, and `obs`.
- **macOS**:
  - **Screenshot** and **Screen Recording** bridge directly to open-source **[macshot](https://github.com/sw33tLie/macshot)** (`macshot://capture`, `macshot://record`, `macshot://scroll-capture`) when installed with `urlSchemeEnabled` enabled, and gracefully fall back to macOS native **Screenshot.app** with an on-screen hint.
  - To enable direct macshot linking on macOS, turn on URL Scheme in macshot Settings or run:
    ```sh
    defaults write com.sw33tlie.macshot.macshot urlSchemeEnabled -bool true
    ```

---

## Non-Activating Native Architecture

- **macOS**: Non-activating `NSPanel` (`NSWindowStyleMaskNonactivatingPanel`, `NSFloatingWindowLevel`, `CanJoinAllSpaces | FullScreenAuxiliary`), native **macOS 26/27 `NSGlassEffectView`** Liquid Glass backdrop (with `NSVisualEffectView` fallback on older macOS), `CoreWLAN`, `IOBluetooth`, `CoreAudio`, `DisplayServices`, `MediaRemote`, `CGEvent` HID chords, and `TISSelectInputSource` IME switching.
- **Linux (NixOS / KDE Plasma Wayland)**: `LayerShellQt` (`LayerOverlay`, `KeyboardInteractivityNone` so FloatingBall stays visible above recording/modal UIs without stealing keyboard focus), `org.kde.kglobalaccel` DBus shortcuts, KWin 6 virtual desktop & live open-window bridge, `StatusNotifierWatcher` system tray integration, NetworkManager / BlueZ / UPower / MPRIS DBus controls, `wpctl` audio, and `wtype` key injection.
- **Windows (Phase 2 target)**: Preserves Win32 non-activating topmost window groundwork and `ms-screenclip:` bridge.

---

## Build & Verification

Keep build and test outputs outside the source checkout:

### macOS

```sh
cmake -S . -B /path/to/build/cmake.noindex \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_OBJCXX_COMPILER=/usr/bin/clang++ \
  -DFLOATINGBALL_BUILD_TESTS=ON
cmake --build /path/to/build/cmake.noindex -j
ctest --test-dir /path/to/build/cmake.noindex --output-on-failure
```

### Linux / NixOS

```sh
nix build . --out-link /path/to/dist/floatingball
```

### CLI Options

```sh
FloatingBall --show-deck
FloatingBall --show-clipboard
FloatingBall --capture
FloatingBall --record
```
