# FloatingBall roadmap

Updated: 2026-10-11. Baseline: `2681eb4`.

Bring fooView's floating-button workflow to Windows, macOS and Linux: select
something on screen, act on it, and keep the result within reach while working.
Optimize for a mouse, keyboard and tiled desktop windows.

This list follows the major capabilities in fooView's official descriptions;
desktop adaptations and priorities below are FloatingBall product decisions.
Checked items describe the existing baseline. Unchecked items are planned,
including work currently in progress. A checked item does not imply support on
every desktop environment or display configuration.

References:

- [Official app listing](https://play.google.com/store/apps/details?id=com.fooview.android.fooview)
- [Official FAQ](https://www.fooview.com/faq/)
- [Official changelog](https://www.fooview.com/changelog/)

Reference snapshot: the official changelog lists 1.7 (2026-09-17), while the
Google Play listing describes 1.6.4. This roadmap covers their major feature
families; individual Android releases can differ.

## P0: Capture a region and immediately use it

Implemented workbench: capture mode/delay -> unique PNG -> floating preview -> annotations/copy/save.

- [x] Start region capture from the floating ball and screenshot panel.
- [x] Hide the ball, satellites and screenshot cards during capture.
- [x] Drag to select a region; cancel with Escape without creating a file.
- [x] Save a unique PNG in the selected screenshot directory.
- [x] Open the new capture immediately in the floating preview.
- [x] Restore previously visible windows after cancellation or failure.
- [x] Show actionable capture, permission and file-write errors.
- [x] Preserve image resolution on scaled displays; verify screen-local origins.
- [x] Verify real desktop capture on macOS, Windows and pk4's KDE Wayland desktop.
- [ ] Complete every interactive window/region picker acceptance scenario on each platform.
- [ ] Add a configurable global screenshot shortcut after the button flow works.

Acceptance: trigger -> select -> preview -> copy image/path or annotate, with no
FloatingBall windows in the captured pixels. Cancellation restores the prior
workspace and creates no screenshot. Capture does not modify the clipboard until
the user chooses Copy.

## P1: Screenshot and floating-image workspace

fooView reference: regional/multiple screenshots, pinned results, image editing
and screenshot stitching.

- [x] Read recent PNG/JPEG/WebP images from a configurable directory.
- [x] Resolve platform default screenshot directories and accept a session override.
- [x] Refresh after directory changes and partially written images.
- [x] Show a compact preview with a scrollable thumbnail gallery.
- [x] Copy an image or its original absolute file path.
- [x] Import a clipboard image as a new PNG.
- [x] Draw pen annotations, undo and save an annotated copy.
- [x] Pin a single screenshot card above normal application windows.
- [x] Drag the card header; unpin for focus-loss dismissal; close with Escape.
- [ ] Pin several images as independent reference windows.
- [ ] Resize, zoom and pan pinned images; restore their positions.
- [ ] Provide collapse/expand, opacity and click-through controls for reference images.
- [x] Add arrows, rectangles, highlighter, text and mosaic.
- [ ] Add ellipses and irreversible solid redaction.
- [ ] Add a screen/image color picker with HEX and RGB copy actions.
- [ ] Offer temporary screen doodles with clear, undo and export controls.
- [ ] Crop an existing image; rotate and mirror it.
- [x] Capture a window, a whole screen and a delayed capture.
- [ ] Support several selections in one capture session.
- [ ] Stitch selected screenshots; then investigate scrolling capture separately.
- [ ] Handle mixed-DPI monitors, negative screen origins and disconnected displays.

## P1: Select text, recognize it and act on it

fooView reference: screen/image text recognition, translation, search and actions
on recognized addresses or other text.

- [ ] Recognize text from a selected screen region or an existing screenshot.
- [ ] Keep recognition results selectable and editable in a floating card.
- [ ] Copy all text or a selected passage; preserve useful line breaks.
- [ ] Support Chinese and English first, then selectable language packs.
- [ ] Offer a local OCR path and show recognition progress/failure clearly.
- [ ] Translate selected text through a configurable provider.
- [ ] Search selected text using configurable web search engines.
- [ ] Detect URLs, email addresses and file paths and offer the appropriate action.
- [ ] Offer map lookup for an address.
- [ ] Search with an image through a chosen provider.
- [ ] Recognize QR codes and barcodes from a region, image or clipboard.

Acceptance: region -> recognized text -> edit/copy/search/translate without
losing the original image. Network-backed actions show which provider is used.

## P1: Floating button, gestures and application shortcuts

fooView reference: configurable gestures, application launching and recent/pinned
applications. Android navigation gestures need desktop-specific actions.

- [x] Click to expand a six-action orbit; animate expansion and return.
- [x] Keep satellites attached during dragging and animation reversal.
- [x] Open Terminal, the browser and the home directory.
- [x] Provide copy/paste actions and a context/long-press menu.
- [ ] Configure which actions appear in the orbit and their order.
- [ ] Launch a chosen application, file, folder, URL or command.
- [ ] Configure click, double-click, long-press, wheel and directional gestures.
- [ ] Keep a clear distinction between moving the ball and invoking an action.
- [ ] Add edge snapping, auto-hide and a compact edge tab.
- [ ] Configure size, opacity, accent, animation intensity and reduced motion.
- [ ] Keep opening and closing animations elastic, reversible and responsive to dragging.
- [ ] Add pinned applications and an optional recent-application list.
- [ ] Search installed applications and open their platform-specific information/settings.
- [ ] Offer desktop actions such as switching windows or showing the desktop
  where the platform exposes a suitable interface.
- [ ] Investigate handwriting gestures after the basic mappings are reliable.
- [ ] Add a tray/menu-bar entry for settings, hide/show and exit.

## P2: Clipboard, notes and quick search

fooView reference: clipboard tools, floating notes, search engines and a
calculator.

- [ ] Add an opt-in text/image clipboard history with pin, search and delete.
- [ ] Reuse a history item through Copy; add paste-to-application where supported.
- [ ] Hand selected text, images and files to another app through native share/open-with actions.
- [ ] Combine selected text snippets in a chosen order.
- [ ] Save OCR results and snippets as local notes.
- [ ] Provide a small floating note editor with explicit save/export.
- [ ] Search notes, screenshot filenames and configured folders.
- [ ] Offer a quick calculator and common unit conversions.
- [ ] Keep limits for retained items and image storage configurable.

## P2: Recording and lightweight media tools

fooView reference: screen recording, GIF creation, media viewing and editing.

- [x] Record a screen; record windows on macOS and Windows.
- [ ] Add window recording on Wayland and region recording on macOS/Windows.
- [x] Provide a compact pause/resume/stop control and save recordings in the selected folder.
- [ ] Configure whether to include the cursor and click indicators.
- [ ] Offer microphone/system audio where the platform supports it.
- [ ] Export short recordings as GIFs or extract selected frames.
- [ ] Preview image sequences, GIFs, audio and video in floating windows.
- [ ] Evaluate trimming clips and basic media controls as separate modules.

## P3: Files, browser and reusable tasks

fooView reference: local/network/cloud file management, a floating browser,
downloads and workflows composed from actions.

- [ ] Quick-open and preview local files from the floating button.
- [ ] Browse selected folders with sorting, filtering, bookmarks and multiple selection.
- [ ] Add explicit file copy, move, rename and trash actions with conflict handling.
- [ ] Drag files or images into a result card to choose the next action.
- [ ] Inspect file metadata, copy paths and calculate checksums.
- [ ] Search chosen folders and inspect their storage usage.
- [ ] Evaluate archive browsing and extraction.
- [ ] Evaluate SMB, FTP/SFTP and WebDAV access as optional integrations.
- [ ] Evaluate cloud-drive providers independently of the core screenshot workflow.
- [ ] Offer a floating web/search view with external-browser handoff.
- [ ] Keep browser tabs, bookmarks and configurable search engines in the floating view.
- [ ] Evaluate a download queue with progress, retry and cancellation.
- [ ] Compose reusable actions: capture -> OCR -> copy, or capture -> annotate -> save.
- [ ] Support manual triggers, configurable shortcuts and scheduled reminders.
- [ ] Export/import task definitions and show execution logs.
- [ ] Trigger tasks from chosen file changes or application events where supported.
- [ ] Evaluate conditional actions, shell commands and scripting as an advanced module.
- [ ] Evaluate voice input, speech output and provider-configured AI actions separately.
- [ ] Evaluate AI explanation of selected text/images and optional local-model support.

Desktop adaptation: Android Back/Home/Recent gestures map to window switching,
show-desktop and application launching. APK management, notification-shade
controls, phone sensors and root/ADB actions remain Android-specific reference
features. Desktop task automation uses each platform's available interfaces.

## Platform work and completion gates

| Area | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Region capture | Native interactive capture | Desktop capture + region selector | X11 capture; Wayland screenshot portal |
| Persistent floating windows | Cocoa tool-window handling | Native topmost windows | LayerShellQt on supported Wayland compositors; X11 hints |
| Global shortcuts | Native registration | Native registration | X11 registration; Wayland portal/compositor support |
| Clipboard interoperation | Test a separate native consumer | Test PNG/DIB consumers | Test a separate Wayland/X11 consumer |
| Recording | Qt screen/window capture | Qt screen/window capture | Qt + PipeWire screen portal; window recording pending |

- [ ] Test mixed display scales and multiple monitors on each platform.
- [ ] Test capture cancellation, denied permission, busy services and unwritable directories.
- [ ] Verify native clipboard interoperability rather than only reading back in Qt.
- [ ] Verify behavior after focus changes, modal dialogs and display reconfiguration.
- [ ] Check preview layout with empty, single-image and large screenshot folders.
- [ ] Maintain reproducible builds and portable/runtime-complete test packages.
- [ ] Keep builds, logs and acceptance evidence in the shared external build workspace.

Ship in this order:

1. Region capture -> floating preview -> copy/annotate/save.
2. Independent pinned images and richer annotation.
3. OCR -> editable text -> search/translation.
4. Configurable shortcuts/gestures, application launching and clipboard history.
5. Recording, notes, media and optional integrations.

Mark a feature complete only after its behavior and platform acceptance are
recorded. The current native baseline was verified on macOS, Windows and KDE
Wayland; it does not establish compatibility with every Linux compositor.
