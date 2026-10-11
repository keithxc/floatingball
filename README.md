# FloatingBall

基于 Qt 的悬浮球小工具，优先适配触控操作与桌面快捷操作。

See [TODO.md](TODO.md) for the fooView-inspired feature roadmap, implementation
priorities and platform acceptance criteria.

## 当前 macOS 版行为

- 单击：展开或收起动作子球
- 左键拖动：移动悬浮球
- 滚轮：缩放悬浮球（带小幅过渡动画）
- 双指缩放：缩放悬浮球（macOS/触控屏）
- 悬停：平滑调整透明度，尺寸和位置保持不变
- 右键 / 长按：打开快捷菜单
- 双击：也可展开或收起动作子球

## 快捷动作（macOS）

- 切换输入法（Ctrl + Space）
- 切换输入法（Command + Space）
- 复制（Command + C）
- 粘贴（Command + V）
- 打开终端
- 打开浏览器
- 打开主目录

快捷键动作会通过系统脚本触发对应按键，再执行你设置的系统行为（比如切换输入法），用于“远程场景里有时键盘事件丢失”时快速补齐操作。

## Screenshot Bubble

Click the main ball, then the screenshot satellite (截图). The other satellites
open Terminal, the browser and the home directory, or trigger copy/paste.
The screenshot panel is also available from the right-click / long-press menu.

- Floating preview of all readable PNG/JPEG/WebP images in the selected directory,
  newest first, with horizontally scrollable thumbnails.
- Circular, AssistiveTouch-inspired charcoal ball with a white center. Its painted
  opacity is 30% while idle, 85% during hover/menu use and 94% while dragging.
  Leaving it idle for 1.2 seconds starts a smooth fade; an expanded orbit stays clear.
  Action satellites use the same circular charcoal surface and stay at 85%
  opacity while expanded, matching the main ball. Pressing an action uses 94%
  opacity for feedback. Their icons keep the same size and stay centered on hover.
  Hovering or leaving any circle preserves every circle's size and position;
  only explicit resizing, dragging, screen adaptation and orbit transitions
  update their geometry.
  A separate translucent label bubble fades/slides in alongside the hovered
  action and passes mouse input through; leaving smoothly dismisses it.
  Orbit return fades are painted directly for Wayland compatibility.
  The ball and orbit adapt to available logical screen space after resolution,
  display scaling or monitor changes. Smaller screens reduce their size; returning
  to a larger screen restores the preferred size and keeps surfaces within bounds.
  A single click responds immediately. Satellites shrink and fade back into the
  ball on close, can reverse direction mid-animation, and follow mouse/touch
  dragging synchronously during expansion, use and return.
  The light screenshot card hides the thumbnail strip for a single image;
  a mouse wheel or trackpad scrolls larger galleries. Drag the card header to move it.
- Separate image clipboard and absolute original-file-path clipboard actions.
- Freehand annotation, undo (up to 12 strokes), and Save Copy as PNG.
- Copy Image includes current annotations; Copy Path still refers to the original.
- The ball and satellites stay visible above normal windows when another app
  becomes active on macOS, Windows and supported Linux desktops. The screenshot panel starts pinned so it can serve
  as a reference while working in other apps. Unpin to dismiss on focus loss;
  Escape or Close hides it in either mode.
- Copy and Undo also support the platform's standard keyboard shortcuts.
- Choose a screenshot directory in the panel; the choice persists across launches.
  macOS uses the configured system screenshot location when available, otherwise
  the Desktop. Windows resolves the Screenshots known folder (including system
  redirection), with a Pictures/Screenshots fallback. Linux looks for existing
  Screenshots, 截图 or 截屏 directories under the system Pictures location, otherwise
  uses Pictures/Screenshots. Linux tools may save elsewhere; choose that folder
  manually. No platform screenshot settings are modified.
- Import from Clipboard explicitly saves the current clipboard image as a unique
  PNG in the selected directory, then shows it immediately. A missing directory
  is created on import; browsing alone does not create it. Empty clipboard and
  write failures show a message without replacing the current image.
- Directory changes trigger a debounced refresh. A two-second fallback detects
  file replacement and retries partially written images while the panel is visible
  and outside annotation mode. Uppercase extensions are accepted.
  It reads PNG/JPEG/WebP files from the selected directory; use a dedicated
  screenshot folder to avoid mixing other images into the recent list.

