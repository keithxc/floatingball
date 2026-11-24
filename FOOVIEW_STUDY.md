# Xiaomi Mi 5 Quick Ball × Android fooView — Touch-PC Synthesis

Updated: 2026-11-24.

## Product Judgment

Touchscreen laptops (including touch-enabled MacBooks and Linux 2-in-1 / external touch displays) suffer from a specific ergonomic gap: desktop window chrome (`12pt` close/minimize/fullscreen traffic lights, narrow tab strips) and keyboard-centric workflows (`⌘[` back, `⌘C`/`⌘V`, `⌘Shift+4` region capture + OCR) are awkward when reaching up to touch the screen with one hand.

Earlier desktop adaptations overloaded mouse modifiers (`Alt + drag`), spawned six separate satellite windows, and bundled heavy desktop utility tabs (file trees, embedded web views, manual task forms) that diluted touch responsiveness.

The 2026-11-24 redesign synthesizes two proven mobile/touch paradigms into one coherent single-finger state machine:

1. **Xiaomi Mi 5 / MIUI Quick Ball (`Edge Half-Ring + 5-Slot Semicircle Flick`)**
   - Rests unobtrusively on the left or right bezel as a half-tucked luminous double-ring sphere.
   - **Tap** opens a 5-slot inward semicircle fan; **Press & Immediate Pull** slides directly onto one of the 5 slots and executes on release in ~0.2s without lifting the finger.
   - **Stationary Hold (~0.38s)** picks up the ball to reposition along either screen edge.
   - Rendered on a **single unified canvas** (`RadialFanOverlay`) so slot hover/selection never alters window geometry or hammers Wayland `LayerShellQt` surface margins.

2. **Android fooView (`Seamless Crosshair Pull + Dwell-to-Crop + Instant Action HUD`)**
   - Pulling past the radial fan's outer boundary transitions the same finger stroke into fooView's live **Crosshair Reticle (`🎯`)** without breaking the pointer/touch grab.
   - Pausing finger motion for `~0.32s` fills a radial dwell arc and locks the crop anchor; dragging further frames the live selection rectangle, and lifting the finger captures clean desktop pixels (excluding all FloatingBall surfaces) and opens **ActionResultHud** right beside the selection with automatic local OCR (`Apple Vision` on macOS, `Tesseract` on Linux), 1-tap Pin to Desktop, Copy, Save, and Annotation handoff.

3. **Non-Activating Touch Rescue (`ClipboardBubble` & `QuickDeck`)**
   - Floating surfaces use `NSWindowStyleMaskNonactivatingPanel` (`CanJoinAllSpaces | FullScreenAuxiliary`) on macOS and `LayerShellQt::Window::KeyboardInteractivityNone` on Wayland so touching the ball, flicking `Back`, tapping `Paste to Window`, or tapping `Close/Fullscreen` in Quick Deck acts directly on the user's currently focused application.
