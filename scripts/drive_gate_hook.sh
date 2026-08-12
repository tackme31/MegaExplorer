#!/usr/bin/env bash
# PreToolUse(Bash) + PostToolUse(Bash) hook guarding ui_shot.py drive, which
# hijacks the real mouse and keyboard. Wired up in .claude/settings.local.json;
# one script for both events, dispatched on hook_event_name below.
#
# Two ways drive runs without a prompt, and they answer different questions:
#
#   drive-allowed          "nobody is here to answer" -- written by
#                          scripts/drive_gate.cmd, expires, survives restarts.
#   drive-session-<id>     "this session already asked and was told yes" --
#                          written by the PostToolUse half, which only fires
#                          when the tool actually ran, i.e. when the prompt was
#                          approved. Without it every drive step re-asks, since
#                          a hook's "ask" overrides Claude Code's own
#                          don't-ask-again and each step is a different command
#                          string anyway.
#
# A denial writes nothing, so the next drive asks again. That is deliberate:
# "no" usually means "not right now", and /evolve is told to stop calling drive
# for the rest of the cycle once it sees one (see .claude/skills/evolve/SKILL.md).
#
# Asking is the default whenever a human is at the desk -- that is the whole
# point of the prompt. What must never happen is *asking nobody*: a permission
# dialog is not covered by dialogExpiry, so an unattended cycle that reached one
# would sit there until someone came back, and the next cron fire never lands
# either. So with no flag and no session approval, presence decides between ask
# and deny, read off the same last-user-prompt stamp and 10-minute window as
# scripts/away_notify_hook.sh. /evolve makes the same check before calling drive
# at all; this is the half that covers walking away in between.
set -uo pipefail

payload=$(cat)
command=$(printf '%s' "$payload" | jq -r '.tool_input.command // ""' 2>/dev/null)

case "$command" in
    *ui_shot.py*drive*) ;;
    *) exit 0 ;;
esac

event=$(printf '%s' "$payload" | jq -r '.hook_event_name // ""' 2>/dev/null)
session=$(printf '%s' "$payload" | jq -r '.session_id // ""' 2>/dev/null)

# Nothing to gate on without %LOCALAPPDATA%: PreToolUse falls through to ask
# below, and PostToolUse has nowhere to record the approval.
if [ -n "${LOCALAPPDATA:-}" ]; then
    dir="${LOCALAPPDATA//\\//}/MegaExplorerLoop"
    flag="$dir/drive-allowed"
    # Sanitized: a session id is a uuid in practice, but it arrives from
    # outside and this is a path.
    marker="$dir/drive-session-$(printf '%s' "$session" | tr -dc 'A-Za-z0-9._-')"
fi

if [ "$event" = "PostToolUse" ]; then
    if [ -n "${LOCALAPPDATA:-}" ] && [ -n "$session" ]; then
        mkdir -p "$dir" && : > "$marker"
        # Markers are per-session and never explicitly cleaned up, so sweep the
        # stale ones rather than growing the directory forever.
        find "$dir" -maxdepth 1 -name 'drive-session-*' -mtime +7 -delete 2>/dev/null
    fi
    exit 0
fi

decide() {
    printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"%s","permissionDecisionReason":%s}}\n' \
        "$1" "$(printf '%s' "$2" | jq -Rs .)"
    exit 0
}

ask_hint='drive hijacks the real mouse and keyboard until it finishes. Approving covers every drive step for the rest of this session, so you are not asked again. Stay at the desk and do not lock the workstation, or the input goes to the secure desktop instead of the app.'

if [ -z "${LOCALAPPDATA:-}" ]; then
    decide ask "The unattended-run flag lives under %LOCALAPPDATA%, which is not set, so it cannot be checked. $ask_hint"
fi

[ -n "$session" ] && [ -f "$marker" ] && decide allow "already approved for this session."

# Ask while someone is there to answer; refuse once the chair is empty, so the
# call comes back immediately instead of parking on a dialog nobody will see.
ask_or_deny() {
    if [ -f "$dir/last-user-prompt" ]; then
        last=$(tr -dc '0-9' < "$dir/last-user-prompt")
        if [ -n "$last" ] && [ "$(( $(date +%s) - last ))" -lt 600 ]; then
            decide ask "$1 $ask_hint"
        fi
    fi
    decide deny "$1 Nobody has typed here for 10+ minutes, so this is treated as an unattended run and drive is refused rather than left waiting on a dialog. Skip it and hand what you wanted to check to the human. To run it unattended, use the 'drive ON' desktop shortcut."
}

if [ -f "$flag" ]; then
    expiry=$(tr -dc '0-9' < "$flag")
    now=$(date +%s)
    if [ -n "$expiry" ] && [ "$now" -lt "$expiry" ]; then
        decide allow "unattended-run flag is set for another $(( (expiry - now) / 60 )) min."
    fi
    ask_or_deny "The unattended-run flag has expired."
fi

ask_or_deny "No unattended-run flag is set, and this session has not approved drive yet."
