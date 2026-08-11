#!/usr/bin/env bash
# PreToolUse(Bash) hook: layer 1 of the guard on ui_shot.py drive, which hijacks
# the real mouse and keyboard. Wired up in .claude/settings.local.json.
#
# The flag file scripts/drive_gate.cmd writes means "nobody is here to answer a
# prompt", not "permission": with it, drive runs unattended; without it, this
# hook asks, so someone at the desk can just approve. Anything else about the
# command is none of this hook's business, so it stays silent and lets the
# normal permission flow decide.
#
# An unattended cycle must therefore never *reach* the prompt -- a permission
# dialog is not covered by dialogExpiry, so it would sit open forever and stall
# the loop. /evolve is what keeps that from happening: it reads this same flag
# itself and skips without calling drive at all when it is absent or expired
# (see .claude/skills/evolve/SKILL.md).
set -uo pipefail

payload=$(cat)
command=$(printf '%s' "$payload" | jq -r '.tool_input.command // ""' 2>/dev/null)

case "$command" in
    *ui_shot.py*drive*) ;;
    *) exit 0 ;;
esac

decide() {
    printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"%s","permissionDecisionReason":%s}}\n' \
        "$1" "$(printf '%s' "$2" | jq -Rs .)"
    exit 0
}

ask_hint='drive hijacks the real mouse and keyboard until it finishes. Approve only if you are at the desk and can leave the machine alone -- and do not lock the workstation, or the input goes to the secure desktop instead of the app.'

if [ -z "${LOCALAPPDATA:-}" ]; then
    decide ask "The unattended-run flag lives under %LOCALAPPDATA%, which is not set, so it cannot be checked. $ask_hint"
fi

flag="${LOCALAPPDATA//\\//}/MegaExplorerLoop/drive-allowed"

[ -f "$flag" ] || decide ask "No unattended-run flag is set. $ask_hint"

expiry=$(tr -dc '0-9' < "$flag")
now=$(date +%s)
if [ -z "$expiry" ] || [ "$now" -ge "$expiry" ]; then
    decide ask "The unattended-run flag has expired. $ask_hint"
fi

decide allow "unattended-run flag is set for another $(( (expiry - now) / 60 )) min."
