---
name: ui-style
description: >-
  Tune MegaExplorer's QML look and feel by screenshotting the running app.
  Launches the app at a chosen size, captures it, and runs the
  edit -> build -> relaunch -> capture loop from one command. Use for any
  visual/styling work: spacing, padding, margins, colors, theme, fonts, icon
  size, alignment, corner radius, panel widths, hover/selected states, layout
  at different window sizes -- and whenever asked to take a screenshot or to
  check how something currently looks.
  スタイル調整・見た目の調整・余白・配色・レイアウト・スクショを撮る、のときに使う。
allowed-tools: Bash(python .claude/skills/ui-style/scripts/ui_shot.py cycle*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py shot*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py launch*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py close*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py build*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py resize*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py move*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py info*), Bash(python .claude/skills/ui-style/scripts/ui_shot.py restore-settings*)
---

# QML style tuning loop

All work goes through one script. Run it from the repo root, no `cd` prefix:

```
python .claude/skills/ui-style/scripts/ui_shot.py <subcommand> [...]
```

PNGs land in `.screenshots/` (gitignored) with an incrementing prefix, e.g.
`.screenshots/007-sidebar.png`. Read the PNG the command reports; that is the
only image you need to look at.

## The loop

1. **Baseline** — `... launch --size 1200x800` then `... shot before`, and Read
   the PNG. (`launch` remembers the size, so later commands can omit `--size`.)
2. **Edit** the QML. `.qml` files are auto-formatted by a PostToolUse hook, so
   don't run `qmlformat` yourself.
3. **Iterate** — `... cycle after` — one command that does
   close → build → launch → screenshot. Read the new PNG, compare, repeat from 2.
4. **Finish** — `... close`. This is not optional: it also rewinds the registry
   (see below). Always close before ending the turn.

`cycle` prints four short lines (close state, build result, launch state, saved
path). If the build fails it prints the deduplicated error lines and stops
without relaunching.

## Rules that keep this cheap

- **Use `cycle`.** Never chain `close` + `build` + `launch` + `shot` as separate
  calls once the loop is running — that is four Bash round-trips for one turn.
- **Crop instead of scaling** when working on one component:
  `... shot panel --crop 0,0,240,800`. A crop is a smaller image *and* shows
  more detail. `--max-width N` exists but loses exactly the detail being judged.
- **Read each PNG once.** Don't re-Read an earlier screenshot to "check again";
  describe what you saw instead.
- **Don't go looking for the raw build log.** `build`/`cycle` already strip
  `third_party` noise and deduplicate MSBuild's repeats. If a warning matters it
  is in the output.
- Test responsive layout with `... resize 800x600` + `... shot narrow` — no
  rebuild needed, so it costs one command.

## Coordinates

Everything is **screenshot pixels** = physical client pixels, origin at the
window's top-left (the CaptionBar is inside the client area, so y=0 is the top
of the tab strip). `... info` reports the DPI scale; QML logical px × scale =
screenshot px. On a 100% display they are the same number.

`... shot x --grid 8` overlays a labelled measuring grid — use it when judging
padding/spacing numerically rather than guessing from a plain capture.

## Driving the UI — ask first

`drive` injects real mouse and keyboard input, so it **takes over the user's
machine for a few seconds**. It is deliberately excluded from this skill's
pre-approved tools.

> Before running `drive`, ask the user for permission in chat and wait for a
> reply. Never run it unannounced, and batch everything into one call so a
> single approval covers the whole sequence.

```
python .claude/skills/ui-style/scripts/ui_shot.py drive "click 400,300 right; wait 400; shot ctxmenu"
```

Steps are `;`-separated: `move`, `click`, `drag`, `scroll`, `key`, `type`,
`wait`, `shot`. The script counts down 2s before taking over and restores the
cursor position afterwards.

## Gotchas

- Launching **auto-logs into the user's real MEGA account**, so screenshots show
  real file names. `.screenshots/` is gitignored; never publish those PNGs.
- After adding or removing a `.qml` from `qt_add_qml_module`, use
  `... cycle x --reconfigure` — an incremental build alone leaves the AOT
  qmlcache aggregator stale and the link fails.
- Window geometry and sort/column state persist to the registry, so the loop
  would otherwise trash the user's saved layout. `launch` snapshots that key and
  `close` rewinds it. If a run was interrupted, `... info` shows
  `settings-backup: present` — run `... restore-settings` to rewind.
- If a window is left over from a crashed run: `... close --force`.

Full option reference and failure modes: [reference.md](reference.md) — read it
only when a command misbehaves or you need a flag not listed above.
