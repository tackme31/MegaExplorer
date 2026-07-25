# Troubleshooting

Recurring environment issues that aren't day-to-day build steps. Read on demand, when you hit the
specific symptom described.

## Git: stale `index.lock`

`git` commands in this repo (add/commit/status, etc.) intermittently fail with `fatal: Unable to
create '.../.git/index.lock': File exists` even though no git operation is actually in progress.
Recurs across sessions; root cause not pinned down (candidate: Qt Creator's own git integration, or
a prior command that got interrupted — e.g. a killed tool call — without cleaning up its lock).
Confirmed-safe recovery, used repeatedly:

1. Verify no `git.exe` process is actually running before touching the lock —
   `tasklist //FI "IMAGENAME eq git.exe"` from Git Bash (or `Get-Process git -ErrorAction
   SilentlyContinue` from PowerShell). If one *is* running, a real operation is in progress — wait
   for it, don't remove the lock.
2. If none is running, the lock is stale: `rm -f .git/index.lock`, then retry the original git
   command.

Don't skip step 1 — removing a lock out from under a live git process can corrupt the index.
