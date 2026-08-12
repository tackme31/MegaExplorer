#!/usr/bin/env bash
# UserPromptSubmit + Notification hooks: push a prompt that is waiting on a human
# answer to the phone, but only when nobody is at the desk. Wired up in
# .claude/settings.local.json; one script for both events, dispatched on
# hook_event_name below.
#
# Two gates, and only the second one is in this file. The settings entry carries
# a matcher naming the notification types that need a human answer
# (permission_prompt, agent_needs_input and the two elicitation dialogs), because
# Notification is not the permission-only event it reads as: it also fires for
# idle_prompt, agent_completed and auth_success, none of which anyone has to
# answer. Without that matcher a backgrounded subagent finishing pushed an
# "awaiting approval" notice to the phone with nothing waiting on approval.
#
# This file holds the other gate, which no matcher can express: the loop runs in
# auto mode, so almost nothing prompts during a cycle -- but a permission dialog
# has no expiry, so the rare one that does appear parks the cycle until someone
# comes back, and the next cron fire never lands either. Meanwhile the same
# types fire for every ordinary interactive approval (plan mode, a first-time
# command), which is pure noise while you are sitting there reading them. Hence
# an idle test: what makes a prompt worth a push is that nobody is watching it.
#
# UserPromptSubmit stamps the last time the human actually spoke. Within the
# window below they are present and the terminal is in front of them; past it,
# assume the chair is empty. No stamp at all is treated as away, since a
# cron-fired turn can raise a prompt without anyone having typed first.
set -uo pipefail

IDLE_SECONDS=600

[ -n "${LOCALAPPDATA:-}" ] || exit 0
dir="${LOCALAPPDATA//\\//}/MegaExplorerLoop"
stamp="$dir/last-user-prompt"

payload=$(cat)
event=$(printf '%s' "$payload" | jq -r '.hook_event_name // ""' 2>/dev/null)

if [ "$event" = "UserPromptSubmit" ]; then
    mkdir -p "$dir" && date +%s > "$stamp"
    exit 0
fi

if [ -f "$stamp" ]; then
    last=$(tr -dc '0-9' < "$stamp")
    if [ -n "$last" ] && [ "$(( $(date +%s) - last ))" -lt "$IDLE_SECONDS" ]; then
        exit 0
    fi
fi

message=$(printf '%s' "$payload" | jq -r '.message // ""' 2>/dev/null)
[ -n "$message" ] || exit 0

bash "$(dirname "$0")/ntfy-send.sh" "$message" "MegaExplorer: 承認待ち" 4 2>/dev/null || true
