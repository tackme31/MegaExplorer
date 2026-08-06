#!/usr/bin/env bash
# Clears a stale .git/index.lock. This repo drops one intermittently -- root
# cause never pinned down; candidates are Qt Creator's own git integration and
# tool calls killed mid-command -- so every git write in this project is
# expected to be chained behind this script rather than repaired after it
# fails. See CLAUDE.md's "Git: every write goes through scripts/git_unlock.sh".
#
# Removing a lock out from under a live git process corrupts the index, so
# both checks below have to pass before anything is deleted. Manual equivalent,
# should this ever need rewriting: tasklist //FI "IMAGENAME eq git.exe" (or
# Get-Process git from PowerShell), then rm -f .git/index.lock only if that
# comes back empty.
#
# Windows/Git Bash only (it asks tasklist whether git.exe is running).
#
# Exit 0: safe to proceed. Exit 1: a git operation may be live, so the caller
# must wait rather than force anything.
set -u

git_dir=$(git rev-parse --absolute-git-dir 2>/dev/null) || {
    echo "git_unlock: not a git repository"
    exit 1
}
lock="$git_dir/index.lock"

if [ ! -e "$lock" ]; then
    echo "git_unlock: no lock"
    exit 0
fi

# Matching on the image name rather than the exit code or the "no tasks" line,
# because that line is localized and this machine's shell is Japanese.
if tasklist //FI "IMAGENAME eq git.exe" 2>/dev/null | grep -qi 'git\.exe'; then
    echo "git_unlock: git.exe is running -- lock left alone, retry shortly"
    exit 1
fi

# Beyond the manual procedure: a lock this fresh may belong to a process that
# started a moment ago and hasn't appeared in tasklist yet. The manual steps
# had a human deciding that, having just watched a command fail; this runs
# unattended before every commit, so it waits instead of guessing.
age=$(($(date +%s) - $(stat -c %Y "$lock")))
if [ "$age" -lt 10 ]; then
    echo "git_unlock: lock is ${age}s old -- too fresh to call stale, retry shortly"
    exit 1
fi

rm -f "$lock" || {
    echo "git_unlock: failed to remove $lock"
    exit 1
}
echo "git_unlock: removed stale lock (${age}s old)"