This first version offers a pen tool. Arrows, shapes and text are future work.
Switching images discards unsaved pen strokes; copy or save the annotated image
before selecting another thumbnail. Original screenshot files are preserved.

Use `--screenshot-directory /absolute/path` to select a folder for one session
without updating the saved folder, and `--show-screenshots` to open its preview
on launch. Choosing a different folder in the panel still saves that explicit choice.

### Build on macOS

Requires Qt 6 Widgets and LinguistTools, CMake, and a C++17 toolchain.
From the umbrella workspace, resolve an external build directory:

```sh
python3 tools/build-paths.py --project floatingball --checkout "$PWD/floatingball" --task dev --purpose work
cmake -S floatingball -B /absolute/resolved/work/cmake -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build /absolute/resolved/work/cmake
```

### Integration checks

Enable `-DFLOATINGBALL_BUILD_TESTS=ON` when configuring the external CMake build,
then run `ctest --test-dir /absolute/resolved/work/cmake --output-on-failure`.
The checks use the offscreen Qt platform, isolated settings and owned temporary
fixtures beneath that build directory. They cover folder reads, clipboard import,
failed writes, case-insensitive extensions, partial captures, directory updates,
annotation/undo, original-file preservation, and the screenshot satellite action.
They also cover transparent clipboard images and hover events during orbit expansion.
Offscreen tests do not verify native window placement, compositor stacking or
system clipboard interoperability. macOS has been compiled and tested locally;
the Cocoa plugin checks also cover clipboard image/path, annotation/undo,
directory updates, compact gallery layout, wheel scrolling, and the satellite action
with isolated settings and clipboard restoration. Native Cocoa checks also verify
elevated window levels, front-to-back ordering above a normal window, and continued
visibility when a separate test application becomes active. The owned focus fixture
exits after verification. Native mouse input and switching Spaces/fullscreen remain
separate acceptance checks.
Windows x86_64 was compiled with Qt 6.8.3 and MinGW 13.1, and passed
both offscreen and native Windows plugin checks. SSH-driven mouse input in the
active Windows desktop also verified the satellite action, folder picker, clipboard,
annotation/save/undo, folder updates, pinning above another window, focus dismissal,
and Escape with the compact layout at 150% display scaling. Actual mouse dragging
kept all satellites within one pixel of their expected positions across eight samples;
closing frames also verified shrink/fade before the satellites disappear. Decoded
clipboard and saved annotation pixels matched exactly.
Linux x86_64 was built and tested on NixOS with Qt 6.11.2 and KDE/KWin 6.7.5.
Native Wayland checks cover orbit motion and the screenshot actions. KWin inspection
verified that the ball and pinned panel remain above a separate active application,
and that modal folder dialogs appear above their temporarily lowered parent. A
separate Wayland clipboard client received both the PNG image and original file path.
Multiple monitors, other Linux compositors and other display scales need separate checks.

### Windows build

With Qt 6 for MinGW and its matching compiler on the process PATH, configure an
external output directory and enable the integration checks:

```powershell
cmake -S . -B C:/path/to/build/floatingball/work/cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/path/to/Qt/6.8.3/mingw_64 -DFLOATINGBALL_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build C:/path/to/build/floatingball/work/cmake --parallel 2
ctest --test-dir C:/path/to/build/floatingball/work/cmake --output-on-failure
```

The Windows default directory uses `FOLDERID_Screenshots`, including system
folder redirection. The compiled application links Shell32, Ole32 and UUID.
Use `windeployqt` to collect runtime DLLs and platform/image-format plugins for
an independent test package.


### Linux / NixOS

The Nix flake includes Qt's Wayland and image-format plugins and LayerShellQt.
Build to an external result link, or run directly without changing system configuration:

```sh
nix build --out-link /absolute/path/to/build/floatingball/dist/nix-package
nix run . -- --screenshot-directory /absolute/path/to/screenshots --show-screenshots
```

For a development build, enter `nix develop`, then use the same external CMake
output-directory commands as above. On Wayland, FloatingBall uses LayerShellQt
6.6 or newer to position each ball and keep the screenshot card in the compositor's
overlay layer. Modal dialogs temporarily lower blocked floating surfaces; closing
the dialog restores the overlay layer. The compositor must support layer-shell;
KDE Plasma is the verified desktop. X11 uses Qt's normal topmost window hints.
Without LayerShellQt, a plain CMake build can run on X11; native Wayland placement
and persistent stacking are not supported by ordinary Qt window hints alone.
