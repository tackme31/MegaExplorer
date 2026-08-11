#!/usr/bin/env bash
# Sends a push to the ntfy mobile app. Claude Code's own mobile push reports
# success but mostly never delivers (claude-code issues #85168 / #84488), so
# notifications go through here instead. Two callers: Claude, when it judges a
# push is warranted, and scripts/away_notify_hook.sh, which reports a waiting
# permission prompt -- the only path that can reach anyone while Claude itself
# sits blocked waiting for the answer.
#
# NTFY_TOPIC is the channel's only secret -- anyone knowing it can read these
# messages or forge them -- so it lives in the gitignored
# .claude/settings.local.json env block, never in this committed file.
#
# Usage: ntfy-send.sh <message> [title] [priority 1-5]
set -euo pipefail

: "${NTFY_TOPIC:?not set -- add it to the env block of .claude/settings.local.json}"

[ $# -ge 1 ] || {
    echo "usage: $(basename "$0") <message> [title] [priority 1-5]" >&2
    exit 2
}

# JSON body rather than -H "Title: ...": HTTP headers are effectively ASCII, so
# a Japanese title sent that way arrives mangled.
python -c '
import json, sys, urllib.request
body = {"topic": sys.argv[1], "message": sys.argv[2]}
if len(sys.argv) > 3 and sys.argv[3]:
    body["title"] = sys.argv[3]
if len(sys.argv) > 4 and sys.argv[4]:
    body["priority"] = int(sys.argv[4])
req = urllib.request.Request(
    "https://ntfy.sh/",
    data=json.dumps(body).encode(),
    headers={"Content-Type": "application/json"},
)
with urllib.request.urlopen(req, timeout=10) as r:
    sys.exit(0 if r.status == 200 else 1)
' "$NTFY_TOPIC" "$@"
