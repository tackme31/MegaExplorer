# Troubleshooting

Recurring environment issues that aren't day-to-day build steps. Read on demand, when you hit the
specific symptom described.

## Git: stale `index.lock`

`git` commands in this repo (add/commit/status, etc.) intermittently fail with `fatal: Unable to
create '.../.git/index.lock': File exists` even though no git operation is actually in progress.
Recurs across sessions; root cause not pinned down (candidate: Qt Creator's own git integration, or
a prior command that got interrupted — e.g. a killed tool call — without cleaning up its lock).
**Use `bash scripts/git_unlock.sh`** rather than doing this by hand. It prints one line and is safe
to run unconditionally before any git write, so it needs no judgement call from the caller:

| Output | Exit | Meaning |
| --- | --- | --- |
| `no lock` | 0 | Nothing to do. |
| `removed stale lock (Ns old)` | 0 | Cleared; retry the git command. |
| `git.exe is running -- lock left alone` | 1 | A real operation is in progress. **Wait, don't force it.** |
| `lock is Ns old -- too fresh to call stale` | 1 | Same: wait and re-run. |

Chain it so a non-zero exit stops the write: `bash scripts/git_unlock.sh && git commit ...`.

What it does, and why in that order — the manual equivalent, should the script ever need changing:

1. Verify no `git.exe` process is actually running before touching the lock —
   `tasklist //FI "IMAGENAME eq git.exe"` from Git Bash (or `Get-Process git -ErrorAction
   SilentlyContinue` from PowerShell). If one *is* running, a real operation is in progress — wait
   for it, don't remove the lock.
2. If none is running, the lock is stale: `rm -f .git/index.lock`, then retry the original git
   command.

Don't skip step 1 — removing a lock out from under a live git process can corrupt the index.

The script adds one guard the manual steps don't have: it refuses a lock younger than 10 seconds.
The manual procedure had a human running it right after watching a command fail; the script runs
unattended before every commit, where a lock that new may belong to a process that started moments
ago and hasn't surfaced in `tasklist` yet.
