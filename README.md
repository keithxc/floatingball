# FloatingBall

Touch-first desktop floating ball for touchscreen laptops and desktops (**macOS**, **Linux KDE Wayland / X11**, and **Windows**).

Combines **Xiaomi Mi 5 (MIUI Quick Ball)** single-stroke radial ergonomics with **Android fooView** live crosshair selection, instant local OCR, floating reference pins, touch clipboard, and window-chrome rescue controls.

See [FOOVIEW_STUDY.md](FOOVIEW_STUDY.md) for the reference workflow study and [TODO.md](TODO.md) for platform milestones.

---

## Core Touch-PC Interaction Grammar

Designed from the ground up so touching the screen on a **Touchscreen MacBook (macOS)** or **Touchscreen Linux Desktop (KDE Plasma Wayland)** feels effortless, immediate, and conflict-free:

1. **Idle Edge Half-Ring (Xiaomi Mi 5 / MIUI Style)**
   - Docks cleanly to the left or right screen bezel.
   - After 2.2 seconds idle, smoothly tucks halfway into the screen edge as a subtle glowing double-ring crescent (`~38%` opacity) — stays out of your content while remaining effortless to catch with a thumb or finger sliding from the bezel.
2. **Short Tap (`Tap`)**
   - Blooms a **5-slot inward semicircle fan** rendered on a unified single-window canvas (zero multi-window compositor thrashing, zero hover geometry jitter). Tap any slot to run, or tap empty desktop space without losing clicks.
3. **Press & Immediate Pull (`Press-Slide-Release` in 0.2s)**
   - Pull inward from the ball without waiting: all 5 radial slots appear immediately. Slide onto a slot (magnetized angular snap + luminous selection ring) and lift your finger to execute in a single stroke. Slide back onto the center `×` to cancel.
4. **Pull Past Fan -> Seamless fooView Crosshair (`🎯`)**
   - Keep pulling your finger past the radial fan ring onto the desktop: the fan smoothly gives way to the **fooView Crosshair Reticle (`🎯`)**!
   - **Dwell ~0.3s steady** to arm a live selection box right at your finger, drag across any text or graphic, and lift your finger: **ActionResultHud** springs open right beside your selection with **automatic local OCR** (`Apple Vision` on macOS, `Tesseract` on Linux) and 44pt+ 1-tap pills (`复制图` / `抠字` / `贴桌面` / `标注` / `保存`).
   - Touching a second finger or pressing `Esc` / right-click cancels at any point.
5. **Stationary Hold (`~0.38s`) & Drag**
   - Hold still on the ball for `0.38s` to pick it up, drag freely across displays, and drop to snap cleanly to the nearest left or right screen edge (preserving relative vertical height across resolution changes). Desktop mouse users can also `Alt/Cmd/Ctrl + Drag` or middle-drag immediately.

---

## Five Default Touch-PC Radial Slots (Customizable)

| Slot | Default Action | Why It Shines on a Touchscreen PC |
| --- | --- | --- |
| **1 (Top)** | **全局返回 (`Back`)** | Sends `⌘[` (macOS) or `Alt+Left` (Linux) to the active app without stealing focus — effortless one-finger back in browsers, Finder/Dolphin, IDEs, and PDF viewers. |
| **2 (Upper)** | **多任务全览 (`Overview`)** | Opens macOS **Mission Control** or KDE Wayland **KWin Overview** for big-card touch window switching. |
| **3 (Center C-Slot)** | **准星圈选 (`Crosshair`)** | Launches region capture, or pull straight through into live fooView Crosshair dwell-and-crop -> instant OCR / Pin / Annotate HUD. |
| **4 (Lower)** | **触控剪贴板 (`Clipboard`)** | Non-activating floating Clipboard Bubble: 1-tap **Paste to Active Window**, pinned snippets (`★`), quick scratchpad notes, and bounded text/image history. |
| **5 (Bottom)** | **触控甲板 (`Quick Deck`)** | Giant `58pt` touch tiles rescuing you from tiny `12pt` window buttons: **Close Tab/Window (`✕`)**, **Minimize (`−`)**, **Fullscreen (`⤢`)**, **Next Tab (`⇥`)**, **Back/Forward**, **Copy/Paste**, **Volume Touch Slider**, **Input Method Switch**, **Screenshot Workbench**, and **Screen Recording**. |

---

## Non-Activating Native Architecture

- **macOS**: Configured as non-activating `NSPanel` (`NSWindowStyleMaskNonactivatingPanel`, `NSFloatingWindowLevel`, `CanJoinAllSpaces | FullScreenAuxiliary`) with native `NSVisualEffectView` dark vibrancy backdrop, `CGEvent` HID chord injection, `TISSelectInputSource` IME switching, and `Apple Vision` local OCR.
- **Linux (NixOS / KDE Plasma Wayland)**: Uses `LayerShellQt` (`LayerOverlay`, `KeyboardInteractivityNone` on touch surfaces so active Wayland apps retain focus), `org.kde.kglobalaccel` DBus shortcuts for KWin Overview / Window Close / Minimize / Maximize, KWin 6 virtual desktop & live open-window bridge, `spectacle` / `xdg-desktop-portal` capture, PipeWire recording, `wpctl` audio, `wtype` key injection, and `Tesseract` (`chi_sim+chi_tra+eng`) local OCR.
- **Windows (Phase 2 target)**: Preserves Win32 `CF_DIBV5 + PNG` lossless clipboard, `FOLDERID_Screenshots`, and topmost window groundwork.

---

## Build & Verification

All builds and test outputs stay outside the source checkout via `tools/build-paths.py` (or `bash scripts/build.sh test` on managed hosts):

### macOS

```sh
bash scripts/build.sh test
```

### Linux / NixOS

```sh
eval "$(python3 ~/mygit/tools/build-paths.py --project floatingball --checkout ~/mygit/floatingball --task dev --shell)"
nix build ~/mygit/floatingball --out-link "$MYGIT_DIST_DIR/nix-package"
```

### CLI Options

```sh
FloatingBall --show-deck
FloatingBall --show-clipboard
FloatingBall --show-screenshots
FloatingBall --capture --capture-mode region --capture-delay 0
FloatingBall --record screen
```
