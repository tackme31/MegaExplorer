# scripts/ — the `/evolve` loop's supporting pieces

Detail moved out of the root `CLAUDE.md` ("Loop engineering" → "Supporting pieces"), which keeps a
one-line summary of each. Loaded when working with files under `scripts/`.

- `usage_gate.sh` — the subscription-quota gate every cycle starts with: exit 1 means skip
  this fire and wait for the next one, since a cycle begun near the limit dies mid-way and strands a
  branch. It reads the 5h/7d percentages out of `claude -p "/usage"`, which resolves locally at zero
  tokens — the only surface that reports them at all (hooks, OTel and the org-scoped Admin Usage API
  do not). **Unreadable output also exits 1**: unattended, a wrong skip costs one cycle, a wrong
  start costs the window. Thresholds (`EVOLVE_USAGE_MAX_{5H,7D}`, defaults in the script) were set
  from 40 measured fires: one cycle eats ~30% of the 5h window and a heavy one 45–52%, so the
  original 70% left too little room. 50% skips under 1 fire in 10; below 40% it is a fifth of them.
  A cycle you want to run anyway is `/evolve force` — it still runs the gate for the numbers but
  starts regardless, and it is deliberately a one-shot: raising `EVOLVE_USAGE_MAX_5H` instead would
  keep applying to every unattended fire after it.
- `drive_gate.cmd` — `ui_shot.py drive` hijacks the real mouse and keyboard, so permission
  is taken **once per thing being verified**: the cycle runs in a subagent, which has no
  `AskUserQuestion`, so it ends its turn with a `DRIVE-PERMISSION-REQUEST` block carrying a ready-made
  `ntfy-send.sh` line, and the loop session pushes that and asks on its behalf in the same turn, then
  resumes it with the answer. **The subagent never pushes** — firing from there rang the phone a whole
  turn before the question reached the app, so the phone said "answer me" and the app had nothing to
  answer yet. That one
  answer covers every `drive` call made against the same binary — two states of the same screen, a `drag` and the shot
  after it — and a rebuild ends it: re-checking after a review fix asks again, as does another feature. A
  refusal means the point is handed to the human as "needs checking on a real run" instead. An
  **expiring** flag file under `%LOCALAPPDATA%\MegaExplorerLoop\`, written by the desktop shortcuts
  (`drive ON 6h/8h/12h`, `drive OFF`), skips the asking: it means **"nobody is here to answer"**,
  not "permission". Without it an unattended cycle that wants `drive` stops until someone answers —
  accepted deliberately, because the stalled cycle leaves a dirty tree and the next cron fire then
  aborts on its clean-tree check instead of redoing the work. Cover the windows where nobody can
  answer with the shortcut, and don't lock the workstation while the flag is set: `SendInput` goes
  to the secure desktop and never reaches the app.
- `ntfy-send.sh` — how the loop reaches a phone. Claude Code's built-in push reports success
  and mostly doesn't deliver, so it isn't used. The **loop session** is the only caller, and both of
  its calls sit next to what they announce: the drive push immediately before `AskUserQuestion`, the
  completion push in the same turn as the report it relays. The subagent writes the text; the session
  holding the human sends it. No hook pushes waiting permission prompts, so a permission dialog
  opening unattended stalls that cycle silently; the cost is the same one the drive gate already
  accepts.
