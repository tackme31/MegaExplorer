# `ui_shot.py` reference

Read this only when a command misbehaves or you need a flag `SKILL.md` doesn't
list. Everything below assumes the prefix:

```
python .claude/skills/ui-style/scripts/ui_shot.py
```

## Subcommands

| Command | Purpose |
| --- | --- |
| `launch [--size WxH] [--pos X,Y] [--theme T] [--timeout 45]` | Snapshot the settings registry key, preset the geometry, start the app, wait until the scene stops changing. Refuses to run if a window is already open. |
| `shot [NAME] [capture opts] [--window]` | Capture and save `.screenshots/NNN-NAME.png`. |
| `cycle [NAME] [--size WxH] [--theme T] [--target T] [--reconfigure] [--no-build] [capture opts]` | `close` → `build` → `launch` → `shot` in one call. Aborts before relaunching if the build fails. |
| `build [--target appMegaExplorer] [--reconfigure]` | Build and print a summary. Exit code 1 on failure. |
| `close [--force] [--keep-settings]` | `WM_CLOSE`, wait up to 12s, then rewind the registry snapshot. |
| `resize WxH` / `move X,Y` | Adjust the live window. `resize` compensates for the non-client delta so the *client* area matches. |
| `info` | Window handle/title/class, pid, client size, screen origin, DPI scale, foreground flag, whether a settings backup is pending. |
| `drive "STEPS"` | Inject input. **Ask the user first** — one answer per check, see SKILL.md. |
| `restore-settings` | Rewind the registry from a stale backup left by an interrupted run. |

## Light/dark (`launch`, `cycle`)

`--theme light|dark|system` sets `MEGAEXPLORER_COLOR_SCHEME` for the launched
process; `main.cpp` turns it into `QStyleHints::setColorScheme()`. That
overrides the scheme application-wide, so both FluentWinUI3 and `Theme.qml`'s
`isLight` follow it — **no need to flip the real Windows theme to check the
light side.** `system` (or omitting the flag on a fresh session) restores OS
following.

Like `--size`, the value is remembered in `.screenshots/.session.json`, so a
follow-up `cycle` stays on the same theme instead of reverting mid-comparison.
The `launch` output line reports `theme=...` so a screenshot's scheme is
recoverable afterwards.

## Capture options (`shot`, `cycle`, `drive`)

| Flag | Meaning |
| --- | --- |
| `--crop x,y,w,h` | Crop in screenshot pixels. Clamped to the capture; errors if fully outside. |
| `--max-width N` | Downscale if wider than N. Prefer `--crop`. |
| `--grid N` | Overlay a magenta measuring grid every N px, labelled every 5th line. |
| `--method auto\|print\|screen` | Capture backend, default `auto` — but `screen` for `drive`, which needs the window frontmost anyway. |
| `--delay MS` | Sleep before capturing (e.g. let an animation finish). |
| `--window` | `shot`/`cycle` only: capture the extended frame bounds (DWM shadow, rounded corners) instead of the client area. |

### How capture works

`auto` first tries `PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT)` into a
DIB section. That composites off-screen, so the window may be behind others.
If the result comes back uniform — which is how `PrintWindow` fails against a
D3D-composited Qt Quick window — it falls back to `screen`: raise the window
and `BitBlt` from the desktop DC. The chosen backend is reported as `method=`.
If you ever see `method=screen`, the window must stay visible and unobstructed;
`method=print` has no such constraint.

`auto` also skips `PrintWindow` outright when the app owns another visible
top-level window, reporting `method=screen-popup`. A Qt Quick `Menu` defaults to
`Popup.Window` on desktop, so it renders in its own window and `PrintWindow` on
the main one returns the app **without the menu** — an image that is not
uniform, so the blank check above cannot catch it. Dialogs, tooltips and
ComboBox popups default to `Popup.Item` and do appear in a `print` capture.
`--method print` opts out of the detection, and is then wrong for menus.

`--window` uses `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`, not
`GetWindowRect` — the app is frameless via QWindowKit and those two rects differ.

## `drive` step grammar

Steps are separated by `;` and parsed with shell-style quoting.

| Step | Example |
| --- | --- |
| `move X,Y` / `hover X,Y` | `move 400,300` — for hover states |
| `click X,Y [right\|middle] [double]` | `click 400,300 right`, `click 400,300 double` |
| `drag X1,Y1 X2,Y2` | `drag 400,300 120,500` — moves in 12 steps so Qt starts the drag |
| `scroll X,Y [notches]` | `scroll 600,400 -3` (negative = down) |
| `key COMBO` | `key ctrl+shift+n`, `key F2`, `key Escape` |
| `type TEXT` | `type "new folder"` — sent as Unicode, layout-independent |
| `wait MS` | `wait 400` |
| `shot [NAME]` | captures mid-sequence, honouring the call's capture options |

`--no-countdown` skips the 2s warning. The cursor is restored to its original
position even if a step fails.

## Environment

| Item | Value | Override |
| --- | --- | --- |
| Exe | `build/msvc-debug/Debug/appMegaExplorer.exe` | — |
| CMake | `C:/Qt/Tools/CMake_64/bin/cmake.exe` (the `cmake` on PATH is Strawberry Perl's and is wrong) | `UI_SHOT_CMAKE` |
| Qt bin | `C:/Qt/6.11.1/msvc2022_64/bin` | `UI_SHOT_QT_BIN` |
| Runtime PATH | Qt bin + `build/msvc-debug/vcpkg_installed/x64-windows-mega/debug/bin`, prepended by `launch` | — |
| App CWD | the exe's own directory, so `megaclient_statecache*.db` lands there (already gitignored) | — |
| Settings key | `HKCU\Software\MegaExplorer\MegaExplorer` | — |

State files: `.screenshots/.session.json` (pid, hwnd, last size, image counter)
and `.screenshots/.regbackup.json` (the pending registry snapshot).

## Failure modes

| Symptom | Cause / fix |
| --- | --- |
| `app window not found` | Not launched, or it exited. `info` to confirm, `launch` to start. |
| `an app window is already open` | A previous run wasn't closed. `close` (or `cycle`, which closes first). |
| `no window appeared within Ns` | App crashed at startup or the machine is slow. Check `%LOCALAPPDATA%\MegaExplorer\MegaExplorer\MegaExplorer.log`, or raise `--timeout`. |
| `ready=timeout` in `launch` output | The scene never stopped changing in the timeout — usually a login spinner (no network / expired session), or an animation that never ends. The screenshot is still taken; check whether it shows the login screen. |
| Screenshot is a login screen | `session.dat` expired or the network is down. The user must log in; don't try to automate credentials. |
| Blank/uniform capture even on `screen` | Window minimized. `resize`/`move` will restore it; or `close --force` and relaunch. |
| Link error mentioning `QmlCacheGeneratedCode` | A `.qml` was added/removed from `qt_add_qml_module`. Rerun with `--reconfigure`. |
| `settings-backup: present` but no app running | An interrupted run. `restore-settings` rewinds the user's geometry/sort state. |

The backup is deliberately **never overwritten** while one exists: re-snapshotting
after a crashed run would freeze the polluted values in as the "original".
