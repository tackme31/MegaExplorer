#!/usr/bin/env bash
# UserPromptSubmit + Notification hooks: push a prompt that is waiting on a human
# answer to the phone, but only when nobody is at the desk. Wired up in
# .claude/settings.local.json; one script for both events, dispatched on
# hook_event_name below.
#
# It covers the prompts Claude cannot announce itself -- a first-time command, an
# MCP dialog -- since Claude is blocked while one is open. Prompts Claude raises
# on purpose (drive permission) are announced by calling ntfy-send.sh directly,
# and nothing here gates drive.
#
# Two gates, and only the second one is in this file. The settings entry carries a
# matcher naming the notification types that need a human answer (permission_prompt,
# agent_needs_input and the two elicitation dialogs), because Notification is not
# the permission-only event it reads as: it also fires for idle_prompt,
# agent_completed and auth_success, none of which anyone has to answer.
#
# This file holds the other gate, which no matcher can express: the same types fire
# for every ordinary interactive approval, which is noise while you are sitting
# there reading them. What makes a prompt worth a push is that nobody is watching
# it. UserPromptSubmit stamps the last time the human actually spoke; within the
# window below they are present, past it assume the chair is empty. No stamp at all
# is treated as away, since a cron-fired turn can raise a prompt without anyone
# having typed first.
set -uo pipefail

IDLE_SECONDS=600

[ -n "${LOCALAPPDATA:-}" ] || exit 0
dir="${LOCALAPPDATA//\\//}/MegaExplorerLoop"
stamp="$dir/last-user-prompt"

payload=$(cat)
event=$(printf '%s' "$payload" | jq -r '.hook_event_name // ""' 2>/dev/null)

if [ "$event" = "UserPromptSubmit" ]; then
    # Task-completion notices arrive as injected prompts, so stamping them would
    # report a human at the desk in the middle of an unattended run.
    head=$(printf '%s' "$payload" | jq -r '.prompt // ""' 2>/dev/null \
        | sed -e 's/^[[:space:]]*//' | head -c 40)
    case "$head" in
        '<task-notification>'*) exit 0 ;;
    esac
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
