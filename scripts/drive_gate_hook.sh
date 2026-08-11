#!/usr/bin/env bash
# PreToolUse(Bash) hook: layer 1 of the guard on ui_shot.py drive, which hijacks
# the real mouse and keyboard. Wired up in .claude/settings.local.json.
#
# Allows drive only while scripts/drive_gate.cmd has left an unexpired flag
# file. Anything else about the command is none of this hook's business, so it
# stays silent and lets the normal permission flow decide.
#
# Never answers "ask": a permission prompt is not covered by dialogExpiry, so it
# would sit open forever and stall an unattended cycle. "deny" lets the loop
# record a skip and move on.
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

if [ -z "${LOCALAPPDATA:-}" ]; then
    decide deny "drive is gated on a flag file under %LOCALAPPDATA%, but LOCALAPPDATA is not set."
fi

flag="${LOCALAPPDATA//\\//}/MegaExplorerLoop/drive-allowed"
off_hint='Skip the drive step, note it in the report as skipped, and carry on. The user turns it on by double-clicking scripts/drive_on_6h.cmd (or 8h/12h) before leaving the desk.'

[ -f "$flag" ] || decide deny "drive is OFF (no flag file). $off_hint"

expiry=$(tr -dc '0-9' < "$flag")
now=$(date +%s)
if [ -z "$expiry" ] || [ "$now" -ge "$expiry" ]; then
    decide deny "drive permission expired. $off_hint"
fi

decide allow "drive allowed for another $(( (expiry - now) / 60 )) min by the flag file."
